#include <memory>
#include <mutex>
#include "amf.h"

class RtmpSink;
class RtmpConnection;
class RtmpSession
{
public:
    using Ptr = std::shared_ptr<RtmpSession>;
    RtmpSession();
    virtual ~RtmpSession();
    void SetAvcSequenceHeader(std::shared_ptr<char> avcSequenceHeader,uint32_t avcSequenceHeaderSize)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        avc_sequence_header_ = avcSequenceHeader;
        avc_sequence_header_size_ = avcSequenceHeaderSize;
    }

    void SetAacSequenceHeader(std::shared_ptr<char> aacSequenceHeader,uint32_t aacSequenceHeaderSize)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        aac_sequence_header_ = aacSequenceHeader;
        aac_sequence_header_size_ = aacSequenceHeaderSize;
    }

    //session添加客户端
    void AddSink(std::shared_ptr<RtmpSink> sink);
    //移除客户端
    void RemoveSink(std::shared_ptr<RtmpSink> sink);
    //获取当前有多少人在直播间，包括主播和观众
    int GetClients();
    //发送消息通知，通知元数据
    void SendMetaData(AmfObjects& metaData);
    //发送数据
    void SendMediaData(uint8_t type,uint64_t timestamp,std::shared_ptr<char> data,uint32_t size);
    //获取推流对象
    std::shared_ptr<RtmpConnection> GetPublisher();
private:
    std::mutex mutex_;
    bool has_publisher_ = false;
    std::weak_ptr<RtmpSink> publisher_;
    std::unordered_map<int,std::weak_ptr<RtmpSink>> rtmp_sinks_;

    std::shared_ptr<char> avc_sequence_header_;
    std::shared_ptr<char> aac_sequence_header_;
    uint32_t avc_sequence_header_size_ = 0;
    uint32_t aac_sequence_header_size_ = 0;
};