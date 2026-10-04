#include "LocalPlayerWgt.h"
#include <QComboBox>
#include <QFileDialog>
#include <QFrame>
#include <QHideEvent>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QSlider>
#include <QStackedWidget>
#include <QVBoxLayout>
#include <QVideoWidget>
#include "LocalPlayer.h"
#include "StyleLoader.h"

namespace
{
//把毫秒格式化为mm:ss，超过一小时显示h:mm:ss
QString formatDuration(qint64 milliseconds)
{
    const qint64 totalSeconds = milliseconds / 1000;
    const qint64 seconds = totalSeconds % 60;
    const qint64 minutes = totalSeconds / 60 % 60;
    const qint64 hours = totalSeconds / 3600;
    if(hours > 0)
    {
        return QString::asprintf("%lld:%02lld:%02lld",hours,minutes,seconds);
    }
    return QString::asprintf("%02lld:%02lld",minutes,seconds);
}
}

LocalPlayerWgt::LocalPlayerWgt(QWidget *parent)
    : QWidget{parent}
{
    setWindowFlag(Qt::FramelessWindowHint);
    setAttribute(Qt::WA_StyledBackground);
    //全尺寸播放器布局：不固定页面尺寸，由QStackedWidget决定实际大小
    setSizePolicy(QSizePolicy::Expanding,QSizePolicy::Expanding);
    setObjectName("LocalPlayerWgt");

    player_ = new LocalPlayer(this);

    QWidget* videoSurface = buildVideoArea();
    QWidget* controlBar = buildControlBar();

    QVBoxLayout* layout = new QVBoxLayout(this);
    //全尺寸布局：无页面外边距、无区域间距，视频区贴顶，控制栏贴底
    layout->setContentsMargins(0,0,0,0);
    layout->setSpacing(0);
    layout->addWidget(videoSurface,1);
    layout->addWidget(controlBar);

    bindPlayerSignals();

    applyLocalPlaybackState();

    StyleLoader::getInstance()->loadStyle(":/UI/brown/main.css",this);
}

//页面销毁时释放媒体资源，避免播放中的文件在退出过程中继续出声。
LocalPlayerWgt::~LocalPlayerWgt()
{
    //player_是本控件的子对象，此时仍然有效
    player_->Close();
}

//构建视频区：空状态提示与实际画面两页，返回控件交给页面布局。
QWidget* LocalPlayerWgt::buildVideoArea()
{
    QFrame* videoSurface = new QFrame(this);
    videoSurface->setObjectName("localVideoSurface");
    videoSurface->setSizePolicy(QSizePolicy::Expanding,QSizePolicy::Expanding);

    //视频区第一页：未打开文件或打开失败时的提示
    emptyStatePage_ = new QWidget(videoSurface);
    //显式命名，避免继承顶层窗口的深色背景，让页面渐变透出来
    emptyStatePage_->setObjectName("localVideoEmptyPage");
    emptyTitle_ = new QLabel(QString::fromUtf8("尚未打开文件"),emptyStatePage_);
    emptyTitle_->setObjectName("localVideoEmptyTitle");
    emptyTitle_->setAlignment(Qt::AlignCenter);
    emptyDescription_ = new QLabel(QString::fromUtf8("点击下方“打开文件”选择本地媒体"),emptyStatePage_);
    emptyDescription_->setObjectName("localVideoEmptyDescription");
    emptyDescription_->setAlignment(Qt::AlignCenter);

    QVBoxLayout* emptyLayout = new QVBoxLayout(emptyStatePage_);
    emptyLayout->setContentsMargins(24,24,24,24);
    emptyLayout->setSpacing(7);
    emptyLayout->addStretch();
    emptyLayout->addWidget(emptyTitle_);
    emptyLayout->addWidget(emptyDescription_);
    emptyLayout->addStretch();

    //视频区第二页：真实画面，控件由LocalPlayer持有并已接入setVideoOutput()
    videoWidget_ = player_->videoWidget();

    videoStack_ = new QStackedWidget(videoSurface);
    videoStack_->setObjectName("localVideoStack");
    videoStack_->addWidget(emptyStatePage_);
    videoStack_->addWidget(videoWidget_);

    QVBoxLayout* surfaceLayout = new QVBoxLayout(videoSurface);
    surfaceLayout->setContentsMargins(0,0,0,0);
    surfaceLayout->addWidget(videoStack_);

    return videoSurface;
}

//构建底部控制栏：进度、时间、按钮、音量与倍速，返回控件交给页面布局。
QWidget* LocalPlayerWgt::buildControlBar()
{
    QFrame* controlBar = new QFrame(this);
    controlBar->setObjectName("localControlBar");

    progressSlider_ = new QSlider(Qt::Horizontal,controlBar);
    progressSlider_->setObjectName("localProgressSlider");
    //内部使用0～1000的千分位刻度，定位时再换算成毫秒
    progressSlider_->setRange(0,1000);
    progressSlider_->setValue(0);
    progressSlider_->setEnabled(false);

    timeLabel_ = new QLabel(QStringLiteral("00:00 / 00:00"),controlBar);
    timeLabel_->setObjectName("localTimeLabel");

    QHBoxLayout* progressLayout = new QHBoxLayout;
    progressLayout->setContentsMargins(0,0,0,0);
    progressLayout->setSpacing(10);
    progressLayout->addWidget(progressSlider_,1);
    progressLayout->addWidget(timeLabel_);

    openButton_ = new QPushButton(QString::fromUtf8("打开文件"),controlBar);
    openButton_->setObjectName("localOpenButton");

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
    //档位文本与倍率一起写入，界面不靠解析文本得到倍率
    speedCombo_->addItem(QStringLiteral("0.5x"),0.5);
    speedCombo_->addItem(QStringLiteral("1.0x"),1.0);
    speedCombo_->addItem(QStringLiteral("1.5x"),1.5);
    speedCombo_->addItem(QStringLiteral("2.0x"),2.0);
    speedCombo_->setCurrentIndex(1);
    speedCombo_->setEnabled(false);

    QHBoxLayout* actionLayout = new QHBoxLayout;
    actionLayout->setContentsMargins(0,0,0,0);
    actionLayout->setSpacing(8);
    actionLayout->addWidget(openButton_);
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

    return controlBar;
}

//接通控制栏与LocalPlayer的信号，并把音量、倍速的初始值同步到后端。
void LocalPlayerWgt::bindPlayerSignals()
{
    connect(openButton_,&QPushButton::clicked,this,&LocalPlayerWgt::openLocalFile);
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

    //鼠标拖动进度条：按下期间锁定滑块，松开时统一提交一次定位
    connect(progressSlider_,&QSlider::sliderPressed,this,[this](){
        seeking_ = true;
    });
    connect(progressSlider_,&QSlider::sliderMoved,this,[this](int){
        updateTimeline();
    });
    connect(progressSlider_,&QSlider::sliderReleased,this,[this](){
        seeking_ = false;
        seekFromSlider();
    });
    //键盘方向键或点击滑槽只改数值、不拖动滑块，因此在这里补上定位
    connect(progressSlider_,&QSlider::valueChanged,this,[this](int){
        if(updatingSlider_ || progressSlider_->isSliderDown())
        {
            return;
        }
        seekFromSlider();
    });

    connect(volumeSlider_,&QSlider::valueChanged,this,[this](int value){
        //滑块是0～100的整数，换算成0.0～1.0的线性音量
        player_->SetVolume(value / 100.0f);
    });
    //让后端音量与界面初始值一致
    player_->SetVolume(volumeSlider_->value() / 100.0f);

    connect(speedCombo_,&QComboBox::currentIndexChanged,this,[this](int){
        setPlaybackRateFromCombo();
    });
    //让后端倍速与界面初始档位一致
    setPlaybackRateFromCombo();

    connect(player_,&LocalPlayer::sig_playbackStateChanged,this,[this](LocalPlayer::PlaybackState){
        applyLocalPlaybackState();
    });
    connect(player_,&LocalPlayer::sig_errorOccurred,this,[this](const QString& message){
        lastError_ = message;
        updateVideoArea();
    });
    connect(player_,&LocalPlayer::sig_positionChanged,this,[this](qint64){
        updateTimeline();
    });
    connect(player_,&LocalPlayer::sig_durationChanged,this,[this](qint64){
        updateTimeline();
    });
}

//离开本地播放页时关闭媒体；QStackedWidget切页不会销毁页面。
void LocalPlayerWgt::hideEvent(QHideEvent* event)
{
    if(!event->spontaneous())
    {
        player_->Close();
    }
    QWidget::hideEvent(event);
}

//按当前业务状态刷新控件可用性和视频区显示。
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
    if(!hasMedia)
    {
        seeking_ = false;
        progressSlider_->setSliderDown(false);
    }
    volumeSlider_->setEnabled(hasMedia);
    speedCombo_->setEnabled(hasMedia);
    updateTimeline();
    updateVideoArea();
}

//按LocalPlayer的真实位置和时长刷新进度条与时间文本。
void LocalPlayerWgt::updateTimeline()
{
    const qint64 duration = player_->duration();

    //拖动期间滑块是唯一依据：位置与时间都跟着手指走，不被后端回发拉回
    if(seeking_)
    {
        const qint64 preview = duration > 0
            ? progressSlider_->value() * duration / progressSlider_->maximum()
            : 0;
        timeLabel_->setText(formatDuration(preview) + QStringLiteral(" / ") + formatDuration(duration));
        return;
    }

    const qint64 position = player_->position();

    //回填滑块时置位标记，避免valueChanged把这次回填当成用户拖动而反过来发起定位
    updatingSlider_ = true;
    progressSlider_->setValue(duration > 0
        ? static_cast<int>(position * progressSlider_->maximum() / duration)
        : 0);
    updatingSlider_ = false;

    timeLabel_->setText(formatDuration(position) + QStringLiteral(" / ") + formatDuration(duration));
}

//把进度条当前位置换算成毫秒并交给LocalPlayer定位。
void LocalPlayerWgt::seekFromSlider()
{
    if(!progressSlider_->isEnabled())
    {
        return;
    }
    const qint64 duration = player_->duration();
    if(duration <= 0)
    {
        return;
    }
    player_->Seek(duration * progressSlider_->value() / progressSlider_->maximum());
}

//把倍速下拉框当前档位对应的倍率交给LocalPlayer。
void LocalPlayerWgt::setPlaybackRateFromCombo()
{
    if(speedCombo_->currentIndex() < 0)
    {
        return;
    }
    player_->SetPlaybackRate(speedCombo_->currentData().toReal());
}

//在空状态提示和视频画面之间切换，打开失败时把中文错误显示在空状态上。
void LocalPlayerWgt::updateVideoArea()
{
    switch(player_->playbackState())
    {
    case LocalPlayer::PlaybackState::Idle:
        emptyTitle_->setText(QString::fromUtf8("尚未打开文件"));
        emptyDescription_->setText(QString::fromUtf8("点击下方“打开文件”选择本地媒体"));
        videoStack_->setCurrentWidget(emptyStatePage_);
        break;
    case LocalPlayer::PlaybackState::Error:
        emptyTitle_->setText(QString::fromUtf8("无法打开文件"));
        emptyDescription_->setText(lastError_);
        videoStack_->setCurrentWidget(emptyStatePage_);
        break;
    default:
        videoStack_->setCurrentWidget(videoWidget_);
        break;
    }
}

//弹出文件选择框，把选中的本地文件交给LocalPlayer打开。
void LocalPlayerWgt::openLocalFile()
{
    const QString path = QFileDialog::getOpenFileName(this,
        QString::fromUtf8("打开媒体文件"),
        QString(),
        QString::fromUtf8("媒体文件 (*.mp4 *.mkv *.avi *.mov *.flv *.wmv *.mp3 *.wav *.aac *.flac *.m4a);;所有文件 (*.*)"));
    if(path.isEmpty())
    {
        return;
    }
    lastError_.clear();
    player_->Open(path);
}
