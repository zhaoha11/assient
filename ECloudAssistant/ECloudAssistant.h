#ifndef ECLOUDASSISTANT_H
#define ECLOUDASSISTANT_H

#include <QWidget>
class MainWgt;
class TitleWgt;
class ListInfoWgt;

class ECloudAssistant : public QWidget
{
    Q_OBJECT

public:
    ECloudAssistant(QWidget *parent = nullptr);
    ~ECloudAssistant();
protected:
    void mouseMoveEvent(QMouseEvent* event)override;
    void mousePressEvent(QMouseEvent* event)override;
    void mouseReleaseEvent(QMouseEvent* event)override;
    bool eventFilter(QObject* watched, QEvent* event)override;
private:
    //无边框窗口缩放：判断全局坐标落在哪条边/角上，无边角时返回0
    Qt::Edges resizeEdgesAt(const QPoint& globalPos)const;
    void updateResizeCursor(const QPoint& globalPos);
    static constexpr int kResizeMargin = 6; //边缘命中带宽（逻辑像素）
    QPoint point_;
    bool is_press_ = false;
    MainWgt* mainWgt_;
    TitleWgt* titleWgt_;
    ListInfoWgt* listWgt_;
};
#endif // ECLOUDASSISTANT_H
