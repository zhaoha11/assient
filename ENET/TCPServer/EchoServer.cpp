#include "EchoServer.h"
#include "LConnection.h"
#include "../EdoyunNet/EventLoop.h"

std::shared_ptr<EchoServer> EchoServer::Create(EventLoop *eventloop)
{
    std::shared_ptr<EchoServer> server(new EchoServer(eventloop));
    return server;
}

EchoServer::EchoServer(EventLoop* eventloop)
    :TcpServer(eventloop)
    ,loop_(eventloop)
{
}

EchoServer::~EchoServer()
{
    Clear();
}

void EchoServer::Clear()
{
}

TcpConnection::Ptr EchoServer::OnConnect(int socket)
{
    return std::make_shared<LConnection>(loop_->GetTaskSchduler().get(),socket);
}
