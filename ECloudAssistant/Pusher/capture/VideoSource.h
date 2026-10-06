#ifndef VIDEOSOURCE_H
#define VIDEOSOURCE_H

#include <QtGlobal>
#include "VideoFrame.h"

// 视频源公共契约：屏幕采集与摄像头采集共用。
// 只包含取帧、停止与生命周期、尺寸及帧计数；屏幕专属的初始化、GPU 输出配置和
// 共享 D3D11 设备留在 ScreenCapture，摄像头不继承 ScreenCapture。
//
// 帧的底层数据所有权由 VideoFrame 自带的共享指针保证：取用方持有该帧期间数据不会被覆盖。
class VideoSource
{
public:
    virtual ~VideoSource() = default;
    // 阻塞到有新画面可用，返回 false 表示已停止。
    virtual bool WaitLatestFrame(VideoFrame& frame) = 0;
    // 只置停止标志并唤醒等待者：幂等、不 join、不释放缓冲池。
    // 必须在消费者线程 join 之前调用，否则消费者会永久阻塞在条件变量上。
    virtual void RequestStop() = 0;
    // 停止采集线程并释放缓冲。不能在消费者仍持有上一帧 VideoFrame 时调用。
    virtual bool Close() = 0;
    virtual quint32 GetWidth() const = 0;
    virtual quint32 GetHeight() const = 0;
    // 源帧数：采集端实际产出的帧总数，供低频统计计源帧率；
    // 与 VideoFrame::sequence 无关：后者是时钟量化后的时间格序号，用于派生 PTS。
    virtual quint64 GetCapturedFrames() const = 0;
    // 发布帧数：真正发布进最新帧槽位的新画面数。
    virtual quint64 GetPublishedFrames() const = 0;
};

#endif // VIDEOSOURCE_H
