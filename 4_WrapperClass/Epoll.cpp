#include "Epoll.hpp"
#include "util.hpp"
#include <string.h>
#include <unistd.h>

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

std::vector<epoll_event> Epoll::poll(int timeout)
{
    int n = epoll_wait(m_epoll_fd, m_events, MAX_EVENTS, timeout);
    errif(n == -1, "epoll_wait failed");
    std::vector<epoll_event> events;
    for(int i = 0; i < n; i++)
    {
        events.push_back(m_events[i]);
    }
    return events;
}