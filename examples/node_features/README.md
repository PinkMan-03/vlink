# node_features/ -- 节点高级特性示例

VLink 节点的高级功能：生命周期管理、MessageLoop 绑定、属性系统、状态监控。

| 工程 | 说明 |
|------|------|
| `lifecycle` | 节点生命周期管理（init/deinit/interrupt） |
| `message_loop_binding` | 将节点绑定到 MessageLoop 进行回调调度 |
| `properties` | 节点属性（set_property/get_property） |
| `status_monitoring` | 节点状态监控（register_status_handler） |

## 相关文档

详细原理参见 [doc/02-node-lifecycle.md](../../doc/02-node-lifecycle.md)。
