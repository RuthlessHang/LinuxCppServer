#pragma once
#include <functional>
class EventLoop;
class Socket;
class Channel;
class Buffer;

class Connection
{
public:
    enum State
    {
        Invalid = 1,
        Handshaking,
        Connected,
        Closed,
        Failed,
    };

    Connection(EventLoop* loop, Socket* socket);
    ~Connection();

      // 回调注册
    void setDeleteConnectionCallback(std::function<void(Socket* socket)> cb);
    void setOnConnectedCallback(std::function<void(Connection* conn)> cb);

     // 状态
    State GetState();

     // 拿 socket
    Socket* GetSocket();
    // 关闭
    void Close();
    // 读写操作（业务方调）
    void Read();
    void Write();

    // 缓冲区操作
    void SetSendBuffer(const char* str);
    Buffer* GetReadBuffer();
    const char* ReadBuffer();
    Buffer* GetWriteBuffer();
    const char* WriteBuffer();
    void GetlineSendBuffer();
private:
    EventLoop* m_loop;
    Socket* m_sock;
    Channel* m_channel;
    Buffer* m_read_buffer;
    Buffer* m_write_buffer;
    State m_state;

    std::function<void(Socket* socket)> m_deleteConnectionCallback;
    std::function<void(Connection* conn)> m_onConnectedCallback;

    void ReadNonBlocking();
    void ReadBlocking();
    void WriteNonBlocking();
    void WriteBlocking();
};