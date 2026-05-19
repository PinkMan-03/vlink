# URL Zenoh -- zenoh:// Eclipse Zenoh 传输详解

## 1. 概述

Eclipse Zenoh 是面向机器人、边缘计算和云基础设施的统一数据管理协议。它支持点对点和路由式 pub/sub，基于键表达式（Key Expression）寻址。

```
zenoh://address[?event=name&domain=N&qos=profile_name][#fragment]
```

## 2. URL 参数详解

| 参数 | 位置 | 默认值 | 说明 |
|------|------|--------|------|
| address | host/path | (必填) | Zenoh 键表达式，如 `robot/arm/joint1` |
| event | ?event= | (空) | 可选二级过滤器 |
| domain | ?domain= | 0 | 会话/域标识符，不同域创建独立会话 |
| qos | ?qos= | (无) | 命名 QoS 配置文件 |
| fragment | # | (空) | 传输提示，如 TCP 连接地址 |

## 3. Zenoh vs DDS 对比

| 特性 | DDS (dds://) | Zenoh (zenoh://) |
|------|-------------|-----------------|
| 发现机制 | SPDP/SEDP 多播 | Scouting/gossip |
| 寻址 | topic + domain | 键表达式 + domain |
| 网络范围 | 通常仅 LAN | LAN + WAN + Cloud |
| 资源占用 | 较重 | 较轻 |
| 通配符 | 仅内容过滤 | 键表达式模式匹配 |
| 传输协议 | UDP 多播 | TCP/UDP/QUIC/WebSocket |

## 4. 关键代码分析

### 4.1 层次化键表达式

```
zenoh://robot/arm/joint1/position
zenoh://robot/arm/joint2/position
zenoh://robot/base/odometry
```

Zenoh 键表达式支持层次化路径，类似文件系统。在 Zenoh 原生客户端中可使用通配符 (`*`, `**`) 订阅。

### 4.2 Domain 隔离

```cpp
"zenoh://vehicle/speed?domain=0"  // 生产环境
"zenoh://vehicle/speed?domain=1"  // 仿真环境
"zenoh://vehicle/speed?domain=2"  // 测试环境
```

不同 domain 创建独立的 Zenoh 会话。

### 4.3 Fragment -- 传输提示

```cpp
"zenoh://vehicle/speed#tcp/192.168.1.1:7447"
"zenoh://vehicle/speed#udp/192.168.1.1:7447"
"zenoh://vehicle/speed#unixsock-stream//tmp/zenoh"
```

fragment 部分传递给 Zenoh 会话作为连接地址，支持 TCP、UDP、Unix Socket 等。

### 4.4 QoS 配置

```cpp
Qos reliable_qos;
reliable_qos.valid = true;
reliable_qos.reliability.kind = Qos::Reliability::kReliable;
reliable_qos.history.depth = 10;
// ZenohConf::register_qos("zenoh_reliable", reliable_qos);
// Subscriber<Msg> sub("zenoh://vehicle/speed?qos=zenoh_reliable");
```

## 5. 编译与运行

```bash
cmake -B build -S . -DCMAKE_PREFIX_PATH=/path/to/vlink/install
cmake --build build --target example_url_zenoh
./build/output/bin/example_url_zenoh
```

## 6. 预期输出

```
[I] === Example 1: Basic key expression ===
[I]   transport:zenoh
[I]   host:vehicle
[I]   path:/speed
[I] === Example 4: Domain parameter ===
[I]   domain:1
[I] === Example 6: Fragment (transport hint) ===
[I]   fragment:tcp/192.168.1.1:7447
...
```

## 7. 扩展思考

- Zenoh 天然支持 WAN 穿越，适合需要跨数据中心或云边协同的场景。
- Zenoh 的键表达式通配符在 VLink URL 中按原样传递，支持灵活的主题匹配。
- 相比 DDS，Zenoh 的发现协议更轻量，启动更快，适合资源受限的嵌入式设备。
- `ZenohConf` 支持直接构造：`ZenohConf(address, event, domain, qos, fragment)`。
