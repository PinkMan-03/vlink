# QoS 历史深度示例

![QoS KeepLast Depth Demo](images/qos-keep-last-depth.png)

## 1. 概述

本示例演示 History 深度对消息保留的影响，包括 KeepLast(depth=1)、KeepLast(depth=10)、KeepAll 模式、URL `depth` 参数，以及 KeepAll 与 ResourceLimits 的配合。

## 2. History 模式

| 模式 | 行为 |
|------|------|
| KeepLast(depth=N) | 保留最近 N 条消息 |
| KeepAll | 保留所有消息（受 ResourceLimits 限制） |

## 3. URL depth 参数

可以不注册 QoS profile，直接在 DDS URL 中设置每个端点的 history depth：

```
dds://vehicle/speed?depth=1
dds://sensor/lidar?depth=20
dds://sensor/lidar?domain=5&depth=20
```

## 4. 编译与运行

```bash
cmake -B build -S . -DCMAKE_PREFIX_PATH=/path/to/vlink/install
cmake --build build --target example_qos_history_depth
./build/output/bin/example_qos_history_depth
```

## 5. 注意事项

- KeepAll 模式下必须配合 ResourceLimits 防止内存无限增长
- History 深度在 `intra://` 上效果有限，在 DDS 传输上最为明显
- depth=1 适合状态值（只关心最新值），depth>1 适合事件流
