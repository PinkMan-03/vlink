# ProxyServer 基础示例

## 概述

本示例演示 VLink `ProxyServer` 守护进程的配置和生命周期管理。`ProxyServer` 是代理子系统的服务端，负责发现主题、广播心跳、转发数据和管理插件。

## 核心功能

- 主题发现：通过 `DiscoveryViewer` 枚举所有活跃的发布者和订阅者
- 心跳广播：每秒发送包含 CPU/内存使用、版本号、主机名的 Time 消息
- 统计信息：每秒发布每个主题的频率、速率、丢包、延迟
- 数据转发：在观察/录制/回放模式下转发消息字节
- 插件管理：加载和管理 `RunablePluginInterface` 插件

## 配置

```cpp
ProxyServer::Config cfg;
cfg.dds_impl = "dds";
cfg.domain_id = 0;
cfg.reliable = false;
cfg.async = true;
cfg.use_iox = false;
```

## 通信架构

```
ProxyAPI (kController)
     |--- Control --> [DDS] --> ProxyServer
     |<-- Time    <-- [DDS] <-- |
     |<-- Info    <-- [DDS] <-- |
     |<-- Data    <-- [DDS/SHM] <-- |
```

## 编译与运行

```bash
cd build
cmake .. && make example_proxy_server_basic
./output/bin/example_proxy_server_basic
```

需要链接 `vlink::proxy_server`。

## 注意事项

- 每个进程只能有一个 ProxyServer（单例约束）
- 需要 DDS 后端可用
- `VLINK_INTRA_BIND` 环境变量启用 intra:// 主题观察
- 插件通过 `Config::runnable_list` 配置
