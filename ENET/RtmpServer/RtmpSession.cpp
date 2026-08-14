#include "RtmpSession.h"
#include "RtmpSink.h"
#include "rtmp.h"
#include "RtmpConnection.h"

RtmpSession::RtmpSession()
{
}

RtmpSession::~RtmpSession()
{
}

void RtmpSession::AddSink(std::shared_ptr<RtmpSink> sink)
{
    std::lock_guard<std::mutex> lock(mutex_);
    rtmp_sinks_[sink->GetId()] = sink;
    if(sink->IsPublisher()) //如果是主播，说明这个直播间刚刚创建，没有设置元数据
    {
        avc_sequence_header_ = nullptr;
        aac_sequence_header_ = nullptr;
        avc_sequence_header_size_ = 0;
        aac_sequence_header_size_ = 0;
        has_publisher_ = true;
        publisher_ = sink;
    }
    return ;
}

void RtmpSession::RemoveSink(std::shared_ptr<RtmpSink> sink)
{
    std::lock_guard<std::mutex> lock(mutex_);
    if(sink->IsPublisher())
    {
        avc_sequence_header_ = nullptr;
        aac_sequence_header_ = nullptr;
        avc_sequence_header_size_ = 0;
        aac_sequence_header_size_ = 0;
        has_publisher_ = false;
    }
    rtmp_sinks_.erase(sink->GetId());
}

int RtmpSession::GetClients()
{
    std::lock_guard<std::mutex> lock(mutex_);
    int clients = 0;
    for(auto iter : rtmp_sinks_)
    {
        auto conn = iter.second.lock();
        if(conn != nullptr)
        {
            clients += 1;
        }
    }
    return clients;
}

void RtmpSession::SendMetaData(AmfObjects &metaData)
{
    std::lock_guard<std::mutex> lock(mutex_);
    for(auto iter = rtmp_sinks_.begin(); iter != rtmp_sinks_.end();)
    {
        auto conn = iter->second.lock();
        if(!conn)
        {
            rtmp_sinks_.erase(iter++); //防止迭代器失效
        }
        else
        {
            //只发送给观众
            if(conn->IsPlayer())
            {
                conn->SendMetaData(metaData);
            }
            iter++;
        }
    }
}

void RtmpSession::SendMediaData(uint8_t type, uint64_t timestamp, std::shared_ptr<char> data, uint32_t size)
{
    std::lock_guard<std::mutex> lock(mutex_);
    //再来发媒体数据
    for(auto iter = rtmp_sinks_.begin();iter != rtmp_sinks_.end();){
        auto conn = iter->second.lock();
        if(conn == nullptr)
        {
            rtmp_sinks_.erase(iter++);
        }
        else //在线用户 观看中(发送音视频) 刚进直播间(发送元数据再发音视频数据)
        {
            if(conn->IsPlayer())
            {
                if(!conn->IsPlaying()) //刚进直播间
                {
                    conn->SendMediaData(RTMP_AAC_SEQUENCE_HEADER,timestamp,aac_sequence_header_,aac_sequence_header_size_); //音频元数据
                    conn->SendMediaData(RTMP_AVC_SEQUENCE_HEADER,timestamp,avc_sequence_header_,avc_sequence_header_size_); //视频元数据
                }
                //观看中
                conn->SendMediaData(type,timestamp,data,size);
            }
            iter++;
        }
    }
    return;
}

std::shared_ptr<RtmpConnection> RtmpSession::GetPublisher()
{
    std::lock_guard<std::mutex> lock(mutex_);
    auto publisher = publisher_.lock();
    if(publisher)
    {
        return std::dynamic_pointer_cast<RtmpConnection>(publisher);
    }
    return nullptr;
}
