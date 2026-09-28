#include "MainWgt.h"
#include "DeviceListWgt.h"
#include "LocalPlayerWgt.h"
#include "RemoteWgt.h"
#include "LoginWgt.h"
#include <QMetaObject>
#include <QVBoxLayout>
MainWgt::MainWgt(QWidget*p):QWidget{p}{setWindowFlag(Qt::FramelessWindowHint);setAttribute(Qt::WA_StyledBackground);setSizePolicy(QSizePolicy::Expanding,QSizePolicy::Expanding);stackWgt_=new QStackedWidget(this);QVBoxLayout*shellLayout=new QVBoxLayout(this);shellLayout->setContentsMargins(0,0,0,0);shellLayout->addWidget(stackWgt_);login_=new LoginWgt(this);remoteWgt_=new RemoteWgt(this);deviceWgt_=new DeviceListWgt(this);localPlayerWgt_=new LocalPlayerWgt(this);settingWgt_=new QWidget(this);settingWgt_->setSizePolicy(QSizePolicy::Expanding,QSizePolicy::Expanding);settingWgt_->setStyleSheet("background-color: #957522");stackWgt_->addWidget(login_);stackWgt_->addWidget(remoteWgt_);stackWgt_->addWidget(deviceWgt_);stackWgt_->addWidget(localPlayerWgt_);stackWgt_->addWidget(settingWgt_);remoteWgt_->setDeviceStatusCallback([this](const QString&s,const QString&d,const QString&t){QMetaObject::invokeMethod(deviceWgt_,[this,s,d,t](){deviceWgt_->setRegistrationState(s,d,t);},Qt::QueuedConnection);});
    connect(login_,&LoginWgt::sig_logined,this,&MainWgt::handleLogined);stackWgt_->setCurrentWidget(login_);}
void MainWgt::slot_ItemCliked(int index){QWidget*w=stackWgt_->widget(index);if(w)stackWgt_->setCurrentWidget(w);}
void MainWgt::handleLogined(const std::string&ip,uint16_t port,const std::string&code,const std::string&account){deviceWgt_->showLoggedInDevice(account,code);remoteWgt_->handleLogined(ip,port,code);stackWgt_->setCurrentWidget(remoteWgt_);}