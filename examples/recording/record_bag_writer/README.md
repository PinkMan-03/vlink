# VLink BagWriter 手动录制示例

## 概述

本示例演示了 `BagWriter` 的直接使用方法。`BagWriter` 是 VLink 录制子系统的核心抽象类，提供了基于 `MessageLoop` 的异步消息录制能力。与 `set_record_path()` 自动录制不同，`BagWriter` 允许开发者完全控制录制过程，包括自定义压缩、文件分割、时间戳和标签等。

## BagWriter 架构

`BagWriter` 是一个抽象基类，继承自 `MessageLoop`。它有两个具体实现：

| 实现类 | 文件扩展名 | 存储后端 |
|--------|-----------|----------|
| `DatabaseWriter` | `.vdb` 及其他 | SQLite 数据库 |
| `McapWriter` | `.vcap` / `.vcapx` | MCAP 二进制格式 |

`BagWriter::create()` 工厂方法根据文件扩展名自动选择实现。

## BagWriter::create() API

```cpp
[[nodiscard]] static std::shared_ptr<BagWriter> create(
    const std::string& path,
    const Config& config = {});
```

| 参数 | 类型 | 说明 |
|------|------|------|
| `path` | `const std::string&` | 输出文件路径，扩展名决定格式 |
| `config` | `const Config&` | 录制配置，默认为空配置 |

返回 `shared_ptr<BagWriter>`。创建后需调用 `async_run()` 启动事件循环线程。

## BagWriter::Config 完整参考

```cpp
struct Config final {
    std::string tag_name;           // 嵌入 bag 头部的标签名
    CompressType compress;          // 压缩算法
    bool wal_mode{false};           // 启用 SQLite WAL 模式
    bool enable_limit{false};       // 启用行数/字节数限制
    bool split_name_by_time{false}; // 分割文件名附加时间戳
    bool sync_mode{false};          // 同步写入磁盘
    bool optimize_on_exit{false};   // 关闭时执行 VACUUM 优化
    int64_t max_row_count;          // 分割前最大行数（50亿）
    int64_t max_bytes_size;         // 分割前最大字节数（512GB）
    int64_t split_by_size;          // 按大小分割阈值（字节）
    int64_t split_by_time{0};       // 按时间分割间隔（毫秒）
    int64_t begin_time{0};          // 录制起始时间戳
    int64_t cache_size;             // SQLite 页缓存大小
    int64_t compress_start_size;    // 最小压缩阈值（字节）
    int64_t compress_level{3};      // 压缩级别
    int64_t max_task_depth;         // 最大待处理写任务数
    int64_t max_memory_size;        // 最大内存缓存大小
    int64_t start_timestamp{0};     // 覆盖 bag 起始时间戳
    std::unordered_set<std::string> ignore_compress_urls; // 不压缩的 URL 集合
};
```

### Config 字段详解

#### tag_name
嵌入到 bag 文件元数据头部的标签字符串。可用于标识录制会话，如 `"regression_test_v2"`。通过 `VLINK_BAG_TAG` 环境变量也可全局设置默认标签。

#### compress（CompressType 枚举）

| 值 | 名称 | 后端实际效果 |
|----|------|------|
| `kCompressNone` (0) | 无压缩 | 所有后端都不压缩 |
| `kCompressAuto` (1) | 自动 | `.vdb` 走 LZAV；`.vcap` 走 Zstandard |
| `kCompressZstd` (2) | Zstandard | `.vcap` 走 Zstd；`.vdb` 路径下被视为不压缩 |
| `kCompressLz4` (3)  | LZ4       | `.vdb` 和 `.vcap` 路径下均被视为不压缩（当前实现未走 LZ4） |
| `kCompressLzav` (4) | LZAV      | `.vdb` 走 LZAV；`.vcap` 路径下被视为不压缩 |

> 枚举值不代表后端实际支持。详见 [doc/12-bag-recording.md](../../../doc/12-bag-recording.md) "压缩矩阵"。

#### compress_start_size
只有负载大小 >= 此阈值（字节）的消息才会被压缩。小于此阈值的消息直接存储。默认 128 字节。

#### split_by_size / split_by_time
文件分割策略。当 bag 文件大小达到 `split_by_size` 字节或经过 `split_by_time` 毫秒后，自动创建新文件。`split_by_time` 为 0 表示禁用时间分割。

#### wal_mode
启用 SQLite WAL（Write-Ahead Log）模式。WAL 模式提供更好的崩溃恢复能力和并发读写性能。仅对 `DatabaseWriter` 有效。

#### optimize_on_exit
关闭文件时执行 VACUUM 操作，回收未使用的数据库页。适用于录制完成后需要最小化文件体积的场景。

## BagWriter::push() API

```cpp
virtual int64_t push(
    const std::string& url,
    const std::string& ser_type,
    SchemaType schema_type,
    ActionType action_type,
    const Bytes& data,
    int64_t* microseconds_timestamp = nullptr,
    bool immediate = false) = 0;
```

| 参数 | 类型 | 说明 |
|------|------|------|
| `url` | `const std::string&` | VLink URL，例如 `"dds://my/topic"` |
| `ser_type` | `const std::string&` | 具体序列化类型名，例如 `"demo.proto.PointCloud"`、`"raw"` |
| `schema_type` | `SchemaType` | 负载所属的粗粒度 schema 家族，例如 `kProtobuf`、`kFlatbuffers`、`kRaw` |
| `action_type` | `ActionType` | 操作类型：`kPublish`、`kSubscribe`、`kSet` 等 |
| `data` | `const Bytes&` | 序列化后的消息负载 |
| `microseconds_timestamp` | `int64_t*` | 自定义时间戳（微秒），`nullptr` 表示使用当前时间 |
| `immediate` | `bool` | `true` 表示同步写入（绕过队列），`false` 表示异步 |

**返回值：** 消息的单调递增序列号，错误时返回负值。

`schema_type` 与 `ser_type` 分离后，BagWriter 可以在 discovery、proxy、viewer、webviz 等运行时链路中直接选择 protobuf 或 flatbuffers 技术栈，而不需要再靠字符串猜测。

## BagWriter::filter_get() API

```cpp
[[nodiscard]] static std::shared_ptr<BagWriter> filter_get(const std::string& path);
```

在全局注册表中查找指定路径的 writer。若存在则返回已有实例的 `shared_ptr`；否则创建新 writer、调用 `async_run()` 启动，并注册到全局表。最后一个 `shared_ptr` 释放时自动从注册表中移除。

这是 `set_record_path()` 内部使用的机制，确保多个节点共享同一路径时不会创建重复的 writer。

## register_split_callback() API

```cpp
virtual void register_split_callback(SplitCallback&& callback, bool before) = 0;
```

| 参数 | 类型 | 说明 |
|------|------|------|
| `callback` | `SplitCallback&&` | 分割发生时的回调，签名为 `void(int split_index, const std::string& split_filename)` |
| `before` | `bool` | `true` 表示在新文件打开**之前**调用；`false` 表示在**之后**调用 |

## 编译和运行

```bash
cmake --build . --target example_record_bag_writer
./output/bin/example_record_bag_writer
```

## 输出文件

| 文件 | 说明 |
|------|------|
| `/tmp/bag_writer_basic.vdb` | 无压缩、无分割的基础录制 |
| `/tmp/bag_writer_compressed.vdb` | 使用 LZAV 压缩 |
| `/tmp/bag_writer_split.vdb` | 按大小分割（100KB） |
| `/tmp/bag_writer_wal.vdb` | 启用 WAL 模式 |
| `/tmp/bag_writer_tagged.vdb` | 带标签的录制 |
| `/tmp/bag_writer_timestamp.vdb` | 自定义时间戳 |
| `/tmp/bag_writer_shared.vdb` | 共享 writer 实例 |

## 最佳实践

1. **始终调用 `async_run()`**：`BagWriter::create()` 返回的 writer 尚未启动事件循环，必须调用 `async_run()` 后才能录制
2. **使用 `filter_get()` 共享 writer**：避免多个组件同时打开同一文件
3. **选择合适的压缩**：高频小消息用 `kCompressNone`；`.vdb` 后端录大消息用 `kCompressLzav`；`.vcap`（MCAP）后端录大消息用 `kCompressZstd`
4. **配合分割使用**：长时间录制务必配置 `split_by_size` 或 `split_by_time`，避免单个文件过大
5. **预留关闭时间**：确保 writer 析构时有足够时间刷新缓冲区
