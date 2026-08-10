#include "Epoll.hpp"
#include "util.hpp"
#include <string.h>
#include <unistd.h>
#include "Channel.hpp"
Epoll::Epoll():m_epoll_fd(-1), m_events(nullptr)
{
    m_epoll_fd =epoll_create1(0);
    errif(m_epoll_fd == -1, "epoll_create1 failed"); 
    m_events = new epoll_event[MAX_EVENTS];
    memset(m_events , 0 , sizeof(epoll_event) * MAX_EVENTS);
}

Epoll::~Epoll()
{
    if(m_epoll_fd != -1)
    {
        close(m_epoll_fd);
    }
    delete[] m_events; //new[] 对应 delete[]，new 对应 delete
}

void Epoll::add(int fd, uint32_t events)
{
    struct epoll_event ev;
    memset(&ev , 0 , sizeof(ev));
    ev.events = events;
    ev.data.fd = fd;
    errif(epoll_ctl(m_epoll_fd, EPOLL_CTL_ADD, fd, &ev) == -1, "epoll_ctl failed");
}

void Epoll::updateChannel(Channel* channel)
{
    int fd = channel->Getfd();
    uint32_t events = channel->GetEvents();
    struct epoll_event ev;
    memset(&ev , 0 , sizeof(ev));
    ev.events = events; 
    ev.data.ptr = channel; // 将Channel对象的指针存储在epoll_event的data.ptr中
    if(channel->GetInEpoll())
    {
        errif(epoll_ctl(m_epoll_fd, EPOLL_CTL_MOD, fd, &ev) == -1, "epoll_ctl failed");
    }
    else
    {
        errif(epoll_ctl(m_epoll_fd, EPOLL_CTL_ADD, fd, &ev) == -1, "epoll_ctl failed");
    }
}

std::vector<Channel*> Epoll::poll(int timeout)
{
    std::vector<Channel*> activeChannels;
    int nfds = epoll_wait(m_epoll_fd, m_events, MAX_EVENTS, timeout);
    errif(nfds == -1, "epoll_wait failed");
    for(int i = 0 ; i < nfds ; i++)
    {
        //内核返回之前存进去的Channel指针
        Channel* channel = static_cast<Channel*>(m_events[i].data.ptr);
        channel->SetRevents(m_events[i].events);
        activeChannels.push_back(channel);  
    }
    return activeChannels;
    
}