# ProxyServer 基础示例

![ProxyServer Overview](images/proxy-server-overview.png)

## 1. 概述

本示例演示 VLink `ProxyServer` 守护进程的配置和生命周期管理。`ProxyServer` 是代理子系统的服务端，负责发现主题、广播心跳、转发数据和管理插件。

## 2. 核心功能

- 主题发现：通过 `DiscoveryViewer` 枚举所有活跃的发布者和订阅者
- Token 握手：默认通过 `HandshakeSrv` 签发 128-bit token，校验所有入站 Control
- 心跳广播：每秒发送包含 CPU/内存使用、版本号、主机名和 token 的 Time 消息
- 统计信息：每秒发布每个主题的频率、速率、丢包、延迟
- 数据转发：在观察/录制/回放模式下转发消息字节
- 插件管理：加载和管理 `RunablePluginInterface` 插件

## 3. 配置

```cpp
ProxyServer::Config cfg;
cfg.dds_impl = "dds";
cfg.domain_id = 0;
cfg.security_key = "";  // 显式设置时必须与 ProxyAPI 一致
cfg.reliable = false;
cfg.async = true;
cfg.use_iox = false;
```

## 4. 通信架构

```
ProxyAPI (kController)
     |--- Handshake RPC --> [DDS secure] --> ProxyServer
     |<-- token         <-- [DDS secure] <-- |
     |--- Control+token --> [DDS secure] --> |
     |<-- Time+token    <-- [DDS secure] <-- |
     |<-- Info    <-- [DDS secure] <-- |
     |<-- Data    <-- [DDS/SHM] <-- |
```

## 5. 编译与运行

```bash
cmake -B build -S . -DCMAKE_PREFIX_PATH=/path/to/vlink/install
cmake --build build --target example_proxy_server_basic
./build/output/bin/example_proxy_server_basic
```

需要链接 `vlink::proxy_server`。

## 6. 注意事项

- 每个进程只能有一个 ProxyServer（单例约束）
- 需要 DDS 后端可用
- ProxyAPI / ProxyServer 的 `security_key` 必须一致；默认握手开启时 Control 和 Time 都携带 token
- `VLINK_INTRA_BIND` 环境变量启用 intra:// 主题观察
- 插件通过 `Config::runnable_list` 配置
