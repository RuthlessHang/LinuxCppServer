# 3_Epoll 逻辑总结

## 一、整体架构：事件驱动单线程并发

本阶段是项目的**第一个转折点**——从"单连接阻塞"跃迁到"多连接并发"。核心思想：用 epoll 让**单线程**同时监控多个 fd，谁有事件就处理谁。

```
                    事件循环（单线程）
                 ┌──────────────────────┐
                 │  while(true) {       │
                 │    epoll_wait()      │ ← 阻塞等事件（不忙等）
                 │    for 每个就绪fd {   │
                 │      分发处理         │
                 │    }                 │
                 │  }                   │
                 └──────────────────────┘
                          │
              ┌───────────┼───────────┐
              ↓           ↓           ↓
         监听fd可读    客户端A可读   客户端B可读
         → accept     → read/write  → read/write
         → 新fd注册    → 循环读到EAGAIN → 循环读到EAGAIN
```

### 与阶段 2 的架构对比

```
阶段 2（阻塞单连接）：               阶段 3（epoll 多路复用）：

  while(true) {                       while(true) {
    read(client_fd)  ← 阻塞！           epoll_wait()  ← 只返回有事件的fd
    write(client_fd)                    for 每个就绪fd {
  }                                       if 是监听fd → accept + 注册新fd
                                          if 是客户端fd → while(read) 回显
  只能服务1个客户端                      }
                                        }
                                        能同时服务N个客户端
```

---

## 二、epoll 三件套：核心架构组件

epoll 的三个系统调用构成了事件驱动的基石，后续所有阶段的 Epoll 类都在封装它们：

### 1. `epoll_create1()` —— 创建 epoll 实例

```cpp
int epfd = epoll_create1(0);
```

在内核中创建一个 epoll 实例，返回 `epfd`。这个 fd "管理"着一群别的 fd。

### 2. `epoll_ctl()` —— 注册/修改/删除 fd

```cpp
ev.events = EPOLLIN | EPOLLET;   // 关心可读 + 边缘触发
ev.data.fd = sockfd;
epoll_ctl(epfd, EPOLL_CTL_ADD, sockfd, &ev);
```

把 fd 加入 epoll 的监控列表。内核**长期保留**这个注册，不用每次重传（这是 epoll 相比 select 的核心优势）。

### 3. `epoll_wait()` —— 等待事件就绪

```cpp
int nfds = epoll_wait(epfd, event, MAX_EVENTS, -1);
```

阻塞等待，只返回**真正就绪**的 fd，不遍历全部注册的 fd。

### 三个 API 的协作关系

```
epoll_create1()  →  创建 epfd（内核建表）
       ↓
epoll_ctl(ADD)   →  注册监听fd（告诉内核：帮我盯着这个fd的可读事件）
       ↓
epoll_wait()     →  阻塞等事件（内核：有fd就绪了，返回给你）
       ↓
epoll_ctl(ADD)   →  注册客户端fd（新连接来了，也加入监控）
       ↓
epoll_wait()     →  继续等事件（现在监控着多个fd了）
```

---

## 三、边缘触发（ET）模式：本阶段的架构核心

本阶段使用 `EPOLLET`（边缘触发），这是理解整个项目的关键设计选择。

### ET vs LT

```
缓冲区数据量变化：
时间  ──────────────────────────────────────────►
       t1           t2           t3           t4
       收到5字节     又收到3字节

LT 模式通知：  ●●●●●●●●●●●●●●●●●●●●  （只要有数据就持续通知）
ET 模式通知：  ●            ●              （仅在状态变化时通知一次）
```

### ET 模式的两条铁律

因为 ET 只通知一次，所以：

1. **fd 必须设为非阻塞**：否则循环 read 读到最后一次，缓冲区空了，阻塞模式的 read 会**永久卡死**。

2. **读必须用 while 循环读到 EAGAIN**：一次 read 不一定读得完，要循环读直到返回 `-1` 且 `errno == EAGAIN`（缓冲区空了）。

```
为什么 ET 必须配非阻塞 + while 循环：

  epoll_wait 返回（fd可读）
    ↓
  read → "hello"（5字节）   ← 读到数据，继续循环
    ↓
  read → -1, errno=EAGAIN   ← 缓冲区空了，break
    ↓
  返回 epoll_wait 等下一个事件

  如果用阻塞fd：最后一次 read 会永久阻塞 → 整个事件循环挂死
  如果不用while循环：剩余数据不会再触发通知 → 丢数据
```

---

## 四、服务器启动流程

```
第1步：socket → bind → listen        （与阶段 1/2 相同的四步曲）
  ↓
第2步：epoll_create1() → epfd        （创建 epoll 实例）
  ↓
第3步：监听fd设非阻塞                  （ET 模式要求）
  ↓
第4步：epoll_ctl(ADD, 监听fd)         （把监听fd注册进 epoll）
  ↓
第5步：while(true) { epoll_wait → 分发 }  （进入事件循环）
```

对应 [server.cpp](file:///home/yaohang/桌面/MakeCppServer/3_Epoll/server.cpp) 的初始化：

```cpp
int sockfd = socket(AF_INET, SOCK_STREAM, 0);
// ... bind, listen ...

int epfd = epoll_create1(0);                    // 创建 epoll

ev.events = EPOLLIN | EPOLLET;                  // ET 模式
ev.data.fd = sockfd;
setNonBlocking(sockfd);                         // 非阻塞
epoll_ctl(epfd, EPOLL_CTL_ADD, sockfd, &ev);    // 注册监听fd
```

**启动完成后**：epoll 里只有 1 个 fd（监听 fd），等待客户端连接。

---

## 五、事件分发逻辑（运行时核心）

epoll_wait 返回后，主循环用 `if/else` 区分两种 fd：

```
epoll_wait 返回就绪事件
  ↓
for 每个就绪事件 event[i]：
  │
  ├─ event[i].data.fd == sockfd？
  │   → YES：监听fd可读 → 有新客户端连接
  │     ├─ accept → 得到 client_fd
  │     ├─ setNonBlocking(client_fd)
  │     └─ epoll_ctl(ADD, client_fd)  ← 新客户端注册进 epoll
  │
  └─ event[i].events & EPOLLIN？
      → YES：客户端fd可读 → 收到数据
        └─ while(true) { read → 回显 }  ← ET 模式必须循环读
```

### 客户端读循环（ET 标准写法）

```cpp
while(true)
{
    int bytes_read = read(fd, buffer, sizeof(buffer));

    if(bytes_read > 0)                              // 读到数据
    {
        std::cout << "Received: " << buffer;
        write(fd, buffer, sizeof(buffer));           // 回显
        // 继续循环！ET 模式要读完
    }
    else if(bytes_read == -1 && errno == EINTR)     // 被信号中断
        continue;                                    // 重试
    else if(bytes_read == -1 && errno == EAGAIN)    // 缓冲区空了
        break;                                       // 本次事件处理完毕
    else if(bytes_read == 0)                        // 客户端断开
    {
        close(fd);
        break;
    }
}
```

### read 返回值的演进（相比阶段 2）

| 返回值 | 阶段 2（阻塞） | 阶段 3（ET 非阻塞） |
|--------|---------------|-------------------|
| `> 0` | 回显 | 回显，**继续循环读** |
| `== 0` | 客户端断开 | 客户端断开 |
| `== -1` | 出错退出 | **细分为 EINTR（重试）/ EAGAIN（读空 break）/ 真错误** |

`EAGAIN` 是 ET 读循环的**退出条件**——读到它说明缓冲区被"榨干"了。

---

## 六、完整运行时序（多客户端并发）

```
[启动] epoll 里只有监听fd

[客户端A connect] → 监听fd可读
  ├─ epoll_wait 返回 event[0].data.fd == sockfd
  ├─ accept → client_fd_A
  ├─ setNonBlocking(client_fd_A)
  └─ epoll_ctl(ADD, client_fd_A)  ← A 注册进 epoll

[客户端A 发 "hello"] → client_fd_A 可读
  ├─ epoll_wait 返回 event[0].data.fd == client_fd_A
  └─ while(read):
       read → "hello" → 打印 + 回显
       read → -1, EAGAIN → break

[客户端B connect] → 监听fd又可读（多客户端并发的关键！）
  ├─ epoll_wait 返回 event[0].data.fd == sockfd
  └─ accept → client_fd_B → 注册进 epoll
  // 此时 A 和 B 同时被 epoll 监控，互不阻塞

[客户端A 发 "world"] → client_fd_A 可读
  └─ while(read): 读到 "world" → 回显 → EAGAIN → break

[客户端B 发 "foo"] → client_fd_B 可读
  └─ while(read): 读到 "foo" → 回显 → EAGAIN → break
```

**关键**：处理 A 的 read 时不会阻塞 B——read 是非阻塞的，A 没数据就立刻 EAGAIN 退出，事件循环转回去 epoll_wait，马上能响应 B。这就是**单线程并发**的本质。

---

## 七、设计理念

### 为什么用 epoll 而不是 select/poll

| 维度 | select/poll | epoll |
|------|-------------|-------|
| fd 拷贝 | 每次调用都全量拷贝 | 注册一次，长期保留 |
| 就绪查找 | 线性遍历全部 fd | 内核维护就绪链表，O(1) 取回 |
| fd 上限 | select 有 1024 限制 | 无上限（受系统 fd 限制） |
| 触发模式 | 只有 LT | 支持 ET（效率更高） |

### 为什么用 ET 而不是 LT

- **LT（水平触发）**：缓冲区还有数据就持续通知，即使你没读完。简单但 epoll_wait 被唤醒次数多。
- **ET（边缘触发）**：只在状态变化时通知一次。效率高但**必须一次性读完**，否则剩余数据不会再通知。

本项目选择 ET，因为它是 muduo 等高性能网络库的标准选择。

### 事件驱动的本质

```
传统阻塞模型：                    事件驱动模型：
  线程被 read 阻塞                  线程在 epoll_wait 等待
  → 什么都做不了                    → 有事件才醒来处理
  → 要并发必须多线程                → 单线程就能并发
```

---

## 八、代码组织说明（仍为结构化）

本阶段所有逻辑都堆在 `main()` 里，没有类封装：

```cpp
void setNonBlocking(int fd);  // 工具函数

int main()
{
    // 1. 建监听 socket（socket → bind → listen）
    // 2. 创建 epoll（epoll_create1）
    // 3. 注册监听 fd（epoll_ctl ADD）
    // 4. while(true) { epoll_wait → for 事件 { if/else 分发 } }
}
```

主循环里用 `event[i].data.fd == sockfd` 比较 fd 来区分类型——这种"拿 fd 自己比对"的方式在 fd 多了之后很难维护，正是后续阶段引入 Channel 的动机。

---

## 九、2→3 演进总结

| 维度 | 2_EchoAndUtil | 3_Epoll |
|------|---------------|---------|
| 并发能力 | ❌ 单客户端 | ✅ 多客户端并发 |
| IO 模型 | 阻塞 IO | 非阻塞 IO + epoll 事件驱动 |
| 触发模式 | — | 边缘触发 ET |
| 读数据 | 单次 read | while 循环读到 EAGAIN |
| accept | 一次性 | 事件驱动，按需 accept |
| 代码组织 | 全在 main() | 仍全在 main()（结构化，未封装） |

---

## 十、编译与运行

```bash
cd 3_Epoll
make

# 终端 1：启动服务器
./server

# 终端 2：客户端 A
./client
> hello

# 终端 3：客户端 B（A 不退出，同时连进来！）
./client
> world
```

服务器交替处理 A 和 B 的消息——**这是阶段 2 做不到的**。

---

## 十一、完整演进脉络

| 阶段 | 核心变化 |
|------|---------|
| 1_SimpleSocket | 最简 socket：accept 一个客户端就退出 |
| 2_EchoAndUtil | 加 read/write 循环 + errif 错误处理 |
| **3_Epoll** | **引入 epoll 多路复用，支持多客户端** |
| 4_WrapperClass | 封装 Socket/InetAddress/Epoll 类 |
| 5_Channel | 引入 Channel 抽象：fd+events+revents 绑定 |
| 6_Server | 引入 EventLoop+Server+回调，Reactor 成型 |
| 7_Acceptor | 拆出 Acceptor，监听逻辑独立 |
| 8_TCP | 拆出 Connection，连接逻辑独立，Server 只管生死 |

> epoll 三件套 + ET 铁律（非阻塞 + while 读到 EAGAIN）贯穿后续所有阶段。到 4_WrapperClass 时，epoll_create1/ctl/wait 被封装进 Epoll 类；到 6_Server 时，事件循环被封装进 EventLoop 类。
