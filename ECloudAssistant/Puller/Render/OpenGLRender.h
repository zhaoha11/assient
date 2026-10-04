#ifndef OPENGLRENDER_H
#define OPENGLRENDER_H
#include <QLabel>
#include <QOpenGLWidget>
#include <QOpenGLFunctions_3_3_Core>
#include "AV_Common.h"
#include "VideoFramePresenter.h"
#include <QOpenGLTexture>
#include <QOpenGLShaderProgram>
#include <QOpenGLPixelTransferOptions>
#include "defin.h"

class OpenGLRender : public QOpenGLWidget, protected  QOpenGLFunctions_3_3_Core
{
    Q_OBJECT
public:
    explicit OpenGLRender(QWidget* parent = nullptr, Qt::WindowFlags f = Qt::WindowFlags());
    OpenGLRender(const OpenGLRender&) = delete;
    OpenGLRender& operator=(const OpenGLRender&) = delete;
    virtual ~OpenGLRender();
public:
    //刷新通知的槽：通知不含帧，只带会话代号和发送时刻。
    //本函数只清待处理标记并安排一次 update()，真正的取帧/上传纹理/绘制在 paintGL()。
    void OnRepaintRequested(quint64 sessionId,qint64 emitUs);
    void ResetPresentation();
    //播放线程与 GUI 线程之间的最新帧交接点，子类（AVPlayer）的播放线程会写入
    VideoFramePresenter& Presenter(){return presenter_;}
    void GetPosRation(MouseMove_Body& body);
protected:
    virtual void showEvent(QShowEvent *event);
    virtual void initializeGL() override;
    virtual void resizeGL(int w, int h) override;
    virtual void paintGL() override;
private:
    void repaintTexYUV420P(AVFramePtr frame);
    void initTexYUV420P(AVFramePtr frame);
    void freeTexYUV420P();
private:
    VideoFramePresenter presenter_;
    quint64 textureSessionId_ = 0; // 仅由 GUI 线程访问
    QLabel* label_ = nullptr;

    QOpenGLTexture* texY_ = nullptr;
    QOpenGLTexture* texU_ = nullptr;
    QOpenGLTexture* texV_ = nullptr;
    QOpenGLShaderProgram* program_ = nullptr;
    QOpenGLPixelTransferOptions options_;

    GLuint VBO = 0;
    GLuint VAO = 0;
    GLuint EBO = 0;
    QSize   m_size;
    QSizeF  m_zoomSize;
    QRect   m_rect;
    QPointF m_pos;
};
#endif // OPENGLRENDER_H
