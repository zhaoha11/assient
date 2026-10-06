#include "SoftwareVideoEncoder.h"

extern "C"
{
    #include <libavutil/opt.h>
}

AVCodec* SoftwareVideoEncoder::FindCodec()
{
    return const_cast<AVCodec*>(avcodec_find_encoder(AV_CODEC_ID_H264));
}

bool SoftwareVideoEncoder::ConfigureCodec()
{
    codecContext_->pix_fmt = AV_PIX_FMT_YUV420P;
    //必须写 BASELINE：libx264 不映射 FF_PROFILE_H264_CONSTRAINED_BASELINE，
    //写后者会落到 default 分支被静默忽略，编码器仍按自己的默认档位输出
    codecContext_->profile = FF_PROFILE_H264_BASELINE;
    //Level 4.0 对 1080p 只支持到 30 FPS（8160 宏块 × 30 = 244800，上限 245760），
    //提高帧率必须同步提高 level，否则码流会超出所声明的等级
    codecContext_->level = 40;
    //再加上码率约束，降低编码延迟
    codecContext_->rc_min_rate = config_.video.bitrate;
    codecContext_->rc_max_rate = config_.video.bitrate;
    codecContext_->rc_buffer_size = config_.video.bitrate;
    //设置字典
    av_opt_set(codecContext_->priv_data,"tune","zerolatency",0);//极速编码
    av_opt_set(codecContext_->priv_data,"preset","ultrafast",0);//极速编码
    return true;
}
