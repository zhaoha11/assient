#include "LoginServer.h"
#include "../EdoyunNet/EventLoop.h"
#include "LoginConnection.h"

std::shared_ptr<LoginServer> LoginServer::Create(EventLoop *eventloop)
{
    std::shared_ptr<LoginServer> server(new LoginServer(eventloop));
    return server;
}

LoginServer::LoginServer(EventLoop* eventloop)
    :TcpServer(eventloop)
    ,loop_(eventloop)
    ,client_(nullptr)
{
    client_.reset(new TcpClient());
    client_->Create();
    if(client_->Connect("192.168.3.130",8523))
    {
        client_->getMonitorInfo();
        id_ = loop_->AddTimer([this](){
            client_->getMonitorInfo();
            return true;
        },1000);
    }
}

LoginServer::~LoginServer()
{
}

TcpConnection::Ptr LoginServer::OnConnect(int socket)
{
    //这个连接的后续事件
    return std::make_shared<LoginConnection>(loop_->GetTaskSchduler().get(),socket);
}
