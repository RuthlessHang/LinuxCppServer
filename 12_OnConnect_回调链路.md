# 12_OnConnect — 业务回调链路全解析

## 核心理念

整个项目就是 **「epoll 等 + 调回调」的循环**：

- 程序启动时，**提前注册好所有回调**
- 运行时，epoll 检测到事件就**一层一层调回调**
- 业务逻辑写在**最底层的回调**里

```
┌─────────────────────────────────────────────────┐
│  while(true) {                                    │
│      events = epoll_wait();   // 等事件            │
│      for(event : events) {                       │
│          channel.handleEvent();   // 调回调         │
│      }                                            │
│  }                                                │
└─────────────────────────────────────────────────┘
```

---

## 回调注册时机（全在程序启动 / 新连接时）

### 阶段 1：程序启动时注册

| # | 注册方 | 注册给谁 | 触发时机 |
|---|--------|---------|---------|
| 1 | 用户 main.cpp | `Server::OnConnect(lambda)` | 程序启动时立即存到 `Server::m_onConnectCallback` |
| 2 | Server 构造 | `Acceptor::setNewConnectionCallback` | Server 构造时注册 |
| 3 | Server 构造 | `Channel::setReadCallback`（Acceptor 内部）| Acceptor 构造时注册 |
| 4 | Server 构造 | `ThreadPool::add(subReactor->loop)` | 启动 N 个子线程跑 subReactor |

### 阶段 2：新连接到达时注册

| # | 注册方 | 注册给谁 | 触发时机 |
|---|--------|---------|---------|
| 5 | Server::newConnection | `Connection::setDeleteConnectionCallback` | 建 Connection 后立即注册 |
| 6 | Server::newConnection | `Connection::setOnConnectedCallback` | 把 Server 存的业务回调传给 Connection |
| 7 | Connection::setOnConnectedCallback | `Channel::setReadCallback`（Connection 内部）| 包装成 lambda 注册给 Channel |

---

## 完整回调链路（从注册到触发）

### 阶段 A：程序启动（main 线程）

```
[main.cpp]
  EventLoop* loop = new EventLoop();                    // 建 mainReactor
  Server* server = new Server(loop);                    // 见阶段 B
  server->OnConnect([](Connection* conn){ ... });      // ★ 1. 用户注册业务回调
  loop->loop();                                          // ★ 进入 epoll_wait 死循环
```

**第 1 步详细**：

```cpp
server->OnConnect([](Connection* conn) {
    conn->Read();
    if(conn->GetState() == Connection::State::Closed) {
        conn->Close();
        return;
    }
    conn->SetSendBuffer(conn->ReadBuffer());
    conn->Write();
});
```

`OnConnect` 内部：

```cpp
void Server::OnConnect(std::function<void(Connection*)> cb) {
    m_onConnectCallback = cb;    // 把用户的 lambda 存到 Server 的成员
}
```

此时：

```
Server::m_onConnectCallback = 用户的 echo lambda
```

### 阶段 B：Server 构造（main 线程，阶段 A 的 new Server 时）

```cpp
Server::Server(EventLoop *loop) {
    // ① 建 Acceptor（监听 socket）
    m_acceptor = new Acceptor(m_mainReactor);
    // ★ 2. 注册 Acceptor 的回调给 Server::newConnection
    m_acceptor->setNewConnectionCallback(
        std::bind(&Server::newConnection, this, std::placeholders::_1)
    );

    // ② 建线程池 + subReactors
    int size = std::thread::hardware_concurrency();
    m_thPool = new ThreadPool(size);
    for(int i = 0; i < size; ++i) {
        m_subReactors.emplace_back(new EventLoop());
    }

    // ★ 3. 把每个 subReactor 的 loop 丢进线程池
    for(int i = 0; i < size; ++i) {
        std::function<void()> task = std::bind(&EventLoop::loop, m_subReactors[i]);
        m_thPool->add(task);    // 子线程开始跑 epoll_wait 死循环
    }
}
```

**Acceptor 构造内部**（第 2 步的延伸）：

```cpp
Acceptor::Acceptor(EventLoop* loop) {
    m_sock = new Socket();
    m_addr = new InetAddress("127.0.0.1", 9999);
    m_sock->bind(m_addr);
    m_sock->listen();
    m_sock->setNonBlocking();
    m_acceptChannel = new Channel(m_loop, m_sock->getSockfd());
    // ★ 4. 注册 Acceptor 的回调给 Channel
    m_acceptChannel->setReadCallback(std::bind(&Acceptor::acceptConnection, this));
    m_acceptChannel->enableRead();
    m_acceptChannel->useET();
}
```

此时：

```
Acceptor::m_newConnectionCallback = Server::newConnection
Acceptor::m_acceptChannel::m_readCallback = Acceptor::acceptConnection
N 个子线程正在跑 subReactor[i]->loop() → epoll_wait 阻塞
```

### 阶段 C：新连接到达（main 线程 epoll_wait 醒来）

```
[mainReactor epoll_wait 醒来，监听 fd 可读]
  ↓
EventLoop::loop → 拿到 activeChannels
  ↓
Channel::handleEvent
  ↓ m_revents & EPOLLIN → 调 m_readCallback
  ↓
Acceptor::acceptConnection
  ↓
  m_newConnectionCallback(m_sock)
  ↓
Server::newConnection(serverSocket)
```

### 阶段 D：newConnection 注册业务回调给 Connection

```cpp
void Server::newConnection(Socket *serverSocket) {
    InetAddress* clientAddr = new InetAddress();
    Socket* clientSocket = new Socket(serverSocket->accept(clientAddr));
    // ...
    clientSocket->setNonBlocking();
    int random = clientSocket->getSockfd() % m_subReactors.size();
    
    // 建 Connection，分给某个 subReactor
    Connection* connection = new Connection(m_subReactors[random], clientSocket);
    
    // ★ 5. 注册断开回调
    connection->setDeleteConnectionCallback(
        std::bind(&Server::deleteConnection, this, std::placeholders::_1)
    );
    
    // ★ 6. 注册业务回调（把阶段 A 存的 lambda 传过去）
    connection->setOnConnectedCallback(m_onConnectCallback);
    
    m_connections[clientSocket->getSockfd()] = connection;
}
```

**Connection 构造内部**：

```cpp
Connection::Connection(EventLoop* loop, Socket* socket) {
    if(loop != nullptr) {
        m_channel = new Channel(loop, socket->getSockfd());
        m_channel->enableRead();
        m_channel->useET();
        // 注意：此时 m_channel->m_readCallback 还没设置！
    }
    m_read_buffer = new Buffer();
    m_write_buffer = new Buffer();
    m_state = Connected;
}
```

**setOnConnectedCallback 内部**（第 6 步的延伸）：

```cpp
void Connection::setOnConnectedCallback(std::function<void(Connection*)> cb) {
    m_onConnectedCallback = cb;    // 存起来
    // ★ 7. 包装成 lambda 注册给 Channel
    m_channel->setReadCallback(
        [this]() { m_onConnectedCallback(this); }    // 签名转换：void() → void(Connection*)
    );
}
```

**为什么要包装**：签名不匹配

| 对象 | 期望的签名 |
|------|----------|
| Channel 的 readCallback | `void()` 无参 |
| 用户的业务回调 | `void(Connection*)` 要传 Connection* |

lambda 捕获 `this`，调时把 `this` 传给用户回调。

此时：

```
Connection::m_onConnectedCallback = 用户的 echo lambda
Connection::m_channel::m_readCallback = [this]{ m_onConnectedCallback(this); }
新 fd 已加入 subReactor[random] 的 Epoll
```

### 阶段 E：客户端数据到达（subReactor 子线程 epoll_wait 醒来）

```
[subReactor[N] 的 epoll_wait 醒来，客户端 fd 可读]
  ↓
EventLoop::loop → 拿到 activeChannels
  ↓
Channel::handleEvent
  ↓ m_revents & EPOLLIN → 调 m_readCallback
  ↓
Connection::setOnConnectedCallback 里注册的 lambda
  ↓
  m_onConnectedCallback(this)
  ↓
  用户的 echo lambda（在 main.cpp 里写的）
  ↓
  conn->Read()       → ReadNonBlocking → read 到 read_buffer
  conn->SetSendBuffer(conn->ReadBuffer())   → 拷到 write_buffer
  conn->Write()      → WriteNonBlocking → write 回客户端
```

**用户的业务逻辑在 subReactor 的子线程里执行**，不是主线程。

---

## 回调链路总图

```
┌─────────────────────────────────────────────────────────────────┐
│ 程序启动                                                         │
│                                                                  │
│ [main.cpp]                                                       │
│   server->OnConnect(用户 lambda)                                  │
│        ↓ 存到                                                    │
│   Server::m_onConnectCallback                                    │
│                                                                  │
│ [Server 构造]                                                    │
│   Acceptor::setNewConnectionCallback(Server::newConnection)     │
│   Acceptor 内部：                                                │
│     Channel::setReadCallback(Acceptor::acceptConnection)        │
│                                                                  │
│   thPool->add(subReactor[i]->loop)  ← N 个子线程跑 epoll_wait    │
│                                                                  │
│ [main.cpp]                                                       │
│   loop->loop()  ← 主线程跑 epoll_wait                            │
└─────────────────────────────────────────────────────────────────┘
                              ↓
            新连接到达（主线程 epoll_wait 醒来）
                              ↓
┌─────────────────────────────────────────────────────────────────┐
│ [Channel::handleEvent] → m_readCallback                          │
│        ↓                                                         │
│ [Acceptor::acceptConnection] → m_newConnectionCallback          │
│        ↓                                                         │
│ [Server::newConnection]                                          │
│   new Connection(subReactor, clientSocket)                       │
│   connection->setOnConnectedCallback(m_onConnectCallback)        │
│        ↓ 内部                                                   │
│   Connection::m_onConnectedCallback = 用户 lambda                 │
│   Channel::setReadCallback([this]{ m_onConnectedCallback(this) })│
└─────────────────────────────────────────────────────────────────┘
                              ↓
            客户端数据到达（子线程 epoll_wait 醒来）
                              ↓
┌─────────────────────────────────────────────────────────────────┐
│ [Channel::handleEvent] → m_readCallback                          │
│        ↓                                                         │
│ [lambda: m_onConnectedCallback(this)]                           │
│        ↓                                                         │
│ [用户的 echo lambda]                                             │
│   conn->Read()                                                  │
│   conn->SetSendBuffer(conn->ReadBuffer())                        │
│   conn->Write()                                                 │
└─────────────────────────────────────────────────────────────────┘
```

---

## 各线程在干什么

### 主线程（mainReactor）

```
while(true) {
    events = epoll_wait();          // 等新连接
    for(event : events) {
        channel.handleEvent();       // → Acceptor → Server::newConnection
    }
}
```

### N 个子线程（subReactor）

```
while(true) {
    events = epoll_wait();          // 等客户端数据
    for(event : events) {
        channel.handleEvent();       // → 用户的业务 lambda
    }
}
```

**所有线程都在 `epoll_wait` 阻塞等事件**，有事件就调回调，没事件就睡。

---

## 回调链路的 3 层包装

### 第 1 层：Server::OnConnect → Server::m_onConnectCallback

```cpp
server->OnConnect([](Connection* conn) { ... });
↓
Server::m_onConnectCallback = lambda;
```

**用户把业务逻辑存到 Server**。

### 第 2 层：Server::newConnection → Connection::setOnConnectedCallback

```cpp
connection->setOnConnectedCallback(m_onConnectCallback);
↓
Connection::m_onConnectedCallback = m_onConnectCallback;
```

**Server 把业务逻辑传给 Connection**。

### 第 3 层：Connection::setOnConnectedCallback → Channel::setReadCallback

```cpp
m_channel->setReadCallback([this](){ m_onConnectedCallback(this); });
```

**Connection 把业务逻辑包装成无参 lambda 注册给 Channel**。

### 触发时：Channel::handleEvent → 一层一层调

```
Channel::handleEvent
  → m_readCallback()
  → [this]{ m_onConnectedCallback(this); }
  → m_onConnectedCallback(this)
  → 用户的 echo lambda(this)
```

---

## 关键设计点

### 1. 为什么用户回调不直接注册给 Channel

**签名不匹配**：

| 对象 | 期望签名 |
|------|---------|
| Channel | `void()` 无参 |
| 用户业务 | `void(Connection*)` 要传 Connection* |

所以 Connection 用 lambda 包装一层：`[this]{ cb(this); }`

### 2. 为什么不直接调用户业务，要走 3 层

**解耦**：

```
用户  ──注册──→  Server  ──传──→  Connection  ──注册──→  Channel
                                                      ↓
用户业务  ←──调──  Connection  ←──调──  Channel  ←──epoll触发
```

- 用户不用关心 epoll，只管业务
- Connection 不用关心业务，只管 I/O
- Channel 不用关心 Connection，只管 fd 事件分发

### 3. 为什么 Connection 构造时不立即注册业务回调

因为业务回调来自 Server，Connection 构造时还没收到。必须等 Server::newConnection 调 `setOnConnectedCallback` 才注册。

**Connection 构造时 m_channel->m_readCallback 是 nullptr**，直到 `setOnConnectedCallback` 被调才设置。

### 4. 为什么 Channel::handleEvent 要判断 `m_revents`

```cpp
void Channel::handleEvent() {
    if(m_revents & (EPOLLIN | EPOLLPRI)) {
        if(m_readCallback) m_readCallback();
    }
    if(m_revents & EPOLLOUT) {
        if(m_writeCallback) m_writeCallback();
    }
}
```

**一个 fd 可能同时发生多种事件**（可读 + 可写），要根据实际事件分发到不同回调。

---

## 业务与网络库的分工

| 谁 | 干什么 | 知不知道业务 |
|----|--------|------------|
| **Channel** | 监听 fd 事件，分发到回调 | ❌ 完全不知道 |
| **Connection** | 管 socket 读写 + 缓冲区 | ❌ 只管 I/O |
| **Server** | 管理连接 + 传递业务回调 | ❌ 只管调度 |
| **用户 lambda** | echo / HTTP / FTP 等业务逻辑 | ✅ 业务在这里 |

**换业务只改用户 lambda**，网络库一行不动：

```cpp
// echo 服务器
server->OnConnect([](Connection* conn) {
    conn->Read();
    conn->SetSendBuffer(conn->ReadBuffer());
    conn->Write();
});

// HTTP 服务器
server->OnConnect([](Connection* conn) {
    conn->Read();
    std::string req = conn->ReadBuffer();
    std::string resp = "HTTP/1.1 200 OK\r\n\r\nhello";
    conn->SetSendBuffer(resp.c_str());
    conn->Write();
});
```

---

## 一句话总结

> **项目本质是「epoll 等 + 调回调」的循环**。启动时用户用 `OnConnect` 注册业务 lambda → Server 存起来 → 新连接到达时传给 Connection → Connection 包装成无参 lambda 注册给 Channel → 客户端数据到达时 epoll 触发 Channel → 调到用户的业务 lambda。整个链路是「提前注册 + 事件触发」的事件驱动架构。
