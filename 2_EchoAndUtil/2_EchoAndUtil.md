# 2_EchoAndUtil 逻辑总结

## 一、整体架构：单连接回显模型

相比阶段 1 的"连上就走"，本阶段加入了**数据收发循环**和**错误处理工具**。架构仍然是单连接阻塞模型，但真正实现了客户端 ↔ 服务器的通信。

```
服务器 (server.cpp)                    客户端 (client.cpp)
┌──────────────────────────┐          ┌──────────────────────────┐
│  socket → bind → listen  │          │  socket → connect        │
│       ↓                  │          │       ↓                  │
│  accept（阻塞等连接）     │◄════════│  连接成功                 │
│       ↓                  │          │       ↓                  │
│  ┌─────────────────┐     │          │  ┌─────────────────┐     │
│  │ while(true)     │     │          │  │ while(true)     │     │
│  │  read ←─────────│─────│──────────│──│  cin.getline    │     │
│  │  write ────────│─┐   │          │  │  write ────────│─┐   │
│  │  (回显)         │ │   │          │  │  (发送)         │ │   │
│  └─────────────────┘ │   │          │  └─────────────────┘ │   │
│        ↑             │   │          │        ↑             │   │
│    read==0 → break   │   │          │    write==0 → break  │   │
│    (客户端断开)       │   │          │                      │   │
└──────────────────────┘   │   │          └──────────────────────┘   │
                           │   │                                   │
                           │   │      echo 回显数据流               │
                           └───│───────────────────────────────────┘
                               │
                        write 把读到的原样写回
```

### 模块结构

```
2_EchoAndUtil/
├── server.cpp     # 服务器：建连 + while(read/write) 回显循环
├── client.cpp     # 客户端：建连 + while(cin/write) 输入循环
├── util.hpp/cpp   # ★新增：errif 错误处理工具
└── Makefile
```

### 相比阶段 1 的变化

| 方面 | 1_SimpleSocket | 2_EchoAndUtil |
|------|----------------|---------------|
| 数据收发 | ❌ 无 | ✅ while 循环回显 |
| 错误处理 | 仅 socket() 检查 | ✅ errif 统一检查 |
| 连接处理 | accept 一次就退出 | accept 后循环读写 |
| 源文件 | 单文件 | 拆出 util 工具模块 |

---

## 二、errif 工具函数（模块化的第一步）

本阶段第一次把代码拆成多个文件。`errif` 是一个独立的工具模块：

```
util.hpp  →  void errif(bool condition, const char* message);
util.cpp  →  if(condition) { perror(message); exit(EXIT_FAILURE); }
```

### 设计思路

把"检查返回值 + 报错 + 退出"三步压成一行：

```cpp
// 阶段 1 的写法（繁琐）
if (sockfd == -1) {
    std::cout << "socket creation failed" << std::endl;
    return 1;
}

// 阶段 2 的写法（清爽）
errif(sockfd == -1, "socket creation failed");
```

`perror` 会读取全局 `errno`，把系统调用的真正失败原因（如 `Address already in use`）拼在消息后面，比 `cout` 有用得多。

---

## 三、服务器流程：建连阶段 + 回显阶段

### 第 1 步：建连（与阶段 1 相同的四步曲）

```cpp
socket() → bind() → listen() → accept()
```

唯一区别：每一步都用 `errif` 包了错误检查，端口改为 `8080`。

### 第 2 步：回显循环（本阶段核心）

```cpp
while(true)
{
    ssize_t read_bytes = read(client_socket, buffer, sizeof(buffer));

    if(read_bytes > 0)          // 读到数据 → 原样写回
        write(client_socket, buffer, read_bytes);

    else if(read_bytes == 0)    // 客户端断开 → close + break
    {
        close(client_socket);
        break;
    }

    else if(read_bytes == -1)   // 出错 → errif 退出
        errif(true, "socket read failed");
}
```

### read 返回值三分法

这是网络编程最基础的知识点，所有后续阶段都在这个框架上扩展：

| 返回值 | 含义 | 处理 |
|--------|------|------|
| `> 0` | 成功读到 N 字节 | 回显 write，继续循环 |
| `== 0` | 对端正常关闭（发了 FIN） | close(fd)，break 退出循环 |
| `== -1` | 出错 | errif 报错退出 |

---

## 四、客户端流程：输入循环

```cpp
while(true)
{
    std::cin.getline(buffer, sizeof(buffer));      // 从键盘读一行
    ssize_t write_bytes = write(sockfd, buffer, strlen(buffer));  // 发给服务器
    // write 返回值同样三分法：>0 成功 / 0 服务器关闭 / -1 出错
}
```

数据流方向：

```
客户端键盘 → cin.getline → write(sockfd) ──→ 网络
                                                    ↓
服务器 read(client_socket) ←────────────────────────┘
        ↓
服务器 write(client_socket) ──→ 网络 ──→ （客户端本阶段不读回显）
```

> 注意：客户端只管发，不读服务器回显的内容。要看到回显需在客户端加 `read`（本阶段未做）。

---

## 五、完整运行时序

```
[服务器] ./server
  ├─ socket → bind → listen
  └─ accept ← 阻塞等待

[客户端] ./client
  ├─ socket → connect
  └─ 进入 while 循环

[客户端] 键盘输入 "hello" → write(sockfd, "hello", 5)
  ↓
[服务器] read → 收到 "hello"
  ├─ 打印 "Received: hello"
  └─ write 回显 "hello"

[客户端] 键盘输入 "world" → write(sockfd, "world", 5)
  ↓
[服务器] read → 收到 "world"
  ├─ 打印 "Received: world"
  └─ write 回显 "world"

[客户端] Ctrl+C / Ctrl+D 退出
  ↓
[服务器] read 返回 0
  ├─ 打印 "Client disconnected"
  ├─ close(client_socket)
  ├─ break 退出循环
  ├─ close(sockfd)
  └─ return 0（服务器也退出！）
```

---

## 六、设计理念

### 回显（echo）为什么是入门首选

回显 = 服务器把收到的数据原封不动发回去。它能验证"读"和"写"两个方向都通了，又不需要设计任何业务协议。所有网络编程教程的 hello world 都是 echo。

### 单连接模型的根本局限

```
客户端A connect ──► 服务器 accept A ──► while(read/write A)
                                            │
客户端B connect ──► 进内核队列，没人 accept  │  （B 一直阻塞）
                                            │
客户端A 断开 ─────► read==0 ──► break ──► 服务器退出
                                            │
                       B 永远等不到 accept ◄─┘
```

服务器被一个客户端的 `read` **阻塞**住了，整个进程挂起，无法响应其他客户端。要支持多客户端，要么多进程/多线程，要么 I/O 多路复用——这就是下一阶段 3_Epoll 要解决的问题。

---

## 七、1→2 演进总结

| 维度 | 1_SimpleSocket | 2_EchoAndUtil |
|------|----------------|---------------|
| 通信能力 | 无（连上就退出） | ✅ echo 回显 |
| 错误处理 | 几乎没有 | ✅ errif 统一 |
| 代码组织 | 单文件 | 拆出 util 模块 |
| 局限 | 无通信 | 单连接阻塞，无法并发 |

---

## 八、编译与运行

```bash
cd 2_EchoAndUtil
make

# 终端 1：启动服务器
./server
# 输出：Server started on port 8080

# 终端 2：启动客户端，输入文字回车发送
./client
> hello
> world
```

> IP `192.168.153.128` 硬编码在源码中，需改成自己机器的 IP 或 `127.0.0.1`。

---

## 九、完整演进脉络

| 阶段 | 核心变化 |
|------|---------|
| 1_SimpleSocket | 最简 socket：accept 一个客户端就退出 |
| **2_EchoAndUtil** | **加 read/write 循环 + errif 错误处理** |
| 3_Epoll | 引入 epoll 多路复用，支持多客户端 |
| 4_WrapperClass | 封装 Socket/InetAddress/Epoll 类 |
| 5_Channel | 引入 Channel 抽象：fd+events+revents 绑定 |
| 6_Server | 引入 EventLoop+Server+回调，Reactor 成型 |
| 7_Acceptor | 拆出 Acceptor，监听逻辑独立 |
| 8_TCP | 拆出 Connection，连接逻辑独立，Server 只管生死 |

> `errif` 和 `read/write` 三分法贯穿后续所有阶段。到 3_Epoll 时，`read == -1` 分支会进一步细分为 `EINTR`（重试）和 `EAGAIN`（读空），这就是 ET 模式的核心。
