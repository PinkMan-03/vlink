# VLink ElapsedTimer 示例

## 1. 概述

本示例演示了 VLink `ElapsedTimer` 的高精度计时功能，包括毫秒/微秒/纳秒三种精度、墙钟时间与 CPU 活跃时间两种时钟源、restart 原子重置，以及静态时间戳工具函数。

## 2. 文件说明

| 文件 | 说明 |
|------|------|
| `elapsed_timer.cc` | ElapsedTimer 功能演示源码 |
| `CMakeLists.txt` | 构建配置，链接 `vlink::all` |

## 3. 构建与运行

```bash
cmake --build . --target example_elapsed_timer
./examples/base/elapsed_timer/example_elapsed_timer
```

## 4. 核心概念

### 4.1 两种时钟源 (Method)

| 时钟源 | 枚举值 | 底层实现 | 用途 |
|--------|--------|---------|------|
| `kCpuTimestamp` | 0 | `CLOCK_MONOTONIC_RAW` / `steady_clock` | 单调递增的墙钟时间，不受 NTP 调整影响 |
| `kCpuActiveTime` | 1 | `getrusage` / `GetProcessTimes` | 进程实际消耗的 CPU 时间（用户态+内核态） |

`kCpuTimestamp` 适合测量实际经过的时间（包括线程休眠），`kCpuActiveTime` 只计算 CPU 实际工作的时间，不包括 sleep 或等待 I/O 的时间。

### 4.2 三种精度 (Accuracy)

| 精度 | 枚举值 | 单位 | 64位范围 |
|------|--------|------|----------|
| `kMilli` | 0 | 毫秒 | ~2.92 亿年 |
| `kMicro` | 1 | 微秒 | ~29.2 万年 |
| `kNano` | 2 | 纳秒 | ~292 年 |

### 4.3 基本用法

```cpp
ElapsedTimer timer(ElapsedTimer::kCpuTimestamp, ElapsedTimer::kMicro);
timer.start();
do_work();
int64_t elapsed = timer.get();  // 微秒，未启动返回 -1
timer.stop();
```

### 4.4 restart - 原子重置

```cpp
int64_t lap = timer.restart();
```

原子地获取已经过的时间并重置起始点。等效于 `get()` + `start()`，但通过原子交换实现，避免竞态。适合连续测量多个阶段的耗时。

### 4.5 静态工具函数

```cpp
uint64_t sys_ts = ElapsedTimer::get_sys_timestamp(ElapsedTimer::kMilli);
uint64_t cpu_ts = ElapsedTimer::get_cpu_timestamp(ElapsedTimer::kNano);
uint64_t active = ElapsedTimer::get_cpu_active_time(ElapsedTimer::kMicro);
```

| 函数 | 说明 |
|------|------|
| `get_sys_timestamp` | 系统实时时钟（可被 NTP 调整） |
| `get_cpu_timestamp` | 单调递增时钟（不受 NTP 影响） |
| `get_cpu_active_time` | 进程累计 CPU 时间 |

## 5. 代码执行流程

1. **默认毫秒计时**：sleep 100ms 并测量
2. **微秒精度**：sleep 50ms 并测量（~50000us）
3. **纳秒精度**：测量 CPU 计算耗时
4. **CPU 活跃时间**：测量纯计算的 CPU 时间
5. **restart 连续测量**：两个 lap 的连续计时
6. **静态工具**：获取系统/CPU/进程时间戳
7. **配置查询**：查询 method 和 accuracy 设置

## 6. 线程安全

`ElapsedTimer` 内部使用 `std::atomic<int64_t>` 存储起始时间，`get()` 的并发读是安全的。但从不同线程并发调用 `start()`/`stop()` 会导致语义上的竞态（虽然不会导致未定义行为）。

## 7. 典型使用场景

### 7.1 函数耗时分析

```cpp
ElapsedTimer timer(ElapsedTimer::kCpuTimestamp, ElapsedTimer::kMicro);
timer.start();
process_data();
MLOG_I("process_data took {}us", timer.get());
timer.stop();
```

### 7.2 连续 Lap 计时

```cpp
ElapsedTimer timer(ElapsedTimer::kCpuTimestamp, ElapsedTimer::kMilli);
timer.start();
step_1();
MLOG_I("Step 1: {}ms", timer.restart());
step_2();
MLOG_I("Step 2: {}ms", timer.restart());
step_3();
MLOG_I("Step 3: {}ms", timer.restart());
timer.stop();
```

### 7.3 CPU 利用率分析

```cpp
ElapsedTimer wall(ElapsedTimer::kCpuTimestamp, ElapsedTimer::kMicro);
ElapsedTimer cpu(ElapsedTimer::kCpuActiveTime, ElapsedTimer::kMicro);
wall.start();
cpu.start();
work_with_io();
double utilization = 100.0 * cpu.get() / wall.get();
MLOG_I("CPU utilization: {:.1f}%", utilization);
```

## 8. 平台差异

| 特性 | Linux | Windows | macOS |
|------|-------|---------|-------|
| `get_sys_timestamp` | `clock_gettime(CLOCK_REALTIME)` | `system_clock` | `system_clock` |
| `get_cpu_timestamp` | `clock_gettime(CLOCK_MONOTONIC_RAW)` | `steady_clock` | `steady_clock` |
| `get_cpu_active_time` | `getrusage(RUSAGE_SELF)` | `GetProcessTimes` | `getrusage` |

## 9. 注意事项

- 定时器构造后**不会自动启动**，必须显式调用 `start()`
- `get()` 在未启动或已停止时返回 `-1`
- Linux 上 `get_sys_timestamp` 在 `high_resolution=true` 时使用 `clock_gettime`（纳秒级精度）
- `kCpuTimestamp` 在 Linux 上使用 `CLOCK_MONOTONIC_RAW`，不受 NTP 调整影响
- 对象大小为 64 字节对齐以避免 false sharing
- `restart()` 通过原子 `exchange` 实现，在多读者场景下是安全的
