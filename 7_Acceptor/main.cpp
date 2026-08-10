#include "src/EventLoop.hpp"
#include "src/Server.hpp"

int main() 
{
    EventLoop* loop = new EventLoop();
    Server* server = new Server(loop);
    loop->loop();
    return 0 ;
}