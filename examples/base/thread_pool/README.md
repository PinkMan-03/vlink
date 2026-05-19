# VLink ThreadPool 示例 -- 深入解析

## 1. 概述

`ThreadPool` 维护固定数量的工作线程，从共享任务队列中取出并**并行**执行任务。与 MessageLoop 的单线程串行模型不同，ThreadPool 适用于 CPU 密集型的并行计算场景。

本示例深入演示并行任务执行、通过 `invoke_task` 获取异步结果、无锁队列类型选择以及策略控制。

## 2. 文件说明

| 文件 | 说明 |
|------|------|
| `thread_pool.cc` | 主程序入口，调用各 demo 函数和状态查询 |
| `parallel_tasks.h` | 并行任务 demo 函数：basic parallel、invoke_task、lockfree |
| `CMakeLists.txt` | 构建配置，链接 `vlink::all` |

## 3. 构建与运行

```bash
cmake -B build -S . -DCMAKE_PREFIX_PATH=/path/to/vlink/install
cmake --build build --target example_thread_pool
./build/output/bin/example_thread_pool
```

## 4. ThreadPool vs MessageLoop

这两者是 VLink 中最重要的任务执行器，各有不同的设计目标。

| 特性 | ThreadPool | MessageLoop |
|------|-----------|-------------|
| 线程数 | N 个工作线程并行 | 单线程串行 |
| 任务执行 | 并发执行 | 串行执行 |
| 共享状态 | 需要外部同步（mutex/atomic） | 无需同步 |
| 定时器支持 | 不支持 | 支持 |
| 生命周期钩子 | 不支持 | begin/end/idle 回调 |
| invoke_task | 支持 (std::future) | 支持 (std::future) |
| 优先级调度 | 不支持 | kPriorityType 支持 |
| 适用场景 | CPU 密集型并行计算 | 事件驱动、回调串行化 |

### 4.1 何时选择 ThreadPool

- **CPU 密集型计算**：图像处理、矩阵运算、压缩/解压缩
- **批量任务**：并行处理 N 个独立的数据块
- **Fan-out/Fan-in 模式**：分发子任务，收集结果

### 4.2 何时选择 MessageLoop

- **事件驱动**：网络消息处理、UI 更新
- **需要定时器**：周期性任务、超时检测
- **需要串行化**：避免共享状态同步开销

## 5. 并行计算模式

### 5.1 模式 1：Fire-and-forget

```cpp
ThreadPool pool(4);
pool.post_task([]() { heavy_computation(); });
// 不关心结果，继续执行其他操作
```

### 5.2 模式 2：Future 收集

```cpp
ThreadPool pool(4);
std::vector<std::future<int>> futures;
for (int i = 0; i < 8; ++i) {
    futures.push_back(pool.invoke_task([i]() -> int {
        return compute(i);
    }));
}
// 收集所有结果
for (auto& f : futures) {
    int result = f.get();  // 阻塞等待
}
```

### 5.3 模式 3：Atomic 累加

```cpp
ThreadPool pool(4);
std::atomic<int> sum{0};
for (int i = 1; i <= 100; ++i) {
    pool.post_task([i, &sum]() {
        sum.fetch_add(i);  // 原子累加
    });
}
// 等待所有任务完成后读取 sum
```

## 6. 关键代码分析

### 6.1 构造与销毁

```cpp
ThreadPool pool(4);                           // 4 线程，默认 Normal 队列
ThreadPool pool(4, ThreadPool::kLockfreeType); // 4 线程，无锁队列

// 析构函数自动调用 shutdown()
// shutdown() 等待所有工作线程完成当前任务后退出
pool.shutdown();
```

工作线程在构造时创建并立即运行。线程数不可动态修改。

### 6.2 post_task

```cpp
bool ok = pool.post_task([]() { work(); });
```

线程安全的任务投递。返回 `false` 表示队列已满。投递的任务被任意空闲的工作线程取出执行。任务间**无序**。

### 6.3 invoke_task

```cpp
auto future = pool.invoke_task([]() -> int { return compute(); });
int result = future.get();  // 阻塞等待结果
```

投递任务并返回 `std::future<T>`。支持任意可调用对象和返回类型。

### 6.4 队列类型

| 类型 | 内部实现 | 适用场景 |
|------|---------|---------|
| `kNormalType` | mutex + FIFO queue（默认） | 通用场景 |
| `kLockfreeType` | MPMC lock-free queue | 高竞争场景 |

### 6.5 调度策略（控制队列已满时 `post_task` 的行为）

| 策略 | 行为 | 适用场景 |
|------|------|---------|
| `kOptimizationStrategy` | 重试最多 10 次（每次 sleep 1 ms），仍满则丢弃最旧任务后入队（默认） | 平衡延迟与背压 |
| `kPopStrategy` | 立即丢弃最旧任务后入队 | 实时场景 |
| `kBlockStrategy` | 无限重试（每次 sleep 1 ms）直到有空位 | 不允许丢任务 |

### 6.6 状态查询

```cpp
pool.get_name();            // 名称
pool.get_type();            // 队列类型
pool.get_strategy();        // 调度策略
pool.get_task_count();      // 队列中待处理任务数
pool.get_max_task_count();  // 队列最大容量
pool.is_in_work_thread();   // 当前线程是否是工作线程
```

## 7. 性能建议

### 7.1 线程数选择

```cpp
// CPU 密集型：线程数 = CPU 核心数
unsigned int n = std::thread::hardware_concurrency();
ThreadPool pool(n);

// I/O 密集型：可以设置更多线程
ThreadPool pool(n * 2);
```

### 7.2 队列类型选择

- **低竞争**（少量生产者）：`kNormalType` 足够
- **高竞争**（多个线程频繁投递）：`kLockfreeType` 更优

### 7.3 策略选择（仅影响队列已满时的入队行为；空闲时始终为条件变量等待）

- **实时优先（宁丢旧不丢新）**：`kPopStrategy`
- **不允许丢任务**：`kBlockStrategy`（背压阻塞）
- **通用场景**：`kOptimizationStrategy`（先重试再退化为丢弃最旧）

### 7.4 避免任务粒度过细

```cpp
// 不推荐：每个元素一个任务
for (auto& item : huge_vector) {
    pool.post_task([&item]() { process(item); });
}

// 推荐：分块处理
size_t chunk = huge_vector.size() / pool_size;
for (int i = 0; i < pool_size; ++i) {
    pool.post_task([&, start = i * chunk, end = (i + 1) * chunk]() {
        for (size_t j = start; j < end; ++j)
            process(huge_vector[j]);
    });
}
```

## 8. 常见错误

### 8.1 错误 1：在工作线程上调用 future.get() 导致死锁

```cpp
pool.post_task([&pool]() {
    auto future = pool.invoke_task([]() { return 42; });
    future.get();  // 如果所有工作线程都在等待 future，死锁！
});
```

### 8.2 错误 2：共享状态未同步

```cpp
int counter = 0;
for (int i = 0; i < 100; ++i) {
    pool.post_task([&counter]() {
        counter++;  // 数据竞争！需要 std::atomic 或 mutex
    });
}
```

### 8.3 错误 3：shutdown 后继续投递

```cpp
pool.shutdown();
pool.post_task([]() { /* ... */ });  // 投递失败
```

### 8.4 错误 4：任务捕获了已销毁的局部变量

```cpp
void enqueue(ThreadPool& pool) {
    std::vector<int> local_data = {1, 2, 3};
    pool.post_task([&local_data]() {
        // local_data 已被销毁！
        process(local_data);
    });
}  // local_data 在这里被销毁
```

应改为值捕获：`[local_data]()` 或 `[data = std::move(local_data)]()`.

## 9. 相关示例

- [message_loop_basic](../message_loop_basic/) -- 单线程串行替代方案
- [graph_task](../graph_task/) -- 基于 DAG 的任务调度，可在 ThreadPool 上执行
- [multi_loop](../multi_loop/) -- 多个 MessageLoop 协作
- [spin_lock](../spin_lock/) -- 自旋锁用于极短临界区
