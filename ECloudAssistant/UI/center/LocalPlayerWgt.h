#ifndef LOCALPLAYERWGT_H
#define LOCALPLAYERWGT_H
#include <QWidget>
//本地播放页面：阶段2.2完成全尺寸播放器布局，不包含媒体播放能力。
class LocalPlayerWgt : public QWidget
{
    Q_OBJECT
public:
    explicit LocalPlayerWgt(QWidget *parent = nullptr);
};
#endif // LOCALPLAYERWGT_H
