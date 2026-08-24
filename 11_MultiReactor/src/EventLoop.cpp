#include <vector>
#include "EventLoop.hpp"
#include "Epoll.hpp"
#include "Channel.hpp"

EventLoop::EventLoop():m_epoll(nullptr) , quit(false)
{
    m_epoll = new Epoll();
}

EventLoop::~EventLoop()
{
    delete m_epoll;

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

