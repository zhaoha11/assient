#ifndef LOCALPLAYERWGT_H
#define LOCALPLAYERWGT_H
#include <QString>
#include <QWidget>
class LocalPlayer;
class QComboBox;
class QHideEvent;
class QLabel;
class QPushButton;
class QSlider;
class QStackedWidget;

//本地播放页面：阶段3.6接入拖动定位、音量与倍速，界面只负责交互和状态展示。
class LocalPlayerWgt : public QWidget
{
    Q_OBJECT
public:
    explicit LocalPlayerWgt(QWidget *parent = nullptr);
    ~LocalPlayerWgt() override;
protected:
    void hideEvent(QHideEvent* event) override;
private:
    void applyLocalPlaybackState();
    void updateVideoArea();
    void updateTimeline();
    void openLocalFile();
    void seekFromSlider();
    void setPlaybackRateFromCombo();
private:
    LocalPlayer* player_ = nullptr;
    QStackedWidget* videoStack_ = nullptr;
    QWidget* emptyStatePage_ = nullptr;
    QWidget* videoWidget_ = nullptr;
    QLabel* emptyTitle_ = nullptr;
    QLabel* emptyDescription_ = nullptr;
    QLabel* timeLabel_ = nullptr;
    QPushButton* openButton_ = nullptr;
    QPushButton* playPauseButton_ = nullptr;
    QPushButton* stopButton_ = nullptr;
    QSlider* progressSlider_ = nullptr;
    QSlider* volumeSlider_ = nullptr;
    QComboBox* speedCombo_ = nullptr;
    //最近一次由LocalPlayer上报的中文错误，用于空状态提示
    QString lastError_;
    //进度条正在被拖动：期间后端上报的位置不覆盖滑块，避免手指与回发打架
    bool seeking_ = false;
    //正在把后端位置回填到进度条：这类程序化赋值不算用户定位
    bool updatingSlider_ = false;
};
#endif // LOCALPLAYERWGT_H
