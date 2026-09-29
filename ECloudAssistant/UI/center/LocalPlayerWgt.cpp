#include "LocalPlayerWgt.h"
#include <QComboBox>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QSlider>
#include <QVBoxLayout>
#include "LocalPlayer.h"
#include "StyleLoader.h"

LocalPlayerWgt::LocalPlayerWgt(QWidget *parent)
    : QWidget{parent}
{
    setWindowFlag(Qt::FramelessWindowHint);
    setAttribute(Qt::WA_StyledBackground);
    //全尺寸播放器布局：不固定页面尺寸，由QStackedWidget决定实际大小
    setSizePolicy(QSizePolicy::Expanding,QSizePolicy::Expanding);
    setObjectName("LocalPlayerWgt");

    QFrame* videoSurface = new QFrame(this);
    videoSurface->setObjectName("localVideoSurface");
    videoSurface->setSizePolicy(QSizePolicy::Expanding,QSizePolicy::Expanding);

    player_ = new LocalPlayer(this);

    QLabel* emptyTitle = new QLabel(QString::fromUtf8("尚未打开文件"),videoSurface);
    emptyTitle->setObjectName("localVideoEmptyTitle");
    emptyTitle->setAlignment(Qt::AlignCenter);

    QLabel* emptyDescription = new QLabel(QString::fromUtf8("点击下方“打开文件”选择本地媒体"),videoSurface);
    emptyDescription->setObjectName("localVideoEmptyDescription");
    emptyDescription->setAlignment(Qt::AlignCenter);

    QVBoxLayout* videoLayout = new QVBoxLayout(videoSurface);
    videoLayout->setContentsMargins(24,24,24,24);
    videoLayout->setSpacing(7);
    videoLayout->addStretch();
    videoLayout->addWidget(emptyTitle);
    videoLayout->addWidget(emptyDescription);
    videoLayout->addStretch();

    QFrame* controlBar = new QFrame(this);
    controlBar->setObjectName("localControlBar");

    progressSlider_ = new QSlider(Qt::Horizontal,controlBar);
    progressSlider_->setObjectName("localProgressSlider");
    progressSlider_->setRange(0,1000);
    progressSlider_->setValue(0);
    progressSlider_->setEnabled(false);

    QLabel* timeLabel = new QLabel(QStringLiteral("00:00 / 00:00"),controlBar);
    timeLabel->setObjectName("localTimeLabel");

    QHBoxLayout* progressLayout = new QHBoxLayout;
    progressLayout->setContentsMargins(0,0,0,0);
    progressLayout->setSpacing(10);
    progressLayout->addWidget(progressSlider_,1);
    progressLayout->addWidget(timeLabel);

    QPushButton* openButton = new QPushButton(QString::fromUtf8("打开文件"),controlBar);
    openButton->setObjectName("localOpenButton");

    playPauseButton_ = new QPushButton(QString::fromUtf8("播放"),controlBar);
    playPauseButton_->setObjectName("localPlayPauseButton");
    playPauseButton_->setEnabled(false);

    stopButton_ = new QPushButton(QString::fromUtf8("停止"),controlBar);
    stopButton_->setObjectName("localStopButton");
    stopButton_->setEnabled(false);

    QLabel* volumeLabel = new QLabel(QString::fromUtf8("音量"),controlBar);
    volumeLabel->setObjectName("localVolumeLabel");

    volumeSlider_ = new QSlider(Qt::Horizontal,controlBar);
    volumeSlider_->setObjectName("localVolumeSlider");
    volumeSlider_->setRange(0,100);
    volumeSlider_->setValue(80);
    volumeSlider_->setFixedWidth(68);
    volumeSlider_->setEnabled(false);

    QLabel* speedLabel = new QLabel(QString::fromUtf8("倍速"),controlBar);
    speedLabel->setObjectName("localSpeedLabel");

    speedCombo_ = new QComboBox(controlBar);
    speedCombo_->setObjectName("localSpeedCombo");
    speedCombo_->addItems({QStringLiteral("0.5x"),QStringLiteral("1.0x"),QStringLiteral("1.5x"),QStringLiteral("2.0x")});
    speedCombo_->setCurrentText(QStringLiteral("1.0x"));
    speedCombo_->setEnabled(false);

    QHBoxLayout* actionLayout = new QHBoxLayout;
    actionLayout->setContentsMargins(0,0,0,0);
    actionLayout->setSpacing(8);
    actionLayout->addWidget(openButton);
    actionLayout->addWidget(playPauseButton_);
    actionLayout->addWidget(stopButton_);
    actionLayout->addStretch();
    actionLayout->addWidget(volumeLabel);
    actionLayout->addWidget(volumeSlider_);
    actionLayout->addWidget(speedLabel);
    actionLayout->addWidget(speedCombo_);

    QVBoxLayout* controlLayout = new QVBoxLayout(controlBar);
    controlLayout->setContentsMargins(12,9,12,10);
    controlLayout->setSpacing(8);
    controlLayout->addLayout(progressLayout);
    controlLayout->addLayout(actionLayout);

    QVBoxLayout* layout = new QVBoxLayout(this);
    //全尺寸布局：无页面外边距、无区域间距，视频区贴顶，控制栏贴底
    layout->setContentsMargins(0,0,0,0);
    layout->setSpacing(0);
    layout->addWidget(videoSurface,1);
    layout->addWidget(controlBar);

    connect(playPauseButton_,&QPushButton::clicked,this,[this](){
        if(player_->playbackState() == LocalPlayer::PlaybackState::Playing)
        {
            player_->Pause();
        }
        else
        {
            player_->Play();
        }
    });
    connect(stopButton_,&QPushButton::clicked,player_,&LocalPlayer::Stop);
    connect(player_,&LocalPlayer::sig_playbackStateChanged,this,[this](LocalPlayer::PlaybackState){
        applyLocalPlaybackState();
    });
    applyLocalPlaybackState();

    StyleLoader::getInstance()->loadStyle(":/UI/brown/main.css",this);
}

void LocalPlayerWgt::applyLocalPlaybackState()
{
    const LocalPlayer::PlaybackState state = player_->playbackState();
    const bool hasMedia = state == LocalPlayer::PlaybackState::Playing ||
                          state == LocalPlayer::PlaybackState::Paused ||
                          state == LocalPlayer::PlaybackState::Stopped;
    const bool isPlaying = state == LocalPlayer::PlaybackState::Playing;

    playPauseButton_->setText(isPlaying ? QString::fromUtf8("暂停") : QString::fromUtf8("播放"));
    playPauseButton_->setEnabled(hasMedia);
    stopButton_->setEnabled(state == LocalPlayer::PlaybackState::Opening ||
                            state == LocalPlayer::PlaybackState::Playing ||
                            state == LocalPlayer::PlaybackState::Paused);
    progressSlider_->setEnabled(hasMedia);
    volumeSlider_->setEnabled(hasMedia);
    speedCombo_->setEnabled(hasMedia);
}
