# record_bag — 直接操作 `BagWriter` + `BagReader` 的完整 API

`record_basic` 演示了"在通信原语上挂 bag 路径"这种最简形态。本示例进一步直接操作 `BagWriter` / `BagReader` API：手工 push 消息、配置压缩 / 分片 / WAL、读取并按时间/URL 过滤、用 `BagReaderProcessor` 合并多个 bag 时序输出。

合并自原 `record_bag_writer` + `record_bag_reader`，覆盖完整 bag 读写流程。

读完本示例你能掌握：

- BagWriter 的完整 API：`create` / `push` / Config 各字段 / split 回调。
- BagReader 的完整 API：`get_info` / play / output 回调 / Config 过滤。
- `filter_get` 共享 writer 实例。
- 多 bag 时序合并：`BagReaderProcessor`。

## 背景与适用场景

适用：

- 离线录制工具（不在业务进程中跑，独立 bag merger）。
- 数据回放仿真器：精确控制时间戳、过滤特定 topic。
- 录制后处理：合并多个 bag、按 URL 切分、转换格式。

不适合：

- 业务进程内简单录制（用 `record_basic/` 的 `set_record_path`）。

## 核心 API

### BagWriter

| API | 签名 | 说明 |
|-----|------|------|
| `BagWriter::create` | `static std::shared_ptr<BagWriter> create(const std::string& path, const Config& = {})` | 工厂；按扩展名选 vdb / vcap |
| `BagWriter::push` | `void push(const std::string& url, SerType, SchemaType, ActionType, const Bytes&, bool immediate = false, int64_t* ts = nullptr)` | 写一条消息 |
| `BagWriter::Config` | `{ compress, split_by_size, wal_mode, tag_name, ... }` | 写入配置 |
| `BagWriter::filter_get` | `static std::shared_ptr<BagWriter> filter_get(const std::string& path)` | 同路径共享实例 |
| `BagWriter::register_split_callback` | `void register_split_callback(cb, before_open)` | 分片切换回调 |

### BagReader

| API | 签名 | 说明 |
|-----|------|------|
| `BagReader::create` | `static std::shared_ptr<BagReader> create(const std::string& path, bool read_only = true)` | 打开 |
| `BagReader::get_info` | `BagInfo get_info() const` | 元信息（含 URL 列表、消息数、时间范围） |
| `BagReader::register_output_callback` | `void` | 回放回调 |
| `BagReader::register_finish_callback` | `void` | 完成回调 |
| `BagReader::play` | `void play(const Config&)` | 启动回放 |
| `BagReader::Config` | `{ rate, times, begin_time, end_time, filter_urls, skip_blank }` | 回放配置 |
| `BagReader::seek` | `bool seek(int64_t timestamp, bool force_play = false)` | 跳到指定时间 |

### BagReaderProcessor

| API | 签名 | 说明 |
|-----|------|------|
| `BagReaderProcessor::push` | `void push(int64_t ts, const std::string& url, ActionType, const Bytes&)` | 合并多 reader 时按时序 push |
| `BagReaderProcessor::register_output_callback` | `void` | 时序输出回调 |

## 代码导读

### 1. 写 50 条消息

```cpp
auto writer = vlink::BagWriter::create("/tmp/example_bag.vdb");

for (int i = 0; i < 50; ++i) {
  int64_t ts = vlink::ElapsedTimer::get_cpu_timestamp(vlink::ElapsedTimer::kMilli) + i * 10;
  std::string text = "msg_" + std::to_string(i);
  vlink::Bytes payload = vlink::Bytes::from_string(text);

  std::string url = (i % 2 == 0) ? "intra://topic_a" : "intra://topic_b";
  writer->push(url, vlink::Serializer::kStringType, vlink::SchemaType::kRaw,
               vlink::ActionType::kPublish, payload, /*immediate=*/false, &ts);
}
writer.reset();   // 析构 flush
```

### 2. Config: 压缩 + 分片 + WAL + tag

```cpp
vlink::BagWriter::Config cfg;
cfg.compress = vlink::CompressType::kLzav;
cfg.split_by_size = 10 * 1024 * 1024;   // 每 10MB 切分
cfg.wal_mode = true;                     // SQLite WAL 模式
cfg.tag_name = "experiment_42";

auto compressed_writer = vlink::BagWriter::create("/tmp/compressed.vdb", cfg);
```

### 3. filter_get 共享 writer

```cpp
auto a = vlink::BagWriter::filter_get("/tmp/shared.vdb");
auto b = vlink::BagWriter::filter_get("/tmp/shared.vdb");
// a 与 b 共享同一 writer 实例；refcount 管理 lifetime
```

### 4. 读元信息

```cpp
auto reader = vlink::BagReader::create("/tmp/example_bag.vdb");
auto info = reader->get_info();
MLOG_I("size={} total_msgs={} begin={} end={}", info.size_bytes, info.total_messages,
       info.begin_time, info.end_time);
for (const auto& [url, stat] : info.url_stats) {
  MLOG_I("  url={} count={}", url, stat.count);
}
```

### 5. 基础回放

```cpp
reader->register_output_callback([](int64_t ts, const std::string& url,
                                    vlink::SerType ser, vlink::SchemaType schema,
                                    vlink::ActionType action, const vlink::Bytes& data) {
  VLOG_I("ts=", ts, " url=", url, " size=", data.size());
});
reader->register_finish_callback([]() { VLOG_I("playback finished"); });

vlink::BagReader::Config cfg;
cfg.rate = 1.0;          // 1x 实时速度
cfg.times = 1;
reader->play(cfg);
```

### 6. 时间 + URL 过滤

```cpp
vlink::BagReader::Config cfg;
cfg.rate = 2.0;                      // 2x 加速回放
cfg.begin_time = 1000;
cfg.end_time = 5000;
cfg.filter_urls = {"intra://topic_a"};
cfg.skip_blank = true;
reader->play(cfg);
```

### 7. 多 bag 时序合并

```cpp
vlink::BagReaderProcessor proc;
proc.register_output_callback([](int64_t ts, const std::string& url, ...) { /* ... */ });

// 把 readerA、readerB 的内容按时间戳 push 进 processor
readerA->register_output_callback([&proc](int64_t ts, const std::string& url, ..., const vlink::Bytes& d) {
  proc.push(ts, url, ..., d);
});
readerB->register_output_callback([&proc](int64_t ts, const std::string& url, ..., const vlink::Bytes& d) {
  proc.push(ts, url, ..., d);
});
readerA->play({});
readerB->play({});
```

processor 按 ts 单调输出；适合"多个并行子系统录制 → 全局时序回放"场景。

## 运行

```bash
./build/output/bin/example_record_bag
```

预期产物 `/tmp/example_bag.vdb`、`/tmp/compressed.vdb` 等。

预期输出（节选）：

```
=== writer 50 messages ===
  pushed 50 messages to /tmp/example_bag.vdb
=== writer config ===
  compressed.vdb written with LZAV + 10MB split
=== reader info ===
  size=4096 total_msgs=50 begin=... end=...
  url=intra://topic_a count=25
  url=intra://topic_b count=25
=== basic playback ===
  ts=... url=intra://topic_a size=5
  ts=... url=intra://topic_b size=5
  ...
  playback finished
=== filtered playback (2x rate) ===
  only topic_a messages
=== BagReaderProcessor merge ===
  time-ordered output
```

## 常见陷阱

1. **push 时 ts 必须单调**：reader 假定 ts 递增；乱序 push 会让 seek / 时间范围过滤错乱。
2. **WAL 模式 + 写满磁盘**：vdb 是 SQLite，磁盘满会失败；用 `register_split_callback` 监控并切换。
3. **filter_get 共享 writer 析构**：每个 shared_ptr 都引用一份；最后一个析构时才真 close。
4. **rate > 1 时 output 回调跑得快**：业务回调若慢会阻塞回放；按需要降 rate 或异步处理。
5. **filter_urls 大小写**：URL 精确匹配，包括大小写。

## 设计要点

- BagWriter 是 MessageLoop 派生：push 投递到内部队列后立即返回。
- VDB 内部用 SQLite + 自定义 schema；vcap 用 MCAP 标准格式。
- BagReaderProcessor 用最小堆按 ts 排序；多 reader 时序合并 O(log N)。

## 配图

无专属配图。

## 参考

- `../record_basic/` — 节点级简单录制
- `../record_mcap/` — VCAP / MCAP 格式
- `../record_compression/` — 压缩对比
- `vlink/include/vlink/extension/bag_writer.h` / `bag_reader.h` / `bag_reader_processor.h` — 完整接口
- 顶层 `doc/12-bag-recording.md` — 录制系统章节
