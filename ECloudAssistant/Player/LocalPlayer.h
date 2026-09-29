#ifndef LOCALPLAYER_H
#define LOCALPLAYER_H

#include <QObject>

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
    void Play();
    void Pause();
    void Stop();

signals:
    void sig_playbackStateChanged(PlaybackState state);

private:
    PlaybackState playbackState_ = PlaybackState::Idle;
};

#endif // LOCALPLAYER_H
