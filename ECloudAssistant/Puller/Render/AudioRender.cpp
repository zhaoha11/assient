#include "AudioRender.h"

AudioRender::AudioRender()
{
    // 解码器输出的是 16 位有符号整数 PCM。
    audioFmt_.setSampleFormat(QAudioFormat::Int16);
}

AudioRender::~AudioRender()
{
    if(audioOut_)
    {
        audioOut_->stop();
        delete audioOut_;
        audioOut_ = nullptr;
        device_ = nullptr;
    }
}

int AudioRender::AvailableBytes()
{
    //获取pcm大小
    if(!audioOut_)
    {
        return -1;
    }
    return static_cast<int>(audioOut_->bytesFree()); // Qt 6 中没有 periodSize()
}

bool AudioRender::InitAudio(int nChannels, int SampleRate, int nSampleSize)
{
    //初始化音频输出
    if(audioOut_ || is_initail_ || device_)
    {
        return true;
    }

    nSampleSize_ = nSampleSize;
    if(nSampleSize_ != 16)
    {
        return false;
    }

    //设置格式
    audioFmt_.setChannelCount(nChannels);
    audioFmt_.setSampleRate(SampleRate);

    // Qt 6 中，原始 PCM 输出由 QAudioSink 完成。
    audioOut_ = new QAudioSink(audioFmt_);
    //设置缓冲区大小
    audioOut_->setBufferSize(409600);
    //设置音量
    audioOut_->setVolume(volume_ / 100.0);
    //创建接入设备
    device_ = audioOut_->start();
    if(!device_)
    {
        delete audioOut_;
        audioOut_ = nullptr;
        return false;
    }
    is_initail_ = true;
    return true;
}

void AudioRender::Write(AVFramePtr frame)
{
    //播放音频
    if(device_ && audioOut_ && nSampleSize_ != -1)
    {
        //获取frame数据
        QByteArray audioData(reinterpret_cast<char*>(frame->data[0]),(frame->nb_samples * frame->channels) * (nSampleSize_ / 8));
        //开始播放
        device_->write(audioData.data(),audioData.size());
        //释放这个frame
        av_frame_unref(frame.get());
    }
}
