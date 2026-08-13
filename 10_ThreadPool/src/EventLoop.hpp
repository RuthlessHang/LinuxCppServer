#ifndef EVENT_LOOP_HPP
#define EVENT_LOOP_HPP 
#include <functional>
class Epoll;
class Channel;
class ThreadPool;

class EventLoop
{
public:
    EventLoop();
    ~EventLoop();
    void loop();
    void updateChannel(Channel* channel);
    void addThreadPool(std::function<void()> task);
    
private:    
    Epoll* m_epoll;
    bool quit;
    ThreadPool* m_threadPool;
};

#endif // EVENT_LOOP_HPP    
