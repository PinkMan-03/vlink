# VLink Bytes 零拷贝示例

## 1. 概述

本示例演示了 VLink `Bytes` 类的五种所有权模式和零拷贝机制，包括自主分配（create）、浅拷贝（shallow_copy）、深拷贝（deep_copy）、贷出模式（loan_internal）、不透明指针包装（shallow_copy_ptr）以及偏移量（offset）前缀区域。`Bytes` 是 VLink 中的核心二进制数据载体，每个序列化或接收的消息都通过 `Bytes` 对象传递。

## 2. 文件说明

| 文件 | 说明 |
|------|------|
| `bytes_zerocopy.cc` | Bytes 零拷贝和所有权模型演示源码 |
| `CMakeLists.txt` | 构建配置，链接 `vlink::all` |

## 3. 构建与运行

```bash
cmake --build . --target example_bytes_zerocopy
./examples/base/bytes_zerocopy/example_bytes_zerocopy
```

## 4. 核心概念

### 4.1 Bytes 对象结构

`Bytes` 的总大小固定为 128 字节：
- 96 字节的内联栈存储（`stack_data_`）用于小缓冲区优化（SBO）
- 剩余 32 字节为元数据（data指针、size、capacity、offset、标志位）

当数据 <= 96 字节时，完全存储在对象内部，零堆分配。超过 96 字节时从内存池或系统堆分配。

### 4.2 五种所有权模式

| 工厂方法 | 拥有内存 | 拷贝行为 | 典型用途 |
|----------|---------|---------|---------|
| `Bytes::create()` | 是 | 深拷贝 | 新分配缓冲区 |
| `Bytes::shallow_copy()` | 否 | 指针别名 | 零拷贝包装外部缓冲区 |
| `Bytes::deep_copy()` | 是 | 深拷贝 | 拥有式拷贝外部缓冲区 |
| `Bytes::loan_internal()` | 否（贷出） | 指针别名 | iceoryx 零拷贝贷出 |
| `Bytes::shallow_copy_ptr()` | 否 | 指针别名 | 包装不透明指针（size==0） |

### 4.3 shallow_copy - 零拷贝别名

```cpp
uint8_t ext[128];
auto view = Bytes::shallow_copy(ext, sizeof(ext));
// view.is_owner() == false
// view.data() == ext（指向同一内存）
```

创建一个不拥有内存的 Bytes 对象，直接指向外部缓冲区。调用者必须确保外部缓冲区的生命周期长于 Bytes 对象。提供 `const` 和非 `const` 两个重载。

### 4.4 deep_copy - 拥有式拷贝

```cpp
auto owned = Bytes::deep_copy(ext, sizeof(ext));
// owned.is_owner() == true
// owned.data() != ext（独立内存）
```

分配新内存并拷贝数据。结果是完全独立的，即使原始缓冲区被释放也安全。

支持带偏移量的变体：
```cpp
auto buf = Bytes::deep_copy(data, size, offset);
// buf.data() == buf.real_data() + offset
```

### 4.5 loan_internal - iceoryx 贷出

```cpp
auto loaned = Bytes::loan_internal(shm_chunk, size);
// loaned.is_owner() == false
// loaned.is_loaned() == true
```

标记为"贷出"的 Bytes 对象在析构时**不会释放内存**，因为内存归 iceoryx RouDi 守护进程所有。这是 `shm://` 传输后端内部使用的工厂方法。

### 4.6 shallow_copy_ptr - 不透明指针

```cpp
auto ptr_bytes = Bytes::shallow_copy_ptr(&handle);
// ptr_bytes.is_ptr() == true（size==0, offset==0, !is_owner）
auto* h = ptr_bytes.to_ptr<HandleType>();
```

包装一个不透明指针，不关联任何字节大小。适合通过 Bytes API 传递 C 句柄或 iceoryx chunk 指针。

### 4.7 offset - 前缀预留区

```cpp
auto buf = Bytes::create(100, 4);  // 4 字节前缀
// buf.data() 指向用户数据区（偏移 4 字节后）
// buf.real_data() 指向缓冲区起始
// buf.real_data() + buf.offset() == buf.data()
```

offset 机制允许传输层在分配时预留缓冲区头部空间，后续可以在不重新分配的情况下原地写入协议头。

### 4.8 to_ptr - 类型转换

```cpp
auto* ptr = bytes.to_ptr<MyStruct>();
// 等效于 reinterpret_cast<MyStruct*>(bytes.real_data())
```

### 4.9 deep_copy_self - 非拥有转拥有

```cpp
auto view = Bytes::shallow_copy(ext, size);
view.deep_copy_self();
// view.is_owner() == true（已分配独立内存并拷贝数据）
```

将非拥有的别名转换为拥有式的深拷贝。如果已经是 owner，则是空操作。

## 5. 拷贝语义

| 源对象类型 | 拷贝构造行为 |
|-----------|-------------|
| owner | 深拷贝（分配新内存） |
| shallow alias | 浅拷贝（共享指针） |
| loaned | 浅拷贝（共享指针） |

移动构造转移所有状态，源对象变为空。

## 6. 代码执行流程

1. **shallow_copy (mutable/const)**：零拷贝包装外部缓冲区，验证别名关系
2. **deep_copy (external)**：拥有式拷贝，验证独立性
3. **deep_copy with offset**：带前缀偏移的深拷贝
4. **实例方法 deep_copy / shallow_copy**：在已有对象上执行拷贝
5. **deep_copy_self**：非拥有转拥有
6. **shallow_copy_ptr / to_ptr**：不透明指针包装与提取
7. **loan_internal**：模拟 iceoryx 零拷贝贷出
8. **拷贝构造语义**：owner 深拷贝 vs non-owner 浅拷贝
9. **移动语义**：状态完整转移

## 7. 内存池支持

```cpp
Bytes::init_memory_pool();    // 启动时调用一次
// ... 使用 Bytes ...
// 全局 MemoryPool 与进程生命周期一致
```

`Bytes` 的堆分配走 `vlink::MemoryPool`（分级 free-list 池）。`init_memory_pool()` 以 `use_env_level=true` 触发 `MemoryPool::global_instance()` 的首次构造，从而读取 `VLINK_MEMORY_LEVEL`（0..9，默认 3；0 = bypass 模式）以选择 tier 配置。

## 8. 注意事项

- `shallow_copy` 的 Bytes 不拥有内存，必须确保外部缓冲区生命周期足够长
- `loan_internal` 的 Bytes 析构时不释放内存
- `to_ptr` 使用 `reinterpret_cast`，需确保对齐兼容
- `is_ptr()` 仅在 `size==0 && offset==0 && !is_owner` 时返回 `true`
- 压缩功能（compress_data/uncompress_data）使用 LZAV 算法，最大 1MiB
- SBO 阈值为 96 字节（`Bytes::stack_size()`）
