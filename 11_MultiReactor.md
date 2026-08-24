# 11_MultiReactor — 主从 Reactor 多线程模式

## 这版解决什么问题

引入**主从 Reactor + one loop per thread**架构，真正实现多线程并发。是整个教程的高潮。

## 核心架构

```
┌─────────────────────────────────────────────────────────┐
│  Server                                                  │
│                                                          │
│  mainReactor (EventLoop)  ← 主线程跑，只管监听新连接      │
│    └─ Acceptor (监听 socket + Channel)                   │
│                                                          │
│  thPool (N 个线程)                                       │
│    └─ 每个线程跑一个 subReactor->loop()                  │
│                                                          │
│  subReactors (N 个 EventLoop)                             │
│    └─ 每个有独立的 Epoll，监听分到的客户端 fd             │
│                                                          │
│  m_connections: map<fd, Connection*>  管理所有客户端连接  │
└─────────────────────────────────────────────────────────┘
```

## 关键成员

```cpp
class Server {
private:
    EventLoop* m_mainReactor;              // 只管接受连接
    Acceptor* m_acceptor;                  // 连接接受器
    std::map<int, Connection*> m_connections;  // TCP 连接
    std::vector<EventLoop*> m_subReactors;    // N 个从 Reactor
    ThreadPool* m_thPool;                  // 线程池
};
```

## 启动流程（构造函数）

```cpp
Server::Server(EventLoop *loop) : m_mainReactor(loop), m_acceptor(nullptr), m_thPool(nullptr) {
    // ① 建 Acceptor（监听 socket + Channel + enableReading）
    m_acceptor = new Acceptor(m_mainReactor);
    std::function<void(Socket*)> callback = std::bind(&Server::newConnection, this, std::placeholders::_1);
    m_acceptor->setNewConnectionCallback(callback);

    // ② 拿 CPU 核数，作为线程数和 subReactor 数
    int size = std::thread::hardware_concurrency();

    // ③ 建线程池（开 N 个线程，全部在 workThread 里待命）
    m_thPool = new ThreadPool(size);

    // ④ 建 N 个 subReactor（每个 EventLoop 有独立 Epoll）
    for(int i = 0; i < size; ++i) {
        m_subReactors.emplace_back(new EventLoop());
    }

    // ⑤ 每个 subReactor 的 loop() 丢进线程池
    //    → N 个线程各跑一个 loop()，各自 epoll_wait 阻塞
    for(int i = 0; i < size; ++i) {
        std::function<void()> task = std::bind(&EventLoop::loop, m_subReactors[i]);
        m_thPool->add(task);
    }
}
```

**关键点**：subReactor 数量 = 线程数量 = CPU 核数。1 对 1 绑定。

## 新连接派发

```cpp
void Server::newConnection(Socket *serverSocket) {
    // accept
    Socket* clientSocket = new Socket(serverSocket->accept(clientAddr));
    if(clientSocket->getSockfd() == -1) { delete clientSocket; return; }
    clientSocket->setNonBlocking();

    // ① 全随机派发：fd % N 选一个 subReactor
    int random = clientSocket->getSockfd() % m_subReactors.size();

    // ② Connection 构造时传入 subReactor（而非 mainReactor）
    Connection* connection = new Connection(m_subReactors[random], clientSocket);

    // ③ 注册断开回调，存进 map
    connection->setDeleteConnectionCallback(...);
    m_connections[fd] = connection;
}
```

Connection 构造里的 `enableReading()` 会把客户端 fd 加进**所选 subReactor** 的 Epoll。

## 完整事件流

```
[新连接]
  mainReactor epoll_wait → Acceptor::acceptConnection → Server::newConnection
  → accept → fd%N → new Connection(subReactor[random], socket)
  → Connection 内部 enableReading → 客户端 fd 加入 subReactor 的 Epoll

[客户端数据]
  客户端 fd 可读
  → 对应 subReactor 的 epoll_wait 醒来
  → 该 subReactor 线程处理 Channel::handleEvent
  → Connection::handleReadEvent → read + echo
  ★ 数据收发在 subReactor 线程里完成，主线程不受影响

[客户端断开]
  read 返回 0 → Connection 调 m_deleteConnectionCallback
  → Server::deleteConnection → map erase + delete Connection
```

## 与第 10 版的差异

| | 10_ThreadPool | 11_MultiReactor |
|---|---------------|-----------------|
| EventLoop 数量 | 1 个 | 1 + N 个 |
| 线程池归谁 | EventLoop 持有 | **Server 持有** |
| 谁管客户端 fd | 唯一 EventLoop | 分发到 N 个 subReactor |
| 谁处理业务 | 主线程 | N 个 subReactor 线程并行 |
| Channel 回调 | 主线程直接调 | subReactor 自己线程内调 |
| 实际并发 | 单线程 | N 个线程真并行 |

## 析构顺序（关键）

```cpp
Server::~Server() {
    delete m_acceptor;       // ① 先删 Acceptor
    delete m_thPool;         // ② 先停线程池（join 等线程退出）
    for(auto& pair : m_connections) delete pair.second;  // ③ 删所有连接
    for(auto& reactor : m_subReactors) delete reactor;    // ④ 最后删 subReactors
}
```

**顺序必须**：thpool → subReactors。先停线程（join 等退出），再删 EventLoop。

## 主从 Reactor 模式总结

> 服务器只有一个 **mainReactor**（主线程）监听新连接，N 个 **subReactor**（N 个子线程）各管一批客户端 fd。新连接按 `fd%N` 随机派发，从此这个 fd 的所有事件都在对应 subReactor 的线程里处理。**one loop per thread**——一个线程一个 EventLoop，一个 EventLoop 管一批 fd。

## 当前版本的不足（后续改进方向）

1. **业务写死 echo**：Connection 里的 echo 逻辑固定，无法自定义
2. **跨线程 map 竞态**：`deleteConnection` 可能被 subReactor 线程调用，与主线程的 `newConnection` 同时操作 map 没加锁
3. **写缓冲区缺失**：同步 write，大数据可能阻塞
4. **调度算法简单**：fd%N 全随机，未考虑负载均衡
5. **无优雅关闭**：直接 close fd，缺少半关闭等机制
