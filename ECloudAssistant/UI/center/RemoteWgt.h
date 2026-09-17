#ifndef REMOTEWGT_H
#define REMOTEWGT_H
#include <QLineEdit>
#include <QWidget>
#include <QPushButton>
#include <functional>
#include "RemoteManager.h"

class RemoteWgt : public QWidget
{
    Q_OBJECT
public:
    explicit RemoteWgt(QWidget *parent = nullptr);
public:
    using DeviceStatusCallback = std::function<void(const QString &, const QString &, const QString &)>;
    void setDeviceStatusCallback(DeviceStatusCallback callback);
public slots:
    void handleLogined(const std::string ip, uint16_t port, const std::string code);
private:
    QString ip_ = "";
    uint16_t port_ = -1;
    EventLoop* loop_ = nullptr;
    QLineEdit* selfCodeEdit_;
    QLineEdit* rmoteCodeEdit_;
    QPushButton* startRmoteBtn_;
    std::unique_ptr<RemoteManager> manager_;
    DeviceStatusCallback deviceStatusCallback_;
};

#endif // REMOTEWGT_H
