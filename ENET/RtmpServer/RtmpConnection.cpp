#include "RtmpConnection.h"
#include "rtmp.h"
#include "RtmpServer.h"

RtmpConnection::RtmpConnection(std::shared_ptr<RtmpServer> rtmp_server, TaskScheduler *scheduler, int socket)
    :RtmpConnection(scheduler,socket,rtmp_server.get())
{
    handshake_.reset(new RtmpHandshake(RtmpHandshake::HANDSHAKE_C0C1));
    rtmp_server_ = rtmp_server;
}

RtmpConnection::RtmpConnection(TaskScheduler* scheduler,int socket,Rtmp* rtmp)
    :TcpConnection(scheduler,socket)
    ,rtmp_chunk_(new RtmpChunk())
    ,state_(HANDSHAKE)
{
    peer_width_ = rtmp->GetPeerBandwidth();
    ackonwledgement_size_ = rtmp->GetAcknowledgementSize();
    max_chunk_size_ = rtmp->GetChunkSize();
    stream_path_ = rtmp->GetStreamPath();
    stream_name_ = rtmp->GetStreamName();
    app_ = rtmp->GetApp();

    //设置回调函数，处理读数据
    this->SetReadCallback([this](std::shared_ptr<TcpConnection> conn,BufferReader& buffer){
        return this->OnRead(buffer);
    });

    //设置关闭回调，释放资源
    this->SetCloseCallback([this](std::shared_ptr<TcpConnection> conn){
        this->OnClose();
    });
}

RtmpConnection::~RtmpConnection()
{
}

bool RtmpConnection::OnRead(BufferReader &buffer)
{
    bool ret = true;
    if(handshake_->IsCompleted())//是否握手完成，完成之后才能发送message
    {
        ret = HandleChunk(buffer);
    }
    else
    {
        std::shared_ptr<char> res(new char[4096],std::default_delete<char[]>());
        int res_size = handshake_->Parse(buffer,res.get(),4096);
        if(res_size < 0)
        {
            ret = false;
        }
        if(res_size > 0)
        {
            this->Send(res.get(),res_size);
        }
        if(handshake_->IsCompleted())
        {
            if(buffer.ReadableBytes() > 0)
            {
                ret = HandleChunk(buffer);
            }
        }
    }
    return ret;
}

void RtmpConnection::OnClose()
{
    this->HandleDeleteStream();
}

bool RtmpConnection::HandleChunk(BufferReader &buffer)
{
    //处理块
    int ret = -1;
    do
    {
        RtmpMessage rtmp_msg;
        ret = rtmp_chunk_->Parse(buffer,rtmp_msg);
        if(ret >= 0) //解析成功，需要处理message
        {
            if(rtmp_msg.IsCompleted())
            {
                if(!HandleMessage(rtmp_msg))
                {
                    return false;
                }
            }
            if(ret == 0) //缓存区没有数据
            {
                break;
            }
        }
        else
        {
            return false;
        }
    } while (buffer.ReadableBytes() > 0);
    return true;
}

bool RtmpConnection::HandleMessage(RtmpMessage &rtmp_msg)
{
    bool ret = true;
    switch (rtmp_msg.type_id)
    {
    case RTMP_VIDEO: //视频数据
        ret = HandleVideo(rtmp_msg);
        break;
    case RTMP_AUDIO:
        ret = HandleAudio(rtmp_msg);
        break;
    case RTMP_INVOKE:
        ret = HandleInvoke(rtmp_msg);
        break;
    case RTMP_NOTIFY:
        ret = HandleNotify(rtmp_msg);
        break;
    case RTMP_SET_CHUNK_SIZE:
        rtmp_chunk_->SetInChunkSize(ReadUint32BE(rtmp_msg.playload.get()));
        break;
    default:
        break;
    }
    return ret;
}

bool RtmpConnection::HandleInvoke(RtmpMessage &rtmp_msg)
{
    //处理消息
    bool ret = true;
    amf_decoder_.reset(); //解析消息

    //解析消息名称
    int bytes_used =  amf_decoder_.decode(rtmp_msg.playload.get(),rtmp_msg.lenght,1);
    if(bytes_used < 0)
    {
        printf("bytes_used < 0\n");
        return false;
    }

    std::string method = amf_decoder_.getString();
    //需要判断流是否创建
    if(rtmp_msg.stream_id == 0)
    {
        bytes_used += amf_decoder_.decode(rtmp_msg.playload.get() + bytes_used ,rtmp_msg.lenght - bytes_used);
        //处理连接或是创建流
        if(method == "connect")
        {
            ret = HandleConnect();
        }
        else if(method == "createStream")
        {
            ret = HandleCreateStream();
        }
    }
    else if(rtmp_msg.stream_id == stream_id_)
    {
        bytes_used += amf_decoder_.decode(rtmp_msg.playload.get() + bytes_used ,rtmp_msg.lenght - bytes_used,3);
        stream_name_ = amf_decoder_.getString();
        stream_path_ = "/" + app_ + "/" + stream_name_;

        if(rtmp_msg.lenght > bytes_used)
        {
            bytes_used += amf_decoder_.decode(rtmp_msg.playload.get() + bytes_used,rtmp_msg.lenght - bytes_used);
        }

        if(method == "publish")
        {
            ret = HandlePublish();
        }
        else if(method == "play")
        {
            ret = HandlePlay();
        }
        else if(method == "DeleteStream")
        {
            ret = HandleDeleteStream();
        }
    }
    return ret;
}

bool RtmpConnection::HandleNotify(RtmpMessage &rtmp_msg)
{
    //准备解码器
    amf_decoder_.reset();

    //解析消息名称
    int bytes_used = amf_decoder_.decode(rtmp_msg.playload.get(),rtmp_msg.lenght,1);

    std::string method = amf_decoder_.getString();
    if(method == "@setDataFrame")
    {
        amf_decoder_.reset();
        bytes_used = amf_decoder_.decode(rtmp_msg.playload.get() + bytes_used,rtmp_msg.lenght - bytes_used,1);
        if(bytes_used < 0)
        {
            return false;
        }
        //是不是元数据
        if(amf_decoder_.getString() == "onMetaData")
        {
            amf_decoder_.decode(rtmp_msg.playload.get() + bytes_used,rtmp_msg.lenght - bytes_used);
            meta_data_ = amf_decoder_.getObjects();
        }

        //设置元数据
        //TODO 获取session设置元数据
        auto server = rtmp_server_.lock();
        if(!server)
        {
            return false;
        }

        auto session = rtmp_session_.lock();
        if(session)
        {
            session->SendMetaData(meta_data_);
        }
    }
    return true;
}

bool RtmpConnection::HandleAudio(RtmpMessage &rtmp_msg)
{
    uint8_t type = RTMP_AUDIO;
    uint8_t* playload = (uint8_t*)rtmp_msg.playload.get();
    uint32_t lenght = rtmp_msg.lenght;
    uint8_t sound_format = (playload[0] >> 4) & 0x0f; //音频格式
    uint8_t code_id = playload[0] & 0x0f; //编码id

    auto server = rtmp_server_.lock();
    if(!server)
    {
        return false;
    }

    auto session = rtmp_session_.lock();
    if(!session)
    {
        return false;
    }

    if(sound_format == RTMP_CODEC_ID_AAC && playload[1] == 0) //说明编码器为aac，第二字节为0说明这个音频数据为AAC序列头
    {
        aac_sequence_header_szie_ = rtmp_msg.lenght;
        aac_sequence_header_.reset(new char[rtmp_msg.lenght],std::default_delete<char[]>());
        memcpy(aac_sequence_header_.get(),rtmp_msg.playload.get(),aac_sequence_header_szie_);

        //TODO session来设置aac序列头
        session->SetAacSequenceHeader(aac_sequence_header_,aac_sequence_header_szie_);
        type = RTMP_AAC_SEQUENCE_HEADER;
    }

    //TODO session 发送音频数据；
    session->SendMediaData(type,rtmp_msg.timestamp,rtmp_msg.playload,rtmp_msg.lenght);
    return true;
}

bool RtmpConnection::HandleVideo(RtmpMessage &rtmp_msg)
{
    uint8_t type = RTMP_VIDEO;
    uint8_t* playload = (uint8_t*)rtmp_msg.playload.get();
    uint32_t lenght = rtmp_msg.lenght;

    uint8_t frame_type = (playload[0] >> 4) & 0x0f;//获取帧类型
    uint8_t code_id = playload[0] & 0x0f; //视频编码ID

    auto server = rtmp_server_.lock();
    if(!server)
    {
        return false;
    }

    auto session = rtmp_session_.lock();
    if(!session)
    {
        return false;
    }

    if(frame_type == 1 && code_id == RTMP_CODEC_ID_H264 && playload[1] == 0) //我们更新264序列头
    {
        avc_sequence_header_size_ = rtmp_msg.lenght;
        avc_sequence_header_.reset(new char[rtmp_msg.lenght],std::default_delete<char[]>());
        memcpy(avc_sequence_header_.get(),rtmp_msg.playload.get(),avc_sequence_header_size_);

        //TODO session来设置avc序列头
        session->SetAvcSequenceHeader(avc_sequence_header_,avc_sequence_header_size_);
        type = RTMP_AVC_SEQUENCE_HEADER;
    }

    //TODO session 发送视频数据；
    session->SendMediaData(type,rtmp_msg.timestamp,rtmp_msg.playload,rtmp_msg.lenght);
    return true;
}

bool RtmpConnection::HandleConnect()
{
    if(!amf_decoder_.hasObject("app"))//是否存在app应用程序
    {
        return false;
    }

    AmfObject amfObj = amf_decoder_.getObject("app");
    app_ = amfObj.amf_string; //获取引用程序的值
    if(app_ == "")
    {
        return false;
    }

    SendAcknowlegement();
    SetPeerBandWidth();
    SetChunkSize();

    //应答
    AmfObjects objects;
    amf_encoder_.reset();
    //编码结果
    amf_encoder_.encodeString("_result",7);
    amf_encoder_.encodeNumber(amf_decoder_.getNumber());

    objects["fmsVer"] = AmfObject(std::string("FMS/4,5,0,297"));
    objects["capabilities"] = AmfObject(255.0);
    objects["mode"] = AmfObject(1.0);
    amf_encoder_.encodeObjects(objects);
    //清空对象
    objects.clear();
    //添加参数
    objects["level"] = AmfObject(std::string("status"));
    objects["code"] = AmfObject(std::string("NetConnection.Connect.Success"));
    objects["description"] = AmfObject(std::string("Connection succeeded"));
    objects["objectEncoding"] = AmfObject(0.0);
    amf_encoder_.encodeObjects(objects);
    SendInvokeMessage(RTMP_CHUNK_INVOKE_ID,amf_encoder_.data(),amf_encoder_.size());
    printf("HandleConnect\n");
    return true;
}

bool RtmpConnection::HandleCreateStream()
{

    int stream_id = rtmp_chunk_->GetStreamId();

    AmfObjects objects;
    //结果
    amf_encoder_.reset();
    amf_encoder_.encodeString("_result",7);
    amf_encoder_.encodeNumber(amf_decoder_.getNumber());
    //需要填一个空对象
    amf_encoder_.encodeObjects(objects);
    amf_encoder_.encodeNumber(stream_id);
    SendInvokeMessage(RTMP_CHUNK_INVOKE_ID,amf_encoder_.data(),amf_encoder_.size());
    stream_id_ = stream_id;
    return true;
}

bool RtmpConnection::HandlePublish()
{
    auto server = rtmp_server_.lock();
    if(!server)
    {
        return false;
    }

    AmfObjects objects;
    amf_encoder_.reset();
    amf_encoder_.encodeString("onStatus",8);
    amf_encoder_.encodeNumber(0);
    amf_encoder_.encodeObjects(objects);

    bool is_error = false;
    //我们需判断是否已经推流
    if(server->HasPublisher(stream_path_))
    {
        printf("HasPublisher\n");
        is_error = true;
        objects["level"] = AmfObject(std::string("error"));
        objects["code"] = AmfObject(std::string("NetStream.Publish.BadName"));//说明当前这个流已经推送
        objects["description"] = AmfObject(std::string("Stream already publishing."));
    }
    //状态是推流状态，也不能推
    else if(state_ == START_PUBLISH)
    {
        printf("START_PUBLISH\n");
        is_error = true;
        objects["level"] = AmfObject(std::string("error"));
        objects["code"] = AmfObject(std::string("NetStream.Publish.BadConnection"));//说明当前这个流已经推送
        objects["description"] = AmfObject(std::string("Stream already publishing."));
    }
    else
    {
        objects["level"] = AmfObject(std::string("status"));
        objects["code"] = AmfObject(std::string("NetStream.Publish.Start"));//说明当前这个流已经推送
        objects["description"] = AmfObject(std::string("Start publishing."));

        //TODO
        //添加Session
        server->AddSesion(stream_path_);
        rtmp_session_ = server->GetSession(stream_path_);

        if(server)
        {
            server->NotifyEvent("publish.start",stream_path_);
        }
    }
    amf_encoder_.encodeObjects(objects);
    SendInvokeMessage(RTMP_CHUNK_INVOKE_ID,amf_encoder_.data(),amf_encoder_.size());

    if(is_error)
    {
        return false;
    }else
    {
        state_ = START_PUBLISH;
        is_publishing_ = true;
    }

    auto session = rtmp_session_.lock();
    if(session)
    {
        session->AddSink(std::dynamic_pointer_cast<RtmpSink>(shared_from_this()));
    }
    return true;
}

bool RtmpConnection::HandlePlay()
{
    auto server = rtmp_server_.lock();
    if(!server)
    {
        return false;
    }
    AmfObjects objects;
    amf_encoder_.reset();

    //添加应答
    amf_encoder_.encodeString("onStatus",8);
    amf_encoder_.encodeNumber(0);
    objects["level"] = AmfObject(std::string("status"));
    objects["code"] = AmfObject(std::string("NetStream.Play.Reset"));
    objects["description"] = AmfObject(std::string("Resetting ond playing stream."));
    amf_encoder_.encodeObjects(objects);
    if(!SendInvokeMessage(RTMP_CHUNK_INVOKE_ID,amf_encoder_.data(),amf_encoder_.size()))
    {
        return false;
    }

    //在发送play
    objects.clear();
    amf_encoder_.reset();
    amf_encoder_.encodeString("onStatus",8);
    amf_encoder_.encodeNumber(0);
    objects["level"] = AmfObject(std::string("status"));
    objects["code"] = AmfObject(std::string("NetStream.Play.Start"));
    objects["description"] = AmfObject(std::string("Started playing."));
    amf_encoder_.encodeObjects(objects);
    if(!SendInvokeMessage(RTMP_CHUNK_INVOKE_ID,amf_encoder_.data(),amf_encoder_.size()))
    {
        return false;
    }

    //再通知客户端权限
    amf_encoder_.reset();
    amf_encoder_.encodeString("|RtmpSampleAccess",17);
    amf_encoder_.encodeBoolean(true); //允许读
    amf_encoder_.encodeBoolean(true); //允许写
    if(!this->SendNotifyMessage(RTMP_CHUNK_DATA_ID,amf_encoder_.data(),amf_encoder_.size()))
    {
        return false;
    }

    //更新状态
    state_ = START_PLAY;
    //TODO 
    //session添加客户端
    rtmp_session_ = server->GetSession(stream_path_);
    auto session = rtmp_session_.lock();
    if(session)
    {
        session->AddSink(std::dynamic_pointer_cast<RtmpSink>(shared_from_this()));
    }

    if(server)
    {
        server->NotifyEvent("Play.start",stream_path_);
    }

    return true;
}

bool RtmpConnection::HandleDeleteStream()
{
    auto server = rtmp_server_.lock();
    if(!server)
    {
        return false;
    }

    if(stream_path_ != "")
    {
        //TODO session移除会话
        auto session = rtmp_session_.lock();
        if(session)
        {
            auto conn = std::dynamic_pointer_cast<RtmpSink>(shared_from_this());
            GetTaskSchduler()->AddTimer([session,conn](){
                session->RemoveSink(conn);
                return false;
            },1);
            if(is_publishing_)
            {
                server->NotifyEvent("publish,stop",stream_path_);
            }
            else if(is_playing_)
            {
                server->NotifyEvent("play.stop",stream_path_);
            }
        }

    is_playing_ = false;
    is_publishing_ = false;
    has_key_frame = false;
    rtmp_chunk_->Clear();
    }
    return true;
}

void RtmpConnection::SetPeerBandWidth()
{
    std::shared_ptr<char> data(new char[5],std::default_delete<char[]>());
    WriteUint32BE(data.get(),peer_width_);
    data.get()[4] = 2; // 0 1 2
    RtmpMessage rtmp_msg;
    rtmp_msg.type_id = RTMP_BANDWIDTH_SIZE;
    rtmp_msg.playload = data;
    rtmp_msg.lenght = 5;
    SendRtmpChunks(RTMP_CHUNK_CONTROL_ID,rtmp_msg);
}

void RtmpConnection::SendAcknowlegement()
{
    std::shared_ptr<char> data(new char[4],std::default_delete<char[]>());
    WriteUint32BE(data.get(),ackonwledgement_size_);

    RtmpMessage rtmp_msg;
    rtmp_msg.type_id = RTMP_ACK_SIZE;
    rtmp_msg.playload = data;
    rtmp_msg.lenght = 4;
    SendRtmpChunks(RTMP_CHUNK_CONTROL_ID,rtmp_msg);
}

void RtmpConnection::SetChunkSize()
{
    rtmp_chunk_->SetOutChunkSize(max_chunk_size_);
    std::shared_ptr<char> data(new char[4],std::default_delete<char[]>());
    WriteUint32BE(data.get(),max_chunk_size_);
    RtmpMessage rtmp_msg;
    rtmp_msg.type_id = RTMP_SET_CHUNK_SIZE;
    rtmp_msg.playload = data;
    rtmp_msg.lenght = 4;
    SendRtmpChunks(RTMP_CHUNK_CONTROL_ID,rtmp_msg);
}

bool RtmpConnection::SendInvokeMessage(uint32_t csid, std::shared_ptr<char> playload, uint32_t playload_size)
{
    if(this->IsClosed())
    {
        return false;
    }

    RtmpMessage rtmp_msg;
    rtmp_msg.type_id = RTMP_INVOKE;
    rtmp_msg.timestamp = 0;
    rtmp_msg.stream_id = stream_id_;
    rtmp_msg.playload = playload;
    rtmp_msg.lenght = playload_size;
    SendRtmpChunks(csid,rtmp_msg);
    return true;
}

bool RtmpConnection::SendNotifyMessage(uint32_t csid, std::shared_ptr<char> playload, uint32_t playload_size)
{
    if(this->IsClosed())
    {
        return false;
    }

    RtmpMessage rtmp_msg;
    rtmp_msg.type_id = RTMP_NOTIFY;
    rtmp_msg.timestamp = 0;
    rtmp_msg.stream_id = stream_id_;
    rtmp_msg.playload = playload;
    rtmp_msg.lenght = playload_size;
    SendRtmpChunks(csid,rtmp_msg);
    return true;
}

bool RtmpConnection::IsKeyFrame(std::shared_ptr<char> data, uint32_t size)
{
    uint8_t frame_type = (data.get()[0] >> 4) & 0x0f;
    uint8_t code_id = data.get()[0] & 0x0f;
    return (frame_type == 1 && code_id == RTMP_CODEC_ID_H264);
}

void RtmpConnection::SendRtmpChunks(uint32_t csid, RtmpMessage &rtmp_msg)
{
    uint32_t capacity = rtmp_msg.lenght + rtmp_msg.lenght / max_chunk_size_ * 5 + 1024; 
    std::shared_ptr<char> buffer(new char[capacity],std::default_delete<char[]>());
    int size = rtmp_chunk_->CreateChunk(csid,rtmp_msg,buffer.get(),capacity);
    if(size > 0)
    {
        this->Send(buffer.get(),size);
    }
}

bool RtmpConnection::SendMetaData(AmfObjects metaData)
{
    if(this->IsClosed())
    {
        return false;
    }

    if(meta_data_.size() == 0)
    {
        return false;
    }

    amf_encoder_.reset();
    amf_encoder_.encodeString("onMetaData",10);
    amf_encoder_.encodeECMA(meta_data_);
    if(!SendNotifyMessage(RTMP_CHUNK_DATA_ID,amf_encoder_.data(),amf_encoder_.size()))
    {
        return false;
    }
    return true;
}

bool RtmpConnection::SendMediaData(uint8_t type, uint64_t timestamp, std::shared_ptr<char> playload, uint32_t playload_size)
{
    if(this->IsClosed())
    {
        return false;
    }

    if(playload_size == 0)
    {
        return false;
    }

    is_playing_ = true;

    if(type == RTMP_AVC_SEQUENCE_HEADER)
    {
        avc_sequence_header_ = playload;
        avc_sequence_header_size_ = playload_size;
    }
    else if(type == RTMP_AAC_SEQUENCE_HEADER)
    {
        aac_sequence_header_ = playload;
        aac_sequence_header_szie_ = playload_size;
    }

    if(!has_key_frame && avc_sequence_header_size_ > 0
    && (type != RTMP_AVC_SEQUENCE_HEADER)
    && (type != RTMP_AAC_SEQUENCE_HEADER)) //说明数据包既不是序列头，还没有收到关键帧
    {
        //开始判断是否为关键帧
        if(IsKeyFrame(playload,playload_size))
        {
            has_key_frame = true;
        }
        else
        {
            return true;
        }
    }

    //收到关键帧之后开始发送message
    RtmpMessage rtmp_msg;
    rtmp_msg._timestamp = timestamp;
    rtmp_msg.stream_id = stream_id_;
    rtmp_msg.playload = playload;
    rtmp_msg.lenght = playload_size;

    //还有msg类型
    if(type == RTMP_VIDEO ||type ==RTMP_AVC_SEQUENCE_HEADER)
    {
        rtmp_msg.type_id = RTMP_VIDEO;
        SendRtmpChunks(RTMP_CHUNK_VIDEO_ID,rtmp_msg);
    }
    else if(type == RTMP_AUDIO ||type ==RTMP_AAC_SEQUENCE_HEADER)
    {
        rtmp_msg.type_id = RTMP_AUDIO;
        SendRtmpChunks(RTMP_CHUNK_AUDIO_ID,rtmp_msg);
    }
    return true;

}
