#ifndef WGCSCREENCAPTURE_H
#define WGCSCREENCAPTURE_H

#include "ScreenCapture.h"
#include <atomic>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <thread>

class D3D11SharedContext;

class WGCScreenCapture : public ScreenCapture
{
public:
    WGCScreenCapture() = default;
    ~WGCScreenCapture() override;
    WGCScreenCapture(const WGCScreenCapture&) = delete;
    WGCScreenCapture& operator=(const WGCScreenCapture&) = delete;

    bool Init(qint64 display_index = 0) override;
    bool WaitLatestFrame(VideoFrame& frame) override;
    void RequestStop() override;
    bool Close() override;
    quint32 GetWidth() const override { return width_.load(); }
    quint32 GetHeight() const override { return height_.load(); }
    // 源帧数：Read 从 WGC FramePool 实际取走的帧总数（含一次取多帧时被主动丢弃的旧帧）。
    // 与 VideoFrame::sequence 无关：后者是时钟量化后的时间格序号。
    quint64 GetCapturedFrames() const override { return sourceFrames_.load(); }
    // 发布帧数：真正发布进最新帧槽位的新画面数；帧池无新帧时复用上一帧补节拍不算。
    // 源帧数 − 发布帧数 = 低延迟丢旧帧主动丢掉的数量。
    quint64 GetPublishedFrames() const override { return publishedFrames_.load(); }

    // WGC 同时支持 CPU 读回与 GPU 纹理直出；默认保持 CPU 读回。
    bool SetOutput(CaptureOutput output) override;
    // 注入与硬件编码共用的 device 后，采集帧与其纹理落在同一 device 上，才能被 VideoProcessor 消费
    bool SetExternalDevice(D3D11SharedContext* device) override;

private:
    void Run();
    void Publish(const VideoFrame& frame, quint64 sequence);

    std::thread worker_;
    std::atomic<bool> stop_{false};
    std::atomic<quint32> width_{0};
    std::atomic<quint32> height_{0};
    std::atomic<quint64> sourceFrames_{0};     // 源帧数：Read 从池里取走的帧总数
    std::atomic<quint64> publishedFrames_{0};  // 发布帧数：真正发布新画面的次数
    // FrameArrived 事件计数（诊断用）。用 shared_ptr 交给回调自己持有一份：
    // 回调跑在 WGC 工作线程，可能在采集对象析构途中仍在执行，绝不能让它碰本对象的成员。
    std::shared_ptr<std::atomic<quint64>> frameArrived_;
    std::atomic<CaptureOutput> captureOutput_{CaptureOutput::CpuReadback};
    // 外部共享 device（可空）。空时 WGC 自建一个，仅供 CPU 读回路径使用。
    std::atomic<D3D11SharedContext*> externalDevice_{nullptr};
    // 单槽位最新帧：CPU 帧持 owner，GPU 帧持 gpu 句柄，均保证取用期间有效。
    VideoFrame latestFrame_;
    std::mutex mutex_;
    std::condition_variable frameReady_;
    std::condition_variable workerWake_;
    std::condition_variable initReady_;
    bool hasNewFrame_ = false;
    bool stopped_ = true;
    bool initDone_ = false;
    bool initOk_ = false;
};

#endif // WGCSCREENCAPTURE_H
