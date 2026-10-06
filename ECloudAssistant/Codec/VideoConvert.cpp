#include "VideoConvert.h"

extern "C"
{
    #include<libavformat/avformat.h>
    #include<libswscale/swscale.h>
}

VideoConverter::VideoConverter()
    :width_(0)
    ,height_(0)
    ,format_(AV_PIX_FMT_NONE)
    ,swsContext_(nullptr)
{

}

bool VideoConverter::Open(qint32 in_width, qint32 in_height, AVPixelFormat in_format, qint32 out_width, qint32 out_height, AVPixelFormat out_format)
{
    //初始化转换器
    if(swsContext_)
    {
        return false;
    }

    //初始化转换器
    // 奇数宽 BGRA 先保留全宽色度再缩放，避免右边缘色度受源行填充内容影响。
    const int flags = SWS_BICUBIC | ((in_format == AV_PIX_FMT_BGRA && (in_width & 1))
                                     ? SWS_FULL_CHR_H_INP : 0);
    swsContext_ = sws_getContext(in_width,in_height,in_format,
                                 out_width,out_height,out_format,
                                 flags,NULL,NULL,NULL);
    width_ = out_width;
    height_ = out_height;
    format_ = out_format;
    return swsContext_ != nullptr;
}

void VideoConverter::Close()
{
    if(swsContext_)
    {
        sws_freeContext(swsContext_);
        swsContext_ = nullptr;
    }
}

qint32 VideoConverter::Convert(AVFramePtr in_frame, AVFramePtr &out_frame)
{
    //格式转换 可以转换像素或者是格式
    if(!swsContext_)
    {
        return -1;
    }

    //创建输出帧
    out_frame.reset(av_frame_alloc(),[](AVFrame* ptr){av_frame_free(&ptr);});

    //初始化输出帧
    out_frame->width = width_;
    out_frame->height = height_;
    out_frame->format = format_;
    out_frame->pts = in_frame->pts;
    out_frame->pkt_dts = in_frame->pkt_dts;

    //获取内存
    if(av_frame_get_buffer(out_frame.get(),0) != 0)
    {
        return -1;
    }

    //开始转换
    uint32_t height_slice = sws_scale(swsContext_,in_frame->data,in_frame->linesize,0,in_frame->height,out_frame->data,out_frame->linesize);
    if(height_slice < 0)
    {
        return -1;
    }
    return height_slice;
}

VideoConverter::~VideoConverter()
{
    Close();
}
