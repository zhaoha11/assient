#include "StyleLoader.h"
#include <mutex>
#include <QFile>

std::unique_ptr<StyleLoader> StyleLoader::instance_ = nullptr;

StyleLoader::~StyleLoader()
{

}

StyleLoader *StyleLoader::getInstance()
{
    static std::once_flag flag;// 函数局部静态：每个线程首次进入时只初始化一次
    std::call_once(flag,[&](){   // 多线程并发调用时，只有一个线程执行 lambda
        instance_.reset(new StyleLoader());
    });
    return instance_.get();   // 其余线程等 call_once 结束后直接拿现成的实例
}

void StyleLoader::loadStyle(const QString &filepath, QWidget *w)
{
    QFile file(filepath);
    if(!file.open(QIODevice::ReadOnly | QIODevice::Text))
    {
        return;
    }
    QString qss = QString::fromUtf8(file.readAll().data());
    //设置这个样式表
    w->setStyleSheet(qss);
}

StyleLoader::StyleLoader()
{

}

