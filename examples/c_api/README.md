# c_api/ -- C API 示例

纯 C 语言绑定示例，覆盖三种通信模型。

当前版本的 C API 创建接口统一使用 `vlink_schema_info_t`，在创建时一次性传入 `ser + schema`。
这里的 `schema` 是精确的运行时 schema family，会被直接映射到底层 C++ `SchemaType`，供 discovery、proxy、bag、viewer 等链路继续传播和消费。

| 工程 | 说明 |
|------|------|
| `c_pubsub` | C API Publisher/Subscriber（Event 模型） |
| `c_rpc` | C API Server/Client（Method 模型） |
| `c_field` | C API Setter/Getter（Field 模型） |

## 1. 相关文档

详细原理参见 [doc/18-c-api.md](../../doc/18-c-api.md)。
