# Method Sync -- VLink 方法模型同步调用示例

## 1. 通信模型概览

![通信模型概览](../event_basic/images/communication-models-overview.png)

## 2. 概述

本示例演示 VLink **方法模型 (Method Model)** 的同步调用模式：Server 注册同步处理函数，Client 使用阻塞式 `invoke()` 发送请求并等待响应。

![Method Sync RPC Sequence](images/method-sync-rpc-sequence.png)

```
Client ──invoke(req)──> [dds://] ──> Server: callback(req, resp) ──> [dds://] ──> Client: resp
```

## 3. 方法模型核心概念

VLink 方法模型是一种请求/响应（RPC）通信模式：
- **Server<Req, Resp>**: 注册处理函数，接收请求，填写响应
- **Client<Req, Resp>**: 发送请求，等待并接收响应

### 3.1 五种调用模式

| 模式 | 方法 | 阻塞 | 返回值 |
|------|------|------|--------|
| 同步引用 | `invoke(req, resp&, timeout)` | 是 | `bool` |
| 同步可选 | `invoke(req, timeout)` | 是 | `optional<Resp>` |
| 回调异步 | `invoke(req, callback)` | 否 | `bool` |
| Future 异步 | `async_invoke(req)` | 否 | `future<Resp>` |
| 即发即忘 | `send(req)` | 否 | `bool` |

本示例聚焦前两种同步模式。异步模式参见 `method_async`，即发即忘参见 `method_fire_forget`。

## 4. 关键代码分析

### 4.1 Server 同步处理函数

```cpp
Server<MathRequest, MathResponse> server("dds://math/calculator");
server.attach(&server_loop);
server.listen([](const MathRequest& req, MathResponse& resp) {
    resp.result = req.x + req.y;
    resp.success = true;
});
```

`listen(ReqRespCallback)` 接收签名为 `void(const Req&, Resp&)` 的回调。回调中必须在返回前填充 `resp`，框架会在回调返回后自动序列化并发送响应。

**关键点**:
- 回调在 `attach()` 指定的 MessageLoop 线程上执行
- 回调是同步的：处理完成后框架立即回复
- `listen()` 只能调用一次，重复调用会触发 Fatal 错误
- 如果不需要响应，使用 `Server<Req>` 配合 `listen(ReqCallback)`

### 4.2 Client 连接检测

```cpp
Client<MathRequest, MathResponse> client("dds://math/calculator");
bool connected = client.wait_for_connected(2000ms);
bool is_conn = client.is_connected();
```

- `wait_for_connected(timeout)`: 阻塞等待 Server 就绪。默认超时 5000ms
- `is_connected()`: 非阻塞查询当前连接状态
- 还有 `detect_connected(callback)` 用于异步通知

### 4.3 invoke(req, resp) -- 引用输出模式

```cpp
MathRequest req{10.0, 3.0, 0};
MathResponse resp{};
bool ok = client.invoke(req, resp);
// resp.result == 13.0
```

阻塞式调用：
1. 序列化 `req`，通过传输层发送到 Server
2. Server 回调处理请求，填写 `resp`
3. Server 框架序列化 `resp`，通过传输层返回
4. Client 反序列化响应，写入 `resp` 引用
5. 返回 `true` 表示成功

返回 `false` 的情况：
- Server 未连接
- 超时（默认 5000ms）
- 传输层错误

### 4.4 invoke(req) -> optional -- 可选返回模式

```cpp
auto result = client.invoke(req);
if (result.has_value()) {
    double answer = result->result;
}
```

与引用输出模式功能相同，但返回 `std::optional<Resp>`：
- 成功：返回包含 `Resp` 的 `optional`
- 失败/超时：返回 `std::nullopt`

这种模式更符合现代 C++ 风格，避免了未初始化引用的问题。

### 4.5 自定义超时

```cpp
bool ok = client.invoke(req, resp, 1000ms);      // 1 秒超时
auto result = client.invoke(req, 1000ms);          // 1 秒超时
```

两种 invoke 模式都支持自定义超时参数。默认超时为 `Timeout::kDefaultInterval`（5000ms）。设置为 0 会被视为无限等待（会产生告警日志）。

## 5. RPC 内部流程

```
1. client.invoke(req)
   ├── 序列化 MathRequest -> Bytes
   ├── 发送到 dds:// 传输层
   └── 阻塞等待响应

2. Server 接收到请求
   ├── 反序列化 Bytes -> MathRequest
   ├── 在 server_loop 线程上执行回调
   ├── 回调填写 MathResponse
   ├── 序列化 MathResponse -> Bytes
   └── 通过传输层返回

3. client.invoke() 收到响应
   ├── 反序列化 Bytes -> MathResponse
   ├── 写入 resp 引用 / 返回 optional
   └── 解除阻塞，返回 true
```

## 6. 编译与运行

```bash
mkdir build && cd build
cmake .. -DCMAKE_PREFIX_PATH=/path/to/vlink/install
make example_method_sync
./output/bin/example_method_sync
```

## 7. 预期输出

```
[I] === VLink Method Sync Example ===
[I] [Server] Listening on dds://math/calculator
[I] [Client] Created on dds://math/calculator
[I] [Client] wait_for_connected: 1
[I] --- Mode 1: invoke(req, resp) ---
[I] [Server] op=0 x=10 y=3 => 13
[I] [Client] 10 + 3 = 13 success=1 ok=1
[I] [Server] op=1 x=100 y=7 => 93
[I] [Client] 100 - 7 = 93 success=1 ok=1
[I] --- Mode 2: invoke(req) -> optional ---
[I] [Server] op=2 x=6 y=7 => 42
[I] [Client] 6 * 7 = 42 success=1
[I] --- Mode 3: invoke with custom timeout ---
[I] [Server] op=3 x=100 y=0 => 0
[I] [Client] 100 / 0 = 0 success=0 ok=1
...
[I] === Example complete ===
```

## 8. 文件结构

| 文件 | 说明 |
|------|------|
| `math_types.h` | POD 消息类型 `MathRequest` / `MathResponse` 的定义 |
| `method_sync.cc` | 单进程合并示例（Server + Client） |
| `server.cc` | 多进程拆分：Server 端（独立可执行文件） |
| `client.cc` | 多进程拆分：Client 端（独立可执行文件） |
| `CMakeLists.txt` | 构建配置（生成 3 个可执行文件） |

### 8.1 多进程运行方式

```bash
# 终端 1: 启动 Server
./output/bin/example_method_sync_server

# 终端 2: 启动 Client
./output/bin/example_method_sync_client
```

## 9. 扩展思考

- 同步 `invoke()` 会阻塞调用线程。如果在 MessageLoop 线程上调用 `invoke()` 会导致死锁（loop 线程等待响应，但响应需要 loop 线程派发）。请在独立线程中调用 `invoke()`。
- 对于需要并发处理的场景，使用 `async_invoke()` 或 `invoke(req, callback)` 参见 `method_async` 示例。
- 将 URL 从 `dds://` 切换为 `someip://` 或 `zenoh://` 可切换传输协议，API 完全一致。
- 如果不需要响应（单向命令），使用 `Server<Req>` + `Client<Req>` 的即发即忘模式，参见 `method_fire_forget` 示例。

## 10. 相关文档

详细原理参见 [doc/04-method-model.md](../../../doc/04-method-model.md)。
