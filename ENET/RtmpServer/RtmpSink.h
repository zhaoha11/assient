#ifndef _RTMPSINK_H_
#define _RTMPSINK_H_
#include <cstdint>
#include <memory>
#include "amf.h"

class RtmpSink
{
public:
    RtmpSink(){}
    virtual ~RtmpSink(){}

    virtual bool SendMetaData(AmfObjects metaData) {return true;} //发送消息，通知后面的数据为元数据
    virtual bool SendMediaData(uint8_t type,uint64_t timestamp,std::shared_ptr<char> playload,uint32_t playload_size) = 0; //纯虚函数

    virtual bool IsPlayer() {return false;}
    virtual bool IsPublisher() {return false;}
    virtual bool IsPlaying() {return false;}
    virtual bool IsPublishing() {return false;}

    virtual uint32_t GetId() = 0;

};
#endif