# zerocopy/ -- 零拷贝示例

高性能场景下的零拷贝数据传输，主要用于 `shm://` 传输后端。

| 工程 | 说明 |
|------|------|
| `zerocopy_loan` | `loan()` / `return_loan()` 基本 API |
| `zerocopy_raw_data` | 零拷贝原始数据传输 |
| `zerocopy_camera_frame` | 零拷贝相机帧传输场景 |
| `zerocopy_point_cloud` | 零拷贝点云数据传输场景 |

## 相关文档

详细原理参见 [doc/10-zerocopy.md](../../doc/10-zerocopy.md)。
