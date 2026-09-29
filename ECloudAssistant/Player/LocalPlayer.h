#ifndef LOCALPLAYER_H
#define LOCALPLAYER_H

#include <QObject>
#include <QString>

class QAudioOutput;
class QMediaPlayer;
class QVideoWidget;

//本地播放后端：封装Qt Multimedia，向界面暴露播放状态、中文错误提示和视频输出控件。
class LocalPlayer : public QObject
{
    Q_OBJECT
public:
    enum class PlaybackState
    {
        Idle,
        Opening,
        Playing,
        Paused,
        Stopped,
        Error
    };
    Q_ENUM(PlaybackState)

    explicit LocalPlayer(QObject* parent = nullptr);
    PlaybackState playbackState() const;
    void UpdatePlaybackState(PlaybackState state);
    //返回内部管理的视频输出控件，由界面负责放入自己的布局
    QVideoWidget* videoWidget() const;
    //当前播放位置与媒体总时长，单位毫秒
    qint64 position() const;
    qint64 duration() const;
    //打开本地文件：校验路径后交给Qt Multimedia加载
    void Open(const QString& path);
    void Play();
    void Pause();
    void Stop();
    //把播放位置移动到指定毫秒，取值会被限制在0～总时长之间，无媒体时忽略
    void Seek(qint64 position);
    //设置音量，取值0.0～1.0，超出范围会被截断
    void SetVolume(float volume);
    //设置播放倍速，1.0为原速
    void SetPlaybackRate(qreal rate);
    //关闭当前媒体并释放播放资源，回到Idle
    void Close();

signals:
    void sig_playbackStateChanged(PlaybackState state);
    //播放失败时携带已转换为中文的提示信息
    void sig_errorOccurred(const QString& message);
    //播放位置与媒体总时长变化，单位毫秒
    void sig_positionChanged(qint64 position);
    void sig_durationChanged(qint64 duration);

private:
    QMediaPlayer* mediaPlayer_ = nullptr;
    QAudioOutput* audioOutput_ = nullptr;
    QVideoWidget* videoWidget_ = nullptr;
    PlaybackState playbackState_ = PlaybackState::Idle;
    qint64 position_ = 0;
    qint64 duration_ = 0;
    //打开文件后等待首帧的标记：解码出第一帧就暂停，实现“打开即显示画面、点击才播放”
    bool firstFramePending_ = false;
};

#endif // LOCALPLAYER_H