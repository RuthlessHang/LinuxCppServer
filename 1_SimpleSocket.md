# 1_SimpleSocket — 最原始的 Socket

## 这版解决什么问题

搭建最原始的 socket 服务器骨架，验证基本的 socket API 调用。**整个项目的起点**。

## 核心代码（server.cpp）

```cpp
int sockfd = socket(AF_INET, SOCK_STREAM, 0);   // ① 创建 socket
bind(sockfd, ...);                              // ② 绑定 IP+端口
listen(sockfd, SOMAXCONN);                      // ③ 开始监听
int client_socket = accept(sockfd, ...);        // ④ 接受一个客户端连接
```

## 4 步核心动作

| 步骤 | API | 作用 |
|------|-----|------|
| ① | `socket()` | 创建套接字，拿到文件描述符 |
| ② | `bind()` | 绑定 IP 地址和端口 |
| ③ | `listen()` | 告诉内核开始监听 |
| ④ | `accept()` | 阻塞等客户端连接，返回新的客户端 fd |

**后续所有版本都绕不开这 4 步**。不管封装得多复杂，底层永远是这 4 个系统调用。

## 这版的限制

1. **只能接 1 个客户端**——accept 一次就退出
2. **完全不能收发数据**——只接连接不读不写
3. **阻塞模型**——accept 卡死等连接，CPU 闲着

## 文件结构

- `server.cpp`：服务端
- `client.cpp`：客户端测试

## 与下一版的衔接

第 2 版将在此基础上增加 `read()` 和 `write()`，实现 echo 功能。
