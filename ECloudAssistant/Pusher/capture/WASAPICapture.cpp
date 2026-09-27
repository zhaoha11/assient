#include "WASAPICapture.h"
#include <QDebug>
#include <chrono>
#include <cstring>
#include <system_error>

WASAPICapture::WASAPICapture()
{
    m_pcmBufSize = 4096;
    m_pcmBuf.reset(new uint8_t[m_pcmBufSize],std::default_delete<uint8_t[]>());
}

WASAPICapture::~WASAPICapture()
{
    //保证采集线程被回收、COM 被配对释放，即使调用方忘了 exit()
    exit();
}

void WASAPICapture::releaseResources()
{
    //必须先停掉采集线程再释放接口，否则线程可能正在用已释放的 IAudioCaptureClient
    m_audioCaptureClient.Reset();
    m_audioClient.Reset();
    m_device.Reset();
    m_enumerator.Reset();

    //GetMixFormat 用 CoTaskMemAlloc 分配，必须配对释放，否则每次重试都会泄漏这份格式
    if(m_mixFormat)
    {
        CoTaskMemFree(m_mixFormat);
        m_mixFormat = NULL;
    }

    m_bufferFrameCount = 0;
    m_hnsActualDuration = 0;
    m_started = false;

    if(m_comInitialized)
    {
        CoUninitialize();
        m_comInitialized = false;
    }
}

int WASAPICapture::init()
{
    std::lock_guard<std::mutex> lock(m_mutex);

    if(m_initialized)
    {
        return 0;
    }

    //上一次 init 中途失败时可能已经取得部分接口，先清干净。
    //这里的 Reset 同时会释放旧指针：GetAddressOf 不释放，直接复用会覆盖并泄漏。
    releaseResources();

    //初始化这个COM库
    HRESULT hr = CoInitialize(NULL);
    if(FAILED(hr))
    {
        qDebug() << "CoInitialize failed";
        return -1;
    }
    m_comInitialized = true;

    hr = CoCreateInstance(CLSID_MMDeviceEnumerator,NULL,CLSCTX_ALL,IID_IMMDeviceEnumerator,(void**)m_enumerator.ReleaseAndGetAddressOf());
    if(FAILED(hr))
    {
        qDebug() << "CoCreateInstance failed";
        releaseResources();
        return -1;
    }

    hr = m_enumerator->GetDefaultAudioEndpoint(eRender,eMultimedia,m_device.ReleaseAndGetAddressOf());
    if(FAILED(hr))
    {
        qDebug() << "GetDefaultAudioEndpoint failed";
        releaseResources();
        return -1;
    }

    //激活音频设备
    hr = m_device->Activate(IID_IAudioClient,CLSCTX_ALL,NULL,(void**)m_audioClient.ReleaseAndGetAddressOf());
    if(FAILED(hr))
    {
        qDebug() << "Activate failed";
        releaseResources();
        return -1;
    }

    //获取音频格式
    hr = m_audioClient->GetMixFormat(&m_mixFormat);
    if(FAILED(hr))
    {
        qDebug() << "GetMixFormat failed";
        releaseResources();
        return -1;
    }

    //调整输出格式为16方便后续编码
    adjustFormatTo16Bits(m_mixFormat);

    //初始化音频客户端

    hr = m_audioClient->Initialize(AUDCLNT_SHAREMODE_SHARED,AUDCLNT_STREAMFLAGS_LOOPBACK,REFTIMES_PER_SEC,0,m_mixFormat,NULL);
    if(FAILED(hr))
    {
        qDebug() << "Initialize failed";
        releaseResources();
        return -1;
    }

    //获取缓冲区大小
    hr = m_audioClient->GetBufferSize(&m_bufferFrameCount);
    if(FAILED(hr))
    {
        qDebug() << "GetBufferSize failed";
        releaseResources();
        return -1;
    }

    //获取音频服务

    hr = m_audioClient->GetService(IID_IAudioCaptureClient,(void**)m_audioCaptureClient.ReleaseAndGetAddressOf());
    if(FAILED(hr))
    {
        qDebug() << "GetService failed";
        releaseResources();
        return -1;
    }

    //计算这个buffer的时长
    m_hnsActualDuration = REFERENCE_TIME(REFTIMES_PER_SEC * m_bufferFrameCount / m_mixFormat->nSamplesPerSec);
    m_initialized = true;
    return 0;
}

int WASAPICapture::exit()
{
    std::lock_guard<std::mutex> lock(m_mutex);

    //停止采集线程并释放接口，使下一次 init() 能从干净状态重新初始化
    stopInternal();
    m_initialized = false;
    releaseResources();
    return 0;
}

int WASAPICapture::start()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return startInternal();
}

int WASAPICapture::startInternal()
{
    if(!m_initialized)
    {
        return -1;
    }

    if(m_isEnabeld.load())
    {
        return 0;
    }

    //上一轮采集线程可能已因 capture() 失败自行退出，先回收。
    //直接 reset 一个 joinable 的线程会触发 std::terminate。
    if(m_threadPtr)
    {
        if(m_threadPtr->joinable())
        {
            m_threadPtr->join();
        }
        m_threadPtr.reset();
    }

    HRESULT hr = m_audioClient->Start();
    if(FAILED(hr))
    {
        qDebug() << "m_audioClient->Start() failed";
        return -1;
    }

    m_isEnabeld.store(true);
    m_started = true;
    try
    {
        m_threadPtr.reset(new std::thread([this](){
            while(this->m_isEnabeld.load())
            {
                if(this->capture() < 0)
                {
                    break;
                }
            }
            //线程自行退出时清掉标志，否则下一次 start() 会误判成「已在运行」而不再起线程
            this->m_isEnabeld.store(false);
        }));
    }
    catch(const std::system_error& e)
    {
        qDebug() << "capture thread create failed" << e.what();
        m_isEnabeld.store(false);
        m_audioClient->Stop();
        return -1;
    }
    return 0;
}

int WASAPICapture::stop()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return stopInternal();
}

int WASAPICapture::stopInternal()
{
    //先清标志再 join，否则线程会一直看到「仍在运行」而永不退出
    m_isEnabeld.store(false);

    if(m_threadPtr)
    {
        if(m_threadPtr->joinable())
        {
            m_threadPtr->join();
        }
        m_threadPtr.reset();
    }

    if(m_audioClient)
    {
        HRESULT hr = m_audioClient->Stop();
        if(FAILED(hr))
        {
            qDebug() << "m_audioClient->Stop() failed";
            return -1;
        }

        //丢弃停止前还没被读走的那部分采集数据。不丢的话，同一个客户端再次 Start 时
        //会把停止前最后几个周期的音频当成新数据重新投递（实测重启后稳定多出约 20ms 旧音频）。
        //Reset 只能对已停止的流调用，此时线程已 join、Stop 已返回，正好允许。
        if(m_started)
        {
            const HRESULT resetHr = m_audioClient->Reset();
            if(FAILED(resetHr))
            {
                qDebug() << "m_audioClient->Reset() failed" << static_cast<long>(resetHr);
            }
            m_started = false;
        }
    }
    return 0;
}

void WASAPICapture::setCallback(PacketCallback callback)
{
    m_callback = callback;
}

int WASAPICapture::adjustFormatTo16Bits(WAVEFORMATEX *pwfx)
{
    //设配16位
    if(pwfx->wFormatTag == WAVE_FORMAT_IEEE_FLOAT)
    {
        pwfx->wFormatTag = WAVE_FORMAT_PCM; /////////////////////////// ==
    }
    else if(pwfx->wFormatTag == WAVE_FORMAT_EXTENSIBLE)
    {
        PWAVEFORMATEXTENSIBLE pEx = reinterpret_cast<PWAVEFORMATEXTENSIBLE>(pwfx);
        if(IsEqualGUID(KSDATAFORMAT_SUBTYPE_IEEE_FLOAT,pEx->SubFormat))
        {
            pEx->SubFormat = KSDATAFORMAT_SUBTYPE_PCM;
            pEx->Samples.wValidBitsPerSample = 16;
        }
    }
    else
    {
        return -1;
    }
    pwfx->wBitsPerSample = 16;
    pwfx->nBlockAlign = pwfx->nChannels * pwfx->wBitsPerSample / 8;
    pwfx->nAvgBytesPerSec = pwfx->nBlockAlign * pwfx->nSamplesPerSec;
    return 0;
}

int WASAPICapture::capture()
{
    HRESULT hr = S_OK;
    uint32_t packetLenght = 0;

    //获取下一个包大小
    hr = m_audioCaptureClient->GetNextPacketSize(&packetLenght);
    if(FAILED(hr))
    {
        qDebug() << "GetNextPacketSize failed";
        return -1;
    }

    if(packetLenght == 0) //没有数据
    {
        //必须让出 CPU，否则采集线程会空转占满一个核
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
        return 0;
    }
    //有数据
    while(packetLenght > 0)
    {
        BYTE* pData = nullptr;
        uint32_t numFrameAvailabel = 0;
        DWORD flags = 0;

        hr = m_audioCaptureClient->GetBuffer(&pData,&numFrameAvailabel,&flags,NULL,NULL);
        if(FAILED(hr))
        {
            qDebug() << "m_audioCaptureClient->GetBuffer failed";
            return -1;
        }

        const size_t packetBytes = static_cast<size_t>(numFrameAvailabel) * m_mixFormat->nBlockAlign;

        if(m_pcmBufSize < packetBytes) //缓冲区大小不足，需要扩容
        {
            m_pcmBufSize = packetBytes;
            m_pcmBuf.reset(new uint8_t[m_pcmBufSize],std::default_delete<uint8_t[]>());
        }

        if(flags & AUDCLNT_BUFFERFLAGS_SILENT)
        {
            //静音包只有「这段是静音」的语义，pData 的内容没有定义，
            //必须按本次包的有效字节数给出确定的零数据，由回调按 samples 长度读取
            memset(m_pcmBuf.get(),0,packetBytes);
        }
        else if(pData == nullptr)
        {
            //非静音却没有数据指针属于非法状态，不能把未定义的指针当 PCM 复制
            qDebug() << "GetBuffer returned null data without SILENT flag";
            m_audioCaptureClient->ReleaseBuffer(numFrameAvailabel);
            return -1;
        }
        else
        {
            memcpy(m_pcmBuf.get(),pData,packetBytes);
        }

        if(m_callback)
        {
            //统一回调 m_pcmBuf：静音与正常数据都指向一份长度确定、内容确定的缓冲
            m_callback(m_mixFormat,m_pcmBuf.get(),numFrameAvailabel);
        }

        //释放这个缓存：上面每个分支都要成对调用，否则采集客户端会一直占着这块缓冲
        hr = m_audioCaptureClient->ReleaseBuffer(numFrameAvailabel);
        if(FAILED(hr))
        {
            qDebug() << "ReleaseBuffer failed";
            return -1;
        }

        //获取下一个包的大小；
        hr = m_audioCaptureClient->GetNextPacketSize(&packetLenght);
        if(FAILED(hr))
        {
            qDebug() << "GetNextPacketSize failed";
            return -1;
        }
    }
    return 0;
}
