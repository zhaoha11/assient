#ifndef STATSWINDOW_H
#define STATSWINDOW_H

#include <QtGlobal>
#include <algorithm>
#include <array>

// 固定长度的滑动窗口，用于报告分位与极值：只保留最近 kCapacity 个样本，
// 报点时才排序一次（每秒一次，256 个元素可忽略），既不逐帧落盘也不随运行时长增长。
// 分位取窗口内真实样本，不做插值。
// 样本类型由 T 决定：耗时用 quint64；帧龄可能为负，必须用 qint64，
// 无符号会把负值折叠成巨大正数，min 也就失去意义。
template<typename T>
class SampleWindow
{
public:
    static constexpr quint32 kCapacity = 256;

    void Add(T sample)
    {
        samples_[next_] = sample;
        next_ = (next_ + 1) % kCapacity;
        if(size_ < kCapacity)
        {
            ++size_;
        }
        if(size_ == 1 || sample < min_)
        {
            min_ = sample;
        }
    }

    T Min() const { return size_ ? min_ : T(0); }

    T Mean() const
    {
        if(!size_)
        {
            return T(0);
        }
        //用有符号累加：T 为 quint64 时样本量级远不及 int64 上限，T 为 qint64 时也只在这里有符号
        qint64 sum = 0;
        for(quint32 i = 0; i < size_; ++i)
        {
            sum += static_cast<qint64>(samples_[i]);
        }
        return static_cast<T>(sum / static_cast<qint64>(size_));
    }

    // p 取 [0,1]
    T Percentile(double p) const
    {
        if(!size_)
        {
            return T(0);
        }
        std::copy(samples_.begin(),samples_.begin() + size_,scratch_.begin());
        std::sort(scratch_.begin(),scratch_.begin() + size_);
        quint32 index = static_cast<quint32>(p * (size_ - 1) + 0.5);
        if(index >= size_)
        {
            index = size_ - 1;
        }
        return scratch_[index];
    }

    void Reset()
    {
        next_ = 0;
        size_ = 0;
    }

private:
    std::array<T,kCapacity> samples_{};
    mutable std::array<T,kCapacity> scratch_{};
    quint32 next_ = 0;
    quint32 size_ = 0;
    T min_ = 0;
};

using UsWindow = SampleWindow<quint64>;

#endif // STATSWINDOW_H
