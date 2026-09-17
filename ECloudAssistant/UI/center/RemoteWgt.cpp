#include "RemoteWgt.h"
#include <QVBoxLayout>
#include <QDebug>
#include "StyleLoader.h"

RemoteWgt::RemoteWgt(QWidget *parent)
    : QWidget{parent}
{
    setWindowFlag(Qt::FramelessWindowHint);
    setAttribute(Qt::WA_StyledBackground);
    setFixedSize(600,510);

    selfCodeEdit_ = new QLineEdit(this);
    rmoteCodeEdit_ = new QLineEdit(this);
    startRmoteBtn_ = new QPushButton(QString("开始远程"),this);
    manager_.reset(new RemoteManager());

    selfCodeEdit_->setReadOnly(true);

    this->setObjectName("RemoteWgt");
    selfCodeEdit_->setObjectName("selfCodeEdit");
    rmoteCodeEdit_->setObjectName("remoteCodeEdit");
    startRmoteBtn_->setObjectName("remoteBtn");

    selfCodeEdit_->setPlaceholderText(QString("本机识别码"));
    rmoteCodeEdit_->setPlaceholderText(QString("远程识别码"));

    QVBoxLayout* layout = new QVBoxLayout(this);
    layout->addStretch(3);
    layout->addWidget(selfCodeEdit_,0,Qt::AlignCenter);
    layout->addSpacing(30);
    layout->addWidget(rmoteCodeEdit_,1,Qt::AlignCenter);
    layout->addWidget(startRmoteBtn_,2,Qt::AlignCenter);
    layout->addStretch(1);
    setLayout(layout);

    StyleLoader::getInstance()->loadStyle(":/UI/brown/main.css",this);

    connect(startRmoteBtn_,&QPushButton::clicked,this,[this](){
        //开始远程
        //获取远程code
        QString code = rmoteCodeEdit_->text();
        if(code.isEmpty() || code.toUtf8().size() > 9)
        {
            qWarning() << "[Remote] target code must contain 1 to 9 bytes";
            return;
        }
        manager_->StartRemote(ip_,port_,code);
    });
}

void RemoteWgt::setDeviceStatusCallback(DeviceStatusCallback callback){ deviceStatusCallback_ = std::move(callback); }
void RemoteWgt::handleLogined(const std::string ip, uint16_t port, const std::string code)
{
    if(code.empty())
    {
        return;
    }

    // 使用登录服务器返回的数据库 USER_CODE 注册当前客户端的信令身份。
    const QString userCode = QString::fromStdString(code);
    selfCodeEdit_->setText(userCode);
    ip_ = QString::fromStdString(ip);
    port_ = port;
    manager_->Init(ip_, port_, userCode, deviceStatusCallback_);
}
