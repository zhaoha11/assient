#include "LocalPlayer.h"
#include <QAudioOutput>
#include <QFileInfo>
#include <QMediaPlayer>
#include <QUrl>
#include <QVideoFrame>
#include <QVideoSink>
#include <QVideoWidget>

namespace
{
//把Qt Multimedia的错误类型转换为面向用户的中文提示，界面不接触底层错误码。
QString toChineseError(QMediaPlayer::Error error)
{
    switch(error)
    {
    case QMediaPlayer::ResourceError:
        return QString::fromUtf8("媒体资源无法访问或已损坏");
    case QMediaPlayer::FormatError:
        return QString::fromUtf8("不支持的媒体格式");
    case QMediaPlayer::NetworkError:
        return QString::fromUtf8("网络错误，无法加载媒体");
    case QMediaPlayer::AccessDeniedError:
        return QString::fromUtf8("没有权限读取该文件");
    default:
        return QString::fromUtf8("播放器发生未知错误");
    }
}
}

// 创建并连接Qt Multimedia本地播放后端。
LocalPlayer::LocalPlayer(QObject* parent)
    : QObject(parent)
    , mediaPlayer_(new QMediaPlayer(this))
    , audioOutput_(new QAudioOutput(this))
    , videoWidget_(new QVideoWidget)
{
    mediaPlayer_->setAudioOutput(audioOutput_);
    mediaPlayer_->setVideoOutput(videoWidget_);

    //裁剪铺满：视频按比例放大到铺满整个控件，超出部分裁掉，避免上下或左右出现黑边
    videoWidget_->setAspectRatioMode(Qt::KeepAspectRatioByExpanding);

    //首帧预览：第一帧解码出来就暂停，让打开文件后立即看到画面，而不是等用户点播放
    connect(videoWidget_->videoSink(),&QVideoSink::videoFrameChanged,this,[this](const QVideoFrame&){
        if(!firstFramePending_)
        {
            return;
        }
        firstFramePending_ = false;
        mediaPlayer_->pause();
    });

    connect(mediaPlayer_,&QMediaPlayer::playbackStateChanged,this,[this](QMediaPlayer::PlaybackState state){
        switch(state)
        {
        case QMediaPlayer::PlayingState:
            //首帧预览期间的起播不对外暴露，避免播放按钮闪一下“暂停”
            if(!firstFramePending_)
            {
                UpdatePlaybackState(PlaybackState::Playing);
            }
            break;
        case QMediaPlayer::PausedState:
            UpdatePlaybackState(PlaybackState::Paused);
            break;
        case QMediaPlayer::StoppedState:
            //只把真实的播放/暂停收敛为Stopped，避免覆盖Opening和Error
            if(playbackState_ == PlaybackState::Playing || playbackState_ == PlaybackState::Paused)
            {
                UpdatePlaybackState(PlaybackState::Stopped);
            }
            break;
        }
    });

    //媒体加载完成表示文件可用，从Opening进入Stopped，并起播以解码首帧作为静态预览
    connect(mediaPlayer_,&QMediaPlayer::mediaStatusChanged,this,[this](QMediaPlayer::MediaStatus status){
        if(status == QMediaPlayer::LoadedMedia && playbackState_ == PlaybackState::Opening)
        {
            UpdatePlaybackState(PlaybackState::Stopped);
            if(mediaPlayer_->hasVideo())
            {
                firstFramePending_ = true;
                mediaPlayer_->play();
            }
        }
        //后端判定媒体不可用时不能把状态留在Opening，否则界面会一直停在加载中
        else if(status == QMediaPlayer::InvalidMedia &&
                playbackState_ != PlaybackState::Idle && playbackState_ != PlaybackState::Error)
        {
            //这里只是兜底：若errorOccurred随后到达，它会覆盖为更具体的中文提示
            firstFramePending_ = false;
            UpdatePlaybackState(PlaybackState::Error);
            emit sig_errorOccurred(QString::fromUtf8("无法识别该媒体文件"));
        }
    });

    //统一把底层错误转换为中文提示后再通知界面
    connect(mediaPlayer_,&QMediaPlayer::errorOccurred,this,[this](QMediaPlayer::Error error,const QString&){
        if(error == QMediaPlayer::NoError)
        {
            return;
        }
        firstFramePending_ = false;
        UpdatePlaybackState(PlaybackState::Error);
        emit sig_errorOccurred(toChineseError(error));
    });

    //位置与时长直接转发给界面，由界面更新进度条和时间文本
    connect(mediaPlayer_,&QMediaPlayer::positionChanged,this,[this](qint64 position){
        //Qt多媒体后端在播放/暂停切换的瞬间会上报position=0，并非真实进度；
        //只有停止状态下的0才代表回到起点（停止与打开新文件都依赖它）
        if(position == 0 && mediaPlayer_->playbackState() != QMediaPlayer::StoppedState)
        {
            return;
        }
        position_ = position;
        emit sig_positionChanged(position_);
    });
    connect(mediaPlayer_,&QMediaPlayer::durationChanged,this,[this](qint64 duration){
        duration_ = duration;
        emit sig_durationChanged(duration_);
    });
}

// 返回当前业务播放状态。
LocalPlayer::PlaybackState LocalPlayer::playbackState() const
{
    return playbackState_;
}

// 更新业务状态并通知界面。
void LocalPlayer::UpdatePlaybackState(PlaybackState state)
{
    if(playbackState_ == state)
    {
        return;
    }
    playbackState_ = state;
    emit sig_playbackStateChanged(playbackState_);
}

// 返回内部管理的视频输出控件。
QVideoWidget* LocalPlayer::videoWidget() const
{
    return videoWidget_;
}

// 返回当前播放位置（毫秒）。
qint64 LocalPlayer::position() const
{
    return position_;
}

// 返回当前媒体总时长（毫秒）。
qint64 LocalPlayer::duration() const
{
    return duration_;
}

// 打开本地文件：先重置时间轴，再校验路径，最后交给Qt Multimedia加载。
void LocalPlayer::Open(const QString& path)
{
    //重置时间轴，避免新文件加载前显示上一个文件的位置和时长
    position_ = 0;
    duration_ = 0;
    emit sig_positionChanged(position_);
    emit sig_durationChanged(duration_);

    firstFramePending_ = false;

    const QFileInfo info(path);
    if(!info.exists() || !info.isFile())
    {
        UpdatePlaybackState(PlaybackState::Error);
        mediaPlayer_->stop();
        mediaPlayer_->setSource(QUrl());
        emit sig_errorOccurred(QString::fromUtf8("文件不存在：%1").arg(path));
        return;
    }
    UpdatePlaybackState(PlaybackState::Opening);
    mediaPlayer_->setSource(QUrl::fromLocalFile(info.absoluteFilePath()));
}

// 请求Qt Multimedia开始播放。
void LocalPlayer::Play()
{
    //用户主动操作优先于首帧预览
    firstFramePending_ = false;
    mediaPlayer_->play();
}

// 请求Qt Multimedia暂停播放。
void LocalPlayer::Pause()
{
    firstFramePending_ = false;
    mediaPlayer_->pause();
}

// 请求Qt Multimedia停止播放。
void LocalPlayer::Stop()
{
    firstFramePending_ = false;
    if(playbackState_ == PlaybackState::Opening)
    {
        Close();
        return;
    }
    mediaPlayer_->stop();
}

// 拖动进度条定位：先按缓存时长截断，再把结果立即回发给界面，最后才请求后端跳转。
void LocalPlayer::Seek(qint64 position)
{
    //没有加载完成的媒体时总时长为0，此时任何定位都无意义
    if(duration_ <= 0)
    {
        return;
    }
    position_ = qBound<qint64>(0, position, duration_);
    //立即回发等价于“拖动结果已生效”，界面无需等待后端确认；
    //后端随后上报的同值位置会覆盖这里的结果，两者不冲突
    emit sig_positionChanged(position_);
    mediaPlayer_->setPosition(position_);
}

// 设置音量：截断到0.0～1.0后交给音频输出。
void LocalPlayer::SetVolume(float volume)
{
    audioOutput_->setVolume(qBound(0.0f, volume, 1.0f));
}

// 设置播放倍速：直接交给Qt Multimedia，界面负责只提供受支持的档位。
void LocalPlayer::SetPlaybackRate(qreal rate)
{
    mediaPlayer_->setPlaybackRate(rate);
}

// 关闭当前媒体：先切回Idle，再停止播放并释放媒体源与时间轴。
void LocalPlayer::Close()
{
    //先切回Idle，避免stop()触发的StoppedState把已经关闭的媒体重新标记成可播放
    UpdatePlaybackState(PlaybackState::Idle);
    firstFramePending_ = false;
    mediaPlayer_->stop();
    //空源让Qt Multimedia释放当前文件与解码资源，避免残留旧文件的声音
    mediaPlayer_->setSource(QUrl());
    position_ = 0;
    duration_ = 0;
    emit sig_positionChanged(position_);
    emit sig_durationChanged(duration_);
}
