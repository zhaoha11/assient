#include "LocalPlayerWgt.h"
#include <QLabel>
#include <QVBoxLayout>
#include "StyleLoader.h"

LocalPlayerWgt::LocalPlayerWgt(QWidget *parent)
    : QWidget{parent}
{
    setWindowFlag(Qt::FramelessWindowHint);
    setAttribute(Qt::WA_StyledBackground);
    setFixedSize(600,510);
    setObjectName("LocalPlayerWgt");

    QLabel* title = new QLabel(QString::fromUtf8("本地播放"),this);
    title->setObjectName("localPageTitle");

    QLabel* desc = new QLabel(QString::fromUtf8("本地媒体文件播放功能建设中，当前页面为占位界面。"),this);
    desc->setObjectName("localPageDescription");
    desc->setWordWrap(true);

    QVBoxLayout* layout = new QVBoxLayout(this);
    layout->setContentsMargins(48,44,48,48);
    layout->setSpacing(0);
    layout->addWidget(title);
    layout->addSpacing(8);
    layout->addWidget(desc);
    layout->addStretch();
    setLayout(layout);

    StyleLoader::getInstance()->loadStyle(":/UI/brown/main.css",this);
}
