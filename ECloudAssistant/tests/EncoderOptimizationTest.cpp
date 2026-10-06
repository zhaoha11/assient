#include "H264Encoder.h"
#include "VideoFrame.h"
#include "EventLoop.h"
#include <TlHelp32.h>
#include <algorithm>
#include <array>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <vector>

static void check(bool ok,const char* name)
{
    if(!ok) { std::cerr << "FAIL: " << name << '\n'; std::exit(1); }
}

static int spsLevel(const std::vector<quint8>& bytes)
{
    for(size_t i = 0; i + 7 < bytes.size(); ++i)
    {
        if(bytes[i] || bytes[i + 1]) continue;
        size_t nal = 0;
        if(bytes[i + 2] == 1) nal = i + 3;
        else if(bytes[i + 2] == 0 && bytes[i + 3] == 1) nal = i + 4;
        if(nal && (bytes[nal] & 31) == 7) return bytes[nal + 3];
    }
    return -1;
}

static void metadataAndLifecycle()
{
    H264Encoder encoder;
    std::array<quint8,1024> bytes{};
    check(encoder.GetSequenceParams(bytes.data(),static_cast<qint32>(bytes.size())) < 0,"metadata before Open");
    encoder.Close();
    encoder.Close();
    for(int fps : {30,60})
    {
        check(encoder.OPen(1920,1080,fps,12000,AV_PIX_FMT_BGRA),"1080p encoder Open/reopen");
        const int size = encoder.GetSequenceParams(bytes.data(),static_cast<qint32>(bytes.size()));
        check(size > 1,"SPS/PPS available");
        check(spsLevel({bytes.begin(),bytes.begin() + size}) == (fps == 30 ? 40 : 42),
              "1080p30/60 SPS level");
        std::array<quint8,1024> guarded;
        guarded.fill(0xA5);
        check(encoder.GetSequenceParams(guarded.data(),size - 1) < 0,"short metadata buffer rejected");
        check(std::all_of(guarded.begin(),guarded.end(),[](quint8 b){ return b == 0xA5; }),
              "short metadata buffer untouched");
        check(encoder.GetSequenceParams(nullptr,size) < 0,"null metadata buffer rejected");
        check(encoder.GetSequenceParams(guarded.data(),size) == size,"exact metadata buffer accepted");
        check(std::equal(bytes.begin(),bytes.begin() + size,guarded.begin()),"metadata byte equality");
    }
    check(!encoder.OPen(0,1080,60,12000,AV_PIX_FMT_BGRA),"invalid reopen rejected");
    check(encoder.GetSequenceParams(bytes.data(),static_cast<qint32>(bytes.size())) < 0,"failed reopen releases old session");
    encoder.Close();
    check(encoder.GetSequenceParams(bytes.data(),static_cast<qint32>(bytes.size())) < 0,"metadata after Close");
}

static unsigned threadCount()
{
    const HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD,0);
    check(snapshot != INVALID_HANDLE_VALUE,"thread snapshot");
    THREADENTRY32 entry{};
    entry.dwSize = sizeof(entry);
    unsigned count = 0;
    if(Thread32First(snapshot,&entry))
    {
        do { if(entry.th32OwnerProcessID == GetCurrentProcessId()) ++count; }
        while(Thread32Next(snapshot,&entry));
    }
    CloseHandle(snapshot);
    return count;
}

static void eventLoopLifecycle()
{
    // Stop 先于 Start 的确定性用例：Start 必须直接返回，不能清掉停止标志。
    SelectTaskScheduler scheduler;
    scheduler.Stop();
    scheduler.Start();
    const unsigned before = threadCount();
    for(int i = 0; i < 20; ++i) { EventLoop loop(1); }
    const unsigned after = threadCount();
    check(before == after,"20 event-loop cycles leave no threads");
    std::cout << "Event-loop cycles=20 threads before=" << before << " after=" << after << '\n';
}

// 比较紧凑行与带填充行的实际解码像素，覆盖借用输入、P 帧和输入尺寸改变。
static std::vector<quint8> roundTrip(bool padded)
{
    H264Encoder encoder;
    check(encoder.OPen(64,32,60,1000,AV_PIX_FMT_BGRA),"small encoder Open");
    std::array<quint8,1024> params{};
    const int paramsSize = encoder.GetSequenceParams(params.data(),static_cast<qint32>(params.size()));
    check(paramsSize > 0,"small encoder metadata");

    const AVCodec* codec = avcodec_find_decoder(AV_CODEC_ID_H264);
    AVCodecContext* decoder = avcodec_alloc_context3(codec);
    check(decoder && avcodec_open2(decoder,codec,nullptr) == 0,"decoder Open");
    AVFramePtr decoded(av_frame_alloc(),[](AVFrame* p){ av_frame_free(&p); });
    AVPacketPtr packet(av_packet_alloc(),[](AVPacket* p){ av_packet_free(&p); });
    std::vector<quint8> bytes;
    std::vector<quint8> pixels;
    int decodedFrames = 0;

    for(int index = 0; index < 4; ++index)
    {
        const quint32 width = index == 0 ? 64 : (index == 3 ? 67 : 65);
        const quint32 height = index == 0 ? 32 : (index == 3 ? 35 : 33);
        const quint32 stride = width * 4 + (padded ? 28 : 0);
        std::vector<quint8> input(stride * height + AV_INPUT_BUFFER_PADDING_SIZE,0xA5);
        for(quint32 y = 0; y < height; ++y)
        {
            for(quint32 x = 0; x < width; ++x)
            {
                const size_t offset = y * stride + x * 4;
                input[offset] = (x * 3 + index) & 255;
                input[offset + 1] = (y * 5 + index) & 255;
                input[offset + 2] = (x + y + index) & 255;
                input[offset + 3] = 255;
            }
        }
        const auto original = input;
        VideoFrame frame;
        frame.width = width;
        frame.height = height;
        frame.cpu.data = input.data();
        frame.cpu.stride = stride;
        VideoEncodeTiming timing;
        if(index == 0)
        {
            frame.cpu.stride = width * 4 - 1;
            check(encoder.EncodeFrame(frame,index,bytes,&timing) < 0,"short input stride rejected");
            frame.cpu.stride = stride;
        }
        check(encoder.EncodeFrame(frame,index,bytes,&timing) > 0,"frame encoded");
        check(input == original,"borrowed input and row padding untouched");
        if(index == 0)
        {
            check(bytes.size() > static_cast<size_t>(paramsSize) &&
                  std::equal(params.begin(),params.begin() + paramsSize,bytes.begin()),
                  "keyframe SPS/PPS prefix");
        }
        // 编码调用结束后立即覆盖源数据，后续解码应只依赖已产出的码流。
        std::fill(input.begin(),input.end(),0);
        check(av_new_packet(packet.get(),static_cast<int>(bytes.size())) == 0,"decoder packet allocation");
        std::memcpy(packet->data,bytes.data(),static_cast<qint32>(bytes.size()));
        check(avcodec_send_packet(decoder,packet.get()) == 0,"encoded access unit accepted");
        av_packet_unref(packet.get());
        int result;
        while((result = avcodec_receive_frame(decoder,decoded.get())) == 0)
        {
            check(decoded->width == 64 && decoded->height == 32 && decoded->format == AV_PIX_FMT_YUV420P,
                  "decoded geometry and pixel format");
            for(int plane = 0; plane < 3; ++plane)
            {
                const int rows = plane ? 16 : 32;
                const int columns = plane ? 32 : 64;
                for(int y = 0; y < rows; ++y)
                {
                    const quint8* row = decoded->data[plane] + y * decoded->linesize[plane];
                    pixels.insert(pixels.end(),row,row + columns);
                }
            }
            ++decodedFrames;
            av_frame_unref(decoded.get());
        }
        check(result == AVERROR(EAGAIN),"decoder consumes each complete access unit");
    }
    check(decodedFrames == 4,"all four encoded frames decoded");
    encoder.Close();
    VideoFrame closedFrame;
    const quint8 dummy[4]{};
    closedFrame.cpu.data = dummy;
    closedFrame.cpu.stride = 4;
    check(encoder.EncodeFrame(closedFrame,0,bytes) < 0 && bytes.empty(),"encode after Close rejected");
    avcodec_free_context(&decoder);
    return pixels;
}

int main()
{
    av_log_set_level(AV_LOG_ERROR);
    eventLoopLifecycle();
    metadataAndLifecycle();
    const auto compact = roundTrip(false);
    const auto padded = roundTrip(true);
    check(compact.size() == padded.size(),"same decoded byte count");
    int maxDifference = 0;
    size_t differentPixels = 0;
    for(size_t i = 0; i < compact.size(); ++i)
    {
        const int difference = std::abs(static_cast<int>(compact[i]) - padded[i]);
        if(difference) ++differentPixels;
        maxDifference = (std::max)(maxDifference,difference);
    }
    std::cout << "Decoded comparison: differences=" << differentPixels << " max=" << maxDifference
              << '\n';
    check(compact == padded,"compact and padded input decode to identical pixels");
    std::cout << "PASS: event-loop shutdown, metadata bounds, lifecycle, SPS levels, input stride and encode/decode equivalence\n";
}
