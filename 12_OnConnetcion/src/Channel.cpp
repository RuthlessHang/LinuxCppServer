#include "Channel.hpp"
#include "EventLoop.hpp"
#include <unistd.h>

Channel::Channel(EventLoop *loop, int fd):m_loop(loop), m_fd(fd), m_events(0), m_revents(0), m_inEpoll(false)
{

}

Channel::~Channel()
{
    if(m_fd != -1)
    {
        close(m_fd);
        m_fd = -1;
    }
}

void Channel::enableRead()
{
    m_events |= EPOLLIN | EPOLLPRI; 
    m_loop->updateChannel(this); 
}

void Channel::useET()
{
    m_events |= EPOLLET;
    m_loop->updateChannel(this); 
}

void Channel::handleEvent()
{
    if(m_revents & (EPOLLIN | EPOLLPRI)) {
        if(m_readCallback) m_readCallback();
    }
    if(m_revents & EPOLLOUT) {
        if(m_writeCallback) m_writeCallback();
    }  
}

int Channel::getFd()
{
    return m_fd;
}

uint32_t Channel::getEvents()
{
    return m_events;
}

uint32_t Channel::getRevents()
{
    return m_revents;
}

bool Channel::getInEpoll()
{
    return m_inEpoll;
}

void Channel::setInEpoll()
{
    m_inEpoll = true;
}

void Channel::setRevents(uint32_t event)
{
    m_revents = event;
}

void Channel::setReadCallback(std::function<void()> cb)
{
    m_readCallback = cb;
}
