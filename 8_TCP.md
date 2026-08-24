# 8_TCP — 连接管理 + Connection 类

## 这版解决什么问题

把客户端连接的 Socket + Channel + 读写逻辑封装成 Connection 类，Server 用 `map<fd, Connection*>` 统一管理多连接，并实现**断开回调清理机制**。

## 引入 Connection 类

Connection = 一个客户端连接的所有资源打包在一起。

```cpp
class Connection {
public:
    Connection(EventLoop* loop, Socket* socket);
    ~Connection();
    void setDeleteConnectionCallback(std::function<void(Socket*)> cb);
    void handleReadEvent(int sockfd);    // 读事件处理（echo）
private:
    EventLoop* m_loop;
    Socket* m_sock;                                         // 客户端 socket
    Channel* m_channel;                                      // 客户端 Channel
    std::function<void(Socket*)> m_deleteConnectionCallback; // 断开时通知 Server
};
```

## Server 新增 map 管理

```cpp
std::map<int, Connection*> m_connections;   // key = 客户端 fd，value = Connection*
```

## 新连接流程

```cpp
void Server::newConnection(Socket *serverSocket) {
    // accept
    Socket* clientSocket = new Socket(serverSocket->accept(clientAddr));
    if(clientSocket->getSockfd() == -1) { delete clientSocket; return; }
    clientSocket->setNonBlocking();

    // 建 Connection
    Connection* connection = new Connection(m_loop, clientSocket);

    // 注册断开回调
    std::function<void(Socket*)> callback = std::bind(&Server::deleteConnection, this, std::placeholders::_1);
    connection->setDeleteConnectionCallback(callback);

    // 存进 map
    m_connections[clientSocket->getSockfd()] = connection;
}
```

## 断开清理机制

Connection 检测到 read==0（客户端断开）时：
```cpp
void Connection::handleReadEvent(int sockfd) {
    while(true) {
        ssize_t bytes_read = read(sockfd, buffer, sizeof(buffer));
        if(bytes_read == 0) {
            if(m_deleteConnectionCallback) {
                m_deleteConnectionCallback(m_sock);   // 通知 Server 清理
            }
            break;
        }
        // ...
    }
}
```

Server 的 deleteConnection：
```cpp
void Server::deleteConnection(Socket* socket) {
    auto it = m_connections.find(socket->getSockfd());
    if(it != m_connections.end()) {
        Connection* connection = it->second;
        m_connections.erase(it);
        delete connection;   // Connection 析构：delete m_sock + delete m_channel
    }
}
```

## 为什么需要回调清理

Connection 知道自己断开了，但它**不能直接操作 Server 的 map**（耦合）。所以 Connection 通过回调通知 Server「我断开了」，由 Server 来从 map 里删除。这是「下层通知上层」的典型解耦模式。

## 与上一版的差异

| | 7_Acceptor | 8_TCP |
|---|-----------|------|
| 客户端资源 | 散在 Server 里 | 封装进 Connection 类 |
| 多连接管理 | 无 | `map<fd,Connection*>` |
| 断开清理 | 无 | 回调机制正确清理 |
| 新增类 | — | Connection |

## 这版的限制

- Buffer 还是栈上数组 `char buffer[1024]`，大小固定
- 没有真正的 `write`，只是 `cout` 打印
- 数据跨 read 调用无法累积

## 与下一版的衔接

第 9 版将引入 `Buffer` 类替代栈数组，支持数据累积和真正的 write echo。
