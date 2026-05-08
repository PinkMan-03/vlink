# QoS 预设配置文件示例

## 概述

本示例演示 VLink 的 QoS 预设配置文件（QosProfile），包括内置预设、名称查找、自定义配置拷贝和 `VLINK_QOS_CONFIG` 环境变量。

## 内置预设

| 预设 | 可靠性 | 历史 | 用途 |
|------|--------|------|------|
| kEvent | Reliable | KeepLast(10) | 控制事件 |
| kMethod | Reliable | KeepAll(1) | RPC 调用 |
| kField | Reliable | KeepLast(1) | 状态同步 |
| kSensor | BestEffort | KeepLast(20) | 传感器数据 |
| kParameter | Reliable | KeepLast(1000) | 配置参数 |
| kService | Reliable | KeepLast(10) | 服务通信 |
| kClock | BestEffort | KeepLast(1) | 时间同步 |
| kLarge | Reliable | KeepLast(500) | 大数据（地图等） |

## 使用方式

```cpp
// 直接使用预设
Qos qos = QosProfile::kSensor;

// 自定义副本
Qos custom = QosProfile::kSensor;
custom.history.depth = 50;

// 名称查找
const auto& map = QosProfile::get_available_qos_map();
auto it = map.find("sensor");
```

## VLINK_QOS_CONFIG

```bash
export VLINK_QOS_CONFIG=/path/to/qos_config.json
```

从 JSON 文件加载自定义 QoS 配置文件，与内置预设合并。

## 编译与运行

```bash
cd build
cmake .. && make example_qos_profiles
./output/bin/example_qos_profiles
```

## 选择指南

| 场景 | 推荐预设 |
|------|---------|
| 摄像头/LiDAR | kSensor |
| 控制指令 | kEvent |
| RPC 调用 | kMethod |
| 状态同步 | kField |
| 诊断数据 | kPoor |
| 时间同步 | kClock |
| 地图数据 | kLarge |
