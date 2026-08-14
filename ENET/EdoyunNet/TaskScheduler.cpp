#include "TaskScheduler.h"

TaskScheduler::TaskScheduler(int id)
    :id_(id)
    ,is_shutdown_(false)
{
}

TaskScheduler::~TaskScheduler()
{
}
//定时器和IO事件处理在一个线程
void TaskScheduler::Start()
{
    is_shutdown_ = false;
    while (!is_shutdown_)
    {
        //处理定时事件
        this->timer_queue_.HandleTimerEvent();
        //处理IO事件
        this->HandleEvent();
    }
    
}

void TaskScheduler::Stop()
{
    is_shutdown_ = true;
}

TimerId TaskScheduler::AddTimer(const TimerEvent &event, uint32_t mesc)
{
    return timer_queue_.AddTimer(event,mesc);
}

void TaskScheduler::RemvoTimer(TimerId timerId)
{
    timer_queue_.RemoveTimer(timerId);
}
