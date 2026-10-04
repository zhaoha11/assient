#include "RemoteWgt.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QDebug>
#include "StyleLoader.h"

RemoteWgt::RemoteWgt(QWidget *parent)
    : QWidget{parent}
{
    setWindowFlag(Qt::FramelessWindowHint);
    setAttribute(Qt::WA_StyledBackground);
    setSizePolicy(QSizePolicy::Expanding,QSizePolicy::Expanding);

    selfCodeEdit_ = new QLineEdit(this);
    rmoteCodeEdit_ = new QLineEdit(this);
    startRmoteBtn_ = new QPushButton(QString("开始远程"),this);
    captureBackendCombo_ = new QComboBox(this);
    manager_.reset(new RemoteManager());

    selfCodeEdit_->setReadOnly(true);

    this->setObjectName("RemoteWgt");
    selfCodeEdit_->setObjectName("selfCodeEdit");
    rmoteCodeEdit_->setObjectName("remoteCodeEdit");
    startRmoteBtn_->setObjectName("remoteBtn");
    captureBackendCombo_->setObjectName("captureBackendCombo");
    captureBackendCombo_->addItem(QString::fromUtf8("采集方式：GDI"));
    captureBackendCombo_->addItem(QString::fromUtf8("采集方式：WGC"));

    selfCodeEdit_->setPlaceholderText(QString("本机识别码"));
    rmoteCodeEdit_->setPlaceholderText(QString("远程识别码"));

    QVBoxLayout* layout = new QVBoxLayout(this);
    layout->addStretch(3);
    layout->addWidget(selfCodeEdit_,0,Qt::AlignCenter);
    layout->addSpacing(30);
    layout->addWidget(rmoteCodeEdit_,1,Qt::AlignCenter);
    QHBoxLayout* remoteActions = new QHBoxLayout;
    remoteActions->addStretch();
    remoteActions->addWidget(startRmoteBtn_);
    remoteActions->addSpacing(12);
    remoteActions->addWidget(captureBackendCombo_);
    remoteActions->addStretch();
    layout->addLayout(remoteActions,2);
    layout->addStretch(1);
    setLayout(layout);

    StyleLoader::getInstance()->loadStyle(":/UI/brown/main.css",this);

    connect(captureBackendCombo_,&QComboBox::currentIndexChanged,this,[this](int index){
        manager_->SetCaptureBackend(index == 1 ? RtmpPushManager::CaptureBackend::WGC
                                               : RtmpPushManager::CaptureBackend::GDI);
    });
    connect(manager_.get(),&RemoteManager::captureBackendStarted,this,[this](int backend){
        captureBackendCombo_->setCurrentIndex(backend);
    },Qt::QueuedConnection);

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
