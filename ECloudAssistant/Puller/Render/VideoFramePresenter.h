#ifndef VIDEOFRAMEPRESENTER_H
#define VIDEOFRAMEPRESENTER_H
#include "AV_Common.h"
#include <mutex>

// 播放线程与 GUI 绘制线程之间的“最新一帧 + 单次刷新通知”交接点。
// 取代原先每帧排队一个携带 AVFramePtr 的 sig_repaint 事件：播放线程只更新
// latestFrame_，并且仅在还没有未执行的刷新通知时才请求再投递一个不含帧的通知；
// GUI 线程执行该通知时只安排一次 update()，真正的取帧、上传纹理和绘制发生在
// paintGL()，绘制成功后才更新已绘制序号（update() 只是异步安排绘制，
// 不能在通知执行时就记成已画完）。
//
// latestFrame_、序号、待处理标记和会话代号都在同一把锁下读写：
// - 清除待处理标记前到达的新帧，会被随后执行的 paintGL() 取到；
// - 清除后到达的新帧，可以投递下一次通知，因此不会丢唤醒；
// - 任何时刻最多只有一个尚未执行的刷新通知，Qt 的 update() 会自然合并绘制请求。
//
// 本类不依赖统计模块，覆盖情况由返回值交给调用方统计。
class VideoFramePresenter
{
public:
    // 播放线程：放入最新一帧的结果。
    struct PublishResult
    {
        bool    notify = false;      // 需要投递一次刷新通知
        bool    overwritten = false; // 覆盖了一帧尚未被 GUI 取走的帧
        quint64 sessionId = 0;       // 投递通知时应附带的会话代号
    };

    // 播放线程：更新 latestFrame_。调用方收到 notify 为 true 时投递刷新通知。
    PublishResult Publish(const AVFramePtr& frame);

    // GUI 线程：刷新通知到达。清除待处理标记并返回 true；
    // sessionId 与当前会话不符说明是上一个会话遗留的排队通知，忽略并返回 false。
    bool OnNotified(quint64 sessionId);

    // GUI 线程：paintGL() 取当前尚未绘制帧的快照；没有新帧时返回 false。
    bool TakeFrameForPaint(AVFramePtr& frame, quint64& seq, quint64& sessionId);

    // GUI 线程：该会话的帧已绘制完成；旧会话返回 false。
    bool MarkPainted(quint64 seq, quint64 sessionId);

    quint64 SessionId() const;

    // 开始或停止拉流时调用：丢弃旧帧与待处理状态，并使旧会话已排队的通知失效。
    void Reset();

private:
    mutable std::mutex mutex_;
    AVFramePtr latestFrame_ = nullptr;
    quint64    sessionId_ = 0;
    quint64    producedSeq_ = 0;  // 已放入 latestFrame_ 的帧序号
    quint64    takenSeq_ = 0;     // GUI 已取走的帧序号
    quint64    paintedSeq_ = 0;   // 已绘制完成的帧序号
    bool       repaintPending_ = false;
};
#endif // VIDEOFRAMEPRESENTER_H
