# Bytes 类型序列化示例

## 1. 类型检测优先级链

![类型检测优先级链](images/serialization-type-chain.png)

## 2. 概述

本示例演示如何使用 VLink 的 `Bytes` 类型进行原始二进制数据的发布与订阅。`Bytes` 是 VLink 序列化体系中优先级最高的类型（`kBytesType = 1`），它是一种**透传类型**——数据不经过任何序列化/反序列化处理，直接以原始字节形式传递。

## 3. 关键代码解析

### 3.1 编译期类型检测

```cpp
static_assert(Serializer::get_type_of<Bytes>() == Serializer::kBytesType,
              "Bytes must be detected as kBytesType");
```

`Serializer::get_type_of<T>()` 是一个 `constexpr` 函数，在**编译期**自动推导任意 C++ 类型对应的序列化器类型。`Bytes` 类型永远被识别为 `kBytesType`，这是类型检测链中的第一个，优先级最高。

### 3.2 创建 Bytes 对象的多种方式

```cpp
// 方式一：从字符串创建（深拷贝字符串内容到 Bytes 缓冲区）
auto bytes_from_str = Bytes::from_string("Hello VLink Bytes!");

// 方式二：使用初始化列表（直接指定原始字节值）
Bytes bytes_from_list{0x48, 0x65, 0x6C, 0x6C, 0x6F};

// 方式三：手动创建固定大小缓冲区并填充
auto bytes_manual = Bytes::create(8);
std::memset(bytes_manual.data(), 0xAB, bytes_manual.size());

// 方式四：从 std::vector<uint8_t> 创建
std::vector<uint8_t> vec = {0x01, 0x02, 0x03, 0x04, 0xFF};
Bytes bytes_from_vec(vec);

// 方式五：空 Bytes（合法的零长度消息）
Bytes{};
```

`Bytes` 类内部使用了**小缓冲区优化（SBO）**：96 字节以内的数据存储在对象的内联数组中，不会触发堆分配。超过 96 字节的数据才会从内存池或堆分配。

### 3.3 发布与订阅

```cpp
Subscriber<Bytes> sub("dds://example/bytes_type");
sub.listen([](const Bytes& msg) {
    std::cout << "size=" << msg.size() << " content: " << msg.to_string() << std::endl;
});

Publisher<Bytes> pub("dds://example/bytes_type");
pub.publish(bytes_from_str);
```

使用 `dds://` 传输方案进行通信。`Bytes` 类型的消息作为原始二进制数据通过传输层传递给订阅者的回调函数。

### 3.4 Bytes 工具函数

```cpp
auto sample = Bytes::from_string("VLink");

sample.to_string();                                          // 转为 std::string
sample.size();                                               // 数据长度（不含偏移前缀）
Bytes::convert_to_hex_str(sample.data(), sample.size());     // 转为十六进制字符串
Bytes::encode_to_base64(sample);                             // Base-64 编码
Bytes::decode_from_base64(b64_str);                          // Base-64 解码
Bytes::get_crc_32(sample);                                   // CRC-32 校验和
Bytes::deep_copy(sample.data(), sample.size());              // 深拷贝
```

这些工具函数覆盖了二进制数据处理中常见的操作场景：格式转换、校验、拷贝等。

## 4. 构建与运行

```bash
cmake -B build -S . -DCMAKE_PREFIX_PATH=/path/to/vlink/install
cmake --build build --target example_bytes_type
./build/output/bin/example_bytes_type
```

## 5. 要点总结

| 要点 | 说明 |
|------|------|
| 序列化类型 | `kBytesType`（优先级 1，检测链首位） |
| 序列化开销 | 零——数据原样透传 |
| 适用场景 | 需要传输原始二进制数据、自定义协议帧、图片/音频等 |
| SBO 阈值 | 96 字节内无堆分配 |
| 所有权模型 | `create` / `deep_copy`（拥有）、`shallow_copy`（引用）、`loan_internal`（借用） |
