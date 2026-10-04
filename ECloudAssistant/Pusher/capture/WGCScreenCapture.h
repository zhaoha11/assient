#ifndef WGCSCREENCAPTURE_H
#define WGCSCREENCAPTURE_H

#include "ScreenCapture.h"
#include <atomic>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

class WGCScreenCapture : public ScreenCapture
{
public:
    WGCScreenCapture() = default;
    ~WGCScreenCapture() override;
    WGCScreenCapture(const WGCScreenCapture&) = delete;
    WGCScreenCapture& operator=(const WGCScreenCapture&) = delete;

    bool Init(qint64 display_index = 0) override;
    bool WaitLatestFrame(CaptureFrameView& frame) override;
    void RequestStop() override;
    bool Close() override;
    quint32 GetWidth() const override { return width_.load(); }
    quint32 GetHeight() const override { return height_.load(); }
    quint64 GetCaptureSequence() const override { return captureSequence_.load(); }

private:
    void Run();
    void Publish(std::shared_ptr<const std::vector<quint8>> pixels, quint32 width, quint32 height,
                 quint64 sequence);

    std::thread worker_;
    std::atomic<bool> stop_{false};
    std::atomic<quint32> width_{0};
    std::atomic<quint32> height_{0};
    std::atomic<quint64> captureSequence_{0};
    std::shared_ptr<const std::vector<quint8>> latestPixels_;
    quint32 latestWidth_ = 0;
    quint32 latestHeight_ = 0;
    quint64 latestSequence_ = 0;
    std::chrono::steady_clock::time_point latestCapturedAt_;
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
