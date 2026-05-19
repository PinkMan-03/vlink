# VLink 录制基础示例

## 1. 概述

本示例演示了 VLink 框架中两种核心录制机制：**逐节点录制**（per-node recording）和**全局录制**（global recording）。录制功能允许将通信过程中的所有消息持久化到 bag 文件中，便于后续回放、调试和分析。

![录制与回放流程](images/recording-flow.png)

VLink 支持两种 bag 文件格式：
- **SQLite 格式**（`.vdb` 扩展名）：默认格式，使用 SQLite 数据库存储，支持快速索引和查询
- **MCAP 格式**（`.vcap` / `.vcapx` 扩展名）：开放标准格式，兼容 Foxglove Studio 等可视化工具

## 2. 录制机制详解

### 2.1 逐节点录制（Per-Node Recording）

逐节点录制通过在任意 VLink 通信节点上调用 `set_record_path(path)` 来启用。每个节点独立记录自己发送或接收的消息。

```cpp
Publisher<Bytes> pub("dds://record_basic/event");
pub.set_record_path("/tmp/record_basic_pub.vdb");
```

**关键特性：**

- **独立性**：每个节点可以使用不同的 bag 文件路径，实现分开录制
- **共享写入器**：如果多个节点使用相同的路径，VLink 内部通过 `BagWriter::filter_get()` 自动复用同一个 `BagWriter` 实例，避免文件冲突
- **传输限制**：`intra://` 方案和 CDR 格式的 DDS 消息不支持录制（内部消息已在进程内处理，CDR 消息有特殊编码）
- **全模型支持**：Publisher、Subscriber、Server、Client、Setter、Getter 六种节点类型均支持 `set_record_path()`

### 2.2 全局录制（Global Recording via VLINK_BAG_PATH）

全局录制通过设置 `VLINK_BAG_PATH` 环境变量来启用。VLink 会自动创建一个进程级别的全局 `BagWriter` 单例，所有节点的消息自动记录到该文件中，无需逐个节点调用 `set_record_path()`。

```bash
VLINK_BAG_PATH=/tmp/global_record.vdb ./example_record_basic
```

**全局录制的优势：**

- **零侵入**：不需要修改任何应用代码
- **集中存储**：所有通信数据集中在一个 bag 文件中
- **运行时可控**：通过环境变量动态开启或关闭

### 2.3 Bag 文件标签（VLINK_BAG_TAG）

设置 `VLINK_BAG_TAG` 环境变量可以为录制的 bag 文件添加标签字符串。标签会被嵌入到 bag 文件的元数据头中，方便后续识别和分类。

```bash
VLINK_BAG_PATH=/tmp/record.vdb VLINK_BAG_TAG=regression_test ./example_record_basic
```

### 2.4 全局写入器 API

在代码中可以通过 `BagWriter::global_get()` 获取全局 BagWriter 实例指针：

```cpp
auto* global_writer = BagWriter::global_get();
if (global_writer) {
    // 全局录制已激活
    global_writer->push("dds://my/topic", "raw",
                        SchemaType::kRaw,
                        ActionType::kPublish, data);
}
```

该函数：
- 若 `VLINK_BAG_PATH` 已设置，首次调用时自动创建全局 `BagWriter` 并启动事件循环
- 若环境变量未设置，返回 `nullptr`

## 3. set_record_path() API 详解

```cpp
void Node<ImplT, SecT>::set_record_path(const std::string& path);
```

**参数说明：**

| 参数 | 类型 | 说明 |
|------|------|------|
| `path` | `const std::string&` | bag 文件的输出路径。`.vdb`/`.vdbx` 使用 SQLite，`.vcap`/`.vcapx` 使用 MCAP |

**行为说明：**

- 内部调用 `BagWriter::filter_get(path)` 获取或创建 `BagWriter` 实例
- 路径后缀不支持时不会创建 writer；支持 `.vdb/.vdbx/.vcap/.vcapx`
- 如果已有同路径的 writer 存在，则复用
- `BagWriter` 的生命周期由 `shared_ptr` 管理，最后一个引用释放时自动关闭文件
- 对 `intra://` 和 CDR DDS 消息不生效（静默忽略）

## 4. 支持的通信模型

| 通信模型 | 节点类型 | 录制的 ActionType |
|----------|----------|-------------------|
| Event（事件） | Publisher | `kPublish` |
| Event（事件） | Subscriber | `kSubscribe` |
| Method（RPC） | Server | `kServerRequest` / `kServerResponse` |
| Method（RPC） | Client | `kClientRequest` / `kClientResponse` |
| Field（字段） | Setter | `kSet` |
| Field（字段） | Getter | `kGet` |

## 5. 编译和运行

```bash
cmake -B build -S . -DCMAKE_PREFIX_PATH=/path/to/vlink/install
cmake --build build --target example_record_basic

# 运行（逐节点录制）
./build/output/bin/example_record_basic

# 运行（全局录制）
VLINK_BAG_PATH=/tmp/global_record.vdb ./build/output/bin/example_record_basic

# 运行（全局录制 + 标签）
VLINK_BAG_PATH=/tmp/global_record.vdb VLINK_BAG_TAG=my_tag ./build/output/bin/example_record_basic
```

## 6. 输出文件说明

运行后会在 `/tmp/` 目录下生成以下 bag 文件：

| 文件 | 内容 |
|------|------|
| `record_basic_pub.vdb` | Publisher 发送的所有消息 |
| `record_basic_sub.vdb` | Subscriber 接收的所有消息 |
| `record_basic_rpc.vdb` | Server/Client RPC 交互的请求和响应 |
| `record_basic_field.vdb` | Setter/Getter 字段值的读写操作 |
| `global_record.vdb`（如设置环境变量） | 所有节点的全部消息 |

## 7. 注意事项

1. **录制是异步的**：`set_record_path()` 和 `BagWriter::push()` 将消息加入内部队列，实际写入由 `MessageLoop` 的后台线程执行
2. **线程安全**：`BagWriter::push()` 是线程安全的，多个节点可以同时向同一个 writer 推送消息
3. **资源释放**：建议在程序退出前预留足够的时间（如 `sleep`），确保所有排队的消息已写入磁盘
4. **磁盘空间**：长时间录制会消耗大量磁盘空间，生产环境建议配合分割模式和压缩功能使用（参见 `record_bag_writer` 和 `record_compression` 示例）

## 8. 相关文档

详细原理参见 [doc/12-bag-recording.md](../../../doc/12-bag-recording.md)。
