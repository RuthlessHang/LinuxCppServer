#ifndef Channel_hpp
#define Channel_hpp
#include <sys/epoll.h>
#include <functional>
class EventLoop;

class Channel 
{
public:
    Channel(EventLoop* loop, int fd);
    ~Channel();  
    
    void enableReading();
    void handleEvent();
    int getFd();
    uint32_t getEvents();
    uint32_t getRevents();
    bool getInEpoll();
    void setInEpoll();
    void setRevents(uint32_t event);
    void setCallback(std::function<void()> cb);
   
private:
    EventLoop* m_loop;
    int m_fd;
    uint32_t m_events;
    uint32_t m_revents;
    bool m_inEpoll;
    std::function<void()> m_Callback;
};

#endif // Channel_hpp 

