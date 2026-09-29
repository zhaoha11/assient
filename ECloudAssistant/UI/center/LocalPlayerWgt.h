#ifndef LOCALPLAYERWGT_H
#define LOCALPLAYERWGT_H
#include <QWidget>
class LocalPlayer;
class QComboBox;
class QPushButton;
class QSlider;

//本地播放页面：阶段3.1建立与LocalPlayer的状态链，尚未接入本地媒体。
class LocalPlayerWgt : public QWidget
{
    Q_OBJECT
public:
    explicit LocalPlayerWgt(QWidget *parent = nullptr);
private:
    void applyLocalPlaybackState();
private:
    LocalPlayer* player_ = nullptr;
    QPushButton* playPauseButton_ = nullptr;
    QPushButton* stopButton_ = nullptr;
    QSlider* progressSlider_ = nullptr;
    QSlider* volumeSlider_ = nullptr;
    QComboBox* speedCombo_ = nullptr;
};
#endif // LOCALPLAYERWGT_H
