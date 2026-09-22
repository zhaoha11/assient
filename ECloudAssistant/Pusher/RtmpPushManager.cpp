#include "RtmpPushManager.h"
#include "GDISreenScapture.h"
#include <chrono>
#include "AAC_Encoder.h"
#include "H264Encoder.h"
#include "AudioCapture.h"
#include "H264Paraser.h"

RtmpPushManager::~RtmpPushManager()
{
    Close();
}

RtmpPushManager::RtmpPushManager()
    :aac_encoder_(nullptr)
    ,h264_encoder_(nullptr)
    ,pusher_(nullptr)
    ,audio_Capture_(nullptr)
    ,screen_Capture_(nullptr)
{
    loop_ = new EventLoop(1);
}

bool RtmpPushManager::Open(const QString &str)
{
    exit_.store(false);
    isConnect.store(false);
    if(!Init())
    {
        return false;
    }
    //通过推流器打开这个url
    const int openUrlResult = pusher_->OpenUrl(str.toStdString(),1000);
    if(openUrlResult < 0) //解析url失败
    {
        qWarning() << "RTMP OpenUrl failed, url =" << str;
        Close();
        return false;
    }

    constexpr int publishTimeoutMs = 5000;
    constexpr int checkIntervalMs = 10;
    int waitedMs = 0;
    while(!pusher_->IsPublishing())
    {
        if(!pusher_->IsConnected())
        {
            qWarning() << "RTMP disconnected before publish, url =" << str;
            Close();
            return false;
        }

        if(waitedMs >= publishTimeoutMs)
        {
            qWarning() << "RTMP publish timeout after" << publishTimeoutMs << "ms, url =" << str;
            Close();
            return false;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(checkIntervalMs));
        waitedMs += checkIntervalMs;
    }

    isConnect.store(true);

    //开始采集视频
    videoCaptureThread_.reset(new std::thread([this](){
        this->EncodeVideo();
    }));
    //开始采集音频
    audioCaptureThread_.reset(new std::thread([this](){
        this->EncodeAudio();
    }));
    return true;
}

bool RtmpPushManager::Init()
{
    //创建一个推流器
    pusher_ = RtmpPublisher::Create(loop_);
    //设置块大小
    pusher_->SetChunkSize(60000);

    //创建视频采集
    //准备“原始画面来源”和“压缩器”。
    screen_Capture_.reset(new GDIScreenCapture()); //采集器采集的像素要跟这个编码器初始化一致
    if(!screen_Capture_->Init())
    {
        qWarning() << "screen capture init failed";
        return false;
    }

    //视频编码
    const quint32 captureWidth = screen_Capture_->GetWidth();
    const quint32 captureHeight = screen_Capture_->GetHeight();
    const qint32 encodeWidth = static_cast<qint32>(captureWidth & ~1U);
    const qint32 encodeHeight = static_cast<qint32>(captureHeight & ~1U);
    if(encodeWidth <= 0 || encodeHeight <= 0)
    {
        qWarning() << "invalid capture size" << captureWidth << "x" << captureHeight;
        return false;
    }
    h264_encoder_.reset(new H264Encoder());
    if(!h264_encoder_->OPen(encodeWidth,encodeHeight,kTargetFramerate,12000,kCapturePixelFormat))//12000 kbps
    {
        qWarning() << "H.264 encoder init failed" << encodeWidth << "x" << encodeHeight;
        return false;
    }
    audio_Capture_.reset(new AudioCapture());
    if(!audio_Capture_->Init())
    {
        qWarning() << "audio capture init failed";
        return false;
    }
    //音频编码
    aac_encoder_.reset(new AACEncoder());
    //初始化
    if(!aac_encoder_->Open(audio_Capture_->GetSamplerate(),audio_Capture_->GetChannels(),AV_SAMPLE_FMT_S16,64))//48000 Hz、双声道、S16 的 PCM
    {
        qWarning() << "AAC encoder init failed";
        return false;
    }
    //获取音频视频编码参数
    MediaInfo mediaInfo;
    uint8_t extradata[1024] = {0};
    int extradatdSize = 0;

    //获取H264编码参数
    extradatdSize = h264_encoder_->GetSequenceParams(extradata,1024);
    if(extradatdSize <= 0)
    {
        qWarning() << "H.264 sequence parameters unavailable";
        return false;
    }
    //获取sps pps
    H264Paraser::Nal sps = H264Paraser::findNal(extradata,extradatdSize);
    if(sps.first != nullptr && sps.second != nullptr && (*sps.first & 0x1f) == 7)//sps数据
    {
        mediaInfo.sps_size = sps.second - sps.first + 1;
        mediaInfo.sps.reset(new uint8_t[mediaInfo.sps_size],std::default_delete<uint8_t[]>());
        memcpy(mediaInfo.sps.get(),sps.first,mediaInfo.sps_size);
        //pps
        H264Paraser::Nal pps = H264Paraser::findNal(sps.second,extradatdSize - (sps.second - (uint8_t*)extradata));
        if(pps.first != nullptr && pps.second != nullptr && (*pps.first & 0x1f) == 8)//pps数据
        {
            mediaInfo.pps_size = pps.second - pps.first + 1;
            mediaInfo.pps.reset(new uint8_t[mediaInfo.pps_size],std::default_delete<uint8_t[]>());
            memcpy(mediaInfo.pps.get(),pps.first,mediaInfo.pps_size);
        }
    }
    //添加音频参数
    uint32_t audioExtraSize = aac_encoder_->GetSpecificConfig(extradata,1024);

    mediaInfo.audio_specific_config_size = audioExtraSize;
    mediaInfo.audio_specific_config.reset(new uint8_t[mediaInfo.audio_specific_config_size],std::default_delete<uint8_t[]>());
    //拷贝数据
    memcpy(mediaInfo.audio_specific_config.get(),extradata,audioExtraSize);

    //发送这个编码参数
    pusher_->SetMediaInfo(mediaInfo);
    return true;
}

void RtmpPushManager::Close()
{
    //释放资源
    exit_.store(true);//结束线程
    isConnect.store(false);

    //必须在 join 编码线程之前唤醒阻塞在条件变量上的它，否则这里会永久等待。
    //此处不 join、不释放缓冲池，所以调用 Close() 的线程不会和采集线程相互等待。
    if(screen_Capture_)
    {
        screen_Capture_->RequestStop();
    }

    StopEncoder();

    if(pusher_)
    {
        if(pusher_->IsConnected())
        {
            pusher_->Close();
        }
        pusher_.reset();
        pusher_ = nullptr;
    }

    StopCapture();

}

void RtmpPushManager::EncodeVideo()
{
    stats_.Reset();
    //线程运行期间 screen_Capture_ 不会被 reset，取一次裸指针避免每轮判空
    GDIScreenCapture* capture = screen_Capture_.get();
    CaptureFrameView view;
    //编码 PTS 以本次编码线程的首帧为原点，重新推流时自然从 0 重新对齐
    quint64 firstSequence = 0;
    bool hasFirstSequence = false;
    //编码节拍完全由采集端的新帧通知驱动，这里不再有轮询和固定休眠
    while(!exit_.load() && isConnect.load() && capture)
    {
        if(!capture->WaitLatestFrame(view))
        {
            break;//采集已停止
        }
        if(!h264_encoder_ || !pusher_)
        {
            break;
        }

        //PTS 取采集序号差而非「已编码帧计数」：编码落后跳过采集帧时序号照样跳跃，
        //时间戳才对应画面真实发生的时刻。编码器时间基是 1/帧率，序号差即帧间隔。
        if(!hasFirstSequence)
        {
            firstSequence = view.sequence;
            hasFirstSequence = true;
        }
        const qint64 framePts = static_cast<qint64>(view.sequence - firstSequence);

        //采集完成到编码开始的等待时间，三缓冲下就只剩条件变量的唤醒延迟
        const quint64 waitUs = std::chrono::duration_cast<std::chrono::microseconds>(
                                   std::chrono::steady_clock::now() - view.capturedAt).count();
        //采集之后，我们需要编码
        std::vector<quint8> out_frame;
        VideoEncodeTiming timing;
        if(h264_encoder_->Encode(view.data,view.width,view.height,framePts,out_frame,&timing) > 0)
        {
            //编码之后开始推送
            if(out_frame.size() > 0)
            {
                PushVideo(&out_frame[0],out_frame.size());
            }
            stats_.OnFrameEncoded(view.sequence,waitUs,timing.convertUs,timing.encodeUs);
        }
        //每秒汇总一次，采集帧数直接从采集端序号取，避免编码线程漏采帧被忽略
        stats_.ReportIfDue(std::chrono::steady_clock::now(),capture->GetCaptureSequence());
    }
    //退出时补一行日志，把「停滞」和「死锁」区分开。
    //括号不能省：<< 优先级高于 ?:，否则整行会被当成条件表达式
    qInfo() << "[PIPE-STATS] encode thread exit, captured ="
            << (capture ? capture->GetCaptureSequence() : 0);
}

void RtmpPushManager::EncodeAudio()
{
    //准备buffer存放音频数据
    std::shared_ptr<uint8_t> pcm_buffer(new uint8_t[48000 * 8],std::default_delete<uint8_t[]>());
    //获取样本数
    uint32_t frame_samples = aac_encoder_->GetFrames();
    while(!exit_.load() && isConnect.load())
    {
        if(audio_Capture_->GetSamples() >= (int)frame_samples)
        {
            if(audio_Capture_->Read(pcm_buffer.get(),frame_samples) != frame_samples)
            {
                continue;//数据不全
            }
            //编码aac
            AVPacketPtr pkt_ptr = aac_encoder_->Encode(pcm_buffer.get(),frame_samples);
            if(pkt_ptr)
            {
                //推送
                PushAudio(pkt_ptr->data,pkt_ptr->size);
            }
        }
        else
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }
}

void RtmpPushManager::StopEncoder()
{
    if(audioCaptureThread_)
    {
        //结束线程
        audioCaptureThread_->join();
        audioCaptureThread_.reset();
        audioCaptureThread_ = nullptr;
    }

    if(videoCaptureThread_)
    {
        //结束线程
        videoCaptureThread_->join();
        videoCaptureThread_.reset();
        videoCaptureThread_ = nullptr;
    }

    //编码器
    if(h264_encoder_)
    {
        h264_encoder_->Close();
        h264_encoder_.reset();
        h264_encoder_ = nullptr;
    }

    if(aac_encoder_)
    {
        aac_encoder_->Close();
        aac_encoder_.reset();
        aac_encoder_ = nullptr;
    }
}

void RtmpPushManager::StopCapture()
{
    if(audio_Capture_)
    {
        audio_Capture_->Close();
        audio_Capture_.reset();
        audio_Capture_ = nullptr;
    }

    if(screen_Capture_)
    {
        screen_Capture_->Close();
        screen_Capture_.reset();
        screen_Capture_ = nullptr;
    }
}

bool RtmpPushManager::IsKeyFrame(const uint8_t *data, uint32_t size)
{
    //判断关键帧 startcode 3 4
    int startcode = 0;
    if(data[0] == 0 && data[1] == 0 && data[2] == 0)
    {
        startcode = 3;
    }
    else if(data[0] == 0 && data[1] == 0 && data[2] == 0 && data[3] == 0)
    {
        startcode = 4;
    }

    //再去获取类型
    int type = data[startcode] & 0x1f;
    if(type == 5 || type == 7)//关键帧
    {
        return true;
    }
    return false;
}

void RtmpPushManager::PushVideo(const quint8 *data, quint32 size)
{
    //推送视频
    //准备buffer size = video size - 4 //startcode
    std::shared_ptr<uint8_t> frame(new uint8_t[size - 4],std::default_delete<uint8_t[]>());
    //拷贝数据
    memcpy(frame.get(),data + 4,size - 4);
    if(size > 0)
    {
        if(pusher_ && pusher_->IsConnected())
        {
            pusher_->PushVideoFrame(frame.get(),size - 4);
        }
    }

}

void RtmpPushManager::PushAudio(const quint8 *data, quint32 size)
{
    std::shared_ptr<uint8_t> frame(new uint8_t[size],std::default_delete<uint8_t[]>());
    //拷贝数据
    memcpy(frame.get(),data,size);
    if(size > 0)
    {
        if(pusher_ && pusher_->IsConnected())
        {
            pusher_->PushAudioFrame(frame.get(),size);
        }
    }
}
