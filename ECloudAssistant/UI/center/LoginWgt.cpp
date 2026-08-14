#include "LoginWgt.h"
#include <QVBoxLayout>
#include "StyleLoader.h"
#include <QDateTime>
#include <QNetworkProxy>

uint64_t GetTimeStamp()
{
    QDateTime currentDateTime = QDateTime::currentDateTime();
    return static_cast<uint64_t>(currentDateTime.toSecsSinceEpoch());
}

LoginWgt::LoginWgt(QWidget *parent)
    : QWidget(parent)
{
    setWindowFlag(Qt::FramelessWindowHint);
    setAttribute(Qt::WA_StyledBackground);
    setFixedSize(600, 510);

    acountEdit_ = new QLineEdit(this);
    passwordEdit_ = new QLineEdit(this);
    acountEdit_->setText("zzh");
    passwordEdit_->setText("123456");

    loginBtn_ = new QPushButton(QString::fromUtf8("\xE7\x99\xBB\xE5\xBD\x95"), this);
    connect(loginBtn_, &QPushButton::clicked, this, [this]() {
        if (!socket_ || !is_connect_)
        {
            return;
        }

        Login_Info info;
        info.timestamp = GetTimeStamp();
        socket_->write(reinterpret_cast<const char *>(&info), info.len);
        socket_->flush();
    });

    socket_ = new QTcpSocket(this);
    socket_->setProxy(QNetworkProxy(QNetworkProxy::NoProxy));
    connect(socket_, SIGNAL(readyRead()), this, SLOT(ReadData()));

    qDebug() << "[1] connect LB: 192.168.3.130:8523";
    socket_->connectToHost("192.168.3.130", 8523);
    if (socket_->waitForConnected(1000))
    {
        is_connect_ = true;
        //qDebug() << "[2] LB connected" << socket_->errorString();
    }
    else
    {
        qDebug() << "[2] LB connect failed" << socket_->errorString();
    }

    setObjectName("Loginer");
    acountEdit_->setObjectName("AcountEdit");
    passwordEdit_->setObjectName("PasswdEdit");
    loginBtn_->setObjectName("login_Btn");

    acountEdit_->setPlaceholderText(QString::fromUtf8("\xE8\xAF\xB7\xE8\xBE\x93\xE5\x85\xA5\xE8\xB4\xA6\xE5\x8F\xB7"));
    passwordEdit_->setPlaceholderText(QString::fromUtf8("\xE8\xAF\xB7\xE8\xBE\x93\xE5\x85\xA5\xE5\xAF\x86\xE7\xA0\x81"));
    passwordEdit_->setEchoMode(QLineEdit::Password);

    QVBoxLayout *layout = new QVBoxLayout(this);
    layout->addStretch(3);
    layout->addWidget(acountEdit_, 0, Qt::AlignCenter);
    layout->addSpacing(30);
    layout->addWidget(passwordEdit_, 1, Qt::AlignCenter);
    layout->addWidget(loginBtn_, 2, Qt::AlignCenter);
    layout->addStretch(1);
    setLayout(layout);

    StyleLoader::getInstance()->loadStyle(":/UI/brown/main.css", this);
}

void LoginWgt::ReadData()
{
    const QByteArray buffer = socket_->readAll();
    if (!buffer.isEmpty())
    {
        HandleMessage(reinterpret_cast<const packet_head *>(buffer.constData()));
    }
}

void LoginWgt::HandleMessage(const packet_head *data)
{
    switch (data->cmd)
    {
    case Login:
        HandleLogin(reinterpret_cast<LoginResult *>(const_cast<packet_head *>(data)));
        break;
    case Register:
        HandleRegister(reinterpret_cast<RegisterResult *>(const_cast<packet_head *>(data)));
        break;
    case ERROR_:
        HandleError(data);
        break;
    default:
        break;
    }
}

void LoginWgt::HandleRegister(RegisterResult *data)
{
    Q_UNUSED(data);
}

void LoginWgt::HandleLogin(LoginResult *data)
{
    if (is_login_)
    {
        qDebug() << "[LoginSvr reply] result =" << data->resultCode;
        switch (data->resultCode)
        {
        case S_OK_:
            if (data->GetCode().empty() || data->GetCode().size() > 9)
            {
                qWarning() << "[Login] failed: server returned an invalid user code (expected 1 to 9 bytes)";
                break;
            }
            qDebug() << "[Login] success: sigIp =" << data->GetIp().c_str() << "sigPort =" << data->port << "userCode =" << data->GetCode().c_str();
            emit sig_logined(data->GetIp(), data->port, data->GetCode());
            break;
        case ACCOUNT_NOT_FOUND:
            qWarning() << "[Login] failed: account does not exist, account =" << acountEdit_->text();
            break;
        case PASSWORD_INCORRECT:
            qWarning() << "[Login] failed: incorrect password, account =" << acountEdit_->text();
            break;
        case ALREADY_LOGIN:
            qWarning() << "[Login] failed: account is already online";
            break;
        case REQUEST_TIMEOUT:
            qWarning() << "[Login] failed: request timeout";
            break;
        default:
            qWarning() << "[Login] failed: server result =" << data->resultCode;
            break;
        }
        return;
    }

    HandleLoadLogin(reinterpret_cast<LoginReply *>(data));
}

void LoginWgt::HandleError(const packet_head *data)
{
    Q_UNUSED(data);
    qDebug() << "login protocol error";
}

void LoginWgt::HandleLoadLogin(LoginReply *data)
{
    ip_ = QString(data->ip.data());
    port_ = data->port;
    qDebug() << "login ip:" << ip_ << "port:" << port_;

    socket_->disconnectFromHost();
    is_connect_ = false;
    socket_->connectToHost(ip_, port_);
    if (!socket_->waitForConnected(1000))
    {
        qDebug() << "LoginSvr connect failed" << socket_->errorString();
        return;
    }

    is_connect_ = true;
    is_login_ = true;

    UserLogin login;
    login.SetCode("123");
    login.SetCount(acountEdit_->text().toStdString());
    login.SetPasswd(passwordEdit_->text().toStdString());
    login.timestamp = GetTimeStamp();
    socket_->write(reinterpret_cast<const char *>(&login), login.len);
    socket_->flush();
}
