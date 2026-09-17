#include "PullerWgt.h"
#include <QCloseEvent>
#include <QResizeEvent>
#include <QVBoxLayout>

PullerWgt::PullerWgt(EventLoop* loop,QWidget *parent)
    : QMainWindow{parent}
{
    this->setMinimumSize(400,250);
    this->resize(800,500);
    //窗口标题
    setWindowTitle(QString("EcloudAssistant"));
    //窗口图标
    setWindowIcon(QIcon(":/UI/brown/center/favicon-32.ico"));
    //设置窗口背景
    setStyleSheet("background-color:#121212");

    auto* central = new QWidget(this);
    player_.reset(new AVPlayer(loop,central));
    //布局
    QVBoxLayout* layout = new QVBoxLayout(central);
    layout->addWidget(player_.get());
    layout->setSpacing(0);
    layout->setContentsMargins(0,0,0,0);
    setCentralWidget(central);
}

bool PullerWgt::Connect(QString ip, uint16_t port, QString code)
{
    return player_->Connect(ip,port,code);
}

void PullerWgt::closeEvent(QCloseEvent *event)
{
    if(player_)
    {
        player_->StopRemote();
    }
    QMainWindow::closeEvent(event);
}

void PullerWgt::resizeEvent(QResizeEvent *event)
{
    //后面处理
    player_->resize(event->size());
    QMainWindow::resizeEvent(event);
}
