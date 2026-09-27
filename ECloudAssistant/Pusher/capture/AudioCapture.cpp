#include "AudioCapture.h"
#include "AudioBuffer.h"
#include "WASAPICapture.h"
#include<QDebug>

AudioCapture::AudioCapture()
    :capture_(nullptr)
    ,audio_buffer_(nullptr)
{
    capture_.reset(new WASAPICapture());
}

AudioCapture::~AudioCapture()
{
    //保证采集线程和 COM 被释放，即使调用方忘了 Close()
    Close();
}

bool AudioCapture::Init(uint32_t size)
{
    if(is_initailed_)
    {
        return true;
    }

    if(capture_->init() < 0)
    {
        return false;
    }

    WAVEFORMATEX* audoFmt = capture_->getAudioFormat();
    if(audoFmt == nullptr)
    {
        capture_->exit();
        return false;
    }
    channels_ = audoFmt->nChannels;
    samplerate_ = audoFmt->nSamplesPerSec;
    bits_per_sample_ = audoFmt->wBitsPerSample;

    //创建buffer
    audio_buffer_.reset(new AudioBuffer(size));
    //启动捕获器来捕获音频
    if(StartCapture() < 0)
    {
        //启动失败要回滚已经取得的设备资源，下一次 Init 才能重新初始化成功
        capture_->exit();
        audio_buffer_.reset();
        return false;
    }
    is_initailed_ = true;
    return true;
}

void AudioCapture::Close()
{
    StopCapture();
    if(capture_)
    {
        capture_->exit();
    }
    is_initailed_ = false;
}

int AudioCapture::GetSamples()
{
    //从缓冲区获取当前有多少音频数据
    if(!audio_buffer_)
    {
        return 0;
    }
    return audio_buffer_->size() * 8 / bits_per_sample_ / channels_;
}

int AudioCapture::Read(uint8_t *data, uint32_t samples)
{
    //从缓冲区读数据
    if(samples > this->GetSamples()) //说明数不足
    {
        return 0;
    }
    audio_buffer_->read((char*)data,samples * bits_per_sample_ / 8 * channels_);
    return samples;
}

int AudioCapture::StartCapture()
{
    //开始捕获
    capture_->setCallback([this](const WAVEFORMATEX *mixFormat, uint8_t *data, uint32_t samples){
        channels_ = mixFormat->nChannels;
        samplerate_ = mixFormat->nSamplesPerSec;
        bits_per_sample_ = mixFormat->wBitsPerSample;
        //静音包在这里也是一份长度确定的零数据，按实际字节数写入即可
        const uint32_t byteCount = mixFormat->nBlockAlign * samples;
        if(!audio_buffer_ || byteCount == 0)
        {
            return;
        }
        //将数据写入缓冲区
        audio_buffer_->write((char*)data,byteCount);
    });

    //音频需要清空来去缓存音频数据：重启后不能把上一轮残留的采样送给编码器
    if(audio_buffer_)
    {
        audio_buffer_->clear();
    }
    //开始捕获音频
    if(capture_->start() < 0)
    {
        return -1;
    }
    is_stared_ = true;
    return 0;
}

int AudioCapture::StopCapture()
{
    //stop() 幂等，无论是否处于启动状态都调用，避免线程能起来的路径漏掉回收
    if(capture_)
    {
        capture_->stop();
    }
    is_stared_ = false;
    return 0;
}
