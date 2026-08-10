#ifndef Channel_hpp
#define Channel_hpp
#include <sys/epoll.h>

class Epoll;

class Channel 
{
public:
    Channel(Epoll* ep, int fd);
    ~Channel();  
    
    void enableReading();
    int Getfd();
    uint32_t GetEvents();
    uint32_t GetRevents();
    bool GetInEpoll();
    void SetInEpoll();
    void SetRevents(uint32_t event);

private:
    Epoll* m_epoll;
    int m_fd;
    uint32_t m_events;
    uint32_t m_revents;
    bool m_inEpoll;
};

#endif /* Channel_hpp */

