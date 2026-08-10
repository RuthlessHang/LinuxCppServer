#ifndef EPOLL_HPP
#define EPOLL_HPP
#include <sys/epoll.h>
#include <vector>

#define MAX_EVENTS 1000
class Channel;

class Epoll {
public:
    Epoll();
    ~Epoll();
    void updateChannel(Channel* channel);
    std::vector<Channel*> poll (int timeout = -1);
private:
    int m_epoll_fd;
    epoll_event *m_events;
    
};

#endif // EPOLL_HPP