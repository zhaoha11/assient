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
    lastCapturedSequence_ = 0;

    hasEncodedSequence_ = false;
    lastEncodedSequence_ = 0;
}

void VideoPipelineStats::ResetInterval()
{
    encodedFrames_ = 0;
    duplicateFrames_ = 0;
    skippedFrames_ = 0;
    waitUs_ = 0;
    waitMaxUs_ = 0;
    convertUs_ = 0;
    encodeUs_ = 0;
}

void VideoPipelineStats::OnFrameEncoded(quint64 sequence, quint64 waitUs, quint64 convertUs, quint64 encodeUs)
{
    if(!kEnabled)
    {
        return;
    }

    if(hasEncodedSequence_)
    {
        if(sequence == lastEncodedSequence_)
        {
            //同一张采集帧被再次编码
            ++duplicateFrames_;
        }
        else if(sequence > lastEncodedSequence_ + 1)
        {
            //编码线程落后于采集线程，中间的画面已被覆盖
            skippedFrames_ += sequence - lastEncodedSequence_ - 1;
        }
    }
    hasEncodedSequence_ = true;
    lastEncodedSequence_ = sequence;

    ++encodedFrames_;
    waitUs_ += waitUs;
    convertUs_ += convertUs;
    encodeUs_ += encodeUs;
    if(waitUs > waitMaxUs_)
    {
        waitMaxUs_ = waitUs;
    }
}

void VideoPipelineStats::ReportIfDue(std::chrono::steady_clock::time_point now, quint64 capturedSequence)
{
    if(!kEnabled)
    {
        return;
    }

    if(!started_)
    {
        //第一次调用只用来对齐采集端序号，不输出半个周期的不完整数据。
        //本帧的编码记录发生在 intervalBegin_ 之前，必须一并清掉，
        //否则第一个窗口会多算一帧编码、少算一帧采集。
        started_ = true;
        intervalBegin_ = now;
        lastCapturedSequence_ = capturedSequence;
        ResetInterval();
        return;
    }

    const quint64 elapsedUs = std::chrono::duration_cast<std::chrono::microseconds>(now - intervalBegin_).count();
    if(elapsedUs < kReportIntervalUs)
    {
        return;
    }

    const quint64 capturedFrames = capturedSequence > lastCapturedSequence_
                                   ? capturedSequence - lastCapturedSequence_ : 0;

    qInfo() << QString("[PIPE-STATS] captureFps = %1 encodeFps = %2 captured = %3 encoded = %4 dup = %5 skip = %6"
                       " waitAvgUs = %7 waitMaxUs = %8 convertAvgUs = %9 encodeAvgUs = %10 seq = %11")
                   .arg(capturedFrames * kMicrosecondsPerSecond / elapsedUs, 0, 'f', 1)
                   .arg(encodedFrames_ * kMicrosecondsPerSecond / elapsedUs, 0, 'f', 1)
                   .arg(capturedFrames)
                   .arg(encodedFrames_)
                   .arg(duplicateFrames_)
                   .arg(skippedFrames_)
                   .arg(AverageUs(waitUs_, encodedFrames_))
                   .arg(waitMaxUs_)
                   .arg(AverageUs(convertUs_, encodedFrames_))
                   .arg(AverageUs(encodeUs_, encodedFrames_))
                   .arg(lastEncodedSequence_);

    intervalBegin_ = now;
    lastCapturedSequence_ = capturedSequence;
    ResetInterval();
}
