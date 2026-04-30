# serialization/ -- 序列化类型示例

VLink 支持的各种序列化类型的独立演示。框架根据模板参数自动选择序列化策略。

| 工程 | 说明 |
|------|------|
| `bytes_type` | 原始字节（Bytes）类型 |
| `string_type` | 字符串（std::string）类型 |
| `pod_type` | POD 标量类型（直接 memcpy） |
| `stream_type` | 流式序列化 |
| `custom_type` | 自定义类型序列化 |
| `dynamic_data` | DynamicData 动态类型 |
| `intra_data` | 进程内零拷贝传输类型 |

## 相关文档

详细原理参见 [doc/06-serialization.md](../../doc/06-serialization.md)。
