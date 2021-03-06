#include "event_loop.h"

#include <stdlib.h>

#include "simple_db/net/poller/select_poller.h"
#include "simple_db/net/poller/epoll.h"

BEGIN_SIMPLE_DB_NS(net)

EventLoop::EventLoop(Poller* poller)
{
    mPoller = poller;
}

EventLoop::~EventLoop()
{
    LOG_INFO << "End of event loop";
    for (auto &item: mHandlerMap) {
        SIMPLE_DB_DELETE_AND_SET_NULL(item.second);
    }
    SIMPLE_DB_DELETE_AND_SET_NULL(mPoller);
}

std::once_flag EventLoop::initFlag;
EventLoop* EventLoop::mEventLoop;

/* static */ EventLoop::GC EventLoop::gc; //全局对象，程序结束时会掉用它的析构函数

/* static */ EventLoop* EventLoop::GetInstance()
{
    std::call_once(initFlag, CreateInstance);
    return mEventLoop;
}

/* static */ void EventLoop::CreateInstance() 
{
    const char *p = getenv(common::POLLER.c_str());
    if (p == nullptr) {
        p = "epoll";
    }
    std::string pollerType(p);

    Poller* poller = nullptr;
    
    if (pollerType == "select") {
        poller = new SelectPoller();
    } else if (pollerType == "epoll") {
        poller = new Epoll();
    }
    if (poller == nullptr) {
        LOG_ERROR << "Create poller failed";
        std::abort();
    }
    mEventLoop = new EventLoop(poller);
}

bool EventLoop::AddHandler(EventHandler *handler, int event)
{    
    if (handler == nullptr) {
        LOG_ERROR << "Handler is nullptr, add failed";
        return false;
    }
    LOG_INFO << "Add new handler" << handler->GetHandlerFd();
    
    std::unique_lock<std::mutex> lock(mMutex);    
    if (mHandlerMap.find(handler->GetHandlerFd()) == mHandlerMap.end()) {
        LOG_INFO << "Add handler " << handler->GetHandlerFd();
        mPoller->Update(handler->GetHandlerFd(), event);
        mHandlerMap[handler->GetHandlerFd()] = handler;
        return true;
    }
    LOG_ERROR << "Hanlder " << handler->GetHandlerFd() << "already exist";
    return false;
}

void EventLoop::RemoveHandler(EventHandler *handler)
{
    std::unique_lock<std::mutex> lock(mMutex);
    if (handler == nullptr) {
        LOG_INFO << "Handler is nullptr ,do nothing";
        return ;
    }

    auto it = mHandlerMap.find(handler->GetHandlerFd());
    if (it != mHandlerMap.end()) {
        mPoller->Unregister(handler->GetHandlerFd());
        mHandlerMap.erase(it);
    }
}

void EventLoop::LoopOnce()
{
    LOG_INFO << "Do once loop";
    std::map<int, int> fdEventMap;    
    mPoller->Poll(1000, fdEventMap);
    if ( fdEventMap.size() == 0)
        return ;

    for (auto &item: fdEventMap) {
        auto it = mHandlerMap.find(item.first);
        if (it == mHandlerMap.end()) {
            LOG_ERROR << "Can not find handler by fd " << item.first;
            continue;
        }
        LOG_INFO << "Handler fd " << it->first;
        it->second->HandleEvent(item.second);
    }
}

void EventLoop::Start()
{
    if (mIsRunning) {
        LOG_ERROR << "Loop already running";
        return;
    }

    mIsRunning = true;
    while (true) {
        LoopOnce();
        if (!mIsRunning)
            break;
    }
}

void EventLoop::Stop()
{
    mIsRunning = false;
}

END_SIMPLE_DB_NS(net)
