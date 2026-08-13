#include <vector>
#include "EventLoop.hpp"
#include "Epoll.hpp"
#include "Channel.hpp"
#include "ThreadPool.hpp"

EventLoop::EventLoop():m_epoll(nullptr) , quit(false)
{
    m_epoll = new Epoll();
    m_threadPool = new ThreadPool();
}

EventLoop::~EventLoop()
{
    delete m_epoll;
    delete m_threadPool;
}

void EventLoop::loop()
{
    while (!quit)
    {
        std::vector<Channel*> activeChannel = m_epoll->poll();
        for (Channel* ch : activeChannel)
        {
            ch->handleEvent();
        }
    }
}

void EventLoop::updateChannel(Channel *channel)
{
    m_epoll->updateChannel(channel);
}

void EventLoop::addThreadPool(std::function<void()> task)
{
    m_threadPool->add(task);
}