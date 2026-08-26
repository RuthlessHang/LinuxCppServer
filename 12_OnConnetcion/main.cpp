#include "src/EventLoop.hpp"
#include "src/Server.hpp"
#include "src/Connection.hpp"
#include "src/Socket.hpp"
#include <iostream>

int main()
{
    EventLoop* loop = new EventLoop();
    Server* server = new Server(loop);

    // 用户注册业务回调：echo 逻辑
    server->OnConnect([](Connection* conn) {
        conn->Read();                                        // 从 socket 读到 read_buffer
        if(conn->GetState() == Connection::State::Closed) {  // 客户端断开
            conn->Close();
            return;
        }
        conn->SetSendBuffer(conn->ReadBuffer());              // 把读到的内容设为要发送的内容
        conn->Write();                                        // 发送回客户端
        std::cout << "Message from client " << conn->GetSocket()->getSockfd()
                  << ": " << conn->ReadBuffer() << std::endl;
    });

    loop->loop();   // 开始事件循环
    delete server;
    delete loop;
    return 0;
}
