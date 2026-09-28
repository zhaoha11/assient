#include "ECloudAssistant.h"
#include "TitleWgt.h"
#include "ListInfoWgt.h"
#include "MainWgt.h"
#include <QApplication>
#include <QCursor>
#include <QMouseEvent>
#include <QPushButton>
#include <QGridLayout>
#include <QWindow>

namespace
{
//递归开启鼠标追踪：无边框窗口的边缘悬停光标依赖子控件转发的MouseMove事件
void enableMouseTracking(QWidget* w)
{
    w->setMouseTracking(true);
    const QList<QObject*> children = w->children();
    for(QObject* c : children)
    {
        if(QWidget* cw = qobject_cast<QWidget*>(c))
        {
            enableMouseTracking(cw);
        }
    }
}

//边缘组合对应的缩放光标形状
Qt::CursorShape cursorForEdges(Qt::Edges edges)
{
    if(edges == (Qt::LeftEdge|Qt::TopEdge) || edges == (Qt::RightEdge|Qt::BottomEdge))
    {
        return Qt::SizeFDiagCursor;
    }
    if(edges == (Qt::RightEdge|Qt::TopEdge) || edges == (Qt::LeftEdge|Qt::BottomEdge))
    {
        return Qt::SizeBDiagCursor;
    }
    if(edges == Qt::LeftEdge || edges == Qt::RightEdge)
    {
        return Qt::SizeHorCursor;
    }
    if(edges == Qt::TopEdge || edges == Qt::BottomEdge)
    {
        return Qt::SizeVerCursor;
    }
    return Qt::ArrowCursor;
}
}

ECloudAssistant::ECloudAssistant(QWidget *parent)
    : QWidget(parent)
{
    setWindowFlag(Qt::FramelessWindowHint);
    setAttribute(Qt::WA_TranslucentBackground);
    //可缩放窗口：初始尺寸与最小尺寸一致，保证右侧页面布局不塌陷
    resize(800,540);
    setMinimumSize(800,540);
    setStyleSheet("background-color: #121212");

    mainWgt_ = new MainWgt(this);
    titleWgt_ = new TitleWgt(this);
    listWgt_ = new ListInfoWgt(this);

    connect(listWgt_,&ListInfoWgt::sig_Select,mainWgt_,&MainWgt::slot_ItemCliked);

    //布局
    QGridLayout* layout = new QGridLayout(this);
    layout->setSpacing(0);
    layout->addWidget(listWgt_,0,0,2,1);
    layout->addWidget(titleWgt_,0,1,1,2);
    layout->addWidget(mainWgt_,1,1,1,2);
    layout->setContentsMargins(0,0,0,0);
    setLayout(layout);

    //无边框缩放：追踪鼠标并在应用级过滤器中命中四边四角
    enableMouseTracking(this);
    qApp->installEventFilter(this);
}

ECloudAssistant::~ECloudAssistant()
{
}

bool ECloudAssistant::eventFilter(QObject* watched, QEvent* event)
{
    QWidget* w = qobject_cast<QWidget*>(watched);
    //只处理本窗口的事件，不影响远程监控等独立窗口
    if(w && w->window() == this)
    {
        switch(event->type())
        {
        case QEvent::MouseMove:
        case QEvent::MouseButtonPress:
        {
            const QPoint g = static_cast<QMouseEvent*>(event)->globalPosition().toPoint();
            //按钮保持优先：命中按钮时不做缩放命中，避免抢占最小化/关闭点击
            if(qobject_cast<QPushButton*>(childAt(mapFromGlobal(g))))
            {
                break;
            }
            if(event->type() == QEvent::MouseMove)
            {
                //拖动过程不更新缩放光标，避免与窗口拖动视觉冲突
                if(static_cast<QMouseEvent*>(event)->buttons() == Qt::NoButton)
                {
                    updateResizeCursor(g);
                }
            }
            else if(static_cast<QMouseEvent*>(event)->button() == Qt::LeftButton)
            {
                const Qt::Edges edges = resizeEdgesAt(g);
                //交给系统原生缩放；命中成功则消费事件，避免同时触发标题拖动
                if(edges && windowHandle() && windowHandle()->startSystemResize(edges))
                {
                    return true;
                }
            }
            break;
        }
        case QEvent::Leave:
        {
            if(!frameGeometry().contains(QCursor::pos()))
            {
                unsetCursor();
            }
            break;
        }
        default:
            break;
        }
    }
    return QWidget::eventFilter(watched,event);
}

Qt::Edges ECloudAssistant::resizeEdgesAt(const QPoint& globalPos)const
{
    //最大化状态下不允许拖拽缩放
    if(isMaximized() || isFullScreen())
    {
        return Qt::Edges();
    }
    const QRect f = frameGeometry();
    Qt::Edges edges;
    if(globalPos.x() <= f.left() + kResizeMargin)
    {
        edges |= Qt::LeftEdge;
    }
    if(globalPos.x() >= f.right() - kResizeMargin + 1)
    {
        edges |= Qt::RightEdge;
    }
    if(globalPos.y() <= f.top() + kResizeMargin)
    {
        edges |= Qt::TopEdge;
    }
    if(globalPos.y() >= f.bottom() - kResizeMargin + 1)
    {
        edges |= Qt::BottomEdge;
    }
    return edges;
}

void ECloudAssistant::updateResizeCursor(const QPoint& globalPos)
{
    const Qt::Edges edges = resizeEdgesAt(globalPos);
    if(edges)
    {
        setCursor(cursorForEdges(edges));
    }
    else
    {
        unsetCursor();
    }
}

void ECloudAssistant::mouseMoveEvent(QMouseEvent *event)
{
    if(event->buttons() & Qt::LeftButton && is_press_)
    {
        if(!qobject_cast<QPushButton*>(childAt(event->pos())))
        {
            move(event->globalPos() - point_);
        }
    }
    QWidget::mouseMoveEvent(event);
}

void ECloudAssistant::mousePressEvent(QMouseEvent *event)
{
    if(!qobject_cast<QPushButton*>(childAt(event->pos())))
    {
        is_press_ = true;
        point_ = event->globalPos() - this->frameGeometry().topLeft();
    }
    QWidget::mousePressEvent(event);
}

void ECloudAssistant::mouseReleaseEvent(QMouseEvent *event)
{
    if(event->button() == Qt::LeftButton)
    {
        is_press_ = false;
    }
    QWidget::mouseReleaseEvent(event);
}
