#include "LConnection.h"

LConnection::LConnection(TaskScheduler *scheduler, int socket)
    :TcpConnection(scheduler,socket)
{
    this->SetReadCallback([this](std::shared_ptr<TcpConnection> conn,BufferReader& buffer){
        return this->OnRead(buffer);
    });
    char buf[] = "sdfhcsdiuhvciadsbcvadsbfcia";
    Send(buf,sizeof(buf));
    //
}

LConnection::~LConnection()
{
    Clear();
}

bool LConnection::OnRead(BufferReader &buffer)
{
    if(buffer.ReadableBytes() > 0)
    {
        HandleMessage(buffer);
    }
    return true;
}

void LConnection::HandleMessage(BufferReader &buffer)
{
    std::string str;
    buffer.ReadAll(str);
    printf("data: %s\n",str.c_str());
    Send(str.c_str(),str.size());
    buffer.RetrieveAll();
}

void LConnection::Clear()
{
}

