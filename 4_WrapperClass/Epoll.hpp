#ifndef EPOLL_HPP
#define EPOLL_HPP
#include <sys/epoll.h>
#include <vector>

#define MAX_EVENTS 1000

class Epoll {
public:
    Epoll();
    ~Epoll();
    void add(int fd, uint32_t events);
    // 定义空的事件容器，用来存放就绪事件
    std::vector<epoll_event> poll(int timeout = -1);
private:
    int m_epoll_fd;
    epoll_event *m_events;
    
};

#endif // EPOLL_HPP