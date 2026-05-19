# POD 类型序列化示例

## 1. 类型检测优先级链

![类型检测优先级链](../bytes_type/images/serialization-type-chain.png)

## 2. 概述

本示例演示如何使用 VLink 发布和订阅 POD（Plain Old Data）类型的消息。POD 类型在序列化体系中被识别为 `kStandardType`（编号 13），其序列化方式是**直接内存拷贝**——将 `sizeof(T)` 字节原封不动地写入 `Bytes` 缓冲区，无需任何编码、解码或 schema 描述。

## 3. 关键代码解析

### 3.1 POD 类型的定义与验证

```cpp
struct Point2D {
  float x;
  float y;
};

static_assert(std::is_trivial_v<Point2D>, "Point2D must be trivial");
static_assert(std::is_standard_layout_v<Point2D>, "Point2D must be standard-layout");
static_assert(sizeof(Point2D) == 8, "Point2D should be 8 bytes (2 x float)");
```

一个类型要被 VLink 识别为 `kStandardType`，必须同时满足两个条件：
- **`std::is_trivial_v<T> == true`**：拥有平凡的默认构造函数、拷贝/移动构造函数和析构函数
- **`std::is_standard_layout_v<T> == true`**：没有虚函数、所有非静态成员具有相同的访问控制级别

使用 `static_assert` 在编译期验证这些属性是最佳实践，可以在代码修改时立即捕获破坏 POD 特性的变更。

### 3.2 编译期类型检测

```cpp
static_assert(Serializer::get_type_of<Point2D>() == Serializer::kStandardType,
              "Point2D must be kStandardType");
static_assert(Serializer::get_type_of<int>() == Serializer::kStandardType,
              "int must be kStandardType");
```

`kStandardType` 在检测链中排名第 13 位，这意味着只有当类型不匹配前面 12 种更高优先级的序列化器时，才会被归类为 `kStandardType`。内置算术类型（`int`、`double` 等）也属于此类别。

### 3.3 sizeof 与序列化大小

```cpp
Point2D sample{1.0f, 2.0f};
Serializer::get_serialized_size(sample);  // 返回 sizeof(Point2D) = 8
```

对于 `kStandardType`，`get_serialized_size` 始终返回 `sizeof(T)`。这是最高效的序列化方式——零编码开销。

### 3.4 手动序列化/反序列化验证

```cpp
Point2D original{42.0f, -7.5f};
Bytes buf;
Serializer::serialize(original, buf);   // 将 8 字节直接拷贝到 buf

Point2D restored{};
Serializer::deserialize(buf, restored); // 从 buf 中拷贝 8 字节回来
// original 和 restored 完全一致
```

`Serializer::serialize` 和 `Serializer::deserialize` 是框架内部自动调用的，用户代码通常不需要直接调用。这里展示它们是为了说明 POD 类型的序列化/反序列化就是简单的 `memcpy`。

### 3.5 多种 POD 结构体示例

示例中定义了三种不同复杂度的 POD 结构体：

| 结构体 | 字段 | 用途 |
|--------|------|------|
| `Point2D` | 2 个 float | 最简单的几何数据 |
| `SensorReading` | 混合类型 + padding | 传感器上报数据 |
| `CanFrame` | 固定数组 + 标志位 | CAN 总线协议帧 |

这些都满足 trivial + standard-layout 的要求，因此都能被正确识别为 `kStandardType` 并直接内存拷贝传输。

## 4. 构建与运行

```bash
cmake -B build -S . -DCMAKE_PREFIX_PATH=/path/to/vlink/install
cmake --build build --target example_pod_type
./build/output/bin/example_pod_type
```

## 5. 文件结构

| 文件 | 说明 |
|------|------|
| `pod_types.h` | POD 结构体定义：`Point2D`、`SensorReading`、`CanFrame` |
| `pod_type.cc` | 主程序：编译期验证、pub/sub、手动序列化测试 |
| `CMakeLists.txt` | 构建配置 |

## 6. 注意事项

- **不要在 POD 结构中使用 `std::string`、`std::vector` 等动态分配类型**——它们会破坏 trivial 特性，导致序列化后传输的是指针而非实际数据
- **跨平台注意**：不同编译器/架构的 struct padding 规则可能不同，跨平台通信时需确保内存布局一致
- **字节序**：`kStandardType` 不做字节序转换，跨大小端平台通信时需要额外处理

## 7. 要点总结

| 要点 | 说明 |
|------|------|
| 序列化类型 | `kStandardType`（编号 13） |
| 检测条件 | `is_trivial && is_standard_layout && !is_pointer` |
| 序列化方式 | `memcpy`，大小 = `sizeof(T)` |
| 性能 | 最高——零编码开销 |
| 限制 | 不支持含动态分配成员的类型，不处理字节序 |
