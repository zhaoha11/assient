#ifndef RTMPPUSHMANAGER_H
#define RTMPPUSHMANAGER_H
#include <atomic>
#include <thread>
#include <memory>
#include "RtmpPublisher.h"
#include "H264Encoder.h"
#include "VideoPipelineStats.h"
#include "ScreenCapture.h"
#include "VideoSource.h"
#include <QObject>

class AACEncoder;
class AudioCapture;
class CameraCapture;
class D3D11SharedContext;
class RtmpPushManager : public QObject
{
    Q_OBJECT
public:
    enum class CaptureBackend { GDI, WGC };
    // 会话级视频源种类：屏幕（GDI/WGC 二选一）或摄像头。摄像头路径独立于 GDI/WGC。
    enum class VideoSourceKind { Screen, Camera };
    virtual ~RtmpPushManager();
    RtmpPushManager();
public:
    bool Open(const QString& str);
    void SetCaptureBackend(CaptureBackend backend) { captureBackend_.store(backend); }
    CaptureBackend GetCaptureBackend() const { return captureBackend_.load(); }
    CaptureBackend GetActiveCaptureBackend() const { return activeCaptureBackend_; }
    // 视频源种类，默认 Screen：只影响屏幕或摄像头这一层，摄像头路径固定 CPU BGRA + 软件编码
    void SetVideoSourceKind(VideoSourceKind kind) { videoSourceKind_.store(kind); }
    VideoSourceKind GetVideoSourceKind() const { return videoSourceKind_.load(); }
    VideoSourceKind GetActiveVideoSourceKind() const { return activeVideoSourceKind_; }
    // 视频编码器种类，默认 Hardware：WGC 会优先尝试 GPU 直通硬编，失败自动回退软编；
    // 退化重建会话时把它设为 Software 即可强制走软编路径。
    void SetEncoderKind(VideoEncoderKind kind) { encoderKind_.store(kind); }
    bool isClose(){return !isConnect.load();}
signals:
    // 运行中编码路径不可恢复地失败，需要上层重建会话（软编路径）
    void videoPathFailed();
protected:
    bool Init();
    // 端到端建一条屏幕采集路径（采集 + 编码 + 音频 + 编码参数），失败时自身已完整回滚
    bool SetupPipeline(CaptureBackend backend,ScreenCapture::CaptureOutput output,
                       D3D11SharedContext* shared,VideoEncoderKind encoderKind);
    // 建立摄像头采集路径。摄像头打开失败明确返回失败，不回退到屏幕推流。
    bool SetupCameraPipeline();
    // 视频源就绪后的公共尾部：视频编码器 + 音频采集/编码 + 编码参数，屏幕与摄像头共用
    bool SetupEncoderAndAudio(quint32 captureWidth, quint32 captureHeight,
                              D3D11SharedContext* shared,VideoEncoderKind encoderKind);
    // 拆除 SetupPipeline 建立的一半资源，可重复调用
    void TeardownPipeline();
    void Close();
    void EncodeVideo();
    void EncodeAudio();
    void StopEncoder();
    void StopCapture();
    void PushVideo(const quint8* data, quint32 size);
    void PushAudio(const quint8* data, quint32 size);
private:
    std::atomic<CaptureBackend> captureBackend_{CaptureBackend::GDI};
    CaptureBackend activeCaptureBackend_{CaptureBackend::GDI};
    std::atomic<VideoSourceKind> videoSourceKind_{VideoSourceKind::Screen};
    VideoSourceKind activeVideoSourceKind_{VideoSourceKind::Screen};
    std::atomic<VideoEncoderKind> encoderKind_{VideoEncoderKind::Hardware};
    std::atomic_bool exit_{false};
    std::atomic_bool isConnect{false};
    std::unique_ptr<EventLoop> loop_;
    // GPU 直通路径下采集与硬编共用的 D3D11 设备；仅在路径 A 创建
    std::unique_ptr<D3D11SharedContext> sharedGpu_;
    QString activePath_;
    std::unique_ptr<AACEncoder>  aac_encoder_;
    std::unique_ptr<H264Encoder> h264_encoder_;
    std::shared_ptr<RtmpPublisher> pusher_;
    std::unique_ptr<AudioCapture> audio_Capture_;
    // 当前会话的活跃视频源：屏幕（GDI/WGC）或摄像头，编码线程只通过 VideoSource 消费
    std::unique_ptr<VideoSource> videoSource_;
    std::unique_ptr<std::thread>  audioCaptureThread_ = nullptr;
    std::unique_ptr<std::thread>  videoCaptureThread_ = nullptr;
    //只在视频编码线程使用
    VideoPipelineStats stats_;
};

#endif // RTMPPUSHMANAGER_H
