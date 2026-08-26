#include "src/Socket.hpp"
#include "src/InetAddress.hpp"
#include "src/Connection.hpp"
#include <iostream>

int main()
{
    Socket* sock = new Socket();
    InetAddress* serverAddr = new InetAddress("192.168.48.128", 8888);
    sock->connect(serverAddr);
    delete serverAddr;

    // 客户端用裸 Connection：loop 传 nullptr，不建 Channel，走阻塞 I/O
    Connection* conn = new Connection(nullptr, sock);

    while(true) {
        conn->GetlineSendBuffer();                            // 从 stdin 读一行到 write_buffer
        conn->Write();                                         // 发送给服务器
        if(conn->GetState() == Connection::State::Closed) {   // 服务器断开
            conn->Close();
            break;
        }
        conn->Read();                                          // 读服务器回显
        std::cout << "Message from server: " << conn->ReadBuffer() << std::endl;
    }

    delete conn;
    return 0;
}
