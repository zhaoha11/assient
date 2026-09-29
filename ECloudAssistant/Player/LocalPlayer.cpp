#include "LocalPlayer.h"

LocalPlayer::LocalPlayer(QObject* parent)
    : QObject(parent)
{
}

LocalPlayer::PlaybackState LocalPlayer::playbackState() const
{
    return playbackState_;
}

void LocalPlayer::UpdatePlaybackState(PlaybackState state)
{
    if(playbackState_ == state)
    {
        return;
    }
    playbackState_ = state;
    emit sig_playbackStateChanged(playbackState_);
}

void LocalPlayer::Play()
{
    if(playbackState_ == PlaybackState::Paused ||
       playbackState_ == PlaybackState::Stopped)
    {
        UpdatePlaybackState(PlaybackState::Playing);
    }
}

void LocalPlayer::Pause()
{
    if(playbackState_ == PlaybackState::Playing)
    {
        UpdatePlaybackState(PlaybackState::Paused);
    }
}

void LocalPlayer::Stop()
{
    if(playbackState_ == PlaybackState::Opening ||
       playbackState_ == PlaybackState::Playing ||
       playbackState_ == PlaybackState::Paused)
    {
        UpdatePlaybackState(PlaybackState::Stopped);
    }
}
