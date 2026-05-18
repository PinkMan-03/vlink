# VLink Timer 高级示例

## 1. 概述

本示例演示了 VLink `Timer` 的高级功能，包括严格模式（catch-up）、优先级定时器、多定时器并行、跨循环的 attach/detach 操作以及定时器统计信息查询。

## 2. 文件说明

| 文件 | 说明 |
|------|------|
| `timer_advanced.cc` | Timer 高级功能演示源码 |
| `CMakeLists.txt` | 构建配置，链接 `vlink::all` |

## 3. 构建与运行

```bash
cmake --build . --target example_timer_advanced
./examples/base/timer_advanced/example_timer_advanced
```

## 4. 核心功能详解

### 4.1 严格模式 (Strict Mode)

```cpp
timer.set_strict(true);
```

严格模式控制定时器在循环线程繁忙时的行为。当循环线程忙于执行其他任务导致定时器 tick 被延迟时：

- **非严格模式（默认）**：错过的 tick 被丢弃，下次在正常间隔后触发
- **严格模式**：错过的 tick 在循环线程空闲后立即补发，以维持总触发次数

严格模式适用于需要保证触发次数准确的场景（如采样计数器），而非严格模式适用于只关心定期执行的场景（如 UI 刷新）。

### 4.2 定时器优先级

```cpp
timer.set_priority(MessageLoop::kHighestPriority);
```

当定时器附加到 `kPriorityType` 类型的 MessageLoop 时，定时器回调会按设定的优先级被分发。高优先级的定时器回调在队列中会被优先执行。

预定义优先级：
- `kLowestPriority` = 1
- `kTimerPriority` = 50（内部默认）
- `kNormalPriority` = 100
- `kHighestPriority` = 65535

### 4.3 多定时器并行

同一个 MessageLoop 可以同时运行多个定时器，每个有独立的间隔、次数和回调。所有定时器回调在同一循环线程上串行执行，因此回调之间不会并发。

每个 MessageLoop 最多支持 100 个并发定时器（`kMaxTimerSize`）。

### 4.4 跨循环 Attach/Detach

```cpp
timer.detach();           // 从当前循环解绑
timer.attach(&new_loop);  // 绑定到新循环
timer.start();            // 在新循环上启动
```

定时器可以在运行时在不同的 MessageLoop 之间迁移。这在动态调度场景中非常有用，例如将定时器从一个即将关闭的循环迁移到另一个活跃的循环。

### 4.5 统计信息

```cpp
timer.get_interval();           // 当前间隔（毫秒）
timer.get_loop_count();         // 配置的总触发次数
timer.get_remain_loop_count();  // 剩余触发次数
timer.get_invoke_count();       // 已触发次数
timer.is_active();              // 是否正在运行
timer.get_message_loop();       // 关联的 MessageLoop 指针
```

这些方法可以随时调用，用于监控定时器的运行状态。

## 5. 代码执行流程

1. **严格模式对比**：创建两个定时器（严格/非严格），模拟 200ms 的繁忙期，对比触发次数差异
2. **优先级定时器**：在 Priority 循环上运行高低优先级定时器
3. **多定时器**：在同一循环上运行快（50ms）慢（200ms）两个定时器
4. **跨循环迁移**：将定时器从 loop_a 迁移到 loop_b
5. **统计查询**：在定时器运行过程中查询各项统计信息

## 6. 严格模式详细分析

### 6.1 非严格模式（默认）

```
时间轴:  0ms   50ms  100ms  150ms  200ms  250ms  300ms
定时器:  |--T--|--T--|--BUSY--------|--T--|--T--|
触发:    1     2     （跳过）         3     4
```

非严格模式下，如果循环线程在 100ms-200ms 之间忙碌，本应在 150ms 触发的 tick 被丢弃。下一次触发在繁忙期结束后的正常间隔时刻。

### 6.2 严格模式

```
时间轴:  0ms   50ms  100ms  150ms  200ms  250ms  300ms
定时器:  |--T--|--T--|--BUSY--------|T|T|-T--|--T--|
触发:    1     2     （积累）         3 4  5     6
```

严格模式下，错过的 tick 在循环线程空闲后立即连续触发以补齐，保证总触发次数与理论值一致。

### 6.3 适用场景对比

| 场景 | 推荐模式 | 原因 |
|------|---------|------|
| 采样计数 | 严格 | 需要精确的触发次数 |
| UI 刷新 | 非严格 | 丢帧可接受，避免卡顿 |
| 心跳检测 | 非严格 | 只关心最近一次心跳 |
| 数据同步 | 严格 | 确保每个同步点都被处理 |

## 7. 注意事项

- 严格模式下的补发会导致短时间内大量回调执行，可能影响循环线程的响应性
- 优先级只对 `kPriorityType` 循环有效，其他类型忽略优先级参数
- detach 后必须 attach 到新循环才能再次 start
- 定时器析构时自动 stop 和 detach
- 定时器回调在循环线程上执行，避免在回调中执行耗时阻塞操作
- 最大定时器数量为每个 MessageLoop 100 个
