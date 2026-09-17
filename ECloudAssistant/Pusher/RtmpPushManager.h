#ifndef RTMPPUSHMANAGER_H
#define RTMPPUSHMANAGER_H
#include <atomic>
#include <thread>
#include <memory>
#include "RtmpPublisher.h"
#include "H264Encoder.h"
#include "VideoPipelineStats.h"
#include <QObject>

class AACEncoder;
class AudioCapture;
class GDIScreenCapture;
class RtmpPushManager : public QObject
{
    Q_OBJECT
public:
    virtual ~RtmpPushManager();
    RtmpPushManager();
public:
    bool Open(const QString& str);
    bool isClose(){return !isConnect.load();}
protected:
    bool Init();
    void Close();
    void EncodeVideo();
    void EncodeAudio();
    void StopEncoder();
    void StopCapture();
    bool IsKeyFrame(const uint8_t* data, uint32_t size);
    void PushVideo(const quint8* data, quint32 size);
    void PushAudio(const quint8* data, quint32 size);
private:
    std::atomic_bool exit_{false};
    std::atomic_bool isConnect{false};
    EventLoop* loop_ = nullptr;
    std::unique_ptr<AACEncoder>  aac_encoder_;
    std::unique_ptr<H264Encoder> h264_encoder_;
    std::shared_ptr<RtmpPublisher> pusher_;
    std::unique_ptr<AudioCapture> audio_Capture_;
    std::unique_ptr<GDIScreenCapture> screen_Capture_;
    std::unique_ptr<std::thread>  audioCaptureThread_ = nullptr;
    std::unique_ptr<std::thread>  videoCaptureThread_ = nullptr;
    //只在视频编码线程使用
    VideoPipelineStats stats_;
};

#endif // RTMPPUSHMANAGER_H
