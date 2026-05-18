# Bytes 高级操作

## 1. 概述

本示例展示 `Bytes` 的高级功能：压缩/解压、Base64 编解码、CRC-32 校验、十六进制转换、用户输入解析、字节序反转等。

## 2. 压缩功能 (LZAV)

VLink 使用 LZAV 压缩算法，提供两种模式：

```cpp
// 标准压缩（快速）
auto compressed = Bytes::compress_data(data, size);

// 高压缩率模式（较慢但压缩比更好）
auto hi_compressed = Bytes::compress_data(data, size, true);
```

压缩后的数据带有 4 字节头部魔数 `{0x17, 0x49, 0xB2, 0x6F}` 和 4 字节尾部魔数 `{0xA7, 0x05, 0xED, 0x71}`，可通过 `is_compress_data()` 检测。

解压时可以选择是否验证魔数：
```cpp
auto decompressed = Bytes::uncompress_data(data, size, true);  // 验证魔数
auto decompressed = Bytes::uncompress_data(data, size, false); // 跳过验证
```

**限制**：超过 1 MiB 的数据不支持压缩，`compress_data()` 返回空 Bytes。

## 3. Base64 编解码

```cpp
std::string encoded = Bytes::encode_to_base64(bytes);   // Bytes -> Base64 字符串
Bytes decoded = Bytes::decode_from_base64(encoded);      // Base64 字符串 -> Bytes
```

适用场景：将二进制数据嵌入 JSON/XML 等文本协议。

## 4. CRC-32 校验

```cpp
uint32_t crc = Bytes::get_crc_32(bytes);
```

用于数据完整性校验。相同内容的 Bytes 总是产生相同的 CRC-32 值。

## 5. 十六进制转换

```cpp
// Bytes -> 十六进制字符串（如 "DE AD BE EF "）
std::string hex = Bytes::convert_to_hex_str(bytes.data(), bytes.size());
```

每个字节渲染为两位大写十六进制数加空格，适合日志输出。

## 6. 用户输入解析

```cpp
bool ok;
auto bytes = Bytes::from_user_input("0x48656C6C6F", &ok);
// ok == true, bytes.to_string() == "Hello"
```

接受 `0x` 开头的十六进制字符串或原始二进制字符串。解析失败时 `ok` 设为 `false`。

## 7. 字节序反转

```cpp
auto reversed = Bytes::reverse_order(original);
// {0x01, 0x02, 0x03, 0x04} -> {0x04, 0x03, 0x02, 0x01}
```

用于大小端转换或协议对齐。

## 8. 编译与运行

```bash
mkdir build && cd build
cmake .. -DCMAKE_PREFIX_PATH=/path/to/vlink/install
make example_bytes_advanced
./output/bin/example_bytes_advanced
```
