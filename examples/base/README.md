# base/ -- 基础库示例

## 1. 概述

VLink 基础工具库（`vlink/base/`）各组件的独立用法演示，包括字节操作、日志、事件循环、定时器、线程池等基础设施。

## 2. 工程列表

| 工程 | 说明 |
|------|------|
| `bytes_basic` | Bytes 基本操作（创建、访问、比较） |
| `bytes_advanced` | Bytes 高级用法（压缩、Base64、CRC-32、十六进制、字节序） |
| `bytes_zerocopy` | Bytes 零拷贝语义 |
| `memory_pool` | MemoryPool 分级金字塔内存池 |
| `logger_basic` | 日志基本配置 |
| `logger_advanced` | 日志高级用法 |
| `message_loop_basic` | MessageLoop 事件循环入门 |
| `message_loop_advanced` | MessageLoop 高级用法 |
| `message_loop_coroutine` | MessageLoop + C++20 协程综合示例（`ENABLE_CXX_STD_20=ON`） |
| `timer_basic` | Timer 定时器入门 |
| `timer_advanced` | Timer 高级用法 |
| `elapsed_timer` | ElapsedTimer 耗时测量 |
| `deadline_timer` | 截止时间检测器 |
| `thread_pool` | 线程池任务调度 |
| `spin_lock` | 自旋锁 |
| `graph_task` | DAG 任务图调度 |
| `cancellation` | 协作取消三件套（`CancellationSource` / `Token` / `Registration` + `OperationCancelled`） |
| `task_handle` | Tracked 任务投递（`post_task_handle` / `PostTaskOptions` / `TaskExecutionState`） |
| `multi_loop` | 多事件循环协同 |
| `object_pool` | 对象池复用 |
| `process` | 进程信息查询 |
| `schedule` | 调度器 |
| `utils` | 工具函数（路径、线程、信号等） |

## 3. 相关文档

详细原理参见 [doc/11-base-library.md](../../doc/11-base-library.md)。
