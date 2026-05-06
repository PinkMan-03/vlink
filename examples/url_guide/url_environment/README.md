# URL Environment -- VLink 环境变量完整指南

## 概述

本示例全面展示 VLink 框架使用的所有重要 `VLINK_` 环境变量。按功能类别分组，涵盖传输配置、URL 重映射、日志、录制、安全、性能分析、发现和系统路径等方面。

## 环境变量分类总览

### 类别 1: 传输配置

| 变量 | 说明 | 默认值 |
|------|------|--------|
| `VLINK_DDS_DOMAIN` | DDS 默认域 ID | 0 |
| `VLINK_DDS_BIND` | 绑定 DDS 到指定网卡 IP | (所有接口) |
| `VLINK_MQTT_BROKER` | MQTT Broker URI | `tcp://localhost:1883` |
| `VLINK_MQTT_CLIENT_ID` | MQTT 客户端 ID 前缀 | `vlink_mqtt` |
| `VLINK_MQTT_QOS` | MQTT 默认 QoS 级别 | 1 |
| `VLINK_MQTT_KEEPALIVE` | MQTT 心跳间隔（秒） | 60 |
| `VLINK_MQTT_DOMAIN` | MQTT 默认域 ID | 0 |
| `VLINK_INTRA_BIND` | 启用 intra:// 代理观察 | (禁用) |

### 类别 2: URL 重映射

| 变量 | 说明 | 默认值 |
|------|------|--------|
| `VLINK_URL_REMAP` | 重映射 JSON 文件路径 | (不设置) |
| `VLINK_URL_USE_REMAP` | 启用自动重映射（设为 `1`） | (禁用) |

### 类别 3: 日志

| 变量 | 说明 | 默认值 |
|------|------|--------|
| `VLINK_LOG_CONSOLE_LEVEL` | 控制台日志级别 (0=Trace ~ 6=Off) | 2 (Info) |
| `VLINK_LOG_DIR` | 日志文件输出目录 | (不输出文件) |

### 类别 4: 录制 (Bag)

| 变量 | 说明 | 默认值 |
|------|------|--------|
| `VLINK_BAG_PATH` | Bag 文件输出目录，设置即启用录制 | (禁用) |
| `VLINK_BAG_TAG` | Bag 文件名标签 | (空) |

### 类别 5: 安全 (SSL/TLS)

| 变量 | 说明 | 默认值 |
|------|------|--------|
| `VLINK_SSL_CA` | CA 证书文件路径 | (不设置) |
| `VLINK_SSL_CERT` | TLS 证书文件路径 | (不设置) |
| `VLINK_SSL_KEY` | TLS 私钥文件路径 | (不设置) |

### 类别 6: 性能分析与诊断

| 变量 | 说明 | 默认值 |
|------|------|--------|
| `VLINK_PROFILER_ENABLE` | 启用 CPU 性能分析 (设为 `1`) | (禁用) |
| `VLINK_QOS_CONFIG` | QoS 配置 JSON 文件路径 | (不设置) |

### 类别 7: 发现

| 变量 | 说明 | 默认值 |
|------|------|--------|
| `VLINK_DISCOVER_DISABLE` | 禁用发现子系统 (设为 `1`) | (启用) |
| `VLINK_PLUGIN_DIR` | 插件共享库搜索目录 | (仅内置传输) |

### 类别 8: 系统路径与内存

| 变量 | 说明 | 默认值 |
|------|------|--------|
| `VLINK_TMP_DIR` | 临时文件目录覆盖 | 平台默认 |
| `VLINK_LOCK_DIR` | 锁文件目录 | 同临时目录 |
| `VLINK_MEMORY_LEVEL` | `MemoryPool` 默认分级（1..6，越大每档预留 chunk 越多） | 3 (Balanced) |

## 关键代码分析

### 1. 程序化读写环境变量

```cpp
// 读取（带默认值）
std::string val = Utils::get_env("VLINK_DDS_DOMAIN", "0");

// 设置（强制覆盖）
Utils::set_env("VLINK_DDS_DOMAIN", "42");

// 设置（不覆盖已有值）
Utils::set_env("VLINK_DDS_DOMAIN", "99", false);  // 如果已存在则不覆盖

// 清除
Utils::unset_env("VLINK_DDS_DOMAIN");
```

### 2. 日志级别控制

```bash
export VLINK_LOG_CONSOLE_LEVEL=0  # 显示所有日志（Trace 级别）
export VLINK_LOG_CONSOLE_LEVEL=3  # 只显示 Warn 及以上
export VLINK_LOG_CONSOLE_LEVEL=6  # 关闭控制台日志
```

级别对应：0=Trace, 1=Debug, 2=Info, 3=Warn, 4=Error, 5=Fatal, 6=Off

### 3. 全局录制

```bash
export VLINK_BAG_PATH=/data/recordings
export VLINK_BAG_TAG=highway_test_001
```

设置 `VLINK_BAG_PATH` 后，所有 Publisher 发布的消息自动录制到该目录下的 bag 文件中。

### 4. TLS 安全配置

```bash
export VLINK_SSL_CA=/etc/vlink/certs/ca.pem
export VLINK_SSL_CERT=/etc/vlink/certs/client.pem
export VLINK_SSL_KEY=/etc/vlink/certs/client.key
```

用于 MQTT SSL (`ssl://`) 和 DDS 安全插件的证书配置。

### 5. 性能分析

```bash
export VLINK_PROFILER_ENABLE=1
```

启用后，VLink 对关键代码路径进行性能采样，可用于定位热点和优化瓶颈。

## Shell 快速参考

```bash
# 传输配置
export VLINK_DDS_DOMAIN=42
export VLINK_DDS_BIND=192.168.1.100
export VLINK_MQTT_BROKER=tcp://broker:1883

# URL 重映射
export VLINK_URL_REMAP=/etc/vlink/remap.json
export VLINK_URL_USE_REMAP=1

# 日志
export VLINK_LOG_CONSOLE_LEVEL=0
export VLINK_LOG_DIR=/var/log/vlink

# 录制
export VLINK_BAG_PATH=/data/recordings
export VLINK_BAG_TAG=test_001

# 安全
export VLINK_SSL_CA=/etc/vlink/certs/ca.pem
export VLINK_SSL_CERT=/etc/vlink/certs/client.pem
export VLINK_SSL_KEY=/etc/vlink/certs/client.key

# 性能分析
export VLINK_PROFILER_ENABLE=1

# 发现与插件
export VLINK_DISCOVER_DISABLE=1
export VLINK_PLUGIN_DIR=/usr/lib/vlink/plugins

# 系统
export VLINK_TMP_DIR=/var/run/vlink
export VLINK_MEMORY_LEVEL=3
export VLINK_INTRA_BIND=1
```

## 编译与运行

```bash
mkdir build && cd build
cmake .. -DCMAKE_PREFIX_PATH=/path/to/vlink/install
make example_url_environment
./output/bin/example_url_environment
```

## 预期输出

```
[I] ==================================================
[I]   Category 1: Transport Configuration
[I] ==================================================
[I]   VLINK_DDS_DOMAIN = (not set)  --  Default DDS domain ID
[I]   -> Set VLINK_DDS_DOMAIN=42
[I]   VLINK_DDS_DOMAIN = 42  --  Default DDS domain ID
[I]   VLINK_DDS_BIND = (not set)  --  Bind DDS to specific NIC IP
[I]   VLINK_MQTT_BROKER = (not set)  --  MQTT broker URI
...
[I] ==================================================
[I]   Shell Command Reference
[I] ==================================================
[I]   export VLINK_DDS_DOMAIN=42
...
```

## 扩展思考

- 在容器化部署中（Docker/K8s），环境变量是最自然的配置注入方式。VLink 的所有关键参数都可以通过环境变量配置，无需修改代码。
- `VLINK_LOG_CONSOLE_LEVEL` 可以在运行时通过 `Logger::set_console_level()` 动态调整。
- `VLINK_BAG_PATH` 配合 CI/CD 可以自动化录制测试数据，用于回归测试和问题复现。
- 安全相关的环境变量（`VLINK_SSL_*`）建议在生产环境中通过 Secret 管理工具注入，不要硬编码在配置文件中。
- `VLINK_MEMORY_LEVEL` 控制 `vlink::MemoryPool` 的分级 chunk 配置（默认 3 Balanced）。`Bytes::init_memory_pool()` 会以 `gen_by_env=true` 触发全局池构造，从而读取该变量；高频小消息场景可调到 4-5 增加预留 block 数。
- `Utils::set_env` 的 `force=false` 参数允许应用程序设置默认值，同时允许部署环境通过 shell 变量覆盖。
