QT = core
CONFIG += console c++17
CONFIG -= app_bundle
TEMPLATE = app
TARGET = encoder_optimization_test
QMAKE_CXXFLAGS += /utf-8

FFMPEG_ROOT = $$(FFMPEG_HOME)
isEmpty(FFMPEG_ROOT): error(Set FFMPEG_HOME to the FFmpeg development directory)
INCLUDEPATH += $$PWD/../Codec $$PWD/../Net $$FFMPEG_ROOT/include
SOURCES += $$PWD/EncoderOptimizationTest.cpp \
    $$PWD/../Codec/H264Encoder.cpp \
    $$PWD/../Codec/VideoEncoder.cpp \
    $$PWD/../Codec/SoftwareVideoEncoder.cpp \
    $$PWD/../Codec/HardwareVideoEncoder.cpp \
    $$PWD/../Codec/VideoConvert.cpp \
    $$PWD/../Codec/D3D11SharedContext.cpp \
    $$PWD/../Net/EventLoop.cpp \
    $$PWD/../Net/TaskScheduler.cpp \
    $$PWD/../Net/SelectTaskScheduler.cpp \
    $$PWD/../Net/Timer.cpp
LIBS += -L$$FFMPEG_ROOT/lib -lavcodec -lavutil -lswscale -ld3d11 -ldxgi -lws2_32
