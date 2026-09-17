#ifndef MAINWGT_H
#define MAINWGT_H
#include <cstdint>
#include <string>
#include <QWidget>
#include <QStackedWidget>
class LoginWgt; class RemoteWgt; class DeviceListWgt;
class MainWgt:public QWidget { Q_OBJECT public: explicit MainWgt(QWidget*parent=nullptr); public slots: void slot_ItemCliked(int index); void handleLogined(const std::string&ip,uint16_t port,const std::string&code,const std::string&account); private: QStackedWidget*stackWgt_; LoginWgt*login_; RemoteWgt*remoteWgt_; DeviceListWgt*deviceWgt_; QWidget*settingWgt_; };
#endif