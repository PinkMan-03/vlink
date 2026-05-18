# URL Basics -- VLink URL 格式解析与传输选择指南

## 1. 概述

本示例演示 VLink URL 的完整格式解析、各传输协议的 URL 结构，以及如何通过 `UrlParser` 解构和重建 URL。VLink 的核心设计理念是**传输无关**：只需更换 URL 的 transport 部分，即可切换底层传输协议，应用代码无需任何修改。

```
transport://host/path[?key=value&key=value][#fragment]
  |        |   |         |                   |
  传输协议  主地址 路径     查询参数             片段/模式
```

## 2. URL 格式详解

### 2.1 完整格式

```
transport://host/path?param1=value1&param2=value2#fragment
```

| 组件 | 说明 | 示例 |
|------|------|------|
| transport | 传输协议 | `intra`, `shm`, `shm2`, `dds`, `ddsc`, `ddsr`, `ddst`, `zenoh`, `someip`, `mqtt`, `fdbus`, `qnx` |
| host | 主题/服务的主地址 | `sensor`, `vehicle`, `0x1234` |
| path | 主题的子路径 | `/lidar`, `/speed`, `/0x5678` |
| query | 传输特定的键值参数 | `domain=1&depth=16&qos=sensor` |
| fragment | 模式或附加提示 | `queue`, `direct`, `tcp://broker:1883` |

### 2.2 传输协议速查表

| Transport | 传输 | 范围 | 外部依赖 | 典型场景 |
|--------|------|------|---------|---------|
| `intra://` | 进程内 | 同进程 | 无 | 单元测试、模块解耦 |
| `shm://` | 共享内存 (Iceoryx) | 同机器 | RouDi 守护进程 | 高性能 IPC |
| `shm2://` | 共享内存 (Iceoryx2) | 同机器 | 无（Iceoryx2 无中央守护） | 改进版 shm |
| `dds://` | Fast-DDS | 跨网络 | 无（内置发现） | 车载/工业网络 |
| `ddsc://` | CycloneDDS | 跨网络 | 无 | 替代 DDS 后端 |
| `ddsr://` | RTI DDS | 跨网络 | RTI Connext DDS（商用许可） | 安全关键/航空航天 |
| `ddst://` | TravoDDS | 跨网络 | TravoDDS | 国内 DDS 后端 |
| `zenoh://` | Eclipse Zenoh | 跨网络/WAN | Zenoh 路由器（可选） | 机器人、边缘计算 |
| `someip://` | SOME/IP | 跨网络 | vsomeip 守护进程 | AUTOSAR 车载以太网 |
| `mqtt://` | MQTT | 跨网络/WAN | MQTT Broker | IoT、远程监控 |
| `fdbus://` | FDBus | 同机器/网络 | FDBus 守护进程 | 嵌入式 IPC |
| `qnx://` | QNX IPC | 同机器 | QNX 系统 | QNX 实时系统 |

## 3. 关键代码分析

### 3.1 UrlParser 解析 URL

```cpp
UrlParser parser("dds://vehicle/speed?domain=42&qos=sensor");
parser.get_transport();     // "dds"
parser.get_host();       // "vehicle"
parser.get_path();       // "/speed"
parser.get_query();      // "domain=42&qos=sensor"
parser.get_fragment();   // ""
```

`UrlParser` 将 URL 字符串拆分为标准组件。`get_query_dictionary()` 返回解析后的键值对 map。

### 3.2 从组件构建 URL

```cpp
std::map<UrlParser::Component, std::string> components;
components[UrlParser::Component::kTransport] = "dds";
components[UrlParser::Component::kHost] = "vehicle";
components[UrlParser::Component::kPath] = "/telemetry/gps";
components[UrlParser::Component::kQuery] = "domain=5&qos=sensor";
UrlParser built(components, UrlParser::Category::kHierarchical, true);
```

可以从独立组件构建 URL 对象，适用于运行时动态生成 URL 的场景。

### 3.3 覆盖 URL 组件

```cpp
UrlParser original("dds://vehicle/speed?domain=0&qos=sensor");
std::map<UrlParser::Component, std::string> overrides;
overrides[UrlParser::Component::kQuery] = "domain=99&qos=best";
UrlParser modified(original, overrides);
```

基于现有 URL 创建修改副本，无需解析和重新拼接字符串。

### 3.4 传输无关 API 验证

```cpp
Subscriber<std::string> sub("intra://demo/url_basics");
Publisher<std::string> pub("intra://demo/url_basics");
pub.publish("Hello!");
// 切换传输只需改 URL:
// Publisher<std::string> pub("dds://demo/url_basics");
// Publisher<std::string> pub("shm://demo/url_basics");
```

所有 VLink 通信原语（Publisher、Subscriber、Setter、Getter、Server、Client）共享统一的 URL 构造接口。

### 3.5 URL 分类工具

```cpp
Url::is_local_type("intra://x");   // true (进程内)
Url::is_local_type("dds://x");     // false (跨网络)
Url::is_intra_type("intra://x");   // true
Url::is_shm_type("shm://x");      // true
Url::get_sort_index("intra://x");  // 排序索引（用于确定性初始化顺序）
```

## 4. 各传输 URL 格式速查

```
intra://address[?event=X&pipeline=N][#queue|#direct]
shm://address[?event=X&domain=N&depth=N&history=N&wait=0|1]
dds://topic[?domain=N&depth=N&qos=profile]
ddsc://topic[?domain=N&depth=N&qos=profile]
zenoh://address[?event=X&domain=N&qos=profile][#fragment]
someip://service_id/instance_id?method=M
someip://service_id/instance_id?groups=G&event=E[&field=1]
mqtt://address[?event=X&domain=N&qos=0|1|2][#broker_uri]
fdbus://address[?event=X][#svc|#ipc]
qnx://address[?event=X]
```

## 5. 编译与运行

```bash
mkdir build && cd build
cmake .. -DCMAKE_PREFIX_PATH=/path/to/vlink/install
make example_url_basics
./output/bin/example_url_basics
```

## 6. 预期输出

```
[I] ========================================
[I] URL:intra://sensor/lidar
[I]   transport:   intra
[I]   host:     sensor
[I]   path:     /lidar
[I]   query:
[I]   fragment:
...
[I] Built URL: dds://vehicle/telemetry/gps?domain=5&qos=sensor
[I] is_local_type('intra://x'):1
[I] is_local_type('dds://x'):0
```

## 7. 扩展思考

- VLink 的传输无关设计意味着在开发阶段可以使用 `intra://` 进行快速测试，部署时切换为 `dds://` 或 `shm://`。
- `UrlRemap` 可以在运行时通过 JSON 文件切换 URL，无需重新编译（参见 `url_remap` 示例）。
- 每种传输的详细参数请参见对应的 `url_xxx` 示例（`url_intra`、`url_dds`、`url_shm` 等）。
- `get_sort_index()` 用于确定性初始化顺序，确保 intra:// 节点先于跨进程节点初始化。

## 8. 相关文档

详细原理参见 [doc/07-transport.md](../../../doc/07-transport.md)。
