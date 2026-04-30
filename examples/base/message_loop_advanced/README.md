# VLink MessageLoop 高级示例

## 概述

本示例演示了 VLink `MessageLoop` 的高级功能，包括三种队列类型的对比、三种调度策略的切换、`exec_task` 与 `Schedule::Config` 的配合使用、`on_then`/`on_else` 结果链式调用，以及 `invoke_task` 的 Future 模式。

## 文件说明

| 文件 | 说明 |
|------|------|
| `message_loop_advanced.cc` | MessageLoop 高级功能演示源码 |
| `CMakeLists.txt` | 构建配置，链接 `vlink::all` |

## 构建与运行

```bash
cmake --build . --target example_message_loop_advanced
./examples/base/message_loop_advanced/example_message_loop_advanced
```

## 核心功能详解

### 1. 三种队列类型

| 类型 | 枚举值 | 内部实现 | 特点 |
|------|--------|----------|------|
| `kNormalType` | 0 | 互斥锁保护的 std::queue | 默认类型，不支持优先级 |
| `kLockfreeType` | 1 | 无锁 MPMC 队列 | 单生产者路径最快 |
| `kPriorityType` | 2 | 优先级队列 | 支持任务优先级分发 |

```cpp
MessageLoop normal_loop(MessageLoop::kNormalType);
MessageLoop lockfree_loop(MessageLoop::kLockfreeType);
MessageLoop priority_loop(MessageLoop::kPriorityType);
```

`kPriorityType` 支持 `post_task_with_priority(callback, priority)` 方法，数值越大的任务越先被执行。预定义的优先级包括 `kLowestPriority(1)`、`kTimerPriority(50)`、`kNormalPriority(100)` 和 `kHighestPriority(65535)`。

### 2. 三种调度策略

| 策略 | 枚举值 | 行为 | 适用场景 |
|------|--------|------|----------|
| `kOptimizationStrategy` | 0 | 队列为空时 yield CPU | 平衡延迟与 CPU 使用（默认） |
| `kPopStrategy` | 1 | 忙等轮询队列 | 最低延迟，最高 CPU 占用 |
| `kBlockStrategy` | 2 | 队列为空时阻塞在条件变量 | 最低 CPU 占用 |

```cpp
loop.set_strategy(MessageLoop::kBlockStrategy);
```

策略可以在运行时动态切换，在下一个空闲周期生效。

### 3. exec_task 与 Schedule::Config

```cpp
loop.exec_task(Schedule::Config{delay_ms, priority, schedule_timeout_ms, execution_timeout_ms},
               []() { /* void callback */ })
    .on_schedule_timeout([]() { /* 任务未在规定时间内开始 */ })
    .on_execution_timeout([]() { /* 任务执行超时 */ })
    .on_catch([](std::exception& e) { /* 任务抛出异常 */ });
```

`Schedule::Config` 的四个字段：

| 字段 | 含义 | 默认值 |
|------|------|--------|
| `delay_ms` | 投递前延迟（通过单次定时器实现） | 0 |
| `priority` | 任务分发优先级 | 0 |
| `schedule_timeout_ms` | 任务开始前的最长等待时间 | 0（禁用） |
| `execution_timeout_ms` | 任务执行的最长时间 | 0（禁用） |

返回的 `Schedule::Status` 对象支持链式注册回调，所有回调都在循环线程上调用。

### 4. on_then / on_else 结果链

当回调返回 `bool` 时，`exec_task` 返回 `Schedule::RetStatus`：

```cpp
loop.exec_task(config, []() -> bool { return try_connect(); })
    .on_then([]() -> bool {
        // callback 返回 true 时触发
        return start_session();
    })
    .on_then([]() -> bool {
        // 上一个 on_then 返回 true 时继续
        return verify_session();
    })
    .on_else([]() {
        // callback 返回 false 时触发
        retry_later();
    });
```

多个 `on_then` 可以串联，形成一个条件执行链。每个 `on_then` 回调返回 `bool`，只有返回 `true` 才会继续执行下一个。如果任何环节返回 `false`，则触发 `on_else`。

### 5. invoke_task 与 Future

```cpp
auto future = loop.invoke_task([]() -> int { return compute(); });
int result = future.get();  // 阻塞等待结果
```

`invoke_task` 将可调用对象投递到循环线程执行，返回 `std::future<T>` 以获取执行结果。支持任意返回类型。

**重要警告**：不要在循环线程上调用 `future.get()`，否则会死锁。`invoke_task_with_priority` 变体支持指定优先级。

### 6. Priority invoke_task

```cpp
auto future = loop.invoke_task_with_priority(
    []() -> int { return result; },
    MessageLoop::kHighestPriority);
```

在优先级队列循环中，高优先级的 invoke_task 会被优先执行。

## 代码执行流程

1. **队列类型对比**：分别创建 Normal、Lockfree、Priority 类型的循环并执行任务
2. **策略切换**：在运行的循环上动态切换三种调度策略
3. **void exec_task**：使用 Schedule::Config 配置延迟和优先级，注册超时和异常回调
4. **bool exec_task 链**：演示 true 路径（on_then 链）和 false 路径（on_else）
5. **invoke_task**：通过 future 获取循环线程上的计算结果
6. **优先级 invoke**：在 Priority 队列上执行不同优先级的 invoke_task

## Schedule::Status 生命周期

`Schedule::Status` 是一个移动语义的 RAII 对象，内部通过 `shared_ptr<StatusImpl>` 管理状态。即使 Status 对象被销毁，已注册的回调仍然有效（因为任务包装器持有 shared_ptr）。

## 性能考量

- `kLockfreeType` 在高频投递场景下开销最低
- `kPriorityType` 有排序开销，但保证关键任务优先处理
- `kBlockStrategy` 在任务间隔较长时节省 CPU 资源
- `kPopStrategy` 适合对延迟极度敏感的场景

## 注意事项

- `invoke_task().get()` 不能在循环线程上调用，否则死锁
- `kPriorityType` 之外的队列类型忽略优先级参数
- `exec_task` 的 `delay_ms > 0` 时通过内部一次性定时器实现延迟
- 所有回调（on_then/on_else/on_catch/on_timeout）都在循环线程上执行
