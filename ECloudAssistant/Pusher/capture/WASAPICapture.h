#ifndef WASAPI_CAPTURE_H
#define WASAPI_CAPTURE_H
#include <Audioclient.h>
#include <mmdeviceapi.h>
#include <wrl.h>
#include <cstdio>
#include <cstdint>
#include <cstddef>
#include <functional>
#include <mutex>
#include <memory>
#include <atomic>
#include <thread>

class WASAPICapture
{
public:
    typedef std::function<void(const WAVEFORMATEX *mixFormat, uint8_t *data, uint32_t samples)> PacketCallback;
    WASAPICapture();
    WASAPICapture(const WASAPICapture&) = delete;
    WASAPICapture& operator=(const WASAPICapture&) = delete;
    ~WASAPICapture();
    int init();
    int exit();
    int start();
    int stop();
    void setCallback(PacketCallback callback);
    WAVEFORMATEX *getAudioFormat() const
    {
        return m_mixFormat;
    }
private:
    bool m_initialized = false;
    //CoInitialize 返回 S_OK 或 S_FALSE 都要求配对 CoUninitialize，用独立标志跟踪是否欠一次配对
    bool m_comInitialized = false;
    //记录流是否真的 Start 过，Reset 只对已停流的客户端有意义
    bool m_started = false;
    //采集线程自己也会读写，stop/start 可能与线程退出同时发生，必须原子访问
    std::atomic<bool> m_isEnabeld{false};
    int adjustFormatTo16Bits(WAVEFORMATEX *pwfx);
    int capture();
    int startInternal();
    int stopInternal();
    void releaseResources();
    const int REFTIMES_PER_SEC = 10000000;
    const int REFTIMES_PER_MILLISEC = 10000;
    const IID IID_IAudioClient = __uuidof(IAudioClient);
    const IID IID_IAudioCaptureClient = __uuidof(IAudioCaptureClient);
    const IID IID_IMMDeviceEnumerator = __uuidof(IMMDeviceEnumerator);
    const CLSID CLSID_MMDeviceEnumerator = __uuidof(MMDeviceEnumerator);

    std::mutex m_mutex;
    size_t m_pcmBufSize;
    uint32_t m_bufferFrameCount = 0;
    PacketCallback m_callback;
    WAVEFORMATEX *m_mixFormat = NULL;
    std::shared_ptr<uint8_t> m_pcmBuf; //捕获之后pcm缓存的这个pcmBuf中
    REFERENCE_TIME m_hnsActualDuration = 0;
    std::shared_ptr<std::thread> m_threadPtr;
    Microsoft::WRL::ComPtr<IMMDevice> m_device;
    Microsoft::WRL::ComPtr<IAudioClient> m_audioClient;
    Microsoft::WRL::ComPtr<IMMDeviceEnumerator> m_enumerator;
    Microsoft::WRL::ComPtr<IAudioCaptureClient> m_audioCaptureClient;
};
#endif
