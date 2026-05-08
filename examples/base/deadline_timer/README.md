# VLink DeadlineTimer 示例

## 概述

本示例演示了 VLink `DeadlineTimer` 的原子绝对截止时间定时器功能。`DeadlineTimer` 存储一个绝对过期时间戳（而非倒计时），使用 64 位原子操作实现无锁读取，专为 VLink 连接管理和请求超时检测而设计。

## 文件说明

| 文件 | 说明 |
|------|------|
| `deadline_timer_demo.cc` | DeadlineTimer 全功能演示源码 |
| `CMakeLists.txt` | 构建配置，链接 `vlink::all` |

## 构建与运行

```bash
cmake --build . --target example_deadline_timer
./examples/base/deadline_timer/example_deadline_timer
```

## 核心设计

### 绝对时间 vs 相对时间

| 方式 | DeadlineTimer | std::chrono 超时 |
|------|--------------|-----------------|
| 存储 | 绝对时间戳 | 持续时间 |
| 过期检查 | `当前时间 > deadline` | 手动计算 |
| 多线程读取 | 原子操作，无锁安全 | 需要锁保护 |
| 修改截止时间 | 单次原子写 | 需要锁保护 |

绝对时间方案的优势在于：过期检查只需一次比较操作，不需要记录起始时间和计算差值。

### 内部结构

```cpp
class DeadlineTimer final {
    alignas(64) std::atomic<uint64_t> deadline_{0};  // 绝对过期时间戳
    Accuracy accuracy_{ElapsedTimer::kMilli};         // 时间精度
};
```

- `deadline_` 使用 `std::atomic<uint64_t>` 存储，并发读取安全
- `alignas(64)` 对齐到缓存行边界，防止伪共享
- 默认精度为毫秒

### 精度模式

| 精度 | 枚举值 | 时间单位 | 典型场景 |
|------|--------|---------|---------|
| `kMilli` | 0 | 毫秒 | 网络超时、连接管理 |
| `kMicro` | 1 | 微秒 | 实时控制、低延迟通信 |
| `kNano` | 2 | 纳秒 | 硬实时系统、精密测量 |

## API 详解

### 构造方式

```cpp
// 默认构造（无效状态，deadline=0）
DeadlineTimer timer;

// 相对截止时间（200ms 后过期）
DeadlineTimer timer(200, ElapsedTimer::kMilli);

// 微秒精度（50ms = 50000us）
DeadlineTimer timer(50000, ElapsedTimer::kMicro);

// 纳秒精度（10ms = 10000000ns）
DeadlineTimer timer(10000000LL, ElapsedTimer::kNano);
```

### set_deadline - 设置相对截止时间

```cpp
timer.set_deadline(300);  // 从现在起 300 个精度单位后过期
```

内部实现：读取当前 CPU 时间戳，加上 `interval`，原子存储为绝对截止时间。`interval <= 0` 设置为立即过期。

### set_deadline_abs - 设置绝对截止时间

```cpp
uint64_t now = ElapsedTimer::get_cpu_timestamp(ElapsedTimer::kMilli);
timer.set_deadline_abs(now + 500);  // 绝对时间戳
```

直接设置绝对过期时间。时间戳必须与 timer 配置的精度单位一致。通常通过 `ElapsedTimer::get_cpu_timestamp()` 获取当前时间戳。

### has_expired - 检查是否已过期

```cpp
if (timer.has_expired()) {
    // 截止时间已过
}
```

等效于 `remaining_time() <= 0`（当 `is_valid()` 为 `true` 时）。对于无效的 timer（`deadline=0`），始终返回 `false`。

### remaining_time - 剩余时间

```cpp
int64_t left = timer.remaining_time();
// 正数：还剩多少时间
// 0：无效（未设置）或已经过期
```

计算方式：`deadline - 当前CPU时间戳`。**已过期或未设置（is_valid==false）时均返回 0**，不会返回负值。判断过期请使用 `has_expired()`。

### reset - 重置为无效状态

```cpp
timer.reset();
// is_valid() == false, deadline() == 0
```

### is_valid - 有效性检查

```cpp
if (timer.is_valid()) {
    // deadline 已设置（非零）
}
```

### get_accuracy - 查询精度

```cpp
auto acc = timer.get_accuracy();
// ElapsedTimer::kMilli / kMicro / kNano
```

## 使用模式

### 模式一：超时检测

```cpp
DeadlineTimer deadline(100);  // 100ms 超时

while (!deadline.has_expired()) {
    if (check_response()) {
        break;  // 收到响应
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
}

if (deadline.has_expired()) {
    VLOG_W("操作超时");
}
```

这是最基本的超时检测模式。在循环中反复检查 `has_expired()`，直到收到响应或超时。

### 模式二：带截止时间的重试

```cpp
DeadlineTimer overall(2000);  // 总共 2 秒
int attempt = 0;

while (!overall.has_expired()) {
    attempt++;
    if (try_connect()) {
        break;
    }
    // 剩余时间不足以再次重试则退出
    if (overall.remaining_time() < 100) {
        break;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
}
```

### 模式三：请求-响应超时

```cpp
// 在 VLink RPC Client 内部
DeadlineTimer request_deadline(rpc_timeout_ms_);
send_request(payload);

while (!request_deadline.has_expired()) {
    if (response_queue_.try_pop(response, 10)) {
        return response;  // 成功
    }
}
throw Exception::RuntimeError("RPC timeout");
```

### 模式四：连接心跳监控

```cpp
DeadlineTimer heartbeat_deadline(5000);  // 5 秒心跳超时

void on_heartbeat_received() {
    heartbeat_deadline.set_deadline(5000);  // 收到心跳，重置截止时间
}

void monitor_loop() {
    while (running) {
        if (heartbeat_deadline.has_expired()) {
            VLOG_W("心跳超时，连接可能断开");
            reconnect();
            heartbeat_deadline.set_deadline(5000);
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
}
```

### 模式五：多阶段超时

```cpp
DeadlineTimer total_deadline(1000);  // 总超时 1 秒

// 阶段 1：连接（最多用总时间的一半）
DeadlineTimer connect_deadline(
    std::min<int64_t>(500, total_deadline.remaining_time()));
connect(connect_deadline);

// 阶段 2：发送（使用剩余时间）
int64_t remaining = total_deadline.remaining_time();
if (remaining > 0) {
    DeadlineTimer send_deadline(remaining);
    send_data(send_deadline);
}
```

## 拷贝语义

```cpp
DeadlineTimer original(500);
DeadlineTimer copy(original);       // 拷贝构造
DeadlineTimer assigned;
assigned = original;                // 拷贝赋值

// copy 和 assigned 共享相同的绝对截止时间
// 但后续的 set_deadline 调用互不影响
```

拷贝操作复制原子值和精度设置。拷贝后的两个 timer 独立工作，修改一个不影响另一个。

## 线程安全

| 操作 | 安全性 |
|------|-------|
| 多线程并发读取（`has_expired`/`remaining_time`） | 安全 |
| 一个线程写、多个线程读 | 安全 |
| 多线程并发写（`set_deadline`） | 原子存储安全，但语义上可能竞态 |

`deadline_` 使用 `std::atomic` 存储，所有读写操作都是原子的。典型使用模式是一个线程设置截止时间，多个线程检查是否过期。

## 与 ElapsedTimer 的关系

`DeadlineTimer` 使用 `ElapsedTimer` 的精度枚举和静态时间戳方法：

```cpp
using Accuracy = ElapsedTimer::Accuracy;

// 内部实现（伪代码）
void set_deadline(int64_t interval) {
    uint64_t now = ElapsedTimer::get_cpu_timestamp(accuracy_);
    deadline_.store(now + interval);
}

bool has_expired() const {
    uint64_t now = ElapsedTimer::get_cpu_timestamp(accuracy_);
    return now >= deadline_.load();
}
```

| 特性 | ElapsedTimer | DeadlineTimer |
|------|-------------|---------------|
| 用途 | 测量已经过的时间 | 检查截止时间是否已到 |
| 存储 | 起始时间戳 | 绝对截止时间 |
| 查询方向 | 向后看（过去多久了） | 向前看（还剩多久） |
| 主要方法 | `get()` | `has_expired()` / `remaining_time()` |

## 注意事项

- 默认构造的 timer 是无效的（`deadline=0`），`has_expired()` 返回 `false`，使用前必须调用 `set_deadline()`
- `remaining_time()` 在已过期或未设置时返回 0，不会返回负值；判定过期请使用 `has_expired()`
- 对齐到 64 字节以防止嵌入多个 timer 时的伪共享
- CPU 时间戳使用 `CLOCK_MONOTONIC_RAW`（Linux），不受 NTP 调整影响
- 在跨进程场景中不可使用（不同进程的单调时钟不可比较）
- `kNano` 精度的 64 位时间戳约 292 年后溢出，实际应用中无需担心
