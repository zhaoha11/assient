#include "../EdoyunNet/TcpConnection.h"
#include "amf.h"
#include "rtmp.h"
#include "RtmpSink.h"
#include "RtmpChunk.h"
#include "RtmpHandshake.h"

class RtmpServer;
class RtmpSession;
class RtmpConnection : public TcpConnection , public RtmpSink
{
public:
    enum ConnectionState
    {
        HANDSHAKE,
        START_CONNECT,
        START_CREATE_STREAM,
        START_DELETE_STREAM,
        START_PLAY,
        START_PUBLISH,
    };
    RtmpConnection(std::shared_ptr<RtmpServer> rtmp_server,TaskScheduler* scheduler,int socket);
    virtual ~RtmpConnection();

    virtual bool IsPlayer() override{return state_ == START_PLAY;}
    virtual bool IsPublisher() override{return state_ == START_PUBLISH;}
    virtual bool IsPlaying() override{return is_playing_;}
    virtual bool IsPublishing()override{return is_publishing_;}
    virtual uint32_t GetId() override{return this->GetSocket();};
private:
    RtmpConnection(TaskScheduler* scheduler,int socket,Rtmp* rtmp);
    bool OnRead(BufferReader& buffer);
    void OnClose();

    bool HandleChunk(BufferReader& buffer);
    bool HandleMessage(RtmpMessage& rtmp_msg);

    bool HandleInvoke(RtmpMessage& rtmp_msg);
    bool HandleNotify(RtmpMessage& rtmp_msg);
    bool HandleAudio(RtmpMessage& rtmp_msg);
    bool HandleVideo(RtmpMessage& rtmp_msg);

    bool HandleConnect();
    bool HandleCreateStream();
    bool HandlePublish();
    bool HandlePlay();
    bool HandleDeleteStream();

    void SetPeerBandWidth();
    void SendAcknowlegement();
    void SetChunkSize();
    
    bool SendInvokeMessage(uint32_t csid,std::shared_ptr<char> playload,uint32_t playload_size);
    bool SendNotifyMessage(uint32_t csid,std::shared_ptr<char> playload,uint32_t playload_size);
    bool IsKeyFrame(std::shared_ptr<char> data,uint32_t size);
    void SendRtmpChunks(uint32_t csid,RtmpMessage& rtmp_msg);

    virtual bool SendMetaData(AmfObjects metaData)override; //发送消息，通知后面的数据为元数据
    virtual bool SendMediaData(uint8_t type,uint64_t timestamp,std::shared_ptr<char> playload,uint32_t playload_size)override; //纯虚函数
private:
    ConnectionState state_;
    std::shared_ptr<RtmpHandshake> handshake_;
    std::shared_ptr<RtmpChunk> rtmp_chunk_;

    std::weak_ptr<RtmpServer> rtmp_server_;
    std::weak_ptr<RtmpSession> rtmp_session_;

    uint32_t peer_width_ = 5000000;
    uint32_t ackonwledgement_size_ = 5000000;
    uint32_t max_chunk_size_ = 128;
    uint32_t stream_id_ = 0;

    AmfObjects meta_data_;
    AmfDecoder amf_decoder_;
    AmfEncoder amf_encoder_;

    bool is_playing_ = false;
    bool is_publishing_ = false;

    std::string app_;
    std::string stream_name_;
    std::string stream_path_;

    bool has_key_frame = false;

    //元数据
    std::shared_ptr<char> avc_sequence_header_;
    std::shared_ptr<char> aac_sequence_header_;
    uint32_t avc_sequence_header_size_ = 0;
    uint32_t aac_sequence_header_szie_ = 0;


};