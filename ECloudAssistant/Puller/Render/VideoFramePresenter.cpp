#include "VideoFramePresenter.h"

VideoFramePresenter::PublishResult VideoFramePresenter::Publish(const AVFramePtr& frame)
{
    PublishResult result;
    std::unique_lock<std::mutex> lock(mutex_);

    // 只有 GUI 尚未取走的旧帧被替换，才计一次实际丢弃
    result.overwritten = (latestFrame_ && producedSeq_ > takenSeq_);

    latestFrame_ = frame;
    ++producedSeq_;
    result.sessionId = sessionId_;

    if(!repaintPending_)
    {
        // 没有未执行的通知时才需要再投递一个，保证任何时刻最多一个待处理通知
        repaintPending_ = true;
        result.notify = true;
    }
    return result;
}

bool VideoFramePresenter::OnNotified(quint64 sessionId)
{
    std::unique_lock<std::mutex> lock(mutex_);
    if(sessionId != sessionId_)
    {
        // 上一个会话遗留的通知，不能清除当前会话的待处理标记
        return false;
    }
    repaintPending_ = false;
    return true;
}

bool VideoFramePresenter::TakeFrameForPaint(AVFramePtr& frame, quint64& seq, quint64& sessionId)
{
    std::unique_lock<std::mutex> lock(mutex_);
    if(!latestFrame_ || producedSeq_ == paintedSeq_)
    {
        // 没有新帧：保留已有纹理，供 resize 等重绘继续使用
        return false;
    }
    frame = latestFrame_;
    seq = producedSeq_;
    sessionId = sessionId_;
    takenSeq_ = seq;
    return true;
}

bool VideoFramePresenter::MarkPainted(quint64 seq, quint64 sessionId)
{
    std::unique_lock<std::mutex> lock(mutex_);
    if(sessionId != sessionId_)
    {
        return false;
    }
    if(seq > paintedSeq_)
    {
        paintedSeq_ = seq;
    }
    return true;
}

quint64 VideoFramePresenter::SessionId() const
{
    std::unique_lock<std::mutex> lock(mutex_);
    return sessionId_;
}

void VideoFramePresenter::Reset()
{
    std::unique_lock<std::mutex> lock(mutex_);
    latestFrame_.reset();
    // 递增会话代号，使上一会话已排队的刷新通知在执行时被忽略
    ++sessionId_;
    producedSeq_ = 0;
    takenSeq_ = 0;
    paintedSeq_ = 0;
    repaintPending_ = false;
}
