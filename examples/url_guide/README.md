# url_guide/ -- URL 与传输后端示例

URL 格式说明和各传输后端的配置指南。VLink URL 的 transport 决定传输模块，host/path 标识话题，query 参数传递协议配置。

| 工程 | 说明 |
|------|------|
| `url_basics` | URL 基本格式和组件解析 |
| `url_environment` | 通过环境变量动态配置 URL |
| `url_remap` | JSON URL 重映射 |
| `url_intra` | `intra://` 进程内传输 |
| `url_shm` | `shm://` Iceoryx 共享内存传输 |
| `url_dds` | `dds://` FastDDS 传输 |
| `url_zenoh` | `zenoh://` Zenoh 传输 |
| `url_someip` | `someip://` SOME/IP 车载以太网传输 |
| `url_mqtt` | `mqtt://` MQTT 传输 |

## 1. 相关文档

详细原理参见 [doc/07-transport.md](../../doc/07-transport.md)。
