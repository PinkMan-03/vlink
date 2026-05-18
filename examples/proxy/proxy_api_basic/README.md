# ProxyAPI 基础示例

![ProxyAPI Client Flow](images/proxy-api-client-flow.png)

## 1. 概述

本示例演示 VLink `ProxyAPI` 客户端的配置、角色、回调注册和控制消息发送。`ProxyAPI` 用于连接正在运行的 `ProxyServer` 守护进程，实现远程监控和控制。

## 2. 核心概念

### 2.1 角色

| 角色 | 描述 |
|------|------|
| `kController` | 可以发送控制消息（观察、录制、回放） |
| `kListener` | 被动观察者，`send_control()` 被拒绝 |

### 2.2 操作模式

| 模式 | 值 | 描述 |
|------|---|------|
| kOffline | 0 | 断开连接 |
| kObserveOne | 1 | 观察单个主题 |
| kObserveAll | 2 | 观察所有主题 |
| kRecord | 3 | 录制匹配主题 |
| kPlay | 4 | 回放录制数据 |
| kEdit | 5 | 编辑/注入模式 |
| kAuto | 6 | 自动观察指定主题 |
| kAutoAndObserveAll | 7 | 自动 + 观察全部 |

### 2.3 配置

```cpp
ProxyAPI::Config cfg;
cfg.role = ProxyAPI::kController;
cfg.dds_impl = "dds";
cfg.domain_id = 0;
cfg.reliable = false;
cfg.match_version = true;
```

## 3. 编译与运行

```bash
cd build
cmake .. && make example_proxy_api_basic
./output/bin/example_proxy_api_basic
```

需要链接 `vlink::proxy_api`。

## 4. 注意事项

- ProxyAPI 需要一个正在运行的 ProxyServer
- 心跳超时 5 秒后声明连接丢失
- `match_version` 为 true 时检查版本匹配
- Controller 和 Server 的 reliable/direct 设置必须一致

## 5. 相关文档

详细原理参见 [doc/16-proxy.md](../../../doc/16-proxy.md)。
