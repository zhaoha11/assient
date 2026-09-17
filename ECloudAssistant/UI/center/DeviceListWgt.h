#ifndef DEVICELISTWGT_H
#define DEVICELISTWGT_H
#include <string>
#include <QWidget>
class QLabel; class QFrame;
class DeviceListWgt:public QWidget{Q_OBJECT public:explicit DeviceListWgt(QWidget*p=nullptr);public slots:void showLoggedInDevice(const std::string&a,const std::string&c);void setRegistrationState(const QString&s,const QString&d,const QString&t);private:QWidget*emptyState_=nullptr;QFrame*deviceSummary_=nullptr;QLabel*accountValue_=nullptr;QLabel*codeValue_=nullptr;QLabel*statusValue_=nullptr;QLabel*statusDetail_=nullptr;};
#endif