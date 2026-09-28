#include "LocalPlayerWgt.h"
#include <QComboBox>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QSlider>
#include <QVBoxLayout>
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

    QSlider* progressSlider = new QSlider(Qt::Horizontal,controlBar);
    progressSlider->setObjectName("localProgressSlider");
    progressSlider->setRange(0,1000);
    progressSlider->setValue(0);
    progressSlider->setEnabled(false);

    QLabel* timeLabel = new QLabel(QStringLiteral("00:00 / 00:00"),controlBar);
    timeLabel->setObjectName("localTimeLabel");

    QHBoxLayout* progressLayout = new QHBoxLayout;
    progressLayout->setContentsMargins(0,0,0,0);
    progressLayout->setSpacing(10);
    progressLayout->addWidget(progressSlider,1);
    progressLayout->addWidget(timeLabel);

    QPushButton* openButton = new QPushButton(QString::fromUtf8("打开文件"),controlBar);
    openButton->setObjectName("localOpenButton");

    QPushButton* playPauseButton = new QPushButton(QString::fromUtf8("播放"),controlBar);
    playPauseButton->setObjectName("localPlayPauseButton");
    playPauseButton->setEnabled(false);

    QPushButton* stopButton = new QPushButton(QString::fromUtf8("停止"),controlBar);
    stopButton->setObjectName("localStopButton");
    stopButton->setEnabled(false);

    QLabel* volumeLabel = new QLabel(QString::fromUtf8("音量"),controlBar);
    volumeLabel->setObjectName("localVolumeLabel");

    QSlider* volumeSlider = new QSlider(Qt::Horizontal,controlBar);
    volumeSlider->setObjectName("localVolumeSlider");
    volumeSlider->setRange(0,100);
    volumeSlider->setValue(80);
    volumeSlider->setFixedWidth(68);
    volumeSlider->setEnabled(false);

    QLabel* speedLabel = new QLabel(QString::fromUtf8("倍速"),controlBar);
    speedLabel->setObjectName("localSpeedLabel");

    QComboBox* speedCombo = new QComboBox(controlBar);
    speedCombo->setObjectName("localSpeedCombo");
    speedCombo->addItems({QStringLiteral("0.5x"),QStringLiteral("1.0x"),QStringLiteral("1.5x"),QStringLiteral("2.0x")});
    speedCombo->setCurrentText(QStringLiteral("1.0x"));
    speedCombo->setEnabled(false);

    QHBoxLayout* actionLayout = new QHBoxLayout;
    actionLayout->setContentsMargins(0,0,0,0);
    actionLayout->setSpacing(8);
    actionLayout->addWidget(openButton);
    actionLayout->addWidget(playPauseButton);
    actionLayout->addWidget(stopButton);
    actionLayout->addStretch();
    actionLayout->addWidget(volumeLabel);
    actionLayout->addWidget(volumeSlider);
    actionLayout->addWidget(speedLabel);
    actionLayout->addWidget(speedCombo);

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

    StyleLoader::getInstance()->loadStyle(":/UI/brown/main.css",this);
}
