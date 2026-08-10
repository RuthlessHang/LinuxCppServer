#include "Channel.hpp"
#include "Epoll.hpp"

Channel::Channel(Epoll *ep, int fd):m_epoll(ep), m_fd(fd), m_events(0), m_revents(0), m_inEpoll(false)
{

}

Channel::~Channel()
{
}

void Channel::enableReading()
{
    m_events = EPOLLIN |EPOLLET; // 设置为边缘触发模式
    m_epoll->updateChannel(this); // 更新Epoll对象中的事件
}

int Channel::Getfd()
{
    return m_fd;
}

uint32_t Channel::GetEvents()
{
    return m_events;
}

uint32_t Channel::GetRevents()
{
    return m_revents;
}

bool Channel::GetInEpoll()
{
    return m_inEpoll;
}

void Channel::SetInEpoll()
{
    m_inEpoll = true;
}

void Channel::SetRevents(uint32_t event)
{
    m_revents = event;
}
