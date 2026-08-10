#ifndef EVENT_LOOP_HPP
#define EVENT_LOOP_HPP 
class Epoll;
class Channel;

class EventLoop
{
public:
    EventLoop();
    ~EventLoop();
    void loop();
    void updateChannel(Channel* channel);
    
private:    
    Epoll* m_epoll;
    bool quit;
};

#endif // EVENT_LOOP_HPP    
