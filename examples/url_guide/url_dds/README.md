# URL DDS -- dds:// 和 ddsc:// DDS 传输详解

## 1. 概述

DDS (Data Distribution Service) 是 VLink 的主要跨网络传输协议。VLink 支持两个 DDS 后端：`dds://` (Fast-DDS/eProsima) 和 `ddsc://` (CycloneDDS/Eclipse)。

```
dds://topic[?domain=N&depth=N&qos=profile_name]
ddsc://topic[?domain=N&depth=N&qos=profile_name]
```

## 2. URL 参数详解

| 参数 | 位置 | 默认值 | 说明 |
|------|------|--------|------|
| topic | host/path | (必填) | DDS 话题名，如 `vehicle/speed` |
| domain | ?domain= | 0 或 `VLINK_DDS_DOMAIN` | DDS Domain ID，不同域完全隔离 |
| depth | ?depth= | 传输默认 | 历史深度 |
| qos | ?qos= | (无) | 命名 QoS 配置文件 |
| qos_ext | ?part/topic/pub/sub/writer/reader= | (无) | 高级 per-entity QoS XML |

## 3. 环境变量

| 变量 | 说明 | 默认值 |
|------|------|--------|
| `VLINK_DDS_DOMAIN` | 所有 `dds://` URL 的默认 domain ID | 0 |
| `VLINK_DDS_BIND` | 将 `dds://` / `ddsf://` URL 重定向到指定 DDS 后端 scheme（如 `ddsc`） | (不重定向) |

## 4. 关键代码分析

### 4.1 Domain ID -- 网络隔离

```cpp
// URL 中指定 domain
Publisher<int> pub("dds://vehicle/speed?domain=42");

// 或通过环境变量设置默认 domain
Utils::set_env("VLINK_DDS_DOMAIN", "42");
Publisher<int> pub("dds://vehicle/speed");  // 使用 domain=42
```

不同 domain 的参与者完全不可见，类似于 VLAN 隔离。在多团队协作或多环境部署中，domain 是最简单的隔离手段。

### 4.2 网卡绑定

```cpp
Utils::set_env("VLINK_DDS_BIND", "ddsc");
```

`VLINK_DDS_BIND` 是 URL 层的 DDS 后端选择开关，值是 `dds` / `ddsf` / `ddsc` / `ddsr` / `ddst` 这类 scheme 名。网卡/IP 绑定请使用 `VLINK_DDS_IP` 或具体后端的网络配置。

### 4.3 QoS 配置文件

```cpp
Qos sensor_qos;
sensor_qos.valid = true;
sensor_qos.reliability.kind = Qos::Reliability::kBestEffort;
sensor_qos.history.depth = 20;
// DdsConf::register_qos("fast_sensor", sensor_qos);
// Publisher<SensorData> pub("dds://sensor/imu?qos=fast_sensor");
```

QoS 配置文件将策略与 URL 分离：
- 注册一次，多处引用
- 运行时可通过不同 URL 切换策略
- 内置配置文件：`event`、`method`、`field`、`sensor`、`parameter`、`service`

### 4.4 dds:// vs ddsc://

| 特性 | dds:// (Fast-DDS) | ddsc:// (CycloneDDS) |
|------|-------------------|---------------------|
| 实现 | eProsima Fast-DDS | Eclipse CycloneDDS |
| QoS XML | 支持 per-entity XML | 不支持 qos_ext |
| 话题注册 | 支持 register_topic | 不支持 |
| 性能 | 大消息优势 | 小消息延迟更低 |
| URL 格式 | 相同 | 相同 |

### 4.5 多域部署示例

```
Domain 0: 底盘动力  dds://chassis/brake_pressure?domain=0
Domain 1: 信息娱乐  dds://media/track_info?domain=1
Domain 2: ADAS 感知  dds://perception/objects?domain=2
Domain 3: V2X 通信   dds://v2x/bsm?domain=3
```

## 5. 编译与运行

```bash
cmake -B build -S . -DCMAKE_PREFIX_PATH=/path/to/vlink/install
cmake --build build --target example_url_dds
./build/output/bin/example_url_dds
```

## 6. 预期输出

```
[I] === Example 1: Basic DDS topic ===
[I]   host:vehicle
[I]   path:/speed
[I] === Example 2: Domain ID ===
[I]   domain ID:42
[I] === Example 4: VLINK_DDS_BIND ===
[I]   Available IPv4 addresses:
[I]     192.168.1.100
...
```

## 7. 扩展思考

- DDS 不需要 Broker 或中央服务器，参与者通过多播自动发现。
- 对于大规模系统，网卡/IP 选择请使用 `VLINK_DDS_IP` 或后端专用网络配置；`VLINK_DDS_BIND` 只负责选择 DDS 后端 scheme。
- `?depth=` 影响 QoS 历史缓冲大小：较大的 depth 有利于迟到订阅者接收历史数据，但增加内存占用。
- Fast-DDS 支持 `DdsConf::load_global_qos_file()` 加载 XML 配置，必须在创建任何 `dds://` 节点之前调用。
