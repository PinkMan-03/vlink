# VLink MultiLoop 示例

## 概述

本示例演示了 VLink `MultiLoop` 的多线程事件分发功能。MultiLoop 继承自 MessageLoop，通过多个工作线程共享同一任务队列实现并行任务处理。调用者使用与单线程 MessageLoop 相同的 `post_task()` API，但任务会被多个工作线程并发执行。

## 文件说明

| 文件 | 说明 |
|------|------|
| `multi_loop.cc` | MultiLoop 功能演示源码 |
| `CMakeLists.txt` | 构建配置，链接 `vlink::all` |

## 构建与运行

```bash
cmake --build . --target example_multi_loop
./examples/base/multi_loop/example_multi_loop
```

## 核心概念

### MultiLoop vs MessageLoop

| 特性 | MessageLoop | MultiLoop |
|------|------------|-----------|
| 工作线程 | 1 个 | N 个（构造时指定） |
| 任务执行 | 串行 | 并行 |
| 同步需求 | 回调无需同步 | 共享状态需同步 |
| 定时器触发 | 在循环线程上 | 在任意工作线程上 |
| API | post_task, exec_task 等 | 完全继承，API 相同 |

### 构造

```cpp
MultiLoop loop(4);                          // 4 工作线程，默认队列类型
MultiLoop loop(4, MultiLoop::kPriorityType); // 4 工作线程，优先级队列
```

### 线程身份检测

```cpp
loop.is_in_same_thread();
```

对于 MultiLoop，如果调用线程是其任何一个工作线程，都返回 `true`。

### 生命周期回调

- `on_begin()` 在每个工作线程启动时各调用一次
- `on_end()` 在每个工作线程退出前各调用一次
- `on_task_changed()` 在执行任务的工作线程上调用

## 代码执行流程

1. **基础并行分发**：20 个任务在 4 个线程上并行执行
2. **线程身份**：从主线程和工作线程检测 is_in_same_thread
3. **优先级队列**：使用 kPriorityType 投递不同优先级的任务
4. **invoke_task**：通过 future 获取并行计算结果
5. **exec_task**：在 MultiLoop 上使用 Schedule::Config
6. **并行计算**：原子累加验证并行正确性

## 详细 API 说明

### 继承自 MessageLoop 的方法

MultiLoop 继承了 MessageLoop 的全部公开 API，以下方法在 MultiLoop 上均可使用：

```cpp
loop.post_task(callback);                      // 投递任务
loop.post_task_with_priority(callback, prio);  // 带优先级投递
loop.exec_task(config, callback);              // 调度任务
loop.invoke_task(function, args...);           // Future 模式
loop.wait_for_idle();                          // 等待队列清空
loop.quit();                                   // 请求退出
loop.wait_for_quit();                          // 等待完全退出
```

### MultiLoop 特有行为

- `is_in_same_thread()` 在任意工作线程上返回 `true`
- `on_begin()` 和 `on_end()` 在每个工作线程上各调用一次
- `on_task_changed()` 在执行任务的工作线程上调用

### 使用建议

| 场景 | 推荐 |
|------|------|
| 高并行 CPU 密集型任务 | MultiLoop (N = CPU 核数) |
| 串行任务链 | MessageLoop |
| DAG 任务图 | MultiLoop + GraphTask |
| 需要定时器但无并行需求 | MessageLoop |

## 与 ThreadPool 的区别

| 特性 | MultiLoop | ThreadPool |
|------|-----------|-----------|
| 继承关系 | 继承 MessageLoop | 独立类 |
| 定时器支持 | 支持 | 不支持 |
| exec_task | 支持 Schedule 调度 | 不支持 |
| 生命周期回调 | on_begin/on_end/on_idle | 不支持 |
| 适用场景 | 需要 MessageLoop 特性的并行场景 | 纯并行计算 |

## 注意事项

- 任务可能并发执行，共享状态必须通过外部同步保护（如 mutex、atomic）
- 定时器回调在非确定性的工作线程上触发
- 析构函数等待所有工作线程完成
- 任务执行顺序不保证与投递顺序一致
- 所有的生命周期回调（on_begin/on_end）在每个工作线程上各调用一次
- MultiLoop 支持与 MessageLoop 相同的三种队列类型和三种调度策略
