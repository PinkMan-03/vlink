# VLink BagReader 回放示例

## 概述

本示例演示了 `BagReader` 的完整使用方法，包括 bag 文件信息查看、各种回放配置、时间范围过滤、URL 过滤以及 `BagReaderProcessor` 时间排序合并。

`BagReader` 是 VLink 录制子系统的回放核心，继承自 `MessageLoop`，提供基于事件循环的异步消息回放能力。

## BagReader 架构

`BagReader` 是一个抽象基类，有两个具体实现：

| 实现类 | 文件扩展名 | 存储后端 |
|--------|-----------|----------|
| `DatabaseReader` | `.vdb` 及其他 | SQLite |
| `McapReader` | `.vcap` / `.vcapx` | MCAP |

`BagReader::create()` 根据文件扩展名自动选择实现。

## BagReader::create() API

```cpp
[[nodiscard]] static std::shared_ptr<BagReader> create(
    const std::string& path,
    bool read_only = true,
    bool try_to_fix = false);
```

| 参数 | 类型 | 默认值 | 说明 |
|------|------|--------|------|
| `path` | `const std::string&` | - | bag 文件路径 |
| `read_only` | `bool` | `true` | 只读模式打开 |
| `try_to_fix` | `bool` | `false` | 文件损坏时尝试修复 |

## BagReader::Info 结构体

通过 `reader->get_info()` 获取 bag 文件元数据：

```cpp
struct Info final {
    std::string file_name;        // 文件绝对路径
    std::string tag_name;         // 标签名
    std::string version;          // 格式版本号
    std::string storage_type;     // 存储后端（"sqlite" / "mcap"）
    std::string compression_type; // 压缩算法名
    std::string date_time;        // 录制开始时间
    std::string process_name;     // 录制进程名
    bool has_completed;           // 是否正常结束录制
    bool has_schema;        // 是否包含已嵌入的 schema
    int64_t start_timestamp;      // 起始时间戳（毫秒）
    int64_t total_duration;       // 总时长（毫秒）
    int64_t file_size;            // 文件大小（字节）
    int64_t message_count;        // 消息总数
    std::vector<UrlMeta> url_metas; // 每个 URL 的统计信息
};
```

### Info::UrlMeta 结构体

```cpp
struct UrlMeta final {
    std::string url;       // 完整 VLink URL
    std::string url_type;  // 通信模型类型（"Event"/"Method"/"Field"）
    std::string ser_type;  // 具体序列化类型名（"demo.proto.PointCloud"/"raw" 等）
    SchemaType schema_type; // 粗粒度 schema 家族（kProtobuf/kFlatbuffers/kRaw/kZeroCopy/kUnknown）
    size_t count;          // 该 URL 的消息总数
    size_t size;           // 该 URL 的压缩后总字节数
    double freq;           // 平均消息频率（Hz）
    double loss;           // 声明的丢包率 [0.0, 1.0]
};
```

## BagReader::Config 回放配置

```cpp
struct Config final {
    int64_t begin_time{0};    // 回放起始时间（毫秒），0 = 从头开始
    int64_t end_time{0};      // 回放结束时间（毫秒），0 = 到末尾
    int times{1};             // 循环次数，kInfinite(-1) = 无限循环
    double rate{1.0};         // 回放速率倍数，1.0 = 实时
    bool skip_blank{false};   // 跳过消息间的静默间隔
    int64_t force_delay{-1};  // 强制消息间隔（毫秒），-1 = 使用时间戳
    bool auto_pause{false};   // 每条消息后自动暂停
    bool auto_quit{false};    // 回放结束后自动退出循环线程
    std::unordered_set<std::string> filter_urls; // URL 白名单，空 = 全部
};
```

### 配置字段详解

#### rate（速率倍数）
控制回放速度。`1.0` 为实时回放，`2.0` 为两倍速，`0.5` 为半速。配合 `skip_blank` 可以快速浏览数据而不等待静默间隔。

#### times（循环次数）
- `1`：单次回放
- `3`：回放三次
- `BagReader::kInfinite`（`-1`）：无限循环

#### begin_time / end_time（时间范围）
以毫秒为单位指定回放范围。`begin_time=0` 从头开始，`end_time=0` 到末尾结束。这两个值是相对于录制起始时间戳的偏移量。

#### filter_urls（URL 过滤）
指定需要回放的 URL 集合。如果为空集则回放所有 URL。适用于只关注特定 topic 的调试场景。

#### skip_blank（跳过静默）
当消息之间存在较长的时间间隔时，启用此选项可以跳过等待时间，连续输出消息。

#### force_delay（强制间隔）
覆盖消息间的时间间隔。设为 `0` 时以最快速度连续输出所有消息，无论原始时间戳如何。`-1` 使用原始时间戳计算间隔。

## 回调类型

### OutputCallback
```cpp
using OutputCallback = vlink::MoveFunction<void(
    int64_t timestamp,         // 消息时间戳（微秒）
    const std::string& url,    // Topic URL
    ActionType action_type,    // 操作类型
    const Bytes& data)>;       // 消息负载（仅回调期间有效）
```

### StatusCallback
```cpp
using StatusCallback = vlink::MoveFunction<void(Status status)>;
// Status: kStopped(0), kPaused(1), kPlaying(2)
```

### FinishCallback
```cpp
using FinishCallback = vlink::MoveFunction<void(bool is_interrupted)>;
// is_interrupted: true 表示由 stop() 中断，false 表示自然结束
```

## 回放控制 API

| 方法 | 说明 |
|------|------|
| `play(config)` | 启动回放 |
| `stop()` | 停止回放并重置到开头 |
| `pause()` | 暂停回放 |
| `resume()` | 恢复暂停的回放 |
| `pause_to_next()` | 前进一条消息后暂停 |
| `jump(begin_time, rate, times, force_to_play)` | 跳转到指定时间点并继续回放 |
| `get_status()` | 获取当前回放状态 |
| `get_timestamp()` | 获取当前回放位置（录制时间戳，毫秒） |

## BagReaderProcessor

`BagReaderProcessor` 是一个时间排序缓冲器，用于将来自多个 `BagReader` 的消息按时间戳顺序合并输出。

```cpp
BagReaderProcessor::Config proc_config;
proc_config.min_cache_time = 500;    // 缓冲 500ms 后再输出
proc_config.max_cache_size = 256MB;  // 最大缓存 256MB

BagReaderProcessor processor(proc_config);
processor.register_output_callback([](int64_t ts, const std::string& url,
                                      ActionType action, const Bytes& data) {
    // 消息按时间戳顺序到达
});
```

典型场景：同时读取多个分割的 bag 文件，将它们的消息按时间顺序合并。

## 数据完整性工具

`BagReader` 提供三个异步完整性操作，返回 `std::future<bool>`：

| 方法 | 说明 |
|------|------|
| `check()` | 验证文件完整性 |
| `reindex()` | 重建索引表 |
| `fix(rebuild)` | 修复损坏文件，`rebuild=true` 从头重建索引 |

```cpp
auto result = reader->check();
if (result.get()) {
    VLOG_I("File integrity OK");
}
```

## 编译和运行

```bash
cmake --build . --target example_record_bag_reader
./output/bin/example_record_bag_reader
```

## 注意事项

1. **必须先调用 `async_run()`**：在调用 `play()` 之前必须启动事件循环
2. **OutputCallback 数据生命周期**：回调中的 `data` 引用仅在回调执行期间有效，如需保留请拷贝
3. **线程模型**：所有回调在 `BagReader` 的事件循环线程上触发
4. **多格式支持**：同一个 `BagReader::create()` 接口同时支持 SQLite 和 MCAP 格式
