# VLink MessageLoop 基础示例 -- 深入解析

## 1. 概述

`MessageLoop` 是 VLink 框架的核心任务调度器。所有任务和定时器回调都在其绑定的线程上**串行**执行。本示例深入演示 MessageLoop 的启动/退出、任务投递、手动事件处理以及生命周期回调。

理解 MessageLoop 是掌握 VLink 框架的关键，因为几乎所有高层通信原语（Publisher、Subscriber、Getter、Setter、Server、Client）的回调最终都在某个 MessageLoop 上执行。

## 2. 文件说明

| 文件 | 说明 |
|------|------|
| `message_loop_basic.cc` | MessageLoop 基础功能演示源码 |
| `CMakeLists.txt` | 构建配置，链接 `vlink::all` |

## 3. 构建与运行

```bash
cmake -B build -S . -DCMAKE_PREFIX_PATH=/path/to/vlink/install
cmake --build build --target example_message_loop_basic
./build/output/bin/example_message_loop_basic
```

## 4. 事件循环概念

![MessageLoop Architecture](images/message-loop-architecture.png)

### 4.1 什么是事件循环

事件循环（Event Loop）是一种编程模式：**单线程不断从任务队列中取出任务并执行**。这种模式的核心优势是回调天然串行化，无需加锁。

在车载软件领域，事件循环模式尤其重要：
- **确定性执行顺序**：任务按 FIFO 投递，按序执行
- **无锁回调**：所有回调在同一线程上执行，共享状态无需 mutex
- **与定时器天然集成**：定时器到期时将回调投递到同一队列
- **便于调试**：所有操作都在一个线程上，堆栈清晰

### 4.2 MessageLoop vs 传统线程模型

传统多线程模型中，每个操作可能在不同线程上执行，需要锁来保护共享状态。MessageLoop 模型将所有操作序列化到一个线程上：

```
传统模型：
  Thread1: read(shared_data) ──┐
  Thread2: write(shared_data) ─┤──> 需要 mutex
  Thread3: read(shared_data) ──┘

MessageLoop 模型：
  Queue: [read, write, read] ──> LoopThread: read -> write -> read (串行)
```

### 4.3 线程模型

MessageLoop 内部维护：
1. **任务队列**：线程安全的 FIFO 队列（可选 Normal/Lockfree/Priority 类型）
2. **定时器注册表**：最多 100 个定时器（`kMaxTimerSize`）
3. **调度线程**：唯一执行任务的线程
4. **生命周期回调**：begin_handler / idle_handler / end_handler

### 4.4 三种队列类型对比

| 类型 | 内部实现 | 适用场景 | 吞吐量 | 延迟 |
|------|---------|---------|--------|------|
| `kNormalType` | mutex + std::queue | 通用场景（默认） | 中 | 中 |
| `kLockfreeType` | MPMC lock-free queue | 高并发投递 | 高 | 低 |
| `kPriorityType` | priority_queue | 需要优先级调度 | 中 | 中 |

### 4.5 三种调度策略对比（队列已满时的入队行为）

策略只控制 `post_task` 在队列已满（达到 `kMaxTaskSize = 10000`）时如何处理；空闲调度恒为条件变量等待，不受策略影响。

| 策略 | 队列已满时行为 | 适用场景 |
|------|--------------|---------|
| `kOptimizationStrategy` | 重试最多 10 次（每次 sleep 1 ms），仍满则丢弃最旧任务再入队（默认） | 平衡延迟与背压 |
| `kPopStrategy` | 立即丢弃最旧任务并入队 | 实时场景（最新数据优先） |
| `kBlockStrategy` | 无限重试（每次 sleep 1 ms）直到空位出现 | 不允许丢任务的场景 |

## 5. 四种运行模式

### 5.1 async_run() -- 异步运行（最常用）

```cpp
MessageLoop loop;
loop.async_run();     // 启动后台线程，立即返回
loop.post_task(cb);   // 投递任务
loop.quit();          // 通知退出
loop.wait_for_quit(); // 等待后台线程完成
```

创建新的后台线程运行事件循环。调用者可以继续执行。这是最常用的模式。

### 5.2 run() -- 阻塞运行

```cpp
MessageLoop loop;
// 需要从其他线程调用 quit() 来终止
loop.run();  // 阻塞当前线程
```

在调用线程上运行事件循环，阻塞直到 `quit()` 被调用。适合在主线程上运行的场景。

### 5.3 spin() -- 自旋模式

```cpp
loop.spin();  // 持续自旋直到 quit()
```

在调用线程上持续自旋处理任务。适合集成到已有事件循环框架中。

### 5.4 spin_once() -- 单次处理

```cpp
loop.spin_once(false);  // 非阻塞：处理一批后返回
loop.spin_once(true);   // 阻塞：队列为空时等待
```

手动处理一批待处理任务。`block` 参数控制队列为空时的行为。适合需要精细控制执行节奏的场景，例如与渲染循环集成。

## 6. 关键代码分析

### 6.1 post_task() 线程安全投递

```cpp
loop.post_task([]() {
    // 在循环线程上执行
});
```

`post_task()` 是**线程安全**的，可以从任意线程调用。内部实现根据队列类型选择不同的入队策略。如果队列已满（`kMaxTaskSize = 10000`），返回 `false`。

### 6.2 生命周期回调

```cpp
loop.register_begin_handler([]() {
    // 循环线程启动后、处理第一个任务前调用一次
    // 适合：设置线程名称、CPU 亲和性、TLS 初始化
});

loop.register_end_handler([]() {
    // quit() 后、线程退出前调用一次
    // 适合：TLS 清理、统计汇总
});

loop.register_idle_handler([]() {
    // 每次任务队列清空时调用
    // 注意：频繁触发，避免做耗时操作
});
```

### 6.3 等待与同步

```cpp
loop.wait_for_idle();           // 等待队列清空
loop.wait_for_idle(1000);       // 带超时（毫秒）
loop.wait_for_quit();           // 等待循环完全退出
loop.wait_for_quit(5000, true); // 带超时和强制退出标志
```

### 6.4 状态查询

```cpp
loop.is_running();         // 循环是否正在运行
loop.is_busy();            // 是否正在执行任务
loop.is_ready_to_quit();   // quit() 是否已被调用
loop.get_task_count();     // 队列中待处理任务数
loop.get_type();           // 队列类型
loop.is_in_same_thread();  // 当前线程是否是循环线程
```

所有查询方法都是线程安全的。

## 7. 代码执行流程

1. **async_run + post_task**：创建循环并异步启动，投递 5 个任务，等待完成后退出
2. **生命周期回调**：注册 begin/end/idle 处理器，观察它们的调用时机
3. **阻塞 run()**：在调用线程上阻塞运行循环，由辅助线程投递任务并调用 quit()
4. **spin_once**：手动调用 spin_once 逐批处理任务
5. **状态查询**：在循环的不同阶段查询运行状态

## 8. 常见错误

### 8.1 错误 1：在循环线程上调用 wait_for_idle()

```cpp
loop.post_task([&loop]() {
    loop.wait_for_idle();  // 死锁！当前线程就是循环线程
});
```

`wait_for_idle()` 等待循环线程清空队列。如果在循环线程内调用，线程无法继续处理队列，形成死锁。

### 8.2 错误 2：在循环线程上调用 invoke_task().get()

```cpp
loop.post_task([&loop]() {
    auto future = loop.invoke_task([]() { return 42; });
    future.get();  // 死锁！future 需要循环线程执行才能 ready
});
```

同理，`invoke_task` 返回的 future 需要循环线程执行任务后才变为 ready。在循环线程上等待它会死锁。

### 8.3 错误 3：忘记调用 wait_for_quit()

```cpp
{
    MessageLoop loop;
    loop.async_run();
    loop.post_task(task);
    loop.quit();
    // 没有 wait_for_quit()!
}  // 析构函数会自动 quit(true) + wait，但可能有资源竞争
```

虽然析构函数会自动处理，但最佳实践是显式调用 `wait_for_quit()`。

### 8.4 错误 4：超过最大队列深度

```cpp
for (int i = 0; i < 20000; ++i) {
    bool ok = loop.post_task([]() { /* ... */ });
    if (!ok) {
        VLOG_W("Queue full! Task dropped.");  // kMaxTaskSize = 10000
    }
}
```

应始终检查 `post_task()` 的返回值，特别是在高吞吐量场景下。

## 9. 线程安全模型

- `post_task()` 可从任意线程安全调用
- 所有任务回调在循环线程上串行执行，回调内部**无需加锁**
- `quit()` 可从任意线程调用
- `is_in_same_thread()` 可用于断言是否在正确的线程上

## 10. 与其他框架的对比

| 特性 | VLink MessageLoop | Qt QEventLoop | libuv event loop | ASIO io_context |
|------|------------------|---------------|------------------|-----------------|
| 线程模型 | 单线程 | 单线程 | 单线程 | 可多线程 |
| 队列类型 | 3 种可选 | 固定 | 固定 | 固定 |
| 定时器集成 | 是 | 是 | 是 | 是 |
| 优先级支持 | 是 (kPriorityType) | 是 | 否 | 否 |
| 零拷贝友好 | 是 | 否 | 否 | 否 |

## 11. 相关示例

- [message_loop_advanced](../message_loop_advanced/) -- 队列类型比较、Schedule::Config、chaining、invoke_task
- [timer_basic](../timer_basic/) -- 与 MessageLoop 集成的定时器
- [multi_loop](../multi_loop/) -- 多循环协作
- [thread_pool](../thread_pool/) -- 并行替代方案，用于 CPU 密集型任务
