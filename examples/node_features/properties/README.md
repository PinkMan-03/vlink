# 节点属性配置示例

## 概述

本示例演示 VLink 节点的属性配置 API，包括 `set_property()` / `get_property()` 设置 QoS 参数、`set_ser_type()` 设置运行时 `ser_type + schema_type` 元数据、`get_schema_type()` 读取当前 family，以及 `set_discovery_enabled()` 控制发现可见性。

## 核心概念

### 属性系统

VLink 节点通过键值对字符串配置底层传输参数。属性必须在 `init()` 之前设置（使用 `kWithoutInit` 延迟初始化模式）。

### 工作流程

```
1. 创建节点 (kWithoutInit)
2. set_property("qos.xxx", "value")
3. set_ser_type("demo.proto.Message", SchemaType::kProtobuf)
4. set_ser_type("demo.proto.MessageV2")  // 只改具体类型名，保留 protobuf family
5. set_discovery_enabled(true/false)
6. init()  -- 属性在此时应用到传输后端
```

## 关键 API

### set_property / get_property

```cpp
pub.set_property("qos.reliability.kind", "1");  // Reliable
pub.set_property("qos.history.depth", "50");
std::string value = pub.get_property("qos.history.depth");  // "50"
```

### set_ser_type

```cpp
pub.set_ser_type("demo.proto.Message", vlink::SchemaType::kProtobuf);
pub.set_ser_type("demo.proto.MessageV2");
```

`ser_type` 与 `schema_type` 都是运行时元数据，会传递给录制系统、代理和发现服务。VLink 不验证负载格式，但运行时解码链路会依赖这两项信息。

补充行为：

- 当你已经同时知道 `ser_type` 和 `schema_type` 时，调用 `set_ser_type(ser_type, schema_type)`
- 第二个参数为默认值 `SchemaType::kUnknown` 时，不会主动覆盖当前 family
- raw / zerocopy family 会按 `ser_type` 自动同步
- 如果当前 family 已明确是 `kProtobuf` 或 `kFlatbuffers`，仅修改具体类型名不会把 family 清空
- 如果当前 family 是 raw / zerocopy，而新的 `ser_type` 不再属于这两个 family，则会回退到 `kUnknown`
- `set_ser_type("")` 会连同 `schema_type` 一起清空

### set_discovery_enabled

```cpp
pub.set_discovery_enabled(false);  // 对 ProxyServer/发现服务隐藏此节点
```

## 可用属性键

| 属性键 | 值范围 | 描述 |
|--------|--------|------|
| `qos.reliability.kind` | 0/1 | BestEffort/Reliable |
| `qos.reliability.block_time` | 毫秒 | 最大阻塞时间 |
| `qos.reliability.heartbeat_time` | 毫秒 | 心跳间隔 |
| `qos.history.kind` | 0/1 | KeepLast/KeepAll |
| `qos.history.depth` | 整数 | 历史深度 |
| `qos.durability.kind` | 0-3 | Volatile/TransientLocal/Transient/Persistent |
| `qos.publish_mode.kind` | 0/1 | Sync/ASync |
| `qos.deadline.period` | 毫秒 | 截止时间 |
| `qos.lifespan.duration` | 毫秒 | 生存时间 |
| `qos.additions.priority` | 1/2/4/6/7 | RealTime/High/Normal/Low/Background |
| `qos.additions.is_express` | true/false | 快速投递 |

## 编译与运行

```bash
cd build
cmake .. && make example_properties
./output/bin/example_properties
```

## 注意事项

- 属性必须在 `init()` 之前设置才能生效
- 使用 `kWithoutInit` 构造模式来延迟初始化
- 属性值都是字符串类型
- `set_discovery_enabled(false)` 可以隐藏内部/调试主题
- 不同传输协议可能忽略不支持的 QoS 参数
