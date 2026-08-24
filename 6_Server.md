# 6_Server — 事件循环 + 回调

## 这版解决什么问题

引入 EventLoop（事件循环骨架）和 Server（业务入口），Channel 获得**回调能力**。**main 函数从此只负责启动，不再处理业务**。

## main 函数对比

**第 5 版**（100+ 行堆在 main 里）：
```cpp
int main() {
    Socket* serverSocket = new Socket();
    InetAddress serverAddr = InetAddress("127.0.0.1", 8080);
    serverSocket->bind(serverAddr);
    serverSocket->listen();
    serverSocket->setNonBlocking();
    Epoll* epoll = new Epoll();
    epoll->add(serverSocket->GetSockfd(), EPOLLIN | EPOLLET);
    // ... 100 多行的 while 循环处理 ...
}
```

**第 6 版**（3 行）：
```cpp
int main() {
    EventLoop* loop = new EventLoop();
    Server* server = new Server(loop);
    loop->loop();
}
```

## 引入 EventLoop

EventLoop = Epoll + while 循环的封装。

```cpp
class EventLoop {
public:
    void loop();                        // while + epoll_wait + for handleEvent
    void updateChannel(Channel* ch);    // 注册 Channel
private:
    Epoll* m_epoll;
    bool quit;
};
```

`loop()` 的核心实现：
```cpp
void EventLoop::loop() {
    while (!quit) {
        std::vector<Channel*> activeChannel = m_epoll->poll();
        for (Channel* ch : activeChannel) {
            ch->handleEvent();    // ← 关键：调 Channel 的 handleEvent
        }
    }
}
```

**EventLoop 完全不知道业务是什么**——不知道这是监听 fd 还是客户端 fd，不知道该怎么处理。它只是机械地「有事件就调对应 Channel 的 handleEvent()」。

## Channel 获得回调能力

```cpp
class Channel {
public:
    void handleEvent();                              // 触发回调
    void setCallback(std::function<void()> cb);      // 注册回调
private:
    std::function<void()> m_Callback;                // 存回调
};

void Channel::handleEvent() {
    m_Callback();    // 直接调用注册的回调
}
```

## 引入 Server

Server 是业务入口，负责：
1. 建监听 Socket/Channel
2. 给监听 Channel 注册回调（新连接时调 Server::newConnection）
3. `newConnection()` 里 accept + 建客户端 Channel + 注册读写回调
4. `handleReadEvent()` 里读数据 + echo

## 核心调用链

```
[启动]
main → new EventLoop → new Server
  Server 构造:
    建监听 Socket + Channel
    Channel::setCallback(Server::newConnection)   ← 注册回调 1
    Channel::enableReading()                      ← 加入 Epoll

main → loop->loop() → epoll_wait 阻塞

[新连接]
  监听 fd 可读 → epoll_wait 返回监听 Channel
  → Channel::handleEvent
  → 调 Server::newConnection（回调 1）
  → accept + 建客户端 Channel
  → 客户端 Channel::setCallback(Server::handleReadEvent)  ← 注册回调 2

[客户端数据]
  客户端 fd 可读 → epoll_wait 返回客户端 Channel
  → Channel::handleEvent
  → 调 Server::handleReadEvent（回调 2）
  → read + echo
```

## 与上一版的差异

| | 5_Channel | 6_Server |
|---|----------|---------|
| main 代码量 | 100+ 行 | 3 行 |
| Channel 能力 | 无回调 | 带回调，事件来了自动调 |
| 事件分发 | if/else 判断 | 调用注册的回调 |
| 新增类 | — | EventLoop, Server |

## 这版的限制

- Server 既管监听又管业务（职责没分离）
- 客户端断开时只 close fd，没清理 Channel（内存泄漏）
- 没有专门的 Connection 类

## 与下一版的衔接

第 7 版将引入 `Acceptor` 类，把监听职责从 Server 中抽出。
