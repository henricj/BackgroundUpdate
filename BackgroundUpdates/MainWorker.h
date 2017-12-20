#pragma once

#include "HandlerThread.h"
#include "YetAnotherThreadPool.h"
#include "WindowsQueueWorkItemThreadPool.h"

class CWASAPICapture;

template<class T, int Align>
class BufferPool;

template<typename T, size_t Size, int Align>
struct alignas(Align)
    AudioBuffer;

template<typename T, size_t Size, int Align>
class AudioDemux;

class MainWorker
{
public:
    MainWorker();
    ~MainWorker();

    void Start();
    void Stop();
private:
    HandlerThread<> main_thread_;
    WindowsQueueWorkItemThreadPool background_pool_;

    Microsoft::WRL::ComPtr<CWASAPICapture> audio_capture_;
    bool audio_started_ = false;

    std::shared_ptr<BufferPool<AudioBuffer<float, 4096, 32>, 32>> float_pool_;
    std::shared_ptr<BufferPool<AudioBuffer<uint16_t, 4096, 32>, 32>> short_pool_;

    std::unique_ptr<AudioDemux<float, 4096, 32>> float_demux_;

    void Init();
};
