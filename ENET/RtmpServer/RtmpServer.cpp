#include "RtmpServer.h"
#include "../EdoyunNet/EventLoop.h"
#include "RtmpConnection.h"

std::shared_ptr<RtmpServer> RtmpServer::Create(EventLoop *eventloop)
{
    std::shared_ptr<RtmpServer> server(new RtmpServer(eventloop));
    return server;
}

RtmpServer::RtmpServer(EventLoop* eventloop)
    :TcpServer(eventloop)
    ,loop_(eventloop)
    ,event_callbacks_(10)
{
    //定时更新session
    loop_->AddTimer([this](){
        std::lock_guard<std::mutex> lock(mutex_);
        for(auto iter = rtmp_sessions_.begin(); iter != rtmp_sessions_.end();){
            if(iter->second->GetClients() == 0)
            {
                rtmp_sessions_.erase(iter++);
            }
            else
            {
                iter++;
            }
        }
        return true;
    },3000); //3秒执行一次，循环执行
}

RtmpServer::~RtmpServer()
{

}

void RtmpServer::SetEventCallback(const EventCallback &cb)
{
    std::lock_guard<std::mutex> lock(mutex_);
    event_callbacks_.push_back(cb);
}

void RtmpServer::AddSesion(std::string stream_path)
{
    std::lock_guard<std::mutex> lock(mutex_);
    if(rtmp_sessions_.find(stream_path) == rtmp_sessions_.end())
    {
        rtmp_sessions_[stream_path] = std::make_shared<RtmpSession>();
    }
}

void RtmpServer::RemoveSession(std::string stream_path)
{
    std::lock_guard<std::mutex> lock(mutex_);
    rtmp_sessions_.erase(stream_path);
}

RtmpSession::Ptr RtmpServer::GetSession(std::string stream_path)
{
    std::lock_guard<std::mutex> lock(mutex_);
    if(rtmp_sessions_.find(stream_path) == rtmp_sessions_.end())
    {
        rtmp_sessions_[stream_path] = std::make_shared<RtmpSession>();
    }
    return rtmp_sessions_[stream_path];
}

bool RtmpServer::HasPublisher(std::string stream_path)
{
    auto session = GetSession(stream_path);
    if(!session)
    {
        return false;
    }
    return (session->GetPublisher() != nullptr);
}

bool RtmpServer::HasSession(std::string stream_path)
{
    std::lock_guard<std::mutex> lock(mutex_);
    return (rtmp_sessions_.find(stream_path) != rtmp_sessions_.end());
}

void RtmpServer::NotifyEvent(std::string type, std::string stream_path)
{
    std::lock_guard<std::mutex> lock(mutex_);
    for(auto event_cb : event_callbacks_)
    {
        if(event_cb)
        {
            event_cb(type,stream_path);
        }
    }
}

TcpConnection::Ptr RtmpServer::OnConnect(int socket)
{
    return std::make_shared<RtmpConnection>(shared_from_this(),loop_->GetTaskSchduler().get(),socket);
}
