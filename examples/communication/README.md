# communication/ -- 通信模型示例

VLink 三种通信模型（Event、Method、Field）的基础和进阶用法。

| 工程 | 说明 |
|------|------|
| `event_basic` | Publisher/Subscriber 基本 Pub-Sub |
| `event_advanced` | 多订阅者、连接检测、强制发布等 |
| `field_basic` | Setter/Getter 基本状态同步 |
| `field_advanced` | 推送模式、轮询模式、监听回调 |
| `method_sync` | Server/Client 同步 RPC 调用 |
| `method_async` | Server/Client 异步 RPC 调用 |
| `method_fire_forget` | Server/Client 单向请求（无返回值） |

## 1. 相关文档

详细原理参见：

- [doc/03-event-model.md](../../doc/03-event-model.md) -- 事件模型（Publisher / Subscriber）
- [doc/04-method-model.md](../../doc/04-method-model.md) -- 方法模型（Client / Server）
- [doc/05-field-model.md](../../doc/05-field-model.md) -- 字段模型（Setter / Getter）
