#ifndef PULLERWGT_H
#define PULLERWGT_H
#include "AVPlayer.h"
#include <QMainWindow>

class QCloseEvent;

class PullerWgt : public QMainWindow
{
    Q_OBJECT
public:
    explicit PullerWgt(EventLoop* loop,QWidget *parent = nullptr);
    bool Connect(QString ip,uint16_t port,QString code);
protected:
    void closeEvent(QCloseEvent *event) override;
    virtual void resizeEvent(QResizeEvent *event);
private:
    std::unique_ptr<AVPlayer> player_;
};

#endif // PULLERWGT_H
