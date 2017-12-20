// THIS CODE AND INFORMATION IS PROVIDED "AS IS" WITHOUT WARRANTY OF
// ANY KIND, EITHER EXPRESSED OR IMPLIED, INCLUDING BUT NOT LIMITED TO
// THE IMPLIED WARRANTIES OF MERCHANTABILITY AND/OR FITNESS FOR A
// PARTICULAR PURPOSE.
//
// Copyright (c) Microsoft Corporation. All rights reserved
//
#include "StdAfx.h"
#include <assert.h>
#include <avrt.h>
#include "WASAPICapture.h"
#include "CoInitializeHandle.h"

//
//  A simple WASAPI Capture client.
//

CWASAPICapture::CWASAPICapture(const Microsoft::WRL::ComPtr<IMMDevice>& Endpoint, bool EnableStreamSwitch, ERole EndpointRole) :
    _Endpoint(Endpoint),
    _EnableStreamSwitch(EnableStreamSwitch),
    _EndpointRole(EndpointRole)
{ }

//
//  Empty destructor - everything should be released in the Shutdown() call.
//
CWASAPICapture::~CWASAPICapture()
{
}


//
//  Initialize WASAPI in event driven mode, associate the audio client with our samples ready event handle, retrieve 
//  a capture client for the transport, create the capture thread and start the audio engine.
//
bool CWASAPICapture::InitializeAudioEngine()
{
    auto hr = _AudioClient->Initialize(AUDCLNT_SHAREMODE_SHARED,
        AUDCLNT_STREAMFLAGS_EVENTCALLBACK | AUDCLNT_STREAMFLAGS_NOPERSIST,
        _EngineLatencyInMS.count(), 0, _MixFormat, nullptr);

    if (FAILED(hr))
    {
        printf("Unable to initialize audio client: %x.\n", hr);
        return false;
    }

    //
    //  Retrieve the buffer size for the audio client.
    //
    hr = _AudioClient->GetBufferSize(&_BufferSize);
    if (FAILED(hr))
    {
        printf("Unable to get audio client buffer: %x. \n", hr);
        return false;
    }

    hr = _AudioClient->SetEventHandle(_AudioSamplesReadyEvent);
    if (FAILED(hr))
    {
        printf("Unable to set ready event: %x.\n", hr);
        return false;
    }

    hr = _AudioClient->GetService(IID_PPV_ARGS(&_CaptureClient));
    if (FAILED(hr))
    {
        printf("Unable to get new capture client: %x.\n", hr);
        return false;
    }

    return true;
}

//
//  Retrieve the format we'll use to capture samples.
//
//  We use the Mix format since we're capturing in shared mode.
//
bool CWASAPICapture::LoadFormat()
{
    const auto hr = _AudioClient->GetMixFormat(&_MixFormat);
    if (FAILED(hr))
    {
        printf("Unable to get mix format on audio client: %x.\n", hr);
        return false;
    }

    _FrameSize = (_MixFormat->wBitsPerSample / 8) * _MixFormat->nChannels;
    return true;
}

//
//  Initialize the capturer.
//
bool CWASAPICapture::Initialize(reference_time EngineLatency)
{
    //
    //  Create our shutdown and samples ready events- we want auto reset events that start in the not-signaled state.
    //
    _ShutdownEvent = CreateEventEx(nullptr, nullptr, 0, EVENT_MODIFY_STATE | SYNCHRONIZE);
    if (_ShutdownEvent == nullptr)
    {
        printf("Unable to create shutdown event: %" PRIu32 ".\n", GetLastError());
        return false;
    }

    _AudioSamplesReadyEvent = CreateEventEx(nullptr, nullptr, 0, EVENT_MODIFY_STATE | SYNCHRONIZE);
    if (_AudioSamplesReadyEvent == nullptr)
    {
        printf("Unable to create samples ready event: %" PRIu32 ".\n", GetLastError());
        return false;
    }

    //
    //  Create our stream switch event- we want auto reset events that start in the not-signaled state.
    //  Note that we create this event even if we're not going to stream switch - that's because the event is used
    //  in the main loop of the capturer and thus it has to be set.
    //
    _StreamSwitchEvent = CreateEventEx(nullptr, nullptr, 0, EVENT_MODIFY_STATE | SYNCHRONIZE);
    if (_StreamSwitchEvent == nullptr)
    {
        printf("Unable to create stream switch event: %" PRIu32 ".\n", GetLastError());
        return false;
    }

    //
    //  Now activate an IAudioClient object on our preferred endpoint and retrieve the mix format for that endpoint.
    //
    void* pAudioClient;
    auto hr = _Endpoint->Activate(__uuidof(IAudioClient), CLSCTX_INPROC_SERVER, nullptr,
        reinterpret_cast<void **>(&pAudioClient));
    if (FAILED(hr))
    {
        printf("Unable to activate audio client: %x.\n", hr);
        return false;
    }

    _AudioClient.Attach(reinterpret_cast<IAudioClient*>(pAudioClient));

    hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_INPROC_SERVER,
        IID_PPV_ARGS(&_DeviceEnumerator));
    if (FAILED(hr))
    {
        printf("Unable to instantiate device enumerator: %x\n", hr);
        return false;
    }

    //
    // Load the MixFormat.  This may differ depending on the shared mode used
    //
    if (!LoadFormat())
    {
        printf("Failed to load the mix format \n");
        return false;
    }

    //
    //  Remember our configured latency in case we'll need it for a stream switch later.
    //
    _EngineLatencyInMS = EngineLatency;

    if (!InitializeAudioEngine())
    {
        return false;
    }

    if (_EnableStreamSwitch)
    {
        if (!InitializeStreamSwitch())
        {
            return false;
        }
    }

    return true;
}

//
//  Shut down the capture code and free all the resources.
//
void CWASAPICapture::Shutdown()
{
    if (_CaptureThread.joinable())
    {
        SetEvent(_ShutdownEvent);
        _CaptureThread.join();
    }

    if (_ShutdownEvent)
    {
        CloseHandle(_ShutdownEvent);
        _ShutdownEvent = nullptr;
    }
    if (_AudioSamplesReadyEvent)
    {
        CloseHandle(_AudioSamplesReadyEvent);
        _AudioSamplesReadyEvent = nullptr;
    }
    if (_StreamSwitchEvent)
    {
        CloseHandle(_StreamSwitchEvent);
        _StreamSwitchEvent = nullptr;
    }

    _Endpoint.Reset();
    _AudioClient.Reset();
    _CaptureClient.Reset();

    if (_MixFormat)
    {
        CoTaskMemFree(_MixFormat);
        _MixFormat = nullptr;
    }

    if (_EnableStreamSwitch)
    {
        TerminateStreamSwitch();
    }
}


//
//  Start capturing...
//
bool CWASAPICapture::Start(std::function<void(const uint8_t *, size_t)> read_callback)
{
    read_callback_ = read_callback;

    //
    //  Now create the thread which is going to drive the capture.
    //
    _CaptureThread = std::thread{ &CWASAPICapture::DoCaptureThread, this };
    //if (_CaptureThread == nullptr)
    //{
    //    printf("Unable to create transport thread: %x.", GetLastError());
    //    return false;
    //}

    //
    //  We're ready to go, start capturing!
    //
    auto hr = _AudioClient->Start();
    if (FAILED(hr))
    {
        printf("Unable to start capture client: %x.\n", hr);
        return false;
    }

    return true;
}

//
//  Stop the capturer.
//
void CWASAPICapture::Stop()
{
    //
    //  Tell the capture thread to shut down, wait for the thread to complete then clean up all the stuff we 
    //  allocated in Start().
    //
    if (_ShutdownEvent)
    {
        SetEvent(_ShutdownEvent);
    }

    auto hr = _AudioClient->Stop();
    if (FAILED(hr))
    {
        printf("Unable to stop audio client: %x\n", hr);
    }

    if (_CaptureThread.joinable())
    {
        _CaptureThread.join();
    }

    read_callback_ = nullptr;
}


//
//  Capture thread - processes samples from the audio engine
//

void CWASAPICapture::read_buffer() const
{
    //
    //  We need to retrieve the next buffer of samples from the audio capturer.
    //
    BYTE* pData;
    UINT32 framesAvailable;
    DWORD flags;

    //
    //  Find out how much capture data is available.  We need to make sure we don't run over the length
    //  of our capture buffer.  We'll discard any samples that don't fit in the buffer.
    //
    auto hr = _CaptureClient->GetBuffer(&pData, &framesAvailable, &flags, nullptr, nullptr);
    if (FAILED(hr))
        return;

    if (nullptr != read_callback_ && framesAvailable)
    {
        if (flags & AUDCLNT_BUFFERFLAGS_SILENT)
            read_callback_(nullptr, framesAvailable * _FrameSize);
        else
            read_callback_(pData, framesAvailable * _FrameSize);
    }

    hr = _CaptureClient->ReleaseBuffer(framesAvailable);
    if (FAILED(hr))
    {
        printf("Unable to release capture buffer: %x!\n", hr);
    }
}

void CWASAPICapture::read_audio()
{
    const HANDLE wait_array[3] = { _ShutdownEvent, _StreamSwitchEvent, _AudioSamplesReadyEvent };

    auto stillPlaying = true;

    while (stillPlaying)
    {
        //HRESULT hr;
        const DWORD waitResult = WaitForMultipleObjects(3, wait_array, FALSE, INFINITE);

        switch (waitResult)
        {
        case WAIT_OBJECT_0 + 0: // _ShutdownEvent
            stillPlaying = false; // We're done, exit the loop.
            break;
        case WAIT_OBJECT_0 + 1: // _StreamSwitchEvent
            //
            //  We need to stop the capturer, tear down the _AudioClient and _CaptureClient objects and re-create them on the new.
            //  endpoint if possible.  If this fails, abort the thread.
            //
            if (!HandleStreamSwitchEvent())
            {
                stillPlaying = false;
            }
            break;
        case WAIT_OBJECT_0 + 2: // _AudioSamplesReadyEvent
            read_buffer();
            break;
        default: 
            break;
        }
    }
}

void CWASAPICapture::DoCaptureThread()
{
    const CoInitializeWrapper co_wrapper{ COINIT_MULTITHREADED };

    auto hr = co_wrapper.get();
    if (FAILED(hr))
    {
        printf("Unable to initialize COM in render thread: %x\n", hr);
        return;
    }

    HANDLE mmcssHandle = nullptr;
    DWORD mmcssTaskIndex = 0;

    if (!DisableMMCSS)
    {
        mmcssHandle = AvSetMmThreadCharacteristics(L"Audio", &mmcssTaskIndex);
        if (mmcssHandle == nullptr)
        {
            printf("Unable to enable MMCSS on capture thread: %" PRIu32 "\n", GetLastError());
        }
    }

    try
    {
        read_audio();
    }
    catch (...)
    {
        capture_thread_exception_ = std::current_exception();
    }

    if (!DisableMMCSS && mmcssHandle)
    {
        AvRevertMmThreadCharacteristics(mmcssHandle);
    }
}


//
//  Initialize the stream switch logic.
//
bool CWASAPICapture::InitializeStreamSwitch()
{
    auto hr = _AudioClient->GetService(IID_PPV_ARGS(&_AudioSessionControl));
    if (FAILED(hr))
    {
        printf("Unable to retrieve session control: %x\n", hr);
        return false;
    }

    //
    //  Create the stream switch complete event- we want a manual reset event that starts in the not-signaled state.
    //
    _StreamSwitchCompleteEvent = CreateEventEx(nullptr, nullptr, CREATE_EVENT_INITIAL_SET | CREATE_EVENT_MANUAL_RESET,
        EVENT_MODIFY_STATE | SYNCHRONIZE);
    if (_StreamSwitchCompleteEvent == nullptr)
    {
        printf("Unable to create stream switch event: %" PRIu32 ".\n", GetLastError());
        return false;
    }
    //
    //  Register for session and endpoint change notifications.  
    //
    //  A stream switch is initiated when we receive a session disconnect notification or we receive a default device changed notification.
    //
    hr = _AudioSessionControl->RegisterAudioSessionNotification(this);
    if (FAILED(hr))
    {
        printf("Unable to register for stream switch notifications: %x\n", hr);
        return false;
    }

    hr = _DeviceEnumerator->RegisterEndpointNotificationCallback(this);
    if (FAILED(hr))
    {
        printf("Unable to register for stream switch notifications: %x\n", hr);
        return false;
    }

    return true;
}

void CWASAPICapture::TerminateStreamSwitch()
{
    auto hr = _AudioSessionControl->UnregisterAudioSessionNotification(this);
    if (FAILED(hr))
    {
        printf("Unable to unregister for session notifications: %x\n", hr);
    }

    _DeviceEnumerator->UnregisterEndpointNotificationCallback(this);
    if (FAILED(hr))
    {
        printf("Unable to unregister for endpoint notifications: %x\n", hr);
    }

    if (_StreamSwitchCompleteEvent)
    {
        CloseHandle(_StreamSwitchCompleteEvent);
        _StreamSwitchCompleteEvent = nullptr;
    }

    _AudioSessionControl.Reset();
    _DeviceEnumerator.Reset();
}

//
//  Handle the stream switch.
//
//  When a stream switch happens, we want to do several things in turn:
//
//  1) Stop the current capturer.
//  2) Release any resources we have allocated (the _AudioClient, _AudioSessionControl (after unregistering for notifications) and 
//        _CaptureClient).
//  3) Wait until the default device has changed (or 500ms has elapsed).  If we time out, we need to abort because the stream switch can't happen.
//  4) Retrieve the new default endpoint for our role.
//  5) Re-instantiate the audio client on that new endpoint.  
//  6) Retrieve the mix format for the new endpoint.  If the mix format doesn't match the old endpoint's mix format, we need to abort because the stream
//      switch can't happen.
//  7) Re-initialize the _AudioClient.
//  8) Re-register for session disconnect notifications and reset the stream switch complete event.
//
bool CWASAPICapture::HandleStreamSwitchEvent()
{
    class ForceValueFalse final
    {
        bool& value_;
    public:
        ForceValueFalse(bool& value) : value_(value)
        {
        }

        ~ForceValueFalse()
        {
            value_ = false;
        }
    };

    assert(_InStreamSwitch);

    ForceValueFalse force_false{ _InStreamSwitch };

    //
    //  Step 1.  Stop capturing.
    //
    auto hr = _AudioClient->Stop();
    if (FAILED(hr))
    {
        printf("Unable to stop audio client during stream switch: %x\n", hr);
        return false;
    }

    //
    //  Step 2.  Release our resources.  Note that we don't release the mix format, we need it for step 6.
    //
    hr = _AudioSessionControl->UnregisterAudioSessionNotification(this);
    if (FAILED(hr))
    {
        printf("Unable to stop audio client during stream switch: %x\n", hr);
        return false;
    }

    _AudioSessionControl.Reset();

    _CaptureClient.Reset();
    _AudioClient.Reset();
    _Endpoint.Reset();

    //
    //  Step 3.  Wait for the default device to change.
    //
    //  There is a race between the session disconnect arriving and the new default device 
    //  arriving (if applicable).  Wait the shorter of 500 milliseconds or the arrival of the 
    //  new default device, then attempt to switch to the default device.  In the case of a 
    //  format change (i.e. the default device does not change), we artificially generate  a
    //  new default device notification so the code will not needlessly wait 500ms before 
    //  re-opening on the new format.  (However, note below in step 6 that in this SDK 
    //  sample, we are unlikely to actually successfully absorb a format change, but a 
    //  real audio application implementing stream switching would re-format their 
    //  pipeline to deliver the new format).  
    //
    const auto waitResult = WaitForSingleObject(_StreamSwitchCompleteEvent, 500);
    if (waitResult == WAIT_TIMEOUT)
    {
        printf("Stream switch timeout - aborting...\n");
        return false;
    }

    //
    //  Step 4.  If we can't get the new endpoint, we need to abort the stream switch.  If there IS a new device,
    //          we should be able to retrieve it.
    //
    hr = _DeviceEnumerator->GetDefaultAudioEndpoint(eCapture, _EndpointRole, &_Endpoint);
    if (FAILED(hr))
    {
        printf("Unable to retrieve new default device during stream switch: %x\n", hr);
        return false;
    }
    //
    //  Step 5 - Re-instantiate the audio client on the new endpoint.
    //
    void* pAudioClient;
    hr = _Endpoint->Activate(__uuidof(IAudioClient), CLSCTX_INPROC_SERVER, nullptr, &pAudioClient);
    if (FAILED(hr))
    {
        printf("Unable to activate audio client on the new endpoint: %x.\n", hr);
        return false;
    }

    _AudioClient.Attach(reinterpret_cast<IAudioClient*>(pAudioClient));

    //
    //  Step 6 - Retrieve the new mix format.
    //
    WAVEFORMATEX* wfxNew;
    hr = _AudioClient->GetMixFormat(&wfxNew);
    if (FAILED(hr))
    {
        printf("Unable to retrieve mix format for new audio client: %x.\n", hr);
        return false;
    }

    //
    //  Note that this is an intentionally naive comparison.  A more sophisticated comparison would
    //  compare the sample rate, channel count and format and apply the appropriate conversions into the capture pipeline.
    //
    if (memcmp(_MixFormat, wfxNew, sizeof(WAVEFORMATEX) + wfxNew->cbSize) != 0)
    {
        printf("New mix format doesn't match old mix format.  Aborting.\n");
        CoTaskMemFree(wfxNew);
        return false;
    }
    CoTaskMemFree(wfxNew);

    //
    //  Step 7:  Re-initialize the audio client.
    //
    if (!InitializeAudioEngine())
    {
        return false;
    }

    //
    //  Step 8: Re-register for session disconnect notifications.
    //
    hr = _AudioClient->GetService(IID_PPV_ARGS(&_AudioSessionControl));
    if (FAILED(hr))
    {
        printf("Unable to retrieve session control on new audio client: %x\n", hr);
        return false;
    }
    hr = _AudioSessionControl->RegisterAudioSessionNotification(this);
    if (FAILED(hr))
    {
        printf("Unable to retrieve session control on new audio client: %x\n", hr);
        return false;
    }

    //
    //  Reset the stream switch complete event because it's a manual reset event.
    //
    ResetEvent(_StreamSwitchCompleteEvent);
    //
    //  And we're done.  Start capturing again.
    //
    hr = _AudioClient->Start();
    if (FAILED(hr))
    {
        printf("Unable to start the new audio client: %x\n", hr);
        return false;
    }

    return true;
}

//
//  Called when an audio session is disconnected.  
//
//  When a session is disconnected because of a device removal or format change event, we just want 
//  to let the capture thread know that the session's gone away
//
HRESULT CWASAPICapture::OnSessionDisconnected(AudioSessionDisconnectReason DisconnectReason)
{
    if (DisconnectReason == DisconnectReasonDeviceRemoval)
    {
        //
        //  The stream was disconnected because the device we're capturing to was removed.
        //
        //  We want to reset the stream switch complete event (so we'll block when the HandleStreamSwitchEvent function
        //  waits until the default device changed event occurs).
        //
        //  Note that we don't set the _StreamSwitchCompleteEvent - that will be set when the OnDefaultDeviceChanged event occurs.
        //
        _InStreamSwitch = true;
        SetEvent(_StreamSwitchEvent);
    }
    if (DisconnectReason == DisconnectReasonFormatChanged)
    {
        //
        //  The stream was disconnected because the format changed on our capture device.
        //
        //  We want to flag that we're in a stream switch and then set the stream switch event (which breaks out of the capturer).  We also
        //  want to set the _StreamSwitchCompleteEvent because we're not going to see a default device changed event after this.
        //
        _InStreamSwitch = true;
        SetEvent(_StreamSwitchEvent);
        SetEvent(_StreamSwitchCompleteEvent);
    }
    return S_OK;
}

//
//  Called when the default capture device changed.  We just want to set an event which lets the stream switch logic know that it's ok to 
//  continue with the stream switch.
//
HRESULT CWASAPICapture::OnDefaultDeviceChanged(EDataFlow Flow, ERole Role, LPCWSTR /*NewDefaultDeviceId*/)
{
    if (Flow == eCapture && Role == _EndpointRole)
    {
        //
        //  The default capture device for our configuredf role was changed.  
        //
        //  If we're not in a stream switch already, we want to initiate a stream switch event.  
        //  We also we want to set the stream switch complete event.  That will signal the capture thread that it's ok to re-initialize the
        //  audio capturer.
        //
        if (!_InStreamSwitch)
        {
            _InStreamSwitch = true;
            SetEvent(_StreamSwitchEvent);
        }
        SetEvent(_StreamSwitchCompleteEvent);
    }
    return S_OK;
}

//
//  IUnknown
//
HRESULT CWASAPICapture::QueryInterface(REFIID Iid, void** Object)
{
    if (Object == nullptr)
    {
        return E_POINTER;
    }
    *Object = nullptr;

    if (Iid == IID_IUnknown)
    {
        *Object = static_cast<IUnknown *>(static_cast<IAudioSessionEvents *>(this));
        AddRef();
    }
    else if (Iid == __uuidof(IMMNotificationClient))
    {
        *Object = static_cast<IMMNotificationClient *>(this);
        AddRef();
    }
    else if (Iid == __uuidof(IAudioSessionEvents))
    {
        *Object = static_cast<IAudioSessionEvents *>(this);
        AddRef();
    }
    else
    {
        return E_NOINTERFACE;
    }
    return S_OK;
}

ULONG CWASAPICapture::AddRef()
{
    return InterlockedIncrement(&_RefCount);
}

ULONG CWASAPICapture::Release()
{
    const auto returnValue = InterlockedDecrement(&_RefCount);
    if (returnValue == 0)
    {
        delete this;
    }
    return returnValue;
}
