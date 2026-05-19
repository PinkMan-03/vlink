# quickstart/ -- 快速入门

## 1. 概述

最小化入门示例，每个示例演示一种通信模型的基本用法。`hello_pubsub` 是单进程示例，默认使用 `intra://` 且无需外部依赖；`hello_rpc` 和 `hello_field` 拆成两个独立进程，双终端运行时应通过 README 中的环境变量切换到 `dds://`、`shm://` 等跨进程传输。

## 2. 工程列表

| 工程 | 说明 |
|------|------|
| `hello_pubsub` | 发布/订阅（Event 模型）入门 |
| `hello_rpc` | 请求/响应（Method 模型）入门 |
| `hello_field` | 状态读写（Field 模型）入门 |

## 3. 相关文档

详细原理参见：

- [doc/03-event-model.md](../../doc/03-event-model.md) -- 事件模型（Publisher / Subscriber）
- [doc/04-method-model.md](../../doc/04-method-model.md) -- 方法模型（Client / Server）
- [doc/05-field-model.md](../../doc/05-field-model.md) -- 字段模型（Setter / Getter）
