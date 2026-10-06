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
    // 已产出的真实采集帧总数，供低频统计计采集帧率。
    // 与 VideoFrame::sequence 无关：后者是时钟量化后的时间格序号。
    quint64 GetCapturedFrames() const override { return capturedFrames_.load(); }

    // WGC 同时支持 CPU 读回与 GPU 纹理直出；默认保持 CPU 读回。
    bool SupportsGpuOutput() const override { return true; }
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
    // 真实采集帧数：每次 Read 取到新帧才 +1，复用上一帧重复发布不计数，供统计用
    std::atomic<quint64> capturedFrames_{0};
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
