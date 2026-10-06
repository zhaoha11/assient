INCLUDEPATH += $$PWD \
               $$PWD/capture \
               $$PWD/rtmp

HEADERS += \
    $$PWD/rtmp/FlvAvcPacket.h \
    $$PWD/RtmpPushManager.h \
    $$PWD/StatsWindow.h \
    $$PWD/VideoPipelineStats.h \
    $$PWD/capture/AudioBuffer.h \
    $$PWD/capture/AudioCapture.h \
    $$PWD/capture/GDISreenScapture.h \
    $$PWD/capture/ScreenCapture.h \
    $$PWD/capture/WGCScreenCapture.h \
    $$PWD/capture/WASAPICapture.h \
    $$PWD/rtmp/RtmpChunk.h \
    $$PWD/rtmp/RtmpConnection.h \
    $$PWD/rtmp/RtmpHandshake.h \
    $$PWD/rtmp/RtmpMessage.h \
    $$PWD/rtmp/RtmpPublisher.h \
    $$PWD/rtmp/amf.h \
    $$PWD/rtmp/rtmp.h

SOURCES += \
    $$PWD/rtmp/FlvAvcPacket.cpp \
    $$PWD/RtmpPushManager.cpp \
    $$PWD/VideoPipelineStats.cpp \
    $$PWD/capture/AudioCapture.cpp \
    $$PWD/capture/GDISreenScapture.cpp \
    $$PWD/capture/WGCScreenCapture.cpp \
    $$PWD/capture/WASAPICapture.cpp \
    $$PWD/rtmp/RtmpChunk.cpp \
    $$PWD/rtmp/RtmpConnection.cpp \
    $$PWD/rtmp/RtmpHandshake.cpp \
    $$PWD/rtmp/RtmpPublisher.cpp \
    $$PWD/rtmp/amf.cpp

INCLUDEPATH += $(FFMPEG_HOME)/include

LIBS += -L$(FFMPEG_HOME)/lib -lavcodec -lavdevice -lavformat -lavutil -lswresample -lswscale

LIBS += -lws2_32 \
        -lgdi32 \
        -luser32 \
        -lole32 \
        -lksuser \
        -ld3d11 \
        -ldxgi \
        -lruntimeobject

win32-msvc: LIBS += windowsapp.lib
