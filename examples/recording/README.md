# recording/ -- 数据录制示例

通信数据的录制、存储和回放功能，支持 Bag 格式、MCAP 格式和压缩。

| 工程 | 说明 |
|------|------|
| `record_basic` | 基本 Bag 录制功能 |
| `record_bag_writer` | BagWriter API 详细用法 |
| `record_bag_reader` | BagReader API 详细用法 |
| `record_compression` | 压缩录制（None / Auto / Zstd / LZ4 / LZAV） |
| `record_mcap` | MCAP 格式录制 |

## 相关文档

详细原理参见 [doc/12-bag-recording.md](../../doc/12-bag-recording.md)。
