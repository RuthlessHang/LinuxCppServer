# 9_Buffer — 数据缓冲累积

## 这版解决什么问题

用 Buffer 类替代栈上 `char buffer[1024]` 数组，支持数据**跨 read 调用累积**，实现真正的 write echo。

## 引入 Buffer 类

Buffer 本质是 `std::string` 的封装，提供网络编程友好的接口。

```cpp
class Buffer {
public:
    void append(const char* str, int len);  // 追加 len 字节
    ssize_t size();                          // 当前数据量
    const char* c_str();                     // 拿 C 风格字符串指针（给 write 用）
    void clear();                            // 清空
    void getLine();                          // 从 stdin 读一行（客户端用）
private:
    std::string m_buffer;                    // 内部 std::string，自动扩容
};
```

## Connection 新增成员

```cpp
Buffer* read_buffer;   // 每个连接独立的读缓冲区（构造时 new，析构时 delete）
```

## read + echo 流程（改进后）

```cpp
void Connection::handleReadEvent(int sockfd) {
    char buffer[MAX_BUFFER_SIZE];
    while(true) {
        memset(buffer, 0, sizeof(buffer));
        ssize_t bytes_read = read(sockfd, buffer, sizeof(buffer));
        if(bytes_read > 0) {
            read_buffer->append(buffer, bytes_read);     // ← 追加进 Buffer
        } else if(bytes_read == 0) {
            // 客户端断开 → 调 m_deleteConnectionCallback
            break;
        } else if(bytes_read == -1 && errno == EAGAIN) {
            // 读完了（socket 缓冲区空了）
            // ← 把 Buffer 里所有数据一次性写回去
            ssize_t bytes_written = write(sockfd, read_buffer->c_str(), read_buffer->size());
            read_buffer->clear();   // ← 清空，准备下次读写
            break;
        }
    }
}
```

## 为什么需要 Buffer

### 没有 Buffer 时的问题

```
客户端发来 "hello world"（分两次 read 到达）

第 1 次 read → 读到 "hello " → cout 打印（无法保存）
第 2 次 read → 读到 "world"  → cout 打印（覆盖了第一次的 buffer）
结果：客户端只看到 "world"，"hello " 丢了
```

### 有 Buffer 时

```
第 1 次 read → 读到 "hello " → append 进 Buffer → Buffer = "hello "
第 2 次 read → 读到 "world"  → append 进 Buffer → Buffer = "hello world"
read 返回 EAGAIN → write(sockfd, "hello world", 11) → 完整 echo
```

## 关键改进点

| | 8_TCP（栈数组） | 9_Buffer（Buffer 类） |
|---|----------------|---------------------|
| 存储位置 | 栈上（函数退出即销毁） | 堆上（成员变量，持久） |
| 大小 | 固定 1024 | 动态扩容（无上限） |
| 跨 read 累积 | ❌ 做不到 | ✅ 可以 |
| write | ❌ 没有（只有 cout） | ✅ 真正 write |
| 溢出风险 | ✅ 有 | ❌ 无 |

## 这版的限制

- 没有线程池，仍是**单线程串行处理**
- 业务写死 echo，没法自定义
- 没有写缓冲区，大数据 write 可能阻塞

## 与下一版的衔接

第 10 版将引入 `ThreadPool` 类作为多线程基础设施（过渡版本，暂未启用）。
