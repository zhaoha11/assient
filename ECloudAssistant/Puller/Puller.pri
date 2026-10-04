INCLUDEPATH += $$PWD/UI \
               $$PWD/Render

HEADERS += \
    $$PWD/Render/OpenGLRender.h \
    $$PWD/Render/VideoFramePresenter.h \
    $$PWD/UI/AVPlayer.h \
    $$PWD/UI/PullerWgt.h \
    $$PWD/Render/AudioRender.h

SOURCES += \
    $$PWD/Render/OpenGLRender.cpp \
    $$PWD/Render/VideoFramePresenter.cpp \
    $$PWD/UI/AVPlayer.cpp \
    $$PWD/UI/PullerWgt.cpp \
    $$PWD/Render/AudioRender.cpp
