# Method Fire-and-Forget -- VLink 即发即忘模式示例

## 1. 通信模型概览

![通信模型概览](../event_basic/images/communication-models-overview.png)

## 2. 概述

本示例演示 VLink **方法模型** 的即发即忘（Fire-and-Forget）模式：`Server<Req>` 只接收请求不发送响应，`Client<Req>` 使用 `send()` 发送请求后立即返回。

![Method Fire-and-Forget Flow](images/method-fire-forget-flow.png)

```
Client<Req> ──send(req)──> [dds://] ──> Server<Req>: callback(req)
                                          (无响应返回)
```

## 3. 即发即忘 vs 完整 RPC

| 特性 | 即发即忘 | 完整 RPC |
|------|---------|---------|
| Server 模板 | `Server<Req>` | `Server<Req, Resp>` |
| Client 模板 | `Client<Req>` | `Client<Req, Resp>` |
| Server 回调 | `void(const Req&)` | `void(const Req&, Resp&)` |
| Client 发送 | `send(req)` | `invoke(req)` |
| 等待响应 | 否 | 是 |
| 适用场景 | 日志、通知、非关键命令 | 查询、计算、需要确认的操作 |

## 4. 核心 API

### 4.1 Server<Req> (无 Resp 类型)

```cpp
Server<LogEntry> server(url);
server.listen([](const LogEntry& entry) {
    // 处理请求，不发送响应
});
```

当 `RespT` 省略时（默认为 `Traits::EmptyType`），Server 只接收请求。`listen` 回调的签名变为 `void(const Req&)`，没有 `Resp&` 参数。

### 4.2 Client<Req> (无 Resp 类型)

```cpp
Client<LogEntry> client(url);
bool ok = client.send(entry);
```

`send()` 方法仅在 `RespT == EmptyType` 时可用。它将请求发送到 Server 后立即返回，不等待任何响应。返回值 `true` 表示传输层成功接受了请求。

## 5. 关键代码分析

### 5.1 基本即发即忘

```cpp
Server<LogEntry> log_server("dds://logging/collector");
log_server.listen([](const LogEntry& entry) {
    // 处理日志条目，无需回复
});

Client<LogEntry> log_client("dds://logging/collector");
log_client.wait_for_connected(2000ms);
bool ok = log_client.send(entry);
```

这是最简洁的 RPC 模式：
1. Server 注册处理回调
2. Client 连接到 Server
3. Client 调用 `send()` 发送请求
4. Server 收到请求，执行回调
5. 没有响应环节，流程完成

### 5.2 通知命令模式

```cpp
Server<NotifyCommand> notify_server("dds://control/notifications");
notify_server.listen([](const NotifyCommand& cmd) {
    // 执行命令（启动电机、重置传感器等）
});

Client<NotifyCommand> notify_client("dds://control/notifications");
notify_client.send({1, 10, 100});  // 启动电机
notify_client.send({5, 10, 0});    // 停止电机
```

即发即忘模式非常适合控制命令场景：
- 命令发送方不需要等待确认
- 命令执行方可以按队列顺序处理
- 减少了往返延迟

### 5.3 高吞吐量场景

```cpp
constexpr int kBurstSize = 100;
for (int i = 0; i < kBurstSize; ++i) {
    burst_client.send(entry);
}
```

由于 `send()` 不等待响应，它的吞吐量远高于 `invoke()`：
- `invoke()`: 必须等待每次响应返回才能发下一个请求
- `send()`: 可以连续发送，不阻塞

这使得即发即忘模式特别适合高频遥测数据和日志收集。

## 6. 使用场景

| 场景 | 说明 |
|------|------|
| 日志收集 | 多个模块将日志发送到中央收集器 |
| 遥测数据 | 传感器高频上报原始数据 |
| 单向命令 | 向执行器发送非关键控制指令 |
| 事件通知 | 广播状态变更通知 |
| 心跳报告 | 周期性上报存活状态 |

## 7. Server 三种 listen 模式对比

```cpp
// 模式 1: 即发即忘（本示例）
Server<Req> server(url);
server.listen([](const Req& req) { ... });

// 模式 2: 同步回复（参见 method_sync）
Server<Req, Resp> server(url);
server.listen([](const Req& req, Resp& resp) { resp = ...; });

// 模式 3: 异步回复（参见 method_async）
Server<Req, Resp> server(url);
server.listen_for_reply([&server](uint64_t req_id, const Req& req) {
    // ... 稍后 ...
    server.reply(req_id, resp);
});
```

## 8. 编译与运行

```bash
cmake -B build -S . -DCMAKE_PREFIX_PATH=/path/to/vlink/install
cmake --build build --target example_method_fire_forget
./build/output/bin/example_method_fire_forget
```

## 9. 预期输出

```
[I] === VLink Method Fire-and-Forget Example ===
[I] --- Section 1: Basic fire-and-forget ---
[I] [LogServer] Listening on dds://logging/collector
[I] --- Section 2: Client send() ---
[I] [LogClient] Sent log entry #0 ok=1
[I] [LogServer] [DEBUG] source=100 ts=1234567890
[I] [LogClient] Sent log entry #1 ok=1
[I] [LogServer] [INFO] source=101 ts=1234567891
...
[I] [LogServer] Total logs received: 5
[I] --- Section 3: Notification commands ---
[I] [NotifyClient] Sent cmd=1 ok=1
[I] [NotifyServer] cmd=1 target=10 payload=100
...
[I] --- Section 4: High-throughput send ---
[I] [Burst] Sent: 100/100
[I] [Burst] Received: 100/100
[I] === Example complete ===
```

## 10. 文件结构

| 文件 | 说明 |
|------|------|
| `log_types.h` | POD 消息类型 `LogEntry` / `NotifyCommand` 的定义 |
| `method_fire_forget.cc` | 主程序：Server + Client 在同一进程 |
| `CMakeLists.txt` | 构建配置 |

## 11. 扩展思考

- 即发即忘模式不提供传输保证。如果需要"至少一次"投递语义，可以：
  - 在应用层实现 ACK 机制（使用完整 RPC + 超时重试）
  - 使用 MQTT 传输的 QoS 1/2（`mqtt://topic?qos=1`）
- `send()` 返回 `false` 通常表示未连接到 Server 或传输层错误，但不保证 Server 已处理完毕。
- 在生产环境中，日志收集器建议使用独立的 MessageLoop 并设置足够的 pipeline 深度，避免日志积压导致阻塞。
- 即发即忘模式可以与事件模型（Publisher/Subscriber）互换使用。选择依据：事件模型拓扑是 N:N 广播，即发即忘 RPC 是 N:1 汇聚；按参与者关系选择。

## 12. 相关文档

详细原理参见 [doc/04-method-model.md](../../../doc/04-method-model.md)。
