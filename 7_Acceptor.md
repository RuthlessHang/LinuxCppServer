# 7_Acceptor — 监听职责分离

## 这版解决什么问题

把「监听职责」从 Server 中抽出，封装成独立的 `Acceptor` 类。Server 彻底聚焦业务。

## 引入 Acceptor 类

Acceptor = 监听器。只关心一件事：**监听端口，新连接到了就调 Server 注册的回调**。

```cpp
class Acceptor {
public:
    Acceptor(EventLoop* loop);
    void acceptConnection();                              // 监听 fd 有事件时调
    void setNewConnectionCallback(std::function<void(Socket*)> cb);  // 注册回调
private:
    Socket* m_Socket;                                     // 监听 socket
    Channel* m_acceptChannel;                             // 监听 fd 的 Channel
    EventLoop* m_loop;
    std::function<void(Socket*)> m_newConnectionCallback; // 新连接时回调谁
};
```

## Server 构造函数对比

**第 6 版**（7 行监听代码在 Server 里）：
```cpp
Server::Server(EventLoop *loop):m_loop(loop) {
    Socket* serverSocket = new Socket();
    InetAddress serverAddr = InetAddress("127.0.0.1", 8080);
    serverSocket->bind(serverAddr);
    serverSocket->listen();
    serverSocket->setNonBlocking();
    Channel* serverChannel = new Channel(m_loop, serverSocket->getSockfd());
    std::function<void()> callback = std::bind(&Server::newConnection, this, serverSocket);
    serverChannel->setCallback(callback);
    serverChannel->enableReading();
}
```

**第 7 版**（只剩 3 行，监听细节交给 Acceptor）：
```cpp
Server::Server(EventLoop *loop):m_loop(loop), m_acceptor(nullptr) {
    m_acceptor = new Acceptor(loop);
    std::function<void(Socket*)> callback = std::bind(&Server::newConnection, this, std::placeholders::_1);
    m_acceptor->setNewConnectionCallback(callback);
}
```

## 三层回调调用链

```
[监听 fd 可读]
  → Channel::handleEvent
  → Acceptor::acceptConnection（Channel 回调，Acceptor 构造时注册）  ← 第 1 层
  → Server::newConnection（Server 在 Acceptor 构造后注册的回调）   ← 第 2 层
```

## 回调注册关系

| 注册方 | 注册给谁 | 回调函数 | 触发时机 |
|--------|---------|---------|---------|
| Acceptor 构造 | Acceptor 内部的 Channel | Acceptor::acceptConnection | 监听 fd 可读 |
| Server 构造 | Acceptor | Server::newConnection | 新连接到来 |

## 为什么用 `std::placeholders::_1`

Server 注册给 Acceptor 的回调签名是 `void(Socket*)`——因为 Acceptor 持有监听 Socket，调起时把自己的 Socket 传过去。注册时还不知道哪个 Socket，用占位符 `_1` 表示「调用时由 Acceptor 传进来」。

## 与上一版的差异

| | 6_Server | 7_Acceptor |
|---|----------|-----------|
| 监听代码位置 | Server 构造里 | Acceptor 构造里 |
| Server 职责 | 监听 + 业务 | 只管业务 |
| 新增类 | — | Acceptor |
| 回调层数 | 2 层 | 3 层（多了 Acceptor 中间层） |

## 这版的限制

- 客户端断开时没清理 Connection
- Server 里没有 `map<fd,Connection*>` 管理多连接
- 没有专门的 Connection 类

## 与下一版的衔接

第 8 版将引入 `Connection` 类，用 map 管理所有客户端连接，并实现断开回调清理机制。
