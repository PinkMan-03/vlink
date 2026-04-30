# URL Intra -- intra:// 进程内传输详解

## 概述

`intra://` 是 VLink 的进程内传输协议，消息在同一 OS 进程的 Publisher/Subscriber 之间直接传递，无需序列化、无 IPC 开销。它是最快的传输方式，也是开发和测试的首选。

```
intra://address[?event=event_name&pipeline=N][#queue|#direct]
```

## URL 参数详解

| 参数 | 位置 | 默认值 | 说明 |
|------|------|--------|------|
| address | host/path | (必填) | 主题名称，如 `sensor/lidar` |
| event | ?event= | (空) | 二级过滤器，同一 address 下可分多个 event |
| pipeline | ?pipeline= | 0 | 管道队列深度。>0 时启用专用管道 |
| fragment | #queue/#direct | queue | 投递模式：queue(默认,异步) 或 direct(同步) |

## 关键代码分析

### 1. 基本用法

```cpp
Subscriber<std::string> sub("intra://basic/topic");
sub.listen([](const std::string& msg) { ... });
Publisher<std::string> pub("intra://basic/topic");
pub.publish("hello");
```

最简形式只需要地址。默认使用 queue 模式异步投递。

### 2. event 参数 -- 二级过滤

```cpp
Subscriber<std::string> sub_scan("intra://sensor/lidar?event=scan");
Subscriber<std::string> sub_status("intra://sensor/lidar?event=status");
Publisher<std::string> pub_scan("intra://sensor/lidar?event=scan");
```

同一 address 下，不同 event 的 Publisher/Subscriber 互不干扰。这允许在同一主题下创建逻辑子通道，如 `scan`（点云数据）和 `status`（状态信息）。

### 3. pipeline 参数 -- 管道深度

```cpp
Subscriber<std::string> sub("intra://pipeline/demo?pipeline=4");
Publisher<std::string> pub("intra://pipeline/demo?pipeline=4");
```

`pipeline=N` (N>0) 启用专用管道 MessageLoop，队列深度为 N：
- **N=0**（默认）：无独立管道，消息通过 attach 的 loop 或内部队列投递
- **N>0**：创建独立管道，缓冲 N 条消息。当消费者处理速度跟不上时，超过深度的旧消息会被丢弃（背压控制）

### 4. queue 模式（默认）

```cpp
// 以下两种写法等价
Subscriber<std::string> sub("intra://mode/demo#queue");
Subscriber<std::string> sub("intra://mode/demo");  // 默认就是 queue
```

queue 模式将消息放入内部队列，Subscriber 的回调在 loop 线程上异步执行。`publish()` 不阻塞。

### 5. direct 模式

```cpp
Subscriber<std::string> sub("intra://mode/demo#direct");
Publisher<std::string> pub("intra://mode/demo#direct");
pub.publish("message");  // 阻塞直到 Subscriber 回调完成
```

direct 模式下，`publish()` 直接在发布者线程上同步调用 Subscriber 的回调：
- **优点**：零延迟，无队列开销
- **缺点**：`publish()` 会阻塞直到所有回调执行完毕；不适合慢回调

### 6. 所有六种节点类型

```cpp
Publisher<T>  pub("intra://topic/event");
Subscriber<T> sub("intra://topic/event");
Setter<T>     setter("intra://topic/field");
Getter<T>     getter("intra://topic/field");
Server<Req,Resp> server("intra://topic/rpc");
Client<Req,Resp> client("intra://topic/rpc");
```

`intra://` 完整支持 VLink 全部六种通信原语。

### 7. 组合参数

```cpp
Subscriber<std::string> sub("intra://sensor/camera?event=frame&pipeline=2#direct");
```

event、pipeline、fragment 可以自由组合。

## intra:// 的优势与限制

| 优势 | 限制 |
|------|------|
| 零拷贝（POD 类型） | 仅同进程内可用 |
| 无外部依赖 | 不支持跨进程通信 |
| 最低延迟 | direct 模式会阻塞发布者 |
| 最高吞吐 | pipeline 深度有限 |
| 适合单元测试 | 无持久化/录制支持（需配合 bag 功能） |

## 编译与运行

```bash
mkdir build && cd build
cmake .. -DCMAKE_PREFIX_PATH=/path/to/vlink/install
make example_url_intra
./output/bin/example_url_intra
```

## 预期输出

```
[I] === Example 1: Basic intra:// address ===
[I] [basic] Received:hello from basic topic
[I] === Example 2: With event parameter ===
[I] [scan] Received:point cloud data frame 1
[I] [status] Received:lidar operational status: OK
[I] === Example 5: Direct mode ===
[I] [direct] Received:message via direct mode (on publisher thread)
[I] [direct] publish() returned -- callback already completed
...
```

## 扩展思考

- 开发阶段使用 `intra://` 可以完全避免外部依赖（如 RouDi、vsomeip、MQTT Broker），大幅提升编译-测试循环速度。
- `pipeline` 参数适用于生产者速度远快于消费者的场景，如高帧率摄像头数据。
- 在性能基准测试中，`intra://` + `direct` 模式可以实现纳秒级延迟。
- 通过 `VLINK_INTRA_BIND=1` 环境变量可以让 intra:// 主题对 VLink Proxy 可见，便于调试。
