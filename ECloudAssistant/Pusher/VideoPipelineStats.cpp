#include "VideoPipelineStats.h"
#include <QDebug>
#include <QString>

namespace
{
quint64 AverageUs(quint64 totalUs,quint64 frames)
{
    return frames > 0 ? totalUs / frames : 0;
}
}

VideoPipelineStats::VideoPipelineStats()
{
    Reset();
}

void VideoPipelineStats::Reset()
{
    ResetInterval();

    started_ = false;
    intervalBegin_ = std::chrono::steady_clock::time_point();

    hasEncodedSequence_ = false;
    lastEncodedSequence_ = 0;
}

void VideoPipelineStats::ResetInterval()
{
    encodedFrames_ = 0;
    skippedFrames_ = 0;
    waitUs_ = 0;
    waitMaxUs_ = 0;
    convertUs_ = 0;
    encodeUs_ = 0;
    encodedBytes_ = 0;

    pullBlockUs_.Reset();
    encodeLockWaitUs_.Reset();
}

void VideoPipelineStats::NoteFramePulled(quint64 blockUs)
{
    if(!kEnabled)
    {
        return;
    }
    pullBlockUs_.Add(blockUs);
}

void VideoPipelineStats::NoteEncodeLockWait(quint64 waitUs)
{
    if(!kEnabled)
    {
        return;
    }
    encodeLockWaitUs_.Add(waitUs);
}

void VideoPipelineStats::OnFrameEncoded(quint64 sequence, quint64 waitUs, quint64 convertUs, quint64 encodeUs, quint64 frameBytes)
{
    if(!kEnabled)
    {
        return;
    }

    //序号严格单调递增：补发也分配新序号，所以「同序号被编码两次」不可能发生，无需重复计数。
    if(hasEncodedSequence_ && sequence > lastEncodedSequence_ + 1)
    {
        //编码线程落后于采集线程，中间的画面已被覆盖
        skippedFrames_ += sequence - lastEncodedSequence_ - 1;
    }
    hasEncodedSequence_ = true;
    lastEncodedSequence_ = sequence;

    ++encodedFrames_;
    waitUs_ += waitUs;
    convertUs_ += convertUs;
    encodeUs_ += encodeUs;
    encodedBytes_ += frameBytes;
    if(waitUs > waitMaxUs_)
    {
        waitMaxUs_ = waitUs;
    }
}

void VideoPipelineStats::ReportIfDue(std::chrono::steady_clock::time_point now)
{
    if(!kEnabled)
    {
        return;
    }

    if(!started_)
    {
        //第一次调用只用来对齐周期起点，不输出半个周期的不完整数据。
        //本帧的编码记录发生在 intervalBegin_ 之前，必须一并清掉，否则第一个窗口会多算一帧编码。
        started_ = true;
        intervalBegin_ = now;
        ResetInterval();
        return;
    }

    const quint64 elapsedUs = std::chrono::duration_cast<std::chrono::microseconds>(now - intervalBegin_).count();
    if(elapsedUs < kReportIntervalUs)
    {
        return;
    }

    //本窗口编码后的 H.264 码流码率：字节 ×8 转比特，再按窗口实际时长折算 kbps
    const double bitrateKbps = encodedBytes_ * 8.0 * kMicrosecondsPerSecond / elapsedUs / 1000.0;

    qInfo() << QString("[PIPE-STATS] 编码帧率 = %1"
                       " 取帧阻塞均值us = %2 取帧阻塞P95us = %3"
                       " 等待均值us = %4 等待峰值us = %5 转换均值us = %6 编码均值us = %7"
                       " 编码取锁均值us = %8 编码取锁P95us = %9"
                       " 跳帧 = %10 码率kbps = %11 序号 = %12")
                   .arg(encodedFrames_ * kMicrosecondsPerSecond / elapsedUs, 0, 'f', 1)
                   .arg(pullBlockUs_.Mean())
                   .arg(pullBlockUs_.Percentile(0.95))
                   .arg(AverageUs(waitUs_, encodedFrames_))
                   .arg(waitMaxUs_)
                   .arg(AverageUs(convertUs_, encodedFrames_))
                   .arg(AverageUs(encodeUs_, encodedFrames_))
                   .arg(encodeLockWaitUs_.Mean())
                   .arg(encodeLockWaitUs_.Percentile(0.95))
                   .arg(skippedFrames_)
                   .arg(bitrateKbps, 0, 'f', 1)
                   .arg(lastEncodedSequence_);

    intervalBegin_ = now;
    ResetInterval();
}
