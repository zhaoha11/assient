#include "SigServer.h"
#include "SigConnection.h"
#include "../EdoyunNet/EventLoop.h"

std::shared_ptr<SigServer> SigServer::Create(EventLoop *eventloop)
{
    std::shared_ptr<SigServer> server(new SigServer(eventloop));
    return server;
}

SigServer::SigServer(EventLoop *eventloop)
    :TcpServer(eventloop)
    ,loop_(eventloop)
{
}

SigServer::~SigServer()
{
}

TcpConnection::Ptr SigServer::OnConnect(int socket)
{
    return std::make_shared<SigConnection>(loop_->GetTaskSchduler().get(),socket);
}
