# C API 字段示例

## 概述

本示例演示使用纯 C 语言的 VLink Setter/Getter（字段模型）API。字段模型保留最新值，支持推送模式（回调通知）和轮询模式（主动获取）。创建接口里的 `vlink_schema_info_t` 会把 `ser + schema` 作为精确运行时 metadata 一次性写入节点。

## 核心 API

### Setter（写入者）

```c
vlink_schema_info_t schema = {
    .ser = "text",
    .schema = VLINK_SCHEMA_RAW,
};

vlink_setter_handle_t setter;
vlink_create_setter("intra://field", &schema, &setter);
vlink_set(setter, data, size);
vlink_destroy_setter(&setter);
```

### Getter 推送模式

```c
void on_change(const uint8_t* data, size_t size, void* ud) {
    printf("Value changed: %.*s\n", (int)size, (const char*)data);
}

vlink_getter_handle_t getter;
vlink_create_getter("intra://field", &schema, &getter, on_change, NULL);
```

### Getter 轮询模式

```c
vlink_getter_handle_t getter;
vlink_create_getter("intra://field", &schema, &getter, NULL, NULL);

uint8_t buf[256];
size_t buf_size = sizeof(buf);
int ret = vlink_get(getter, buf, &buf_size);
// buf_size 被更新为实际数据大小
```

## vlink_get 返回码

| 返回值 | 含义 |
|--------|------|
| `VLINK_RET_NO_ERROR` | 成功，`*size` 更新为实际大小 |
| `VLINK_RET_TRANSFER_ERROR` | 尚无值可用 |
| `VLINK_RET_MEMORY_ERROR` | 缓冲区太小，`*size` 不变 |
| `VLINK_RET_INVALID_ERROR` | 无效参数 |

## 编译与运行

```bash
cd build
cmake .. && make example_c_field
./output/bin/example_c_field
```

## 注意事项

- Getter 的 `msg_callback` 为 NULL 时使用轮询模式
- 通过 `vlink_schema_info_t` 显式传入 `ser + schema`
- `schema` 会按 `VLINK_SCHEMA_*` 的语义直接映射到底层 `SchemaType`
- `vlink_get()` 将最新值拷贝到用户提供的缓冲区
- 入口时 `*size` 必须为缓冲区容量，出口时更新为实际大小
- Setter 的每次 `vlink_set()` 覆盖前一个值
- 字段模型支持迟到的 Getter 获取最新值（late-join）

## 相关文档

详细原理参见 [doc/18-c-api.md](../../../doc/18-c-api.md)。
