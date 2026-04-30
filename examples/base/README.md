# base/ -- 基础库示例

VLink 基础工具库（`vlink/base/`）各组件的独立用法演示，包括字节操作、日志、事件循环、定时器、线程池等基础设施。

| 工程 | 说明 |
|------|------|
| `bytes_basic` | Bytes 基本操作（创建、访问、比较） |
| `bytes_advanced` | Bytes 高级用法（切片、拼接、转换） |
| `bytes_zerocopy` | Bytes 零拷贝语义 |
| `logger_basic` | 日志基本配置 |
| `logger_advanced` | 日志高级用法 |
| `message_loop_basic` | MessageLoop 事件循环入门 |
| `message_loop_advanced` | MessageLoop 高级用法 |
| `timer_basic` | Timer 定时器入门 |
| `timer_advanced` | Timer 高级用法 |
| `elapsed_timer` | ElapsedTimer 耗时测量 |
| `deadline_timer` | 截止时间检测器 |
| `thread_pool` | 线程池任务调度 |
| `spin_lock` | 自旋锁 |
| `graph_task` | DAG 任务图调度 |
| `multi_loop` | 多事件循环协同 |
| `object_pool` | 对象池复用 |
| `process` | 进程信息查询 |
| `schedule` | 调度器 |
| `utils` | 工具函数（路径、线程、信号等） |

## 相关文档

详细原理参见 [doc/11-base-library.md](../../doc/11-base-library.md)。
