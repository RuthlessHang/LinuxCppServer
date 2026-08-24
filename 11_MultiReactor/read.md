# day12 主从 Reactor 多线程模式 - 从零看懂整个项目

> 这份文档带你**顺着代码从头 trace 到尾**。打开文件跟着看，看完应该能回答：
> - 每个类是干啥的？
> - 服务器启动后各线程在干什么？
> - 客户端连上后发生了什么？
> - 客户端发数据时是怎么被处理的？
> - 客户端断开怎么清理？
> - 所有「调来调去的回调」到底怎么串起来的？

---

## 一、所有类的角色（一行一个）

| 类 | 一句话 | 持有的关键资源 |
|----|--------|--------------|
| **Socket** | 对 socket fd 的薄封装 | `int m_sockfd` |
| **InetAddress** | 对 sockaddr_in 的薄封装 | IP + port |
| **Epoll** | 对 epoll 系统调用的封装 | `int m_epoll_fd`（内核 epoll 句柄） |
| **Channel** | 「fd + 它的事件回调」的绑定体 | `EventLoop*` + `fd` + `callback` |
| **EventLoop** | 事件循环（一个 Epoll + while 循环） | `Epoll* m_epoll` |
| **Acceptor** | 监听 socket + 「有新连接」的回调 | 监听 `Socket*` + 监听 `Channel*` |
| **Connection** | 一个客户端连接的封装（socket + channel + echo 业务） | `Socket*` + `Channel*` + `Buffer*` |
| **ThreadPool** | 线程池（N 个工作线程 + 任务队列） | N 个 `std::thread` |
| **Server** | 顶层管理：把所有零件组装起来 | mainReactor + Acceptor + subReactors + thpool + connections |

---

## 二、谁拥有谁（所有权图）

```
                              Server
                                │
        ┌───────────────────────┼──────────────────────────┐
        │                       │                          │
        ▼                       ▼                          ▼
  m_mainReactor           m_thpool (线程池)         m_connections (map)
  (EventLoop*)            │                          ├── fd5 → Connection*
        │                 ├── thread 0               ├── fd6 → Connection*
        │                 ├── thread 1               └── ...
        │                 └── thread N-1
        │
        ▼
     m_acceptor (Acceptor)
        └─ 监听 Socket + 监听 Channel


   m_subReactors (vector)
        ├── m_subReactors[0] (EventLoop*) ←── thread 0 跑它的 loop()
        ├── m_subReactors[1] (EventLoop*) ←── thread 1 跑它的 loop()
        ├── ...
        └── m_subReactors[N-1] ←────────── thread N-1 跑它的 loop()

   每个 subReactor 内部：
   EventLoop → Epoll → 监听一堆 Channel（每个 Channel = 一个客户端 fd）
                              │
                              └── 每个 Channel 属于一个 Connection
```

**关键关系**：
- Server **拥有** mainReactor、Acceptor、thpool、subReactors、connections
- thpool **拥有** N 个线程
- 每个线程 1 对 1 跑一个 subReactor 的 `loop()`
- 每个 subReactor 内部的 Epoll 监听多个 Channel（= 多个客户端 fd）

---

## 三、服务器怎么启动的（trace main → Server 构造）

### Step 1: main 函数

打开 [main.cpp](file:///home/yaohang/桌面/MakeCppServer/11_MultiReactor/main.cpp)：

```cpp
int main() 
{
    EventLoop* loop = new EventLoop();      // ① 建 mainReactor（一个 EventLoop 对象）
    Server* server = new Server(loop);     // ② 构造 Server（最复杂的一步，见下）
    loop->loop();                          // ③ 主线程进入 mainReactor 的 loop()
    return 0 ;
}
```

### Step 2: new EventLoop() 干了啥

打开 [EventLoop.cpp#L6-9](file:///home/yaohang/桌面/MakeCppServer/11_MultiReactor/src/EventLoop.cpp#L6-L9)：

```cpp
EventLoop::EventLoop() : m_epoll(nullptr), quit(false)
{
    m_epoll = new Epoll();    // ← 创建一个 Epoll 对象
}
```

再看 [Epoll.cpp#L7-13](file:///home/yaohang/桌面/MakeCppServer/11_MultiReactor/src/Epoll.cpp#L7-L13)：

```cpp
Epoll::Epoll() : m_epoll_fd(-1), m_events(nullptr)
{
    m_epoll_fd = epoll_create1(0);    // ← 向内核申请一个 epoll 句柄
    m_events = new epoll_event[MAX_EVENTS];
}
```

**结论**：`new EventLoop()` 这一行 = 创建一个 EventLoop + 创建一个 Epoll + 创建一个内核 epoll fd。**这就是 mainReactor 的 epoll 在哪管理**——藏在 EventLoop 的私有成员 `m_epoll` 里。

### Step 3: Server 构造（最复杂的一步）

打开 [Server.cpp#L13-31](file:///home/yaohang/桌面/MakeCppServer/11_MultiReactor/src/Server.cpp#L13-L31)：

```cpp
Server::Server(EventLoop *loop) : m_mainReactor(loop), m_acceptor(nullptr), m_thPool(nullptr)
{
    // (a) Acceptor 挂在 mainReactor 上
    m_acceptor = new Acceptor(m_mainReactor);

    // (b) 注册回调：Acceptor 发现新连接 → 调用 Server::newConnection
    std::function<void(Socket*)> callback = std::bind(&Server::newConnection, this, std::placeholders::_1);
    m_acceptor->setNewConnectionCallback(callback);

    // (c) 拿 CPU 核数 N
    int size = std::thread::hardware_concurrency();

    // (d) 建线程池（这一行直接开了 N 个线程，它们都阻塞着等任务）
    m_thPool = new ThreadPool(size);

    // (e) 建 N 个 subReactor（只是 new 出 N 个 EventLoop 对象，还没线程跑）
    for(int i = 0; i < size; ++i)
        m_subReactors.emplace_back(new EventLoop());
    // ↑ 每个 new EventLoop() 又会创建一个独立的 Epoll + epoll fd

    // (f) 把每个 subReactor 的 loop() 函数丢进线程池
    //     → 每个空闲线程被唤醒，取走一个 loop() 任务
    //     → 各自跑 m_subReactors[i]->loop()，进入 while 死循环
    //     → 在 epoll_wait 上阻塞，等待客户端事件
    for(int i = 0; i < size; ++i) {
        std::function<void()> task = std::bind(&EventLoop::loop, m_subReactors[i]);
        m_thPool->add(task);
    }
}
```

**这一步执行完后**：线程池里 N 个线程都各自跑着一个 subReactor 的 `loop()`，全部阻塞在 epoll_wait 上等事件。但此刻每个 subReactor 的 Epoll 还没监听任何客户端 fd（因为还没客户端连上）。

### Step 4: Acceptor 构造（嵌套在 Server 构造里）

打开 [Acceptor.cpp#L7-17](file:///home/yaohang/桌面/MakeCppServer/11_MultiReactor/src/Acceptor.cpp#L7-L17)：

```cpp
Acceptor::Acceptor(EventLoop* loop) : m_loop(loop), m_sock(nullptr), ...
{
    m_sock = new Socket();                                // 建监听 socket
    m_addr = new InetAddress("192.168.48.128", 8888);
    m_sock->bind(m_addr);                                 // bind + listen
    m_sock->listen();
    m_sock->setNonBlocking();

    m_acceptChannel = new Channel(m_loop, m_sock->getSockfd());  // 给监听 fd 建一个 Channel
    m_acceptChannel->setCallback(std::bind(&Acceptor::acceptConnection, this));
    //               ↑ 注册回调：Channel 的事件触发时调 Acceptor::acceptConnection

    m_acceptChannel->enableReading();
    //               ↑ 让 Epoll 开始监听这个 fd
}
```

### Step 5: enableReading 怎么把监听 fd 加进 mainReactor 的 Epoll

打开 [Channel.cpp#L13-17](file:///home/yaohang/桌面/MakeCppServer/11_MultiReactor/src/Channel.cpp#L13-L17)：

```cpp
void Channel::enableReading()
{
    m_events = EPOLLIN | EPOLLET;       // 设置要监听的事件（可读 + 边沿触发）
    m_loop->updateChannel(this);        // ← 调 EventLoop::updateChannel
}
```

跟到 [EventLoop.cpp#L29-32](file:///home/yaohang/桌面/MakeCppServer/11_MultiReactor/src/EventLoop.cpp#L29-L32)：

```cpp
void EventLoop::updateChannel(Channel *channel)
{
    m_epoll->updateChannel(channel);    // ← 调 Epoll::updateChannel
}
```

再跟到 [Epoll.cpp#L24-41](file:///home/yaohang/桌面/MakeCppServer/11_MultiReactor/src/Epoll.cpp#L24-L41)：

```cpp
void Epoll::updateChannel(Channel* channel)
{
    int fd = channel->getFd();
    ...
    epoll_ctl(m_epoll_fd, EPOLL_CTL_ADD, fd, &ev);    // ← 真正把 fd 加进内核 epoll
}
```

**调用链**：

```
Acceptor 构造
  → Channel::enableReading
    → EventLoop::updateChannel(channel)
      → Epoll::updateChannel(channel)
        → epoll_ctl(mainReactor 的 m_epoll_fd, ADD, 监听 fd, ...)
```

### Step 6: 启动完毕的状态

回到 main.cpp 第 8 行 `loop->loop()`，主线程进入 mainReactor 的 while 循环：

```cpp
void EventLoop::loop()    // [EventLoop.cpp#L17-27]
{
    while (!quit)
    {
        std::vector<Channel*> activeChannel = m_epoll->poll();   // ← epoll_wait 阻塞
        for (Channel* ch : activeChannel)
            ch->handleEvent();
    }
}
```

**此时整个系统状态**：

```
主线程：跑 mainReactor->loop()，在 epoll_wait 上阻塞
       mainReactor 的 Epoll 监听 1 个 fd：监听 socket

线程池 N 个线程：
  thread 0 跑 m_subReactors[0]->loop()，在 epoll_wait 上阻塞（监听 0 个 fd，空等）
  thread 1 跑 m_subReactors[1]->loop()，在 epoll_wait 上阻塞（空等）
  ...
  thread N-1 跑 m_subReactors[N-1]->loop()，在 epoll_wait 上阻塞（空等）
```

服务器启动完毕，等客户端来。

---

## 四、注册 vs 触发（最关键的概念）

整套系统靠「**注册回调**」串联起来。**注册不触发任何函数**，只是把函数指针存起来。**触发**只发生在 `epoll_wait` 返回之后。

### 启动时一共注册了 4 个回调

| # | 注册位置 | 注册给谁 | 注册了什么 | 何时被触发 |
|---|---------|---------|----------|----------|
| 1 | [Acceptor.cpp#L15](file:///home/yaohang/桌面/MakeCppServer/11_MultiReactor/src/Acceptor.cpp#L15) | 监听 Channel | `Acceptor::acceptConnection` | 监听 fd 可读时（被 mainReactor 调） |
| 2 | [Server.cpp#L16](file:///home/yaohang/桌面/MakeCppServer/11_MultiReactor/src/Server.cpp#L16) | Acceptor | `Server::newConnection` | Acceptor::acceptConnection 内部调 |
| 3 | [Connection.cpp#L17](file:///home/yaohang/桌面/MakeCppServer/11_MultiReactor/src/Connection.cpp#L17) | 客户端 Channel | `Connection::handleReadEvent` | 客户端 fd 可读时（被 subReactor 调） |
| 4 | [Server.cpp#L63](file:///home/yaohang/桌面/MakeCppServer/11_MultiReactor/src/Server.cpp#L63) | Connection | `Server::deleteConnection` | 客户端断开（read 返回 0）或 write 失败时 |

**注册的顺序不重要，只要在 epoll_wait 返回之前都注册好就行**。

---

## 五、新客户端连接进来时发生了什么（trace 调用链）

### 触发点

客户端调 `connect()` → 内核完成三次握手 → 监听 socket 变可读 → **mainReactor 的 epoll_wait 返回**，带着监听 socket 对应的 Channel。

### 调用链（trace 代码）

```
T0: 客户端 connect → 内核 3 次握手 → 监听 socket 变可读
        ↓
T1: mainReactor 的 epoll_wait 返回（[EventLoop.cpp#L21] 的 m_epoll->poll()）
    拿到监听 socket 的 Channel
        ↓
T2: EventLoop::loop() 遍历活跃 Channel，调 ch->handleEvent()
    （[EventLoop.cpp#L24]）
        ↓
T3: Channel::handleEvent() 调用 m_Callback()
    （[Channel.cpp#L21]，m_Callback 就是 T0 时刻注册的 Acceptor::acceptConnection）
    ← 第 1 个回调被触发
        ↓
T4: Acceptor::acceptConnection() 函数体执行（[Acceptor.cpp#L26-29]）：
    void Acceptor::acceptConnection() {
        m_newConnectionCallback(m_sock);   // ← 调用注册的第 2 个回调
    }
        ↓
T5: m_newConnectionCallback 就是注册的 Server::newConnection
    ← 第 2 个回调被触发
        ↓
T6: Server::newConnection(m_sock) 执行（[Server.cpp#L50-67]）：
    (a) accept() 拿到客户端 fd
    (b) fd % N 选一个 subReactor
    (c) new Connection(subReactors[random], clientSocket)
            ↓
T7: Connection 构造（[Connection.cpp#L13-20]）：
    (a) new Channel(subReactor, clientFd)
    (b) channel->setCallback(绑定 Connection::handleReadEvent)   ← 注册第 3 个回调
    (c) channel->enableReading()
            ↓
T8: channel->enableReading → subReactor->updateChannel → epoll->updateChannel
    → epoll_ctl 把客户端 fd 加到 subReactors[random] 的 Epoll 里
    ────────────────────────────────────
    从此这个客户端 fd 就被那个 subReactor 监听了！
```

### 调用链简化图

```
[内核] 客户端 connect
    ↓
[mainReactor 线程]
    epoll_wait 返回监听 Channel
    → Channel::handleEvent                    (EventLoop.cpp L24)
    → Acceptor::acceptConnection              (注册的第 1 个回调)
    → Server::newConnection                   (注册的第 2 个回调)
        → accept 拿 fd
        → fd % N 选 subReactor
        → new Connection(subReactor, sock)
            → new Channel(subReactor, fd)
            → channel->setCallback(handleReadEvent)  (注册第 3 个回调)
            → channel->enableReading → subReactor 的 Epoll 加这个 fd
    ↓
[subReactor 线程] (从此以后这个客户端的事件归它管)
    epoll_wait 现在也开始监听这个 fd 了
```

**注意**：第 1~5 步在 mainReactor 线程里跑，第 6~8 步**也是被 mainReactor 线程调用的**（Connection 构造、Channel::enableReading 都在 mainReactor 线程内执行），但**结果**是把这个 fd 加到了 subReactor 的 Epoll 里。从此之后，这个 fd 的**事件处理**就交给 subReactor 的线程了。

---

## 六、客户端发数据时发生了什么（trace 调用链）

### 触发点

客户端 `write()` → 内核收到数据 → 客户端 fd 变可读 → **某个 subReactor 的 epoll_wait 返回**，带着那个客户端的 Channel。

### 调用链

```
T0: 客户端 write → 内核把数据放到客户端 fd 的接收缓冲 → fd 变可读
        ↓
T1: subReactors[random] 的 epoll_wait 返回，拿到客户端 fd 的 Channel
    （注意：这里的 EventLoop::loop() 在 subReactor 自己的线程里跑）
        ↓
T2: subReactor 的 EventLoop::loop() 遍历活跃 Channel，调 ch->handleEvent()
        ↓
T3: Channel::handleEvent() 调 m_Callback()
    （这个 callback 是 T7 时刻注册的 Connection::handleReadEvent）
    ← 第 3 个回调被触发
        ↓
T4: Connection::handleReadEvent(sockfd) 执行（[Connection.cpp#L29-74]）：
    (a) read(sockfd) 把数据读到 read_buffer
    (b) 读到 EAGAIN（缓冲区空了）
    (c) write(sockfd, read_buffer 的内容) 把数据原样写回（echo）
    (d) read_buffer->clear()
    
    如果 read 返回 0（客户端断开）：
        → 调用 m_deleteConnectionCallback(m_sock)
        → 这个 callback 就是注册的第 4 个回调 Server::deleteConnection
        → Server::deleteConnection 从 m_connections map 删除并 delete 这个 Connection
```

### 简化图

```
[客户端] write 数据
    ↓
[内核] 把数据放到客户端 fd 的接收缓冲
    ↓
[subReactor 线程]
    epoll_wait 返回客户端 Channel
    → Channel::handleEvent
    → Connection::handleReadEvent              (注册的第 3 个回调)
        → read → 写入 read_buffer
        → write 把数据 echo 回去
    
    如果 read 返回 0（断开）：
        → Server::deleteConnection             (注册的第 4 个回调)
```

**核心**：同一个连接的所有事件**永远在同一个 subReactor 线程里处理**。这就是 one loop per thread，没有锁、没有竞态。

---

## 七、客户端断开时发生了什么

### 触发点

客户端 `close()` → 服务端 read 返回 0 → 表示对端关闭。

### 调用链

```
1. subReactor 线程跑 Connection::handleReadEvent
2. read 返回 0
3. 调用 m_deleteConnectionCallback(m_sock)   ← 第 4 个回调触发
        ↓
4. Server::deleteConnection(socket)（[Server.cpp#L69-77]）
   (a) 从 m_connections map 里找到这个 fd 对应的 Connection*
   (b) 从 map erase 掉
   (c) delete connection
       → Connection 析构：delete sock / channel / read_buffer
```

**注意**：`Server::deleteConnection` 是被 **subReactor 线程**调用的，但它操作的是 Server 持有的 `m_connections` map。这就是 day12 遗留的**跨线程访问 map 的竞态问题**——mainReactor 线程在 `newConnection` 写 map，subReactor 线程在 `deleteConnection` 写同一个 map，没加锁。教程后面会用跨线程通信机制（类似 muduo 的 `runInLoop` + `wakeup`）解决。

---

## 八、所有回调的全景图（一张图理清）

整个系统就是一层层注册回调 + 一层层触发回调。把整张图存脑子里就懂了：

```
┌─────────────────────────────────────────────────────────────┐
│                    [启动阶段 - 全是注册]                     │
│                                                              │
│  Acceptor 构造时：                                           │
│    监听 Channel 注册回调 → Acceptor::acceptConnection       │
│                                                              │
│  Server 构造时：                                             │
│    Acceptor 注册回调 → Server::newConnection                 │
│                                                              │
│  Connection 构造时（每次新连接才注册）：                    │
│    客户端 Channel 注册回调 → Connection::handleReadEvent    │
│    Connection 注册回调 → Server::deleteConnection           │
│                                                              │
│  ↑↑↑ 注册只是把函数存好，不触发任何调用                     │
└─────────────────────────────────────────────────────────────┘

┌─────────────────────────────────────────────────────────────┐
│                [运行阶段 - epoll 驱动触发]                   │
│                                                              │
│  [新连接事件]                                                │
│  mainReactor.poll()                                          │
│    → Acceptor 的 Channel.handleEvent                         │
│    → Acceptor::acceptConnection                             │
│    → Server::newConnection (注册的第 2 个回调)               │
│        → new Connection → 新建 Channel 并加入 subReactor     │
│                                                              │
│  [客户端数据事件]                                            │
│  subReactor.poll()                                           │
│    → Connection 的 Channel.handleEvent                      │
│    → Connection::handleReadEvent (注册的第 3 个回调)         │
│        → read + write (echo)                                 │
│        → 如果断开：Server::deleteConnection (第 4 个回调)     │
└─────────────────────────────────────────────────────────────┘
```

**整个系统就是一层层注册回调**：Channel 不知道业务，EventLoop 不知道业务，Server 只管组装和派发，具体的读写在 Connection 里。这就是事件驱动 + 回调注册的解耦设计。

---

## 九、线程模型总览

```
┌──────────────────────────────────────────────────────────┐
│  主线程 (main)                                            │
│  跑 mainReactor->loop()                                   │
│  在 epoll_wait 上阻塞                                     │
│  Epoll 监听：监听 socket（来自 Acceptor）                 │
│  职责：发现新连接 → accept → 派发给 subReactor             │
│  不处理已建立连接的数据                                    │
└──────────────────────────────────────────────────────────┘

┌──────────────────────────────────────────────────────────┐
│  线程池 thread 0                                         │
│  跑 m_subReactors[0]->loop()                              │
│  在 epoll_wait 上阻塞                                     │
│  Epoll 监听：被分到自己的所有客户端 fd                     │
│  职责：处理这些客户端的所有事件（读、写、断开）            │
└──────────────────────────────────────────────────────────┘

┌──────────────────────────────────────────────────────────┐
│  线程池 thread 1                                         │
│  跑 m_subReactors[1]->loop()                              │
│  ...（同上）                                              │
└──────────────────────────────────────────────────────────┘

... 一直到 thread N-1 ...
```

**特点**：
- **1 + N 个事件循环**（1 个 mainReactor + N 个 subReactor）
- **1 + N 个线程**（主线程 + 线程池 N 个）
- **mainReactor 只做 accept**，不碰业务
- **每个 subReactor 处理多个客户端**，但每个客户端只属于一个 subReactor
- **同一连接的事件永远在同一线程处理** → 无锁、无竞态、缓存友好

---

## 十、完整生命周期一图流

```
[启动]
  main: new EventLoop (mainReactor) → 内部 new Epoll → epoll_create1
  main: new Server
    └─ 建线程池（N 个线程启动，全在等任务）
    └─ Acceptor: bind+listen 监听 socket，建 Channel + 注册回调，epoll_ctl ADD 监听 fd
    └─ 建 N 个 subReactor（每个内部又有独立的 Epoll）
    └─ 把每个 subReactor 的 loop() 丢进线程池
        → N 个线程被唤醒，各自跑 loop()，进入 epoll_wait 阻塞
  main: loop->loop()
        → 主线程进入 epoll_wait 阻塞

[客户端 A 连接]
  内核: A 完成 3 次握手
  mainReactor: epoll_wait 返回监听 fd
  → Acceptor 回调 → Server::newConnection
  → accept 拿到 fd=5
  → 5 % N 选 subReactor[5%N]
  → new Connection → Channel 加入 subReactor[5%N] 的 Epoll
  
[客户端 A 发数据]
  subReactor[5%N] 线程: epoll_wait 返回 fd=5
  → Connection 的 Channel.handleEvent
  → Connection::handleReadEvent
  → read + write echo 回去

[客户端 A 断开]
  subReactor[5%N] 线程: read 返回 0
  → Connection::m_deleteConnectionCallback
  → Server::deleteConnection(fd=5)
  → 从 map 删除 + delete Connection

[服务器关闭]
  main: delete Server
  → Server 析构：
    delete m_acceptor
    delete m_thPool     → 线程池析构：m_stop=true + notify_all + join 所有线程
                          所有跑 loop() 的线程退出
    delete m_connections 里的每个 Connection
    delete m_subReactors 里的每个 EventLoop
  main: delete mainReactor
  main: 退出
```

---

## 十一、读完之后你应该能回答

1. **为什么需要主从两个 Reactor？**
   - main Reactor 专注 accept，不被业务事件干扰，新连接响应快
   - sub Reactor 各管一摊，事件处理在不同线程并行，不被单线程瓶颈卡住

2. **线程数 = CPU 核数，10 个客户端够用吗？**
   - 够。一个线程靠 epoll 能监听几千个 fd，大部分时间在 epoll_wait 睡觉
   - 只在 fd 有事件时被唤醒处理，处理完立刻回去睡
   - I/O 密集型业务（echo、HTTP）线程数 = 核数是最优

3. **同一个客户端的事件会被不同线程处理吗？**
   - 不会。`fd % N` 选定 subReactor 后，这个 fd 的事件永远归那个线程
   - 这是 one loop per thread 的核心，避免跨线程竞态

4. **Channel、EventLoop、Connection 是什么关系？**
   - Channel = fd + 回调，是「事件入口」
   - EventLoop = Epoll + while 循环，是「事件分发器」
   - Connection = Socket + Channel + Buffer + 业务，是「一个客户端的完整封装」
   - EventLoop 不知道 Connection 存在；Connection 通过 Channel 间接和 EventLoop 通信

5. **回调怎么一层层串起来的？**
   - Acceptor 把 Server::newConnection 注册为自己的回调
   - Connection 把 Server::deleteConnection 注册为自己的回调
   - Channel 把 Acceptor::acceptConnection / Connection::handleReadEvent 注册为自己的回调
   - 真正的「事件 → 处理」就是 Channel.handleEvent → 一路调到业务代码

6. **mainReactor 的 Epoll 在哪管理？**
   - 在 main 函数 `new EventLoop()` 时创建，藏在 EventLoop 的私有成员 `m_epoll` 里
   - 外部通过 EventLoop 的 public 方法 `updateChannel()` 和 `loop()` 间接操作它

如果以上都能答上来，day12 就吃透了。

---

## 十二、想继续深入的话

day12 的架构已经搭完，但还有两个明显的不足：

1. **业务写死了 echo**：[Connection.cpp#L29-74](file:///home/yaohang/桌面/MakeCppServer/11_MultiReactor/src/Connection.cpp#L29-L74) 里的 `handleReadEvent` 把读到的数据原样 write 回去。要做 HTTP/FTP 就得改 Connection 源码，破坏通用性。day14 会引入业务回调注册机制。

2. **deleteConnection 跨线程访问 map**：subReactor 线程调 deleteConnection 写 `m_connections`，mainReactor 线程调 newConnection 也写同一个 map，没加锁。后面会用跨线程通信机制（类似 muduo 的 `runInLoop` + `wakeup`）解决，把跨线程操作转成「在所属线程里执行」。
