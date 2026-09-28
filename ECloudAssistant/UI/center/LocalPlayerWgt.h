#ifndef LOCALPLAYERWGT_H
#define LOCALPLAYERWGT_H
#include <QWidget>
//本地播放页面：阶段一仅作为占位界面，不包含任何媒体播放能力。
class LocalPlayerWgt : public QWidget
{
    Q_OBJECT
public:
    explicit LocalPlayerWgt(QWidget *parent = nullptr);
};
#endif // LOCALPLAYERWGT_H
