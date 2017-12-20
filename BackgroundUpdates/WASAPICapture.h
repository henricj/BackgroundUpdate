// THIS CODE AND INFORMATION IS PROVIDED "AS IS" WITHOUT WARRANTY OF
// ANY KIND, EITHER EXPRESSED OR IMPLIED, INCLUDING BUT NOT LIMITED TO
// THE IMPLIED WARRANTIES OF MERCHANTABILITY AND/OR FITNESS FOR A
// PARTICULAR PURPOSE.
//
// Copyright (c) Microsoft Corporation. All rights reserved
//
#pragma once
#include <MMDeviceAPI.h>
#include <AudioClient.h>
#include <AudioPolicy.h>

//
//  WASAPI Capture class.
class CWASAPICapture : public IAudioSessionEvents, IMMNotificationClient
{
public:
    typedef std::chrono::duration<REFERENCE_TIME, std::ratio<1, 10000000>> reference_time;

    //  Public interface to CWASAPICapture.
    CWASAPICapture(const Microsoft::WRL::ComPtr<IMMDevice>& Endpoint, bool EnableStreamSwitch, ERole EndpointRole);
    bool Initialize(reference_time EngineLatency);
    void Shutdown();
    bool Start(std::function<void(const uint8_t *, size_t)> read_callback);
    void Stop();
    void read_buffer() const;
    void read_audio();
    WORD ChannelCount() const noexcept { return _MixFormat->nChannels; }
    UINT32 SamplesPerSecond() const noexcept { return _MixFormat->nSamplesPerSec; }
    UINT32 BytesPerSample() const noexcept { return _MixFormat->wBitsPerSample / 8; }
    size_t FrameSize() const noexcept { return _FrameSize; }
    WAVEFORMATEX* MixFormat() const noexcept { return _MixFormat; }
    STDMETHOD_(ULONG, AddRef)() override;
    STDMETHOD_(ULONG, Release)() override;

private:
    ~CWASAPICapture(); // Destructor is private to prevent accidental deletion.
    ULONG _RefCount = 1;
    //
    //  Core Audio Capture member variables.
    //
    Microsoft::WRL::ComPtr<IMMDevice> _Endpoint;
    Microsoft::WRL::ComPtr<IAudioClient> _AudioClient;
    Microsoft::WRL::ComPtr<IAudioCaptureClient> _CaptureClient;

    std::thread _CaptureThread;
    std::exception_ptr capture_thread_exception_;

    HANDLE _ShutdownEvent = nullptr;
    HANDLE _AudioSamplesReadyEvent = nullptr;

    WAVEFORMATEX* _MixFormat = nullptr;
    size_t _FrameSize = 0;
    UINT32 _BufferSize = 0;

    //
    //  Capture buffer management.
    //
    std::function<void(const uint8_t *, size_t)> read_callback_;

    void DoCaptureThread();
    //
    //  Stream switch related members and methods.
    //
    bool _EnableStreamSwitch = false;
    ERole _EndpointRole;
    HANDLE _StreamSwitchEvent = nullptr; // Set when the current session is disconnected or the default device changes.
    HANDLE _StreamSwitchCompleteEvent = nullptr; // Set when the default device changed.
    Microsoft::WRL::ComPtr<IAudioSessionControl> _AudioSessionControl;
    Microsoft::WRL::ComPtr<IMMDeviceEnumerator> _DeviceEnumerator;
    reference_time _EngineLatencyInMS { 0 };
    bool _InStreamSwitch = false;

    bool InitializeStreamSwitch();
    void TerminateStreamSwitch();
    bool HandleStreamSwitchEvent();

    STDMETHOD(OnDisplayNameChanged)(LPCWSTR /*NewDisplayName*/, LPCGUID /*EventContext*/) override { return S_OK; };
    STDMETHOD(OnIconPathChanged)(LPCWSTR /*NewIconPath*/, LPCGUID /*EventContext*/) override { return S_OK; };
    STDMETHOD(OnSimpleVolumeChanged)(float /*NewSimpleVolume*/, BOOL /*NewMute*/, LPCGUID /*EventContext*/) override
    {
        return S_OK;
    }
    STDMETHOD(OnChannelVolumeChanged)(DWORD /*ChannelCount*/, float /*NewChannelVolumes*/[], DWORD /*ChangedChannel*/,
                                      LPCGUID /*EventContext*/) override { return S_OK; };
    STDMETHOD(OnGroupingParamChanged)(LPCGUID /*NewGroupingParam*/, LPCGUID /*EventContext*/) override { return S_OK; };
    STDMETHOD(OnStateChanged)(AudioSessionState /*NewState*/) override { return S_OK; };
    STDMETHOD(OnSessionDisconnected)(AudioSessionDisconnectReason DisconnectReason) override;
    STDMETHOD(OnDeviceStateChanged)(LPCWSTR /*DeviceId*/, DWORD /*NewState*/) override { return S_OK; }
    STDMETHOD(OnDeviceAdded)(LPCWSTR /*DeviceId*/) override { return S_OK; };
    STDMETHOD(OnDeviceRemoved)(LPCWSTR /*DeviceId(*/) override { return S_OK; };
    STDMETHOD(OnDefaultDeviceChanged)(EDataFlow Flow, ERole Role, LPCWSTR NewDefaultDeviceId) override;
    STDMETHOD(OnPropertyValueChanged)(LPCWSTR /*DeviceId*/, const PROPERTYKEY /*Key*/) override { return S_OK; };
    //
    //  IUnknown
    //
    STDMETHOD(QueryInterface)(REFIID iid, void** pvObject) override;

    //
    //  Utility functions.
    //
    bool InitializeAudioEngine();
    bool LoadFormat();
};
