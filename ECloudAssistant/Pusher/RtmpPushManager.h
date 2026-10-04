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
class ScreenCapture;
class RtmpPushManager : public QObject
{
    Q_OBJECT
public:
    enum class CaptureBackend { GDI, WGC };
    virtual ~RtmpPushManager();
    RtmpPushManager();
public:
    bool Open(const QString& str);
    void SetCaptureBackend(CaptureBackend backend) { captureBackend_.store(backend); }
    CaptureBackend GetCaptureBackend() const { return captureBackend_.load(); }
    CaptureBackend GetActiveCaptureBackend() const { return activeCaptureBackend_; }
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
    std::atomic<CaptureBackend> captureBackend_{CaptureBackend::GDI};
    CaptureBackend activeCaptureBackend_{CaptureBackend::GDI};
    std::atomic_bool exit_{false};
    std::atomic_bool isConnect{false};
    EventLoop* loop_ = nullptr;
    std::unique_ptr<AACEncoder>  aac_encoder_;
    std::unique_ptr<H264Encoder> h264_encoder_;
    std::shared_ptr<RtmpPublisher> pusher_;
    std::unique_ptr<AudioCapture> audio_Capture_;
    std::unique_ptr<ScreenCapture> screen_Capture_;
    std::unique_ptr<std::thread>  audioCaptureThread_ = nullptr;
    std::unique_ptr<std::thread>  videoCaptureThread_ = nullptr;
    //只在视频编码线程使用
    VideoPipelineStats stats_;
};

#endif // RTMPPUSHMANAGER_H
