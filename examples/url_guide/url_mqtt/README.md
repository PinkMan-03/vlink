# URL MQTT -- mqtt:// MQTT 物联网传输详解

## 1. 概述

MQTT 是面向受限设备和低带宽网络的轻量级 pub/sub 协议。VLink 使用 Eclipse Paho MQTT C 库作为后端。与 DDS 不同，MQTT 需要一个中心化的 Broker。

```
mqtt://address[?event=name&domain=N&qos=0|1|2][#broker_uri]
```

## 2. URL 参数详解

| 参数 | 位置 | 默认值 | 说明 |
|------|------|--------|------|
| address | host/path | (必填) | MQTT 主题路径 |
| event | ?event= | (空) | 可选二级过滤器 |
| domain | ?domain= | 0 | 域/命名空间 |
| qos | ?qos= | 1 | MQTT QoS 级别：0/1/2 |
| fragment | # | `VLINK_MQTT_BROKER` | Broker URI 覆盖 |

## 3. MQTT QoS 三级

| QoS | 名称 | 投递保证 | 适用场景 |
|-----|------|---------|---------|
| 0 | At most once | 最多一次，可能丢失 | 高频传感器数据 |
| 1 | At least once | 至少一次，可能重复 | 控制命令、状态更新（默认） |
| 2 | Exactly once | 恰好一次，四步握手 | 金融交易、关键告警 |

## 4. 环境变量

| 变量 | 说明 | 默认值 |
|------|------|--------|
| `VLINK_MQTT_BROKER` | 默认 Broker URI | `tcp://localhost:1883` |
| `VLINK_MQTT_CLIENT_ID` | 客户端 ID 前缀 | `vlink_mqtt` |
| `VLINK_MQTT_QOS` | 默认 QoS 级别 | 1 |
| `VLINK_MQTT_KEEPALIVE` | 心跳间隔（秒） | 60 |
| `VLINK_MQTT_DOMAIN` | 默认域 ID | 0 |

## 5. 关键代码分析

### 5.1 Broker URI 覆盖

```cpp
// 通过 fragment 指定 Broker
"mqtt://home/temperature?qos=1#tcp://192.168.1.100:1883"
// 或通过环境变量
Utils::set_env("VLINK_MQTT_BROKER", "tcp://mqtt.example.com:1883");
```

Broker URI 格式：
- `tcp://host:1883` -- 未加密 TCP
- `ssl://host:8883` -- TLS 加密
- `ws://host:9001` -- WebSocket
- `wss://host:9001` -- 安全 WebSocket

### 5.2 QoS 选择

```cpp
"mqtt://sensor/temperature?qos=0"  // 高频，允许丢失
"mqtt://control/command?qos=1"     // 重要命令，至少一次
"mqtt://payment/transaction?qos=2" // 关键事务，恰好一次
```

### 5.3 客户端 ID

```cpp
Utils::set_env("VLINK_MQTT_CLIENT_ID", "my_robot");
// 实际 ID: my_robot_<pid>_<counter>
```

MQTT Broker 要求每个客户端有唯一 ID。VLink 自动附加进程 ID 和计数器。

## 6. 前置条件

需要运行 MQTT Broker（如 Eclipse Mosquitto）：
```bash
mosquitto -d -p 1883
```

## 7. 编译与运行

```bash
cmake -B build -S . -DCMAKE_PREFIX_PATH=/path/to/vlink/install
cmake --build build --target example_url_mqtt
./build/output/bin/example_url_mqtt
```

## 8. 预期输出

```
[I] === Example 1: Basic MQTT topic ===
[I]   transport:mqtt
[I]   host:home
[I]   path:/temperature
[I] === Example 3: QoS levels ===
[I]   QoS 0:0 (at most once)
[I]   QoS 1:1 (at least once)
[I]   QoS 2:2 (exactly once)
[I] === Example 4: Broker URI override ===
[I]   fragment (broker):tcp://192.168.1.100:1883
...
```

## 9. 扩展思考

- MQTT 适合边缘设备到云端的数据上传，带宽占用极低。
- QoS 2 的四步握手增加了延迟，仅在需要精确一次投递时使用。
- 不同 `mqtt://` URL 可以通过 fragment 连接不同 Broker，实现多 Broker 架构。
- MQTT 5.0 支持主题别名和消息过期时间，VLink 未来版本可能会支持这些特性。
- `MqttConf` 支持直接构造：`MqttConf(address, event, domain, qos, fragment)`。
