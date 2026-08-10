# 8_TCP 逻辑总结

## 一、类与类的包含关系

这是整个项目的核心架构，理解这个图就理解了一切：

```
main.cpp
  ├── EventLoop* loop          （事件循环：发动机）
  │     └── Epoll* m_epoll     （事件分发：管理所有fd）
  │           └── epoll里注册了多个Channel*（通过data.ptr存储）
  │
  └── Server* server           （业务管理：只管Acceptor和Connection的生死）
        ├── Acceptor* m_acceptor          （监听：站岗放哨）
        │     ├── Socket* m_sock          （监听fd）
        │     ├── InetAddress* m_addr     （服务端地址）
        │     └── Channel* m_acceptChannel（监听Channel，回调=acceptConnection）
        │
        └── map<int, Connection*> m_connections  （所有客户端连接）
              ├── fd1 → Connection*
              │         ├── Socket* m_sock        （客户端fd）
              │         ├── Channel* m_channel     （客户端Channel，回调=handleReadEvent）
              │         └── m_deleteConnectionCallback（回调=deleteConnection）
              ├── fd2 → Connection*（第二个客户端）
              └── ...（成千上万个）
```

### 每个类的职责

| 类 | 职责 | 存了什么 |
|----|------|---------|
| `EventLoop` | 死循环等事件，分发给Channel处理 | Epoll* |
| `Epoll` | 管理所有fd的注册/修改/等待 | epoll_fd + events数组 |
| `Channel` | 一个fd的事件抽象：fd + events + 回调 | fd, events, revents, inEpoll, callback |
| `Socket` | fd的RAII封装：构造创建fd，析构close fd | m_sockfd |
| `InetAddress` | 地址封装：IP + Port | sockaddr_in |
| `Acceptor` | 监听Socket的初始化 + bind + listen + 通知Server有新连接 | Socket*, InetAddress*, Channel*, callback |
| `Connection` | 一个客户端连接的完整封装：Socket + Channel + 读写 | Socket*, Channel*, callback |
| `Server` | 只管Acceptor和Connection的创建与删除 | EventLoop*, Acceptor*, map<fd, Connection*> |

### 每个类的生命周期管理

```
Server 拥有并管理：
  ├── Acceptor*      → delete in ~Server()
  └── Connection*    → delete in deleteConnection() 或 ~Server()

Acceptor 拥有并管理：
  ├── Socket*        → delete in ~Acceptor()
  ├── InetAddress*   → delete in ~Acceptor()
  └── Channel*       → delete in ~Acceptor()

Connection 拥有并管理：
  ├── Socket*        → delete in ~Connection()
  └── Channel*       → delete in ~Connection()
```

**规则**：谁 new 的谁 delete。Server new 了 Acceptor 和 Connection，所以 Server 负责删它们。Acceptor new 了自己的 Socket/Channel，所以 Acceptor 析构时删它们。Connection 同理。

---

## 二、启动流程（服务端监听建立）

### 第 1 步：main.cpp

```cpp
EventLoop* loop = new EventLoop();   // 创建事件循环，内部 new Epoll()
Server* server = new Server(loop);   // 创建服务器，传入loop
loop->loop();                        // 进入死循环，等待事件
```

### 第 2 步：EventLoop 构造

```cpp
EventLoop::EventLoop() {
    m_epoll = new Epoll();    // 创建Epoll，内部 epoll_create1()
}
```

### 第 3 步：Server 构造

```cpp
Server::Server(EventLoop *loop) : m_loop(loop), m_acceptor(nullptr) {
    m_acceptor = new Acceptor(loop);    // 创建Acceptor（进入Acceptor构造）
    // 存回调②：告诉Acceptor"有新连接时调用我的newConnection"
    std::function<void(Socket*)> callback =
        std::bind(&Server::newConnection, this, std::placeholders::_1);
    m_acceptor->setNewConnectionCallback(callback);
}
```

### 第 4 步：Acceptor 构造（完成服务端监听的所有初始化）

```cpp
Acceptor::Acceptor(EventLoop* loop) {
    m_sock = new Socket();                          // 1. 创建监听fd
    m_addr = new InetAddress("192.168.48.128", 8888); // 2. 设置地址
    m_sock->bind(*m_addr);                          // 3. 绑定
    m_sock->listen();                               // 4. 监听
    m_sock->setNonBlocking();                       // 5. 设非阻塞（ET模式必须）

    m_acceptChannel = new Channel(m_loop, m_sock->getSockfd()); // 6. 创建监听Channel
    m_acceptChannel->setCallback(std::bind(&Acceptor::acceptConnection, this)); // 7. 存回调①
    m_acceptChannel->enableReading();               // 8. 注册到epoll
    //  → Channel::enableReading()
    //    → m_events = EPOLLIN | EPOLLET
    //    → m_loop->updateChannel(this)
    //      → m_epoll->updateChannel(this)
    //        → epoll_ctl(ADD, listen_fd)  ← 监听fd加入epoll
}
```

**启动完成后的内存状态**：

```
epoll里只有1个fd：监听fd
Channel(监听fd).m_Callback = acceptConnection        ← 回调①
Acceptor.m_newConnectionCallback = newConnection     ← 回调②（第3步存的）
```

然后 `loop->loop()` 开始死循环：

```cpp
void EventLoop::loop() {
    while (!quit) {
        vector<Channel*> active = m_epoll->poll();   // epoll_wait 阻塞等事件
        for (Channel* ch : active) {
            ch->handleEvent();                        // 调用Channel存的回调
        }
    }
}
```

---

## 三、客户端连接流程（四条回调链）

### 回调链全景

```
链① Channel.m_Callback = acceptConnection          （监听fd有事件时）
链② Acceptor.m_newConnectionCallback = newConnection（acceptConnection内部转调）
链③ Channel.m_Callback = handleReadEvent            （客户端fd有事件时）
链④ Connection.m_deleteConnectionCallback = deleteConnection（客户端断开时转调）
```

### 新客户端连接（链①②）

```
客户端 connect()
  ↓
内核：监听fd可读
  ↓
epoll_wait 返回监听Channel
  ↓
Channel::handleEvent() → m_Callback()
  → 链①：Acceptor::acceptConnection()
    → 里面只有一行：m_newConnectionCallback(m_sock)
      → 链②：Server::newConnection(监听Socket)
```

### newConnection 逐行解析

```cpp
void Server::newConnection(Socket *serverSocket) {
    // 1. accept：用监听Socket接受连接，得到客户端fd
    InetAddress clientAddr;
    Socket* clientSocket = new Socket(serverSocket->accept(clientAddr));

    // 2. accept失败就退出
    if(clientSocket->getSockfd() == -1) {
        delete clientSocket;
        return;
    }

    // 3. 客户端fd设非阻塞
    clientSocket->setNonBlocking();

    // 4. 创建Connection（内部自动创建Channel + 注册到epoll）
    Connection* connection = new Connection(m_loop, clientSocket);

    // 5. 存回调④：告诉Connection"客户端断开时调用我的deleteConnection"
    std::function<void(Socket*)> callback =
        std::bind(&Server::deleteConnection, this, std::placeholders::_1);
    connection->setDeleteConnectionCallback(callback);

    // 6. 存入map（key=客户端fd，value=Connection指针）
    m_connections[clientSocket->getSockfd()] = connection;
}
```

### Connection 构造（第4步展开）

```cpp
Connection::Connection(EventLoop* loop, Socket* socket) {
    m_channel = new Channel(loop, socket->getSockfd());      // 创建客户端Channel
    m_channel->setCallback(std::bind(&Connection::handleReadEvent, this, socket->getSockfd())); // 存回调③
    m_channel->enableReading();                                // 注册到epoll
    //  → epoll_ctl(ADD, client_fd)  ← 客户端fd加入epoll
}
```

**newConnection 执行完后**：epoll 里现在有 2 个 fd（监听fd + 客户端fd），各有自己的 Channel 和回调。

### 客户端发数据（链③）

```
客户端 write("hello")
  ↓
内核：客户端fd可读
  ↓
epoll_wait 返回客户端Channel
  ↓
Channel::handleEvent() → m_Callback()
  → 链③：Connection::handleReadEvent(fd)
    → read() → 打印 "Received: hello"
    → read() → EAGAIN → break（ET模式要读完）
```

**注意**：这一步不经过 Server，Connection 自己处理读写。

### 客户端断开（链③④）

```
客户端 close()
  ↓
内核：客户端fd可读（read返回0）
  ↓
epoll_wait 返回客户端Channel
  ↓
Channel::handleEvent() → m_Callback()
  → 链③：Connection::handleReadEvent(fd)
    → read() 返回 0 → 客户端断开
      → 链④：Server::deleteConnection(socket)
        → m_connections.erase(fd)        // 从map移除
        → delete connection               // 释放内存
          → ~Connection() → delete m_sock  → close(fd)
          → ~Connection() → delete m_channel
```

### 四条链对比

| 链 | 存储位置 | 触发时机 | 调用谁 |
|----|---------|---------|--------|
| ① | Channel.m_Callback | 监听fd可读 | Acceptor::acceptConnection() |
| ② | Acceptor.m_newConnectionCallback | acceptConnection内部 | Server::newConnection() |
| ③ | Channel.m_Callback | 客户端fd可读 | Connection::handleReadEvent() |
| ④ | Connection.m_deleteConnectionCallback | read返回0 | Server::deleteConnection() |

**链①③都是Channel.m_Callback，怎么区分？** 它们是不同Channel对象的回调：
- Acceptor的Channel → 回调① = acceptConnection
- Connection的Channel → 回调③ = handleReadEvent

---

## 四、完整生命周期（一个连接从生到死）

```
出生：客户端 connect
  1. epoll_wait → 监听Channel → 链① acceptConnection → 链② newConnection
  2. accept() → 得到 client_fd
  3. new Connection → 内部 new Channel + 存回调③ + enableReading(注册到epoll)
  4. 存回调④ deleteConnection
  5. map[client_fd] = connection

工作：客户端发数据
  1. epoll_wait → 客户端Channel → 链③ handleReadEvent
  2. read() → 打印数据 → EAGAIN → break

死亡：客户端 close
  1. epoll_wait → 客户端Channel → 链③ handleReadEvent
  2. read() 返回 0
  3. 链④ deleteConnection
  4. map.erase(fd) + delete Connection → delete Socket(close fd) + delete Channel
```

---

## 五、设计理念

### 为什么这样分层

```
EventLoop + Epoll + Channel  = 可复用的Reactor框架（不知道业务）
Acceptor + Connection        = 业务模块（知道具体做什么）
Server                       = 管理者（管Acceptor和Connection的生死）
```

**以后要改成HTTP服务器**：只需把 Connection 里的 `handleReadEvent` 从 echo 改成 HTTP 解析，其他类不用动。

### 每个服务端和客户端都会做的事

每个 Socket（不管是监听的还是客户端的）都会：
1. `new Socket` → 创建fd
2. `new Channel(loop, fd)` → 把fd包装成Channel
3. `setCallback(...)` → 设置事件回调
4. `enableReading()` → 注册到epoll

**监听Socket的回调 = acceptConnection，客户端Socket的回调 = handleReadEvent**。

---

## 六、代码问题修复

### 已修复的问题

| 文件 | 问题 | 修复 |
|------|------|------|
| Connection.cpp | 构造函数未初始化 m_deleteConnectionCallback | 初始化列表加 nullptr |
| Connection.cpp | 调用空回调可能崩溃 | 加 if 判空 |
| Server.cpp | newConnection 没有 accept | 加 serverSocket->accept() |
| Server.cpp | 直接把监听Socket传给Connection | 改为传 accept 后的 clientSocket |
| Server.cpp | 没有 setNonBlocking | 加 clientSocket->setNonBlocking() |
| Server.cpp | map key 用监听fd（所有连接相同） | 改为用 clientSocket->getSockfd() |
| Server.cpp | deleteConnection 没有安全检查 | 加 find() 判断 |
| Server.cpp | 析构函数不清理 connections | 加遍历 delete 所有 Connection |
| Server.cpp | accept 失败不处理 | 加 if(fd==-1) delete + return |
| Server.cpp | 缺少 #include "InetAddress.hpp" | 补上 |
| **Epoll.hpp/cpp** | **add() 是死代码，无人调用** | **删除** |
| **Epoll.cpp** | **updateChannel 的 ADD 分支漏调 setInEpoll()** | **补上 channel->setInEpoll()** |

### Epoll::add 删除说明

`add()` 是 4_WrapperClass 阶段的旧接口，使用 `ev.data.fd` 存 fd。从 5_Channel 阶段引入 Channel 后，改为 `updateChannel()` 使用 `ev.data.ptr` 存 Channel 指针。`add()` 从未被调用，属于历史遗留死代码。

### updateChannel 的 bug 说明

```cpp
// 修复前
else {
    epoll_ctl(m_epoll_fd, EPOLL_CTL_ADD, fd, &ev);  // ADD成功
    // ← 漏了 channel->setInEpoll()！
}
// 下次再调 updateChannel 时，getInEpoll() 返回 false
// 会再次走 ADD 分支，epoll_ctl 报错 "File exists"

// 修复后
else {
    epoll_ctl(m_epoll_fd, EPOLL_CTL_ADD, fd, &ev);
    channel->setInEpoll();  // ← 标记已注册
}
```

---

## 七、7→8 演进总结

| 阶段 | 核心变化 |
|------|---------|
| 1_SimpleSocket | 最简socket：accept一个客户端就退出 |
| 2_EchoAndUtil | 加read/write循环 + errif错误处理 |
| 3_Epoll | 引入epoll多路复用，支持多客户端 |
| 4_WrapperClass | 封装Socket/InetAddress/Epoll类 |
| 5_Channel | 引入Channel抽象：fd+events+revents绑定 |
| 6_Server | 引入EventLoop+Server+回调，Reactor成型 |
| 7_Acceptor | 拆出Acceptor，监听逻辑独立 |
| **8_TCP** | **拆出Connection，连接逻辑独立，Server只管生死** |

> 到此，单线程 Reactor 的核心模块全部成型。后续加线程池就能变成多线程 Reactor。
