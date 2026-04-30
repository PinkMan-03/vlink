# Event Basic -- VLink 事件模型基础示例

## 通信模型概览

![通信模型概览](images/communication-models-overview.png)

## 概述

本示例演示 VLink **事件模型 (Event Model)** 的基本用法：使用 `Publisher` 发布消息、`Subscriber` 接收消息，并配合 `Timer` 实现定时周期性发布。这是 VLink 六大通信原语中最核心的模式。

```
Timer (500ms) --> Publisher<SensorData> --publish()--> [dds://] --callback--> Subscriber<SensorData>
```

## 核心概念

### 事件模型 vs 字段模型

| 特性 | 事件模型 (Publisher/Subscriber) | 字段模型 (Setter/Getter) |
|------|-------------------------------|--------------------------|
| 值保留 | 不保留，Subscriber 只收到注册后的消息 | 保留最新值，Getter 可随时读取 |
| 适用场景 | 传感器数据流、日志、通知 | 配置参数、状态字段 |
| API | publish() / listen() | set() / get() / wait_for_value() |

### 使用的 VLink 组件

- **Publisher<T>**: 消息发布者，调用 `publish(msg)` 将数据发送到所有订阅者
- **Subscriber<T>**: 消息订阅者，通过 `listen(callback)` 注册接收回调
- **Timer**: 定时器，绑定到 `MessageLoop` 上周期性触发回调
- **MessageLoop**: 事件循环，用于线程安全的回调派发
- **Utils::register_terminate_signal**: 注册 SIGINT/SIGTERM 处理函数

## 关键代码分析

### 1. 信号处理与优雅退出

```cpp
std::atomic<bool> running{true};
Utils::register_terminate_signal([&running](int sig) {
    VLOG_I("Signal ", sig, " received, shutting down...");
    running = false;
});
```

`register_terminate_signal` 注册一个在收到 SIGINT 或 SIGTERM 时调用的回调。回调将 `running` 标志设为 `false`，主循环检测到后开始有序关闭。这是 VLink 应用程序的标准退出模式。

### 2. POD 消息类型（定义在 `sensor_types.h`）

```cpp
struct SensorData {
    int id;
    float value;
    int64_t timestamp;
};
```

VLink 对 POD（标准布局、可平凡拷贝）类型使用 `kStandardType` 序列化策略，本质上是 `memcpy` 直接传输，无需定义 schema 或 IDL。这是最高效的序列化方式。

### 3. MessageLoop 与 attach

```cpp
MessageLoop loop;
loop.set_name("main_loop");
loop.async_run();
// ...
sub.attach(&loop);
```

`MessageLoop` 是 VLink 的事件派发器。默认情况下，Subscriber 的回调在传输层内部线程上执行。通过 `attach(&loop)` 将回调调度到 loop 线程上执行，确保：
- 回调与 loop 上的其他任务串行执行
- 避免竞态条件
- Timer 回调和 Subscriber 回调在同一线程上运行

`async_run()` 在后台线程启动事件循环，主线程不阻塞。

### 4. 等待订阅者连接

```cpp
pub.wait_for_subscribers();
```

`wait_for_subscribers()` 阻塞等待至少一个 Subscriber 出现。默认超时为 `Timeout::kDefaultInterval`（5000ms）。在 `dds://` 传输中，连接速度取决于 DDS 发现机制。

### 5. Timer 定时发布

```cpp
Timer timer(&loop, 500, Timer::kInfinite, [&pub, &publish_count, &running]() {
    // ... create and publish SensorData ...
    pub.publish(data);
});
timer.start();
```

`Timer` 构造参数：
- `&loop`: 绑定的 MessageLoop
- `500`: 间隔毫秒数
- `Timer::kInfinite`: 无限重复
- lambda: 每次触发时执行的回调

Timer 的回调在 loop 线程上执行，与 Subscriber 的回调串行化，无需额外加锁。

### 6. publish() 返回值

```cpp
bool ok = pub.publish(data);
```

`publish()` 返回 `true` 表示传输层成功接收数据。对于 `dds://`，只要有活跃的 Subscriber，总是返回 `true`。注意：默认情况下，没有 Subscriber 时 `publish()` 是空操作（返回 `false`），除非传入 `force=true`。

### 7. 主循环与关闭流程

```cpp
while (running) {
    std::this_thread::sleep_for(100ms);
}
timer.stop();
loop.wait_for_idle(1000);
loop.quit();
loop.wait_for_quit();
```

关闭流程：
1. `timer.stop()` -- 停止定时器，不再产生新任务
2. `loop.wait_for_idle(1000)` -- 等待所有待处理的回调完成
3. `loop.quit()` -- 请求 loop 停止
4. `loop.wait_for_quit()` -- 等待 loop 线程完全退出

## 消息流转过程

```
1. Timer 触发 (每500ms)
   --> 回调在 loop 线程上执行
   --> 创建 SensorData{id, value, timestamp}

2. pub.publish(data)
   --> 序列化 SensorData (memcpy, 因为是 POD 类型)
   --> dds:// 传输层将数据投递到 Subscriber 的 pipeline

3. Subscriber pipeline 检测到新消息
   --> 因为 attach 了 loop，post_task 到 loop 线程
   --> loop 线程执行 listen 回调
   --> 反序列化 -> SensorData
   --> 打印日志，递增计数器
```

## 编译与运行

```bash
mkdir build && cd build
cmake .. -DCMAKE_PREFIX_PATH=/path/to/vlink/install
make example_event_basic
./output/bin/example_event_basic
```

按 Ctrl+C 可以触发优雅关闭。

## 预期输出

```
[I] === VLink Event Basic Example ===
[I] [Subscriber] Listening on dds://sensor/temperature
[I] [Publisher]  Publishing on dds://sensor/temperature
[I] [Publisher]  Subscriber detected, starting timer...
[I] [Publisher]  Published #1 value=20.5 ok=1
[I] [Subscriber] id=1 value=20.5 ts=1234567890
[I] [Publisher]  Published #2 value=21 ok=1
[I] [Subscriber] id=2 value=21 ts=1234567891
...
[I] Published: 10 Received: 10
[I] === Example complete ===
```

## 文件结构

| 文件 | 说明 |
|------|------|
| `sensor_types.h` | POD 消息类型 `SensorData` 的定义 |
| `event_basic.cc` | 主程序：Publisher + Subscriber + Timer 在同一进程 |
| `CMakeLists.txt` | 构建配置 |

## 扩展思考

- 将 `dds://sensor/temperature` 替换为 `shm://sensor/temperature` 或 `zenoh://sensor/temperature`，代码无需修改即可切换传输协议。
- Timer 的间隔可以动态调整：`timer.set_interval(new_ms)` + `timer.restart()`。
- 如果需要保留最新值给迟到的读者，应使用 Field 模型（参见 `field_basic` 示例）。
- `register_terminate_signal` 也可以设置为异步模式（`is_async=true`），在信号处理线程中执行。

## 相关文档

详细原理参见 [doc/03-event-model.md](../../../doc/03-event-model.md)。
