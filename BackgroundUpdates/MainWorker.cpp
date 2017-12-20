#include "stdafx.h"

#include <MMDeviceAPI.h>
#include <functiondiscoverykeys.h>

#include "MainWorker.h"
#include "BufferPool.h"
#include "WASAPICapture.h"
#include "thread_pool_enqueue.h"

#if defined(_MSC_VER) && _MSC_VER >= 1800
#define RESTRICT __restrict
#elif 0 // check for sufficiently new versions of clang and gcc
#define RESTRICT __restrict__
#else
#define RESTRICT
#endif

template<typename T, size_t Size, int Align>
struct alignas(Align)
    AudioBuffer
{
    int length;
    alignas(Align)
        std::array<T, Size> data;

    void reset() { length = 0; }
};

template<typename T, size_t Size, int Align>
class AudioDemux
{
public:
    typedef BufferPool<AudioBuffer<T, Size, Align>, Align> pool_type;

    AudioDemux(std::shared_ptr<pool_type> pool, const int channels)
        : pool_(std::move(pool)), channels_(channels)
    { }
    AudioDemux() = delete;
    AudioDemux(const AudioDemux &) = delete;

    void add(const void* data, const size_t data_size);

    int channels() const noexcept { return channels_; }

private:
    std::shared_ptr<pool_type> pool_;
    const int channels_;
};

template<typename T, size_t Size, int Align>
void AudioDemux<T, Size, Align>::add(const void* data, const size_t data_size)
{
    if (data_size < sizeof(T) * channels_)
        return;

    auto item_count = data_size / sizeof(T);
    auto block_count = item_count / channels_;

    auto buffer_size =  Size;

    if (!data)
    {
        decltype(buffer_size) written = 0;

        for(;;)
        {
            const auto length = std::min(buffer_size, item_count - written);

            if (length <= 0)
                return;

            for (auto i = 0; i < channels_; ++i)
            {
                auto buffer = pool_->allocate();

                memset(&buffer->data[0], 0, sizeof(T) * length);

                buffer->length = length;

                // Enqueue buffer
            }

            written += length;
        }
    }

    std::vector<typename pool_type::unique_ptr_type> buffers;

    buffers.reserve(channels_);

    for (auto i = 0; i < channels_; ++i)
    {
        buffers.push_back(pool_->allocate());

        if (!*buffers.rbegin())
            return;
    }

    auto p = reinterpret_cast<const T* RESTRICT>(data);
    auto p_end = p + item_count;

    auto index = 0;

    while (p < p_end)
    {
        if (index >= buffer_size)
        {
            for (auto& b : buffers)
            {
                b->length = index;

                auto full = std::move(b);

                b = pool_->allocate();

                if (!b)
                    return;

                // Enqueue "full"
            }
        }

        const auto remaining_output = buffer_size - index;
        const auto remaining_input = (p_end - p) / channels_;
        const auto length = std::min<size_t>(remaining_output, remaining_input);

        if (length < 1)
            break;      // TODO: Partial data remaining?

        for (auto i = 0; i < length; ++i)
        {
            for (auto& b : buffers)
                b->data[index] = *p++;

            ++index;
        }
    }

    if (index > 0)
    {
        for (auto& b : buffers)
        {
            b->length = index;

            auto full = std::move(b);

            // Enqueue "full"
        }
    }
}

bool DisableMMCSS;

namespace
{
    bool UseConsoleDevice = true;
    bool UseCommunicationsDevice;
    bool UseMultimediaDevice;

    wchar_t* OutputEndpoint;

    //
    //  Retrieves the device friendly name for a particular device in a device collection.  
    //
    std::unique_ptr<wchar_t[]> GetDeviceName(const Microsoft::WRL::ComPtr<IMMDeviceCollection>& DeviceCollection,
        const UINT DeviceIndex)
    {
        Microsoft::WRL::ComPtr<IMMDevice> device;
        auto hr = DeviceCollection->Item(DeviceIndex, &device);
        if (FAILED(hr))
        {
            printf("Unable to get device %d: %x\n", DeviceIndex, hr);
            return nullptr;
        }

        LPWSTR deviceId;
        hr = device->GetId(&deviceId);
        if (FAILED(hr))
        {
            printf("Unable to get device %d id: %x\n", DeviceIndex, hr);
            return nullptr;
        }

        Microsoft::WRL::ComPtr<IPropertyStore> propertyStore;
        hr = device->OpenPropertyStore(STGM_READ, &propertyStore);
        device.Reset();
        if (FAILED(hr))
        {
            printf("Unable to open device %d property store: %x\n", DeviceIndex, hr);
            return nullptr;
        }

        PROPVARIANT friendlyName;
        PropVariantInit(&friendlyName);
        hr = propertyStore->GetValue(PKEY_Device_FriendlyName, &friendlyName);
        propertyStore.Reset();

        if (FAILED(hr))
        {
            printf("Unable to retrieve friendly name for device %d : %x\n", DeviceIndex, hr);
            return nullptr;
        }

        wchar_t deviceName[128];
        hr = StringCbPrintf(deviceName, sizeof(deviceName), L"%s (%s)",
            friendlyName.vt != VT_LPWSTR ? L"Unknown" : friendlyName.pwszVal, deviceId);
        if (FAILED(hr))
        {
            printf("Unable to format friendly name for device %d : %x\n", DeviceIndex, hr);
            return nullptr;
        }

        PropVariantClear(&friendlyName);
        CoTaskMemFree(deviceId);

        std::unique_ptr<wchar_t[]> returnValue{ _wcsdup(deviceName) };

        if (!returnValue)
        {
            printf("Unable to allocate buffer for return\n");
            return nullptr;
        }

        return returnValue;
    }

    //
    //  Based on the input switches, pick the specified device to use.
    //
    Microsoft::WRL::ComPtr<IMMDevice> PickDevice(bool& IsDefaultDevice, ERole& DefaultDeviceRole)
    {
        Microsoft::WRL::ComPtr<IMMDeviceEnumerator> deviceEnumerator;
        Microsoft::WRL::ComPtr<IMMDeviceCollection> deviceCollection;

        IsDefaultDevice = false; // Assume we're not using the default device.

        auto hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_INPROC_SERVER,
            IID_PPV_ARGS(&deviceEnumerator));
        if (FAILED(hr))
        {
            printf("Unable to instantiate device enumerator: %x\n", hr);
            return nullptr;
        }

        Microsoft::WRL::ComPtr<IMMDevice> device;

        //
        //  First off, if none of the console switches was specified, use the console device.
        //
        if (!UseConsoleDevice && !UseCommunicationsDevice && !UseMultimediaDevice && OutputEndpoint == nullptr)
        {
            //
            //  The user didn't specify an output device, prompt the user for a device and use that.
            //
            hr = deviceEnumerator->EnumAudioEndpoints(eCapture, DEVICE_STATE_ACTIVE, &deviceCollection);
            if (FAILED(hr))
            {
                printf("Unable to retrieve device collection: %x\n", hr);
                return nullptr;
            }

            printf("Select an output device:\n");
            printf("    0:  Default Console Device\n");
            printf("    1:  Default Communications Device\n");
            printf("    2:  Default Multimedia Device\n");
            UINT deviceCount;
            hr = deviceCollection->GetCount(&deviceCount);
            if (FAILED(hr))
            {
                printf("Unable to get device collection length: %x\n", hr);
                return nullptr;
            }
            for (UINT i = 0; i < deviceCount; i += 1)
            {
                auto deviceName = GetDeviceName(deviceCollection.Get(), i);
                if (!deviceName)
                {
                    return nullptr;
                }
                printf("    %d:  %S\n", i + 3, deviceName.get());
            }
            wchar_t choice[10];
            _getws_s(choice); // Note: Using the safe CRT version of _getws.

            wchar_t* endPointer;

            const auto deviceIndex = wcstoul(choice, &endPointer, 0);
            if ((deviceIndex == ULONG_MAX || deviceIndex == 0) && endPointer == choice)
            {
                printf("unrecognized device index: %S\n", choice);
                return nullptr;
            }
            switch (deviceIndex)
            {
            case 0:
                UseConsoleDevice = true;
                break;
            case 1:
                UseCommunicationsDevice = true;
                break;
            case 2:
                UseMultimediaDevice = true;
                break;
            default:
                hr = deviceCollection->Item(deviceIndex - 3, &device);
                if (FAILED(hr))
                {
                    printf("Unable to retrieve device %d: %x\n", deviceIndex - 3, hr);
                    return nullptr;
                }
                break;
            }
        }
        else if (OutputEndpoint != nullptr)
        {
            hr = deviceEnumerator->GetDevice(OutputEndpoint, &device);
            if (FAILED(hr))
            {
                printf("Unable to get endpoint for endpoint %S: %x\n", OutputEndpoint, hr);
                return nullptr;
            }
        }

        if (!device)
        {
            auto deviceRole = eConsole; // Assume we're using the console role.
            if (UseConsoleDevice)
            {
                deviceRole = eConsole;
            }
            else if (UseCommunicationsDevice)
            {
                deviceRole = eCommunications;
            }
            else if (UseMultimediaDevice)
            {
                deviceRole = eMultimedia;
            }
            hr = deviceEnumerator->GetDefaultAudioEndpoint(eCapture, deviceRole, &device);
            if (FAILED(hr))
            {
                printf("Unable to get default device for role %d: %x\n", deviceRole, hr);
                return nullptr;
            }
            IsDefaultDevice = true;
            DefaultDeviceRole = deviceRole;
        }

        return device;
    }
}

MainWorker::MainWorker() : main_thread_{}, audio_capture_{}, float_pool_{}, short_pool_{}
{
    Init();
}

MainWorker::~MainWorker()
{
    if (main_thread_.is_running())
    {
        auto audio_stop_future = main_thread_.enqueue_work([this]()
        {
            main_thread_.verify_on_thread();

            if (!audio_started_)
                return;

            audio_started_ = false;

            if (!audio_capture_)
                return;

            // We shouldn't have to worry about COM ->Release() races, since all
            // the mangement work should be happening on our main_thread_.
            audio_capture_->Shutdown();
        });

        audio_stop_future.wait();

        main_thread_.request_stop();
    }
}

int add(int a, int b)
{
    return a + b;
}

void MainWorker::Init()
{
    using namespace std::chrono_literals;

    seeded_random rng;
    std::uniform_real<> dist{ 0.25, 8.5 };

    auto f = thread_pool_enqueue(background_pool_, &add, 5, 8);

    for (auto i = 0; i < 1000; ++i)
    {
        const auto delay = dist(rng) * 30ms;

        background_pool_.enqueue_work([this, i, delay]
        {
            std::this_thread::sleep_for(delay);
            printf("{%d}", i);
            background_pool_.enqueue_work([i, delay]
            {
                std::this_thread::sleep_for(delay * (i & 0x03));
                printf("<%d>", i);
            });
        });
    }

    auto f2 = thread_pool_enqueue(background_pool_, &add, 2, 3);

    auto apb = f2.get();
}

void MainWorker::Start()
{
    auto audio_future = main_thread_.enqueue_work([this]()
    {
        using namespace std::chrono_literals;

        main_thread_.verify_on_thread();

        bool isDefaultDevice;
        ERole role;

        auto device = PickDevice(isDefaultDevice, role);

        if (!device)
            return;

        audio_capture_.Attach(new(std::nothrow) CWASAPICapture(device.Get(), isDefaultDevice, role));

        if (audio_capture_ == nullptr)
            return;

        const auto TargetLatency = 20ms;

        if (!audio_capture_->Initialize(TargetLatency))
            return;

        const auto wave_format = audio_capture_->MixFormat();

        // TODO: Do stuff to see what the format really is.
        // For now, assume 32-bit float.

        const auto channels = audio_capture_->ChannelCount();

        if (!float_demux_ || float_demux_->channels() != channels)
        {
            if (!float_pool_)
                float_pool_ = std::make_shared<BufferPool<AudioBuffer<float, 4096, 32>, 32>>(32);

            float_demux_ = std::make_unique<AudioDemux<float, 4096, 32>>(float_pool_, channels);
        }

        audio_started_ = audio_capture_->Start([this](const uint8_t* p, size_t s)
        {
            if (s <= 0)
                return;

            float_demux_->add(p, s);
        });
    });
}

void MainWorker::Stop()
{
}
