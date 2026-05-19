# 16. 代理监控

## 16.1 概述

VLink Proxy 是一个基于 DDS 传输的中间层代理系统，专为跨网段通信监控、数据录制与回放、远程注入等场景设计。它由三个核心组件构成。

> **相关文档**：可视化工具 vlink-viewer 使用 ProxyAPI 连接参见 [14-viewer.md](14-viewer.md)；CLI 监控工具参见 [13-cli-tools.md](13-cli-tools.md#136-vlink-monitor--实时通信状态监控)；服务发现机制参见 [17-discovery.md](17-discovery.md)。

核心组件：

- **`vlink-proxy`**：独立运行的代理守护进程（可执行文件）
- **`ProxyServer`**：代理服务端库，可嵌入到应用进程中
- **`ProxyAPI`**：代理客户端库，供监控工具、CLI 工具、上位机连接使用

---

## 16.2 为什么需要代理层

VLink 的原生传输（shm、dds 等）要求通信双方在同一网段或 DDS 域内可直接发现彼此。在以下场景中需要引入代理层：

| 场景                     | 说明                                                         |
| ------------------------ | ------------------------------------------------------------ |
| 跨网段/跨 VLAN 通信      | 测试台架与开发机、调试机与目标机之间存在路由/防火墙隔离（单播 DDS 发现，非 NAT 穿透）|
| 远程监控与可视化         | 上位机工具（如 vlink-monitor）需要实时查看话题数据和统计信息 |
| 数据录制与回放           | 代理服务器代为订阅所有话题并将数据转发给录制客户端           |
| 数据注入与仿真           | 回放或仿真工具通过代理向车端节点注入消息                     |
| 跨进程自动路由（Auto 模式）| 在无需手动配置路由的情况下自动订阅并转发指定话题             |

---

## 16.3 架构概览

![Proxy 架构](images/proxy-architecture.png)

### 16.3.1 通信信道说明

| 信道        | 方向              | 传输方式           | 安全加密 | 说明                         |
| ----------- | ----------------- | ------------------ | -------- | ---------------------------- |
| Control     | Client -> Server  | DDS（安全）        | 是       | 客户端发送模式控制指令       |
| Time        | Server -> Client  | DDS（安全）        | 是       | 心跳，每秒一次，携带版本/时间/CPU |
| InfoList    | Server -> Client  | DDS（安全）        | 是       | 每秒一次的话题统计列表       |
| Data        | 双向              | DDS 或 SHM（直连） | 否       | 原始消息载荷转发             |

---

## 16.4 通信流程与时序

![Proxy 通信时序](images/proxy-communication-sequence.png)

Proxy 系统的通信分为六个阶段：

### 16.4.1 Phase 1: 节点发现

VLink 业务节点在启动时通过 `DiscoveryReporter` 以 UDP 组播方式广播自身的元信息（URL、序列化类型、节点类型、PID、主机名）。ProxyServer 内部运行 `DiscoveryViewer`，持续监听组播地址 `239.255.0.100`，聚合所有在线节点的 URL 列表，并计算每个话题的频率、吞吐量、丢包率和延迟统计。

### 16.4.2 Phase 2: 客户端连接

ProxyServer 每秒通过 DDS 安全信道广播两类信息：
- **Time 心跳**：携带 VLINK_VERSION、主机名、CPU 占用、内存占用、系统时间（Unix epoch 微秒）和启动时长
- **InfoList**：所有已发现话题的统计数组（url、ser、status、freq、rate、loss、latency、process_list）

ProxyAPI 客户端订阅 Time 心跳，收到第一条心跳后触发 `ConnectCallback(true)`，确认连接成功。同时开始接收 InfoList，更新本地话题列表。

### 16.4.3 Phase 3: 模式控制

`kController` 角色的客户端通过 `send_control()` 向 ProxyServer 发送 Control 指令，指定工作模式和关注的 URL 列表。ProxyServer 收到后按指令订阅/取消订阅实际话题。

### 16.4.4 Phase 4: 数据转发

ProxyServer 订阅的实际话题收到消息后，将原始数据封装为 `ProxyData`（url + ser + schema + raw + timestamp + seq），通过 Data 信道（DDS 或 SHM 直连）转发给所有已连接的 ProxyAPI 客户端。客户端的 `DataCallback` 被触发。

### 16.4.5 Phase 5: 前端推送

ProxyAPI 客户端收到数据后，根据自身类型执行不同的后续处理：
- **vlink-viewer**（桌面 GUI）：通过 Qt Signal/Slot 将数据投递到 GUI 线程，渲染图像/点云/数据表
- **vlink-foxglove**（Web 可视化）：通过 `FoxgloveConverter` 将数据转换为 Foxglove FlatBuffer Schema，再通过 WebSocket 推送到 Foxglove Studio 浏览器
- **vlink-rerun**（Web 可视化）：通过 `RerunConverter` 将数据转换为 Rerun Archetype，再通过 `RecordingStream::log()` 推送到 Rerun Viewer
- **自定义工具**：在 DataCallback 中执行用户自定义逻辑

### 16.4.6 Phase 6: 断线与重连

ProxyAPI 客户端通过心跳检测连接状态。若连续 **5 秒** 未收到 Time 心跳，判定连接断开，触发 `ConnectCallback(false)`。`kController` 角色在重连成功后会自动重新发送最后一次 Control 指令，无需用户干预。

---

## 16.5 各工具在 Proxy 体系中的角色

| 工具/组件             | Proxy 角色                  | 说明                                                         |
| --------------------- | --------------------------- | ------------------------------------------------------------ |
| `vlink-proxy`         | **ProxyServer 守护进程**    | 独立运行，聚合所有传输后端数据，转发给客户端                 |
| `ProxyServer`（库）   | **嵌入式服务端**            | 可嵌入业务进程，无需独立守护进程                             |
| `vlink-viewer`        | ProxyAPI **kController**    | 桌面 GUI，按需订阅话题，实时渲染图像/点云/数据               |
| `vlink-player`        | 回放 GUI / 进程编排器       | Bag 回放界面；通过拉起 `vlink-proxy` 等子进程完成数据注入与联动 |
| `vlink-analyzer`      | ProxyAPI **kListener**      | 数据分析，只读监听统计信息                                   |
| `vlink-foxglove`      | ProxyAPI **kController**    | Foxglove 桥接，按需订阅（kAuto 模式），通过 WebSocket 推送   |
| `vlink-rerun`         | ProxyAPI **kController**    | Rerun 桥接，全量订阅（kAutoAndObserveAll），通过 gRPC 推送   |
| `vlink-monitor`       | 不使用 Proxy（DiscoveryViewer）| CLI 工具，直接通过组播发现节点，不经过 Proxy                |
| `vlink-bag`           | BagReader / BagWriter       | CLI 录制、回放、检查与转换工具，不通过 ProxyAPI 控制面工作   |
| 自定义监控工具        | ProxyAPI **kController/kListener** | 用户自行开发的监控、录制、注入工具                    |

> `vlink-foxglove` 和 `vlink-rerun` 在 Proxy 体系中都是 **ProxyAPI 客户端**，它们的角色等同于 `vlink-viewer`——通过 ProxyAPI 连接到 ProxyServer 获取数据。区别在于输出端：Viewer 输出到 Qt GUI，Foxglove 输出到 WebSocket，Rerun 输出到 gRPC。

---

## 16.6 ProxyServer 说明

`ProxyServer` 是代理系统的服务端，继承自 `MessageLoop`，以事件循环方式运行。

### 16.6.1 主要职责

1. 运行 `DiscoveryViewer`，枚举 DDS 域内所有活跃的 Publisher/Subscriber/Server/Client/Setter/Getter
2. 接收来自 `ProxyAPI` 客户端的 `Control` 指令，切换工作模式
3. 每秒广播一次 `Time` 心跳（携带版本、主机名、CPU 占用、内存占用、时间戳）
4. 每秒广播一次 `InfoList`（各话题的频率、吞吐量、丢包率、延迟统计）
5. 在 Observe/Record/Auto 模式下，订阅实际话题并将数据转发给客户端
6. 在 Play/Edit/Auto 模式下，接收客户端注入的数据并发布到实际话题

### 16.6.2 单例约束

每个操作系统进程中只能存在一个 `ProxyServer` 实例。重复构造将打印 Fatal 日志并直接返回，不会初始化任何通道。

### 16.6.3 ProxyServer::Config 字段说明

| 字段                   | 类型                    | 默认值   | 说明                                                         |
| ---------------------- | ----------------------- | -------- | ------------------------------------------------------------ |
| `async`                | `bool`                  | `false`  | 异步转发：在 MessageLoop 线程上发布数据，false 则在订阅回调中内联发布 |
| `reliable`             | `bool`                  | `false`  | 数据通道使用 DDS 可靠 QoS；必须与所有客户端一致              |
| `enable_tcp`           | `bool`                  | `false`  | 数据通道使用 TCP 传输；必须与所有客户端一致                  |
| `direct`               | `bool`                  | `false`  | 使用 SHM（Iceoryx）作为数据通道，替代 DDS；必须与所有客户端一致 |
| `native_mode`          | `bool`                  | `false`  | 将所有 DDS 流量限制到 127.0.0.1（仅本机，用于本机测试）      |
| `domain_id`            | `int`                   | `0`      | DDS 域 ID，范围 0~255，所有客户端必须使用相同的域 ID         |
| `buf_size`             | `uint32_t`              | `0`      | DDS socket 收发缓冲区大小（字节），0 使用内置默认值（8MB）   |
| `mtu_size`             | `uint32_t`              | `0`      | DDS 分片 MTU 大小（字节），0 使用内置默认值（65500 字节）    |
| `max_packet_size`      | `double`                | `0`      | 单条消息最大转发大小（MiB），超出则丢弃。**注意**：当前实现不存在"0 表示不限制"的特判，字段为 `0` 时实际会把所有非空消息都丢弃；要放行大包必须显式设置一个足够大的 MiB 值（CLI 默认 4.0，头文件注释与实现不符，以此处描述为准） |
| `security_key`         | `std::string`           | `""`     | Time/InfoList/Control 通道的对称密钥。**必须非空**——这三条通道使用 `SecurityPublisher`/`SecuritySubscriber`，运行时不再有公共 `enable_security()` 入口，`Security::Config::key` 只能通过构造函数传入。空字符串会让 `NodeImpl::security` 保持空，控制面节点初始化时会 fatal 并抛 `RuntimeError`。客户端与服务端的 `security_key` 必须一致。 |
| `bind_ip`              | `std::string`           | `""`     | DDS socket 绑定的本地 IP，空字符串表示绑定所有接口           |
| `peer_ip`              | `std::string`           | `""`     | DDS 单播对端 IP，空字符串使用多播发现                        |
| `dds_impl`             | `std::string`           | `"dds"`  | DDS 实现选择：如 `"dds"`（FastDDS）、`"ddsc"`（CycloneDDS）、`"ddsr"`、`"ddst"` |
| `use_iox`              | `bool`                  | `false`  | 是否在启动时内嵌启动 Iceoryx RouDi 守护进程                  |
| `iox_monitoring`       | `bool`                  | `true`   | 是否启用 Iceoryx 监控/自省功能                               |
| `iox_strategy`         | `int`                   | `1`      | Iceoryx 内存分配策略（1=低内存，2=中等，3=高内存），默认低内存 |
| `iox_config`           | `std::string`           | `""`     | Iceoryx TOML 配置文件路径，空字符串使用默认配置              |
| `runnable_version_major` | `uint16_t`            | `1`      | 加载的 runnable 插件（API 名为 `RunablePluginInterface`）所需的最低主版本号 |
| `runnable_version_minor` | `uint16_t`            | `0`      | 加载的 runnable 插件（API 名为 `RunablePluginInterface`）所需的最低次版本号 |
| `runnable_prefix`      | `std::string`           | `""`     | 插件共享库文件名前缀                                         |
| `runnable_list`        | `std::vector<std::string>` | `{}` | 启动时加载的 runnable 插件名称列表（实际 API 类型名为 `RunablePluginInterface`） |

### 16.6.4 嵌入式使用示例

```cpp
#include <vlink/external/proxy_server.h>
#include <vlink/base/utils.h>
#include <vlink/base/logger.h>

int main() {
    vlink::Logger::init("my-proxy");

    vlink::ProxyServer::Config cfg;
    cfg.dds_impl       = "dds";
    cfg.domain_id      = 0;
    cfg.reliable       = false;
    cfg.enable_tcp     = false;
    cfg.direct         = false;
    cfg.native_mode    = false;
    cfg.max_packet_size = 4.0;  // 4 MiB
    cfg.security_key   = "my_secret_key";

    vlink::ProxyServer server(cfg);

    // 注册终止信号处理，Ctrl+C 时优雅退出
    vlink::Utils::register_terminate_signal([&server](int) {
        server.quit(true);
    });

    server.run();  // 阻塞直到 quit() 被调用
    return 0;
}
```

---

## 16.7 vlink-proxy 命令行工具

`vlink-proxy` 是 ProxyServer 的独立可执行文件，直接在终端启动即可。

### 16.7.1 命令行参数

```
vlink-proxy [选项]

选项：
  -a, --async             启用异步转发模式
  -r, --reliable          启用可靠 QoS 模式
  -t, --tcp               启用 TCP 传输
  -g, --direct            启用 SHM 直连模式（需要 Iceoryx）
  -d, --domain_id INT     DDS 域 ID（0~255，默认 0）
  -k, --key STRING        安全密钥
  -b, --bind_ip STRING    绑定本地 IP 地址
  -p, --peer_ip STRING    单播对端 IP 地址
  -s, --buf_size UINT     DDS 收发缓冲区大小（字节）
  -e, --mtu_size UINT     DDS MTU 大小（字节）
  -n, --native            限制 DDS 流量到 127.0.0.1
  -x, --max_packet_size FLOAT  单条消息最大大小（MiB，默认 4.0）
  -c, --iox_config PATH   Iceoryx TOML 配置路径（**给 -c 即隐式 use_iox=true**，无独立 use_iox 开关）
  -l, --iox_strategy INT  Iceoryx 内存策略（1/2/3，CLI 默认 2；注意 ProxyServer::Config 结构体默认是 1，此处为 vlink-proxy CLI 覆盖）
  -m, --iox_monitoring STR  Iceoryx 监控（on/off，默认 on）
  --dds_impl STRING       DDS 实现（CLI 仅接受 dds/ddsc，依赖编译期 ENABLE_DDS/ENABLE_DDSC，默认 dds；ddsr/ddst 仅可在程序里通过 ProxyServer::Config.dds_impl 设置）
  --runnable NAME...      加载 runnable 插件名称列表（API 名为 RunablePluginInterface）
```

### 16.7.2 典型启动示例

```bash
# 最简启动（默认配置）
vlink-proxy

# 指定域 ID 和安全密钥
vlink-proxy -d 1 -k "secure_key_2026"

# 启用 TCP + 可靠模式，绑定指定 IP，限制最大包大小为 8 MiB
vlink-proxy -r -t -b 192.168.1.100 -x 8.0

# 启用 SHM 直连模式（内嵌 RouDi，使用高内存策略）
vlink-proxy -g -c /etc/iceoryx/config.toml -l 3

# 仅本机通信（native 模式）
vlink-proxy -n

# 单播 DDS 发现（指定对端 IP 跳过多播）
vlink-proxy -b 10.0.0.1 -p 10.0.0.2

# 加载 runnable 插件（API 名为 RunablePluginInterface）
vlink-proxy --runnable my_plugin_a my_plugin_b
```

### 16.7.3 注意事项

- `vlink-proxy` 内置了单例检测，同一机器上不允许同时运行两个实例
- 如果需要 DDS 多播发现，可能需要为网络接口添加多播/广播路由规则（启动时会打印提示地址）
- 启动时环境变量 `VLINK_INTRA_BIND`（参见 [21-environment-vars.md](21-environment-vars.md#211-核心运行时环境变量)）若被设置，服务器将同时订阅 `intra://` 话题

---

## 16.8 ProxyAPI 说明

`ProxyAPI` 是代理系统的客户端，继承自 `MessageLoop`，通过 DDS 连接到运行中的 `ProxyServer`。

### 16.8.1 角色

| 角色           | 枚举值        | 权限                                        |
| -------------- | ------------- | ------------------------------------------- |
| `kController`  | 0             | 可调用 `send_control()` 和 `send_data()`    |
| `kListener`    | 1             | 只读，`send_control()` 和 `send_data()` 返回 false |

### 16.8.2 工作模式

| 模式               | 枚举值 | 说明                                               |
| ------------------ | ------ | -------------------------------------------------- |
| `kOffline`         | 0      | 离线，服务器释放所有订阅                           |
| `kObserveOne`      | 1      | 只观察 `url_meta_list` 中指定的单个话题            |
| `kObserveAll`      | 2      | 观察 DDS 域内发现的全部话题                        |
| `kRecord`          | 3      | 录制 `url_meta_list` 中指定话题的数据              |
| `kPlay`            | 4      | 回放：客户端通过 `send_data()` 注入，服务器转发    |
| `kEdit`            | 5      | 编辑模式：服务器转发客户端注入的数据               |
| `kAuto`            | 6      | 自动模式：观察指定话题并自动转发给订阅方           |
| `kAutoAndObserveAll` | 7   | Auto 模式 + 同时观察所有话题                       |

### 16.8.3 错误码

| 错误码               | 枚举值 | 触发条件                                         |
| -------------------- | ------ | ------------------------------------------------ |
| `kNoError`           | 0      | 无错误，连接正常                                 |
| `kModeError`         | 1      | 请求了不支持的工作模式                           |
| `kControlError`      | 2      | 服务器返回的 control_id 与客户端不匹配           |
| `kReliableCompError` | 3      | 客户端与服务器的 reliable 设置不一致             |
| `kTcpCompError`      | 4      | 客户端与服务器的 enable_tcp 设置不一致           |
| `kDirectCompError`   | 5      | 客户端与服务器的 direct 设置不一致               |
| `kMultiProxyError`   | 7      | 同一 DDS 域内检测到多个 ProxyServer              |
| `kVersionCompError`  | 8      | 客户端与服务器的 VLINK_VERSION 字符串不匹配      |
| `kUnknownError`      | 9      | 未分类错误                                       |

### 16.8.4 ProxyAPI::Config 字段说明

| 字段            | 类型           | 默认值        | 说明                                                          |
| --------------- | -------------- | ------------- | ------------------------------------------------------------- |
| `role`          | `Role`         | `kController` | 客户端角色                                                    |
| `domain_id`     | `int`          | `0`           | DDS 域 ID，必须与服务器一致                                   |
| `dds_impl`      | `std::string`  | `"dds"`       | DDS 实现选择                                                  |
| `security_key`  | `std::string`  | `""`          | 安全密钥，必须与服务器 `security_key` 一致                    |
| `native`        | `bool`         | `false`       | 是否限制 DDS 流量到 127.0.0.1                                 |
| `reliable`      | `bool`         | `false`       | 数据通道 QoS，必须与服务器一致                                |
| `direct`        | `bool`         | `false`       | 是否使用 SHM 直连，必须与服务器一致                           |
| `enable_tcp`    | `bool`         | `false`       | 是否使用 TCP，必须与服务器一致                                |
| `match_version` | `bool`         | `true`        | 是否校验 VLINK_VERSION 版本字符串                             |
| `allow_ip`      | `std::string`  | `""`          | 绑定本地 DDS socket IP                                        |
| `peer_ip`       | `std::string`  | `""`          | 单播对端 IP                                                   |
| `buf_size`      | `int`          | `0`           | Socket 缓冲区大小（字节），0 使用默认值                       |
| `mtu_size`      | `int`          | `0`           | DDS MTU 大小（字节），0 使用默认值                            |

### 16.8.5 心跳与断线重连机制

- `ProxyAPI` 内部通过 `TimeSub` 订阅服务器每秒广播的心跳
- 若连续 5 秒未收到心跳，判定连接断开，触发 `ConnectCallback(false)`
- `kController` 角色会在断线重连后自动重新发送最后一次 `Control` 指令

---

## 16.9 连接配置详解

### 16.9.1 普通 DDS 模式（默认）

客户端与服务器均使用 DDS 传输数据，适合跨机器、跨网段场景。

```cpp
vlink::ProxyAPI::Config cfg;
cfg.role         = vlink::ProxyAPI::kController;
cfg.dds_impl     = "dds";          // 使用 FastDDS
cfg.domain_id    = 0;
cfg.reliable     = false;           // 与服务器一致
cfg.enable_tcp   = false;           // 与服务器一致
cfg.direct       = false;           // 与服务器一致
cfg.security_key = "my_secret_key"; // 与服务器 security_key 一致
cfg.match_version = true;
```

### 16.9.2 SHM 直连模式（direct）

数据通道改走 Iceoryx 共享内存，零拷贝，延迟极低，适合同机监控。

```cpp
cfg.direct = true;  // 服务器也必须设置 direct=true
// direct 模式下 get_latency() 始终返回 0
```

### 16.9.3 单播 DDS 模式（跨子网）

```cpp
cfg.allow_ip = "192.168.10.5";   // 本机出口 IP
cfg.peer_ip  = "192.168.20.100"; // 对端 ProxyServer 所在 IP
```

> 此模式是 DDS 层的单播发现（显式 peer IP），不做 NAT 穿透；双方仍需 IP 可达。真正需要 NAT 穿透请考虑 `zenoh://`。

### 16.9.4 本机测试模式（native）

```cpp
cfg.native = true;
// 所有 DDS 流量绑定到 127.0.0.1
```

---

## 16.10 监控活跃节点、话题与消息统计

### 16.10.1 注册 InfoCallback

```cpp
vlink::ProxyAPI::Config cfg;
cfg.role = vlink::ProxyAPI::kListener;  // 只读监控
// ... 其他配置

vlink::ProxyAPI api(cfg);

api.register_connect_callback([](bool connected) {
    if (connected) {
        // ProxyServer 上线
    } else {
        // ProxyServer 断线
    }
});

api.register_info_callback([](const std::vector<vlink::ProxyAPI::Info>& list) {
    for (const auto& info : list) {
        // info.url      — 话题 URL，如 "dds://sensor/lidar"
        // info.ser      — 具体序列化类型名，如 "demo.proto.PointCloud"
        // info.schema   — 粗粒度 schema 家族，如 kProtobuf / kFlatbuffers / kUnknown
        // info.status   — kActive / kInActive / kPending / kInvalid
        // info.freq     — 消息频率（条/秒，加权移动平均）
        // info.rate     — 吞吐量（字节/秒，加权移动平均）
        // info.loss     — 丢包率 [0, 1]
        // info.latency  — 端到端延迟（毫秒）
        // info.process_list — 发布/订阅该话题的进程列表

        for (const auto& proc : info.process_list) {
            // proc.host — 主机名
            // proc.pid  — 进程 ID
            // proc.name — 进程名
            // proc.ip   — IP 地址
            // proc.type — ImplType 位掩码
        }
    }
});

api.async_run();  // 启动内部事件循环（后台线程）
```

### 16.10.2 话题状态说明

| 状态        | 含义                                             |
| ----------- | ------------------------------------------------ |
| `kActive`   | 话题正在活跃接收数据                             |
| `kInActive` | 话题存在但最近 2 秒内未收到数据                  |
| `kPending`  | 话题刚被发现，统计数据尚在累积中（前 10 秒）    |
| `kInvalid`  | 话题类型不支持观察（例如纯订阅端）               |

### 16.10.3 获取服务器时间和系统信息

```cpp
api.register_time_callback([](uint64_t sys_time, uint64_t boot_time) {
    // sys_time  — 服务器系统时间（微秒，Unix epoch）
    // boot_time — 服务器自启动以来的时长（微秒）

    auto formatted = vlink::ProxyAPI::get_format_sys_time(sys_time);
    // -> "2026/03/18 10:30:00:123"

    auto elapsed = vlink::ProxyAPI::get_format_boot_time(boot_time);
    // -> "0d 01:23:45.678"
});

// 获取当前 CPU/内存占用（来自最近一次心跳）
double cpu = api.get_current_cpu_usage();    // 0~100
double mem = api.get_current_memory_usage(); // 0~100
```

---

## 16.11 通过 Proxy 进行跨网段通信

典型场景：车载计算单元（EdgePC）运行实际业务节点，开发机（DevPC）通过代理进行监控和数据注入。

```
[EdgePC 192.168.1.100]                    [DevPC 192.168.2.50]
  Publisher<LidarData>                       vlink-monitor / 自定义工具
  Subscriber<ControlCmd>                            |
        |                                           |
  vlink-proxy -b 192.168.1.100 \           ProxyAPI::Config::
              -p 192.168.2.50  \              allow_ip = "192.168.2.50"
              -d 1 -k "key"               peer_ip  = "192.168.1.100"
                                           domain_id = 1
                                           security_key = "key"
```

启动步骤：

1. 在 EdgePC 上启动代理服务器：
   ```bash
   vlink-proxy -b 192.168.1.100 -p 192.168.2.50 -d 1 -k "key"
   ```

2. 在 DevPC 上连接并开始观察所有话题：
   ```cpp
   vlink::ProxyAPI::Config cfg;
   cfg.allow_ip     = "192.168.2.50";
   cfg.peer_ip      = "192.168.1.100";
   cfg.domain_id    = 1;
   cfg.security_key = "key";

   vlink::ProxyAPI api(cfg);

   vlink::ProxyAPI::Control ctrl;
   ctrl.mode = vlink::ProxyAPI::kObserveAll;
   api.send_control(ctrl);

   api.register_data_callback([](const vlink::ProxyAPI::Data& data) {
       // data.url       — 话题 URL
       // data.ser       — 序列化类型
       // data.schema   — 粗粒度 schema 家族
       // data.raw       — 原始字节（浅拷贝，仅在回调内有效）
       // data.timestamp — 自代理会话起的相对时间（微秒，int64_t）；-1 表示未设置
       // data.seq       — 发布序号
   });

   api.async_run();
   ```

---

## 16.12 数据录制与回放

### 16.12.1 录制模式

```cpp
vlink::ProxyAPI::Control ctrl;
ctrl.mode = vlink::ProxyAPI::kRecord;
ctrl.url_meta_list = {
    {"dds://sensor/lidar",  "demo.proto.PointCloud", vlink::SchemaType::kProtobuf, vlink::kSubscriber},
    {"dds://sensor/camera", "demo.proto.CameraFrame", vlink::SchemaType::kProtobuf, vlink::kSubscriber},
};
api.send_control(ctrl);  // 每个 UrlMeta 都必须显式带上 ser + schema

// 在 DataCallback 中将数据写入文件
api.register_data_callback([&bag](const vlink::ProxyAPI::Data& data) {
    // data.raw 仅在回调内有效；异步写 bag 前需要先深拷贝
    // bag.push(data.url, data.ser, data.schema, vlink::ActionType::kSubscribe,
    //          vlink::Bytes::deep_copy(data.raw.data(), data.raw.size()), &data.timestamp);
});
```

### 16.12.2 回放模式

```cpp
vlink::ProxyAPI::Control ctrl;
ctrl.mode = vlink::ProxyAPI::kPlay;
ctrl.url_meta_list = {
    {"dds://sensor/lidar",  "demo.proto.PointCloud", vlink::SchemaType::kProtobuf, vlink::kPublisher},  // 服务器将作为 Publisher
};
api.send_control(ctrl);

// 从文件读取数据并注入
vlink::ProxyAPI::Data data;
data.url       = "dds://sensor/lidar";
data.ser       = "demo.proto.PointCloud";
data.schema    = vlink::SchemaType::kProtobuf;
data.raw       = vlink::Bytes::deep_copy(buf, size);
data.timestamp = elapsed_us;
data.seq       = seq_num;
api.send_data(data);  // send_data() 同样要求显式传入 ser + schema
```

---

## 16.13 ProxyData 零拷贝传输

当编译时启用 `VLINK_PROXY_ENABLE_ZEROCOPY_DATA`（默认开启），数据通道使用 `zerocopy::ProxyData` 结构体代替 Protobuf 序列化。

`ProxyData` 是一个 **80 字节固定头部 + 变长尾部** 的平坦内存结构：

```
[ magic_begin (4B) | ProxyData struct (80B) | raw + url + ser + hostname | magic_end (4B) ]
```

优点：
- 零额外分配：整个 payload 在一次 `create()` 调用中完成内存布局
- 零拷贝读取：`url()`、`ser()`、`hostname()` 返回 `string_view`，直接引用尾部内存
- 反序列化时（`operator<<`）不需要拷贝，直接借用 wire buffer

注意事项：
- `ProxyData` 的 `string_view` 字段的生命周期与持有它的 `Bytes` 对象绑定，不可在 `Bytes` 销毁后继续访问
- 32 位架构不支持（编译时会发出警告）

---

## 16.14 安全配置

代理系统的 Time、InfoList、Control 三个信道使用 DDS 安全扩展进行加密和认证。

### 16.14.1 配置安全密钥

服务器端：
```cpp
cfg.security_key = "my_32_byte_secret_key_for_aes_auth";
```

客户端：
```cpp
api_cfg.security_key = "my_32_byte_secret_key_for_aes_auth";
```

### 16.14.2 版本兼容校验

默认情况下（`match_version = true`），客户端会在第一次心跳时检查服务器的 `VLINK_VERSION` 字符串是否与自身一致。版本不匹配将触发 `kVersionCompError` 并拒绝建立连接。

如需跨版本连接（不推荐），可禁用：
```cpp
cfg.match_version = false;
```

---

## 16.15 话题过滤

`ProxyAPI::Control` 支持在服务器端进行话题过滤，减少不必要的订阅和数据转发。

```cpp
vlink::ProxyAPI::Control ctrl;
ctrl.mode = vlink::ProxyAPI::kObserveAll;

// 按 URL 关键词过滤（空格分隔，不区分大小写，满足其一即通过）
ctrl.filter_by_process = false;
ctrl.filter_str = "lidar camera";

// 或按进程名过滤
ctrl.filter_by_process = true;
ctrl.filter_str = "sensor_node";

// 按节点类型过滤
// filter_type = 0: 全部
// filter_type = 1: 仅显示同时有 Publisher 和 Subscriber 的话题
// filter_type = 2: 仅显示同时有 Server 和 Client 的话题
// filter_type = 3: 仅显示同时有 Setter 和 Getter 的话题
// filter_type = 4: 显示所有 Event 类话题（有 Publisher 或 Subscriber）
// filter_type = 5: 显示所有 Method 类话题（有 Server 或 Client）
// filter_type = 6: 显示所有 Field 类话题（有 Setter 或 Getter）
// filter_type = 7..12: 分别仅显示 Publisher / Subscriber / Server / Client / Setter / Getter
ctrl.filter_type = 1;

api.send_control(ctrl);
```

注意：过滤功能由编译宏 `VLINK_PROXY_ENABLE_FILTER`（默认为 1）控制，可通过 `ProxyAPI::is_enable_filter()` 检查运行时是否启用。

---

## 16.16 与 CLI 工具的配合

`vlink-monitor`（参见 [13-cli-tools.md](13-cli-tools.md#136-vlink-monitor--实时通信状态监控)）是 VLink 提供的命令行监控工具，内部使用 `DiscoveryViewer` 发现节点和话题。`vlink-viewer`（参见 [14-viewer.md](14-viewer.md)）则通过 `ProxyAPI` 连接到 `vlink-proxy` 获取详细数据。

典型工作流程：

```
1. 在目标设备上启动代理服务
   $ vlink-proxy -d 0 -k "key"

2. 在开发机上启动 vlink-monitor（基于 DiscoveryViewer 发现）
   $ vlink-monitor

3. 或使用 vlink-viewer 通过 ProxyAPI 以更丰富的功能连接
   - 接收 Time 心跳（实时展示 CPU/内存/时间）
   - 接收 InfoList（实时展示所有话题的频率/吞吐量/延迟/丢包）
   - 按需发送 Control 指令观察特定话题的原始数据
```

---

## 16.17 完整使用示例：部署 + 连接 + 监控

### 16.17.1 部署端（EdgePC，运行业务节点 + 代理服务器）

```cpp
// edge_main.cpp
#include <vlink/external/proxy_server.h>
#include <vlink/base/utils.h>
#include <vlink/base/logger.h>
#include <vlink/vlink.h>

int main() {
    vlink::Logger::init("edge-app");

    // 启动业务节点
    vlink::Publisher<MyMsg> pub("dds://sensor/data");
    // ... pub.publish(...) 在业务线程中持续发布 ...

    // 启动代理服务器
    vlink::ProxyServer::Config proxy_cfg;
    proxy_cfg.domain_id = 0;
    proxy_cfg.security_key = "secret";
    proxy_cfg.bind_ip   = "192.168.1.100";
    proxy_cfg.peer_ip   = "192.168.2.50";

    vlink::ProxyServer proxy(proxy_cfg);

    vlink::Utils::register_terminate_signal([&proxy](int) {
        proxy.quit(true);
    });

    proxy.run();
    return 0;
}
```

### 16.17.2 监控端（DevPC）

```cpp
// monitor_main.cpp
#include <vlink/external/proxy_api.h>
#include <iostream>

int main() {
    vlink::ProxyAPI::Config cfg;
    cfg.role         = vlink::ProxyAPI::kController;
    cfg.domain_id    = 0;
    cfg.security_key = "secret";
    cfg.allow_ip     = "192.168.2.50";
    cfg.peer_ip      = "192.168.1.100";

    vlink::ProxyAPI api(cfg);

    api.register_connect_callback([&api](bool connected) {
        std::cout << (connected ? "[+] 代理服务器已连接" : "[-] 代理服务器断开") << std::endl;

        if (connected) {
            // 连接后立即切换到观察所有话题模式
            vlink::ProxyAPI::Control ctrl;
            ctrl.mode = vlink::ProxyAPI::kObserveAll;
            api.send_control(ctrl);
        }
    });

    api.register_error_callback([](vlink::ProxyAPI::Error err) {
        if (err != vlink::ProxyAPI::kNoError) {
            std::cerr << "代理错误码: " << static_cast<int>(err) << std::endl;
        }
    });

    api.register_time_callback([](uint64_t sys_time, uint64_t boot_time) {
        std::cout << "服务器时间: "
                  << vlink::ProxyAPI::get_format_sys_time(sys_time)
                  << " | 运行时长: "
                  << vlink::ProxyAPI::get_format_boot_time(boot_time)
                  << std::endl;
    });

    api.register_info_callback([](const std::vector<vlink::ProxyAPI::Info>& list) {
        std::cout << "=== 话题列表 ===" << std::endl;
        for (const auto& info : list) {
            std::cout << "  " << info.url
                      << "  schema=" << static_cast<int>(info.schema)
                      << "  freq=" << info.freq << " Hz"
                      << "  rate=" << info.rate / 1024 << " KB/s"
                      << "  loss=" << info.loss * 100 << "%"
                      << "  lat=" << info.latency << " ms"
                      << std::endl;
        }
    });

    api.register_data_callback([](const vlink::ProxyAPI::Data& data) {
        std::cout << "收到数据: " << data.url
                  << " schema=" << static_cast<int>(data.schema)
                  << " [" << data.raw.size() << " bytes]"
                  << " seq=" << data.seq
                  << std::endl;
    });

    // 启动事件循环（阻塞）
    api.async_run();
    api.wait_for_quit();

    return 0;
}
```

---

## 16.18 CMake 集成

```cmake
# CMakeLists.txt

# 仅需要客户端（ProxyAPI）
find_package(vlink REQUIRED)
target_link_libraries(my_monitor PRIVATE vlink::proxy_api)

# 需要服务端（ProxyServer）
target_link_libraries(my_server PRIVATE vlink::proxy_server)

# 通常不需要手动链接 vlink-proxy 可执行文件，直接使用系统安装的版本即可
```

编译宏说明：

| 宏                          | 含义                                                        |
| --------------------------- | ----------------------------------------------------------- |
| `VLINK_ENABLE_PROXY`        | 由 proxy_api/proxy_server 库自动添加，标识代理功能已启用    |
| `VLINK_PROXY_API_LIBRARY`   | 动态库构建时由 proxy_api 添加                               |
| `VLINK_PROXY_SERVER_LIBRARY`| 动态库构建时由 proxy_server 添加                            |

---

## 16.19 常见问题与注意事项

1. **`reliable`/`enable_tcp`/`direct` 三个配置必须客户端与服务器完全一致**，任何一个不匹配都会在心跳校验时触发对应的错误码（`kReliableCompError` 等），客户端将拒绝连接。

2. **同一 DDS 域内只能运行一个 ProxyServer**，否则 ProxyAPI 会检测到 `kMultiProxyError`。每个域需要独立使用不同的 `domain_id`。

3. **`DataCallback` 中的 `Data::raw` 是浅拷贝**，仅在回调执行期间有效。若需在回调外使用数据，必须进行深拷贝（`vlink::Bytes::deep_copy(data.raw.data(), data.raw.size())`、`data.raw.deep_copy_self()` 或使用 `std::vector<uint8_t>`）。

4. **`send_data()` 返回 `false` 不一定是链路故障**，也可能是对端暂时没有订阅者，或者调用方没有显式传入完整的 `ser + schema` 路由元数据。

5. **`ProxyServer` 的析构是同步阻塞的**，会等待所有 DDS 句柄和 DiscoveryViewer 完成清理后才返回，应确保在进程退出前调用。

6. **SHM 直连模式（direct）需要 Iceoryx RouDi 守护进程已运行**，可以通过 `-c` 参数让 `vlink-proxy` 自动内嵌启动，也可以外部单独启动 `iox-roudi`。

7. **`max_packet_size=0` 不是"不限制"**。过滤逻辑是 `if (bytes.size() > real_max_packet_size) return;`（`real_max_packet_size = max_packet_size * 1024 * 1024`），没有 `0 = unlimited` 的特判；字段为 `0` 时所有非空消息都会被丢弃。若要放行大包请显式设置足够大的 MiB 值（CLI 默认 `4.0`）。
