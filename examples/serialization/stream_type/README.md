# Stream 流类型序列化示例

## 类型检测优先级链

![类型检测优先级链](../bytes_type/images/serialization-type-chain.png)

## 概述

本示例演示通过 `std::stringstream` 的 `operator<<` 和 `operator>>` 进行序列化的类型，即 `kStreamType`（编号 12）。这种序列化方式将对象转换为**文本格式**传输，适用于已有 iostream 重载的轻量值类型。

## 关键代码解析

### 定义支持流序列化的类型

```cpp
struct Color {
  int r = 0, g = 0, b = 0, a = 255;

  friend std::ostream& operator<<(std::ostream& os, const Color& c) {
    return os << c.r << " " << c.g << " " << c.b << " " << c.a;
  }

  friend std::istream& operator>>(std::istream& is, Color& c) {
    return is >> c.r >> c.g >> c.b >> c.a;
  }
};

static_assert(Serializer::get_type_of<Color>() == Serializer::kStreamType, "...");
```

`kStreamType` 的检测条件是：类型同时支持 `std::stringstream << t` 和 `std::stringstream >> t`（双向流操作），且不被更高优先级的序列化器匹配。

序列化时，VLink 将对象通过 `operator<<` 写入 `std::stringstream`，再将 stream 内容转为 `Bytes`。反序列化时反向操作。

### 为什么 kStreamType 优先级低于 kCustomType

```cpp
struct Hybrid {
  int value = 0;

  // 同时具有 stream 和 Bytes 运算符
  friend std::ostream& operator<<(std::ostream& os, const Hybrid& h) { return os << h.value; }
  friend std::istream& operator>>(std::istream& is, Hybrid& h) { return is >> h.value; }

  void operator>>(Bytes& out) const { /* ... */ }
  void operator<<(const Bytes& in)  { /* ... */ }
};

// 结果：kCustomType（位置 9）优先于 kStreamType（位置 12）
static_assert(Serializer::get_type_of<Hybrid>() == Serializer::kCustomType, "...");
```

检测顺序：`kCustomType`（位置 9）在 `kStreamType`（位置 12）之前。如果一个类型同时提供了 `Bytes` 运算符和 `stream` 运算符，VLink 会优先使用 `kCustomType`（二进制编码），而不是 `kStreamType`（文本编码）。

完整检测链排序（数字为 `Type` 枚举值，位置为检测顺序）：
```
Bytes(1) -> Dynamic(2) -> CDR(4) -> Proto(5) -> ProtoPtr(6)
-> FlatTable(7) -> FlatPtr(8) -> FlatBuilder(9)
-> Custom(3) -> String(10) -> Chars(11)
-> Standard(13) -> StandardPtr(14) -> Stream(12)
```
注意 `kStreamType` 在检测链中位列**最后**（Standard / StandardPtr 之后）。
因此像 `int`、`double` 这类既是 POD 又支持 `stringstream` 的类型，会被识别为
`kStandardType` 而不是 `kStreamType`。`kStreamType` 仅匹配那些**不**满足
trivial+standard-layout 条件、但提供 `operator<<` / `operator>>` 的非指针类型。

### 文本格式的线路数据

```cpp
Color c{100, 200, 50, 128};
Bytes buf;
Serializer::serialize(c, buf);
// buf.to_string() == "100 200 50 128"  （文本格式，非二进制）
```

与 `kStandardType`（二进制 memcpy）和 `kCustomType`（自定义二进制编码）不同，`kStreamType` 使用**文本格式**。这意味着：
- 数据大小取决于文本表示的长度（不固定）
- 可读性好，便于调试
- 编码/解码开销较高（文本解析 vs 二进制拷贝）

## 构建与运行

```bash
cd build
cmake .. && make example_stream_type
./output/bin/example_stream_type
```

## 文件结构

| 文件 | 说明 |
|------|------|
| `stream_types.h` | 流序列化类型定义：`Color`、`Size2D`、`Hybrid` |
| `stream_type.cc` | 主程序：pub/sub、线路格式展示、优先级对比 |
| `CMakeLists.txt` | 构建配置 |

## 要点总结

| 要点 | 说明 |
|------|------|
| 序列化类型 | `kStreamType`（编号 12） |
| 检测条件 | 支持 `stringstream << t` 和 `stringstream >> t` |
| 编码格式 | 文本（通过 stringstream 转换） |
| 优先级 | 检测链中位列最末，仅当类型不匹配 Custom / String / Chars / Standard / StandardPtr 时才落入此分支 |
| 适用场景 | 已有 iostream 重载的轻量类型，调试友好 |
| 性能 | 低于二进制编码方式（文本解析开销） |
