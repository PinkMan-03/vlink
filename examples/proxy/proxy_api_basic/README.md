# proxy_api_basic — ProxyAPI 客户端：配置、角色、模式、回调

本示例演示 `vlink::ProxyAPI`（Proxy 客户端）的基础用法：配置 Config、注册各类回调（连接 / 模式 / 数据 / Control）、发送 Control 命令。前提是另一进程跑着 `ProxyServer`；没有 Server 时回调会报 disconnected。

读完本示例你能掌握：

- ProxyAPI Config 的各个字段语义。
- Controller / Listener 两种 Role 的差异。
- 八种 OperationMode 的工程含义。
- 完整的错误码体系。

## 背景与适用场景

适用：

- 写远程调试 / 监控工具的客户端侧。
- 跨机器/跨进程"观察 vlink topics"的需求。
- 录制 / 回放控制（remote bag recorder）。
- 测试时往目标进程注入消息。

不适合：

- 同进程内的消息查看（直接订阅就行）。
- 高吞吐数据通道（Proxy 走 Control + Data 双通道，吞吐受 Server 调度影响）。

## 角色

| Role | 说明 |
|------|------|
| `kController` | 可发 Control 消息（observe / record / play / edit） |
| `kListener` | 被动观察者；`send_control()` 被 Server 拒绝 |

## 模式（OperationMode）

| 值 | 模式 | 含义 |
|---|------|------|
| 0 | `kOffline` | 未连接 |
| 1 | `kObserveOne` | 观察单个 topic |
| 2 | `kObserveAll` | 观察所有已发现的 topic |
| 3 | `kRecord` | 录制匹配的 topic |
| 4 | `kPlay` | 回放录制的数据 |
| 5 | `kEdit` | 通过 Server 注入 / 编辑 |
| 6 | `kAuto` | 自动观察指定 topic |
| 7 | `kAutoAndObserveAll` | 组合 |

## 核心 API

| API | 签名 | 说明 |
|-----|------|------|
| `ProxyAPI(Role)` | 构造 | 默认 Controller |
| `ProxyAPI::Config` | 结构 | 见下表 |
| `connect` | `ErrorCode connect(const std::string& proxy_url = "")` | 连接 Server |
| `disconnect` | `ErrorCode` | 断开 |
| `is_connected` | `bool` | 状态查询 |
| `set_operation_mode` | `ErrorCode set_operation_mode(OperationMode)` | 切换模式 |
| `send_control` | `ErrorCode send_control(const ProxyControl&)` | 发 Control 命令 |
| `send_data` | `ErrorCode send_data(const std::string& url, const Bytes&)` | 注入数据 |
| `register_mode_changed_callback` | `void register_mode_changed_callback(Function<void(OperationMode)>&&)` | 模式变化回调 |
| `register_control_callback` | `void register_control_callback(Function<void(const ProxyControl&)>&&)` | 收到 Control 回调 |
| `register_data_callback` | `void register_data_callback(Function<void(const std::string&, const Bytes&)>&&)` | 收到 Data 回调 |

## Config 字段

```cpp
ProxyAPI::Config cfg;
cfg.role          = ProxyAPI::kController;
cfg.dds_impl      = "dds";           // DDS 实现：dds / ddsc / ddsr / ddst
cfg.domain_id     = 0;
cfg.security_key  = "";              // 非空时与 ProxyServer 必须一致
cfg.reliable      = false;            // DDS reliable 模式
cfg.match_version = true;             // 严格 vlink 版本匹配
```

## 错误码

| Code | 名称 | 原因 |
|---:|------|------|
| 0 | `kNoError` | OK |
| 2 | `kControlError` | Control ID 不匹配 |
| 3-5 | reliable / tcp / direct 组合 | 客户端与 Server 配置不一致 |
| 7 | `kMultiProxyError` | 网络中检测到多个 Server |
| 8 | `kVersionCompError` | 版本不匹配 |
| 9 | `kTokenError` | 握手拒绝或 token 不一致 |

## 代码导读

### 1. 构造 + 配置

```cpp
vlink::ProxyAPI api(vlink::ProxyAPI::kController);
vlink::ProxyAPI::Config cfg;
cfg.dds_impl = "dds";
cfg.domain_id = 0;
api.apply_config(cfg);
```

### 2. 回调注册

```cpp
api.register_mode_changed_callback([](vlink::ProxyAPI::OperationMode mode) {
  VLOG_I("mode changed: ", static_cast<int>(mode));
});

api.register_data_callback([](const std::string& url, const vlink::Bytes& data) {
  VLOG_I("data: url=", url, " size=", data.size());
});
```

### 3. 连接 + 切模式 + 发 Control

```cpp
auto rc = api.connect();
if (rc == vlink::ProxyAPI::kNoError) {
  api.set_operation_mode(vlink::ProxyAPI::kObserveAll);

  vlink::ProxyControl ctrl;
  ctrl.command_id = 100;
  api.send_control(ctrl);
}
```

### 4. 注入数据（Edit 模式）

```cpp
api.set_operation_mode(vlink::ProxyAPI::kEdit);
api.send_data("intra://debug/inject", vlink::Bytes::from_string("hello"));
```

### 5. 断开

```cpp
api.disconnect();
```

## 运行

```bash
# 在另一终端先跑 server
./build/output/bin/example_proxy_server_basic &

# 再跑 api
./build/output/bin/example_proxy_api_basic
```

预期输出（节选）：

```
connect: 0 (kNoError)
mode changed: 2 (ObserveAll)
data: url=intra://... size=...
```

没有 Server 时：

```
connect: 9 (kTokenError) or connection refused
```

## 常见陷阱

1. **没启 Server 直接 connect**：返回 kTokenError 或超时；先确保 Server 已就绪。
2. **security_key 不一致**：握手失败；要么两端都设同样 key、要么都留空。
3. **reliable 配置不匹配**：返回 3-5 系列错误；client 和 server 必须一致。
4. **Listener 调 send_control**：被 Server 拒绝；权限不足。
5. **回调里阻塞**：vlink 内部线程阻塞，影响心跳和数据接收。

## 设计要点

- ProxyAPI 内部用 DDS 做 Control / Data / Heartbeat 三套独立 topic。
- 握手用 RPC（client 拿到 128-bit token；后续消息带 token）。
- 心跳每秒一次，含 server 端 CPU / mem 状态。

## 配图

![ProxyAPI client flow](./images/proxy-api-client-flow.png)

图中展示 ProxyAPI 从 connect → handshake → mode change → data flow 的完整时序。

## 参考

- `../proxy_server_basic/` — Server 端
- `../proxy_runnable_plugin/` — Runnable 插件集成
- `vlink/include/vlink/external/proxy_api.h` — 接口
- 顶层 `doc/16-proxy.md` — Proxy 章节
