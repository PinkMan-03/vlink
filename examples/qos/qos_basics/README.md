# QoS 基础示例

## 概述

本示例演示 VLink 的服务质量（QoS）配置系统，包括创建自定义 `Qos` 结构体、配置所有子策略，以及通过 `set_property` API 应用 QoS 参数。

## 核心概念

### Qos 结构体

VLink 的 `Qos` 结构体包含以下子策略：

| 子策略 | 描述 | 主要参数 |
|--------|------|---------|
| Reliability | 可靠性 | kind (BestEffort/Reliable), block_time, heartbeat_time |
| History | 历史 | kind (KeepLast/KeepAll), depth |
| Durability | 持久性 | kind (Volatile/TransientLocal/Transient/Persistent) |
| PublishMode | 发布模式 | kind (Sync/ASync) |
| Liveliness | 活跃度 | kind, duration |
| DestinationOrder | 目标排序 | kind (ReceptionTimestamp/SourceTimestamp) |
| Ownership | 所有权 | kind (Shared/Exclusive) |
| Deadline | 截止时间 | period (ms) |
| Lifespan | 生存期 | duration (ms) |
| LatencyBudget | 延迟预算 | duration (ms) |
| ResourceLimits | 资源限制 | max_samples, max_instances, max_samples_per_instance |
| Additions | 扩展选项 | priority, is_express |

### 两种配置方式

1. **Qos 结构体**：直接构造并填充字段值
2. **set_property API**：通过字符串键值对逐项设置

### valid 标志

`qos.valid` 必须设为 `true` 才能被传输层应用。默认构造的 Qos 有 `valid=false`。

## 关键代码

```cpp
// 方式 1：结构体
Qos sensor_qos;
sensor_qos.valid = true;
sensor_qos.reliability.kind = Qos::Reliability::kBestEffort;
sensor_qos.history.depth = 20;

// 方式 2：set_property
pub.set_property("qos.reliability.kind", "1");  // Reliable
pub.set_property("qos.history.depth", "50");
```

## 编译与运行

```bash
cd build
cmake .. && make example_qos_basics
./output/bin/example_qos_basics
```

## 源文件说明

| 文件 | 描述 |
|------|------|
| `qos_basics.cc` | 主程序：QoS 配置、注册和使用示例 |
| `qos_helpers.h` | 辅助函数：`print_qos()` 和 `print_profile_summary()` |

## 头文件

```cpp
#include "qos_helpers.h"              // 本示例的 QoS 打印辅助函数
#include <vlink/extension/qos.h>
#include <vlink/extension/qos_profile.h>
```

## 注意事项

- 不同传输协议可能忽略不支持的 QoS 参数
- `intra://` 对 QoS 参数的支持有限
- DDS 传输完整支持所有 QoS 策略
- `set_property` 必须在 `init()` 之前调用
- Qos 名称字段最多 32 字节

## 相关文档

详细原理参见 [doc/08-qos.md](../../../doc/08-qos.md)。
