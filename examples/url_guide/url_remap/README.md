# URL Remap -- VLink URL 运行时重映射详解

## 1. 概述

`UrlRemap` 通过 JSON 配置文件实现运行时 URL 翻译，无需重新编译即可切换传输协议、重命名主题，或实现多环境部署。

```
应用代码 --> "intra://sensor/lidar" --> UrlRemap --> "dds://sensor/lidar"
```

## 2. JSON 配置格式

```json
{
  "intra://sensor/lidar": "dds://sensor/lidar",
  "intra://sensor/camera": "shm://sensor/camera",
  "intra://vehicle/speed": "zenoh://vehicle/speed"
}
```

扁平键值对：key 是源 URL 模式，value 是目标 URL。

## 3. 核心 API

| 方法 | 说明 |
|------|------|
| `load(file_path)` | 加载 JSON 文件，返回 bool |
| `unload()` | 清空重映射表 |
| `reload(file_path)` | 卸载后重新加载 |
| `convert(url)` | 翻译 URL，无匹配则返回原始 URL |
| `is_valid()` | 是否已加载 |
| `set_enable_log(true)` | 启用转换日志 |
| `get_error_string()` | 最近一次操作的错误信息 |

## 4. 匹配算法

`convert()` 按顺序扫描重映射列表，返回第一个 key 作为输入 URL **子串**匹配成功的目标 URL。结果被缓存以避免重复扫描。

**重要**: 顺序敏感！更具体的规则应放在更通用的规则之前。

```json
{
  "intra://sensor/lidar": "dds://sensor_lidar",
  "intra://sensor": "dds://sensor_default"
}
```

## 5. 环境变量

| 变量 | 说明 |
|------|------|
| `VLINK_URL_REMAP` | 重映射 JSON 文件路径 |

在启用 URL remap 的构建中，进程首次构造 `Url` 前设置 `VLINK_URL_REMAP` 后，每个 VLink 节点构造器都会自动执行重映射。

## 6. 关键代码分析

### 6.1 基本使用

```cpp
UrlRemap remap;
remap.load("/etc/vlink/remap.json");
const std::string& result = remap.convert("intra://sensor/lidar");
// result == "dds://sensor/lidar"
```

### 6.2 多环境切换

```json
// dev.json
{"app://sensor/lidar": "intra://sensor/lidar"}
// staging.json
{"app://sensor/lidar": "dds://sensor/lidar?domain=10"}
// prod.json
{"app://sensor/lidar": "zenoh://prod/sensor/lidar?domain=0"}
```

应用代码始终使用 `app://` URL，部署环境决定加载哪个 JSON 文件。

### 6.3 reload() -- 热重载

```cpp
remap.reload("/new/config.json");
```

`reload()` 原子性地卸载旧配置并加载新配置，适用于运行中的配置更新。

### 6.4 错误处理

```cpp
if (!remap.load("/nonexistent.json")) {
    VLOG_E("Error: ", remap.get_error_string());
}
```

### 6.5 自动重映射

```bash
export VLINK_URL_REMAP=/etc/vlink/remap.json
```

设置后，所有 VLink 节点构造函数自动对传入的 URL 执行重映射。

## 7. 编译与运行

```bash
mkdir build && cd build
cmake .. -DCMAKE_PREFIX_PATH=/path/to/vlink/install
make example_url_remap
./output/bin/example_url_remap
```

## 8. 预期输出

```
[I] === Example 1: Basic UrlRemap usage ===
[I]   load():1
[I]   is_valid():1
[I]   intra://sensor/lidar ->  dds://sensor/lidar
[I]   intra://sensor/camera ->  shm://sensor/camera
[I]   intra://unknown/topic ->  intra://unknown/topic (unchanged)
[I] === Example 3: unload() and reload() ===
[I]   After unload - is_valid():0
...
[I] === Example 5: Environment-based URL switching ===
[I]   [dev] app://sensor/lidar ->  intra://sensor/lidar
...
```

## 9. 扩展思考

- UrlRemap 的子串匹配意味着同一个规则可以匹配带不同查询参数的 URL（如 `intra://sensor` 匹配 `intra://sensor?event=foo`），但目标 URL 是固定的。
- 在 CI/CD 流水线中，可以为不同部署阶段维护不同的 remap JSON 文件。
- `convert()` 的结果缓存使得重复转换的开销为 O(1)。
- UrlRemap 不是线程安全的，所有方法应在单一线程中调用，或由调用者提供同步保护。
- 结合 `VLINK_BAG_PATH` 可以在不同环境下录制到不同路径，实现完整的测试/生产隔离。
