# 2_EchoAndUtil — 读写 + 工具函数

## 这版解决什么问题

在接收客户端连接后，能读取数据并将数据回显（echo）给客户端。同时封装了错误处理工具函数。

## 核心改动

| 新增/改动 | 说明 |
|----------|------|
| `util.cpp` / `util.hpp` | 封装 `errif()` 报错函数，失败时自动打印错误信息并退出 |
| `server.cpp` | accept 之后增加 `read()` 读取客户端数据，`write()` 回显回去 |

## 核心代码

```cpp
// 接受连接后循环读取并回显
while(true) {
    memset(buffer, 0, sizeof(buffer));
    ssize_t bytes_read = read(client_socket, buffer, sizeof(buffer));
    if(bytes_read > 0) {
        write(client_socket, buffer, bytes_read);  // echo 回去
    } else if(bytes_read == 0) {
        break;  // 客户端断开
    }
}
```

## `errif()` 函数的作用

```cpp
#define errif(cond, msg) \
    if(cond) { perror(msg); exit(EXIT_FAILURE); }

// 使用：
errif(sockfd == -1, "socket create failed");
```

把「检查错误 + 打印信息 + 退出」三步合并成一个宏，代码更简洁。

## 与上一版的差异

| | 1_SimpleSocket | 2_EchoAndUtil |
|---|---------------|--------------|
| 数据读写 | ❌ 没有 | ✅ read + write echo |
| 错误处理 | ❌ 手动 if | ✅ errif 宏封装 |

## 这版的限制

- **仍然只能服务 1 个客户端**
- **阻塞模型**——`read` 会卡住，CPU 空转
- **没有缓冲**——读到直接 write，跨 read 调用的数据存不住
- **没有 I/O 复用**，无法支持多客户端

## 与下一版的衔接

第 3 版将引入 epoll，实现 I/O 多路复用，支持同时服务多个客户端。
