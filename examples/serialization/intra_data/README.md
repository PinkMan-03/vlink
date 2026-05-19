# IntraData 零序列化进程内消息示例

## 1. 概述

本示例演示 VLink 的 `IntraData` 零序列化消息容器。`IntraData` 专用于 `intra://` 传输方案，当发布者和订阅者位于同一进程时，通过直接传递 `shared_ptr` 来避免所有序列化和数据拷贝。

## 2. 核心概念

### 2.1 什么是 IntraData？

在普通的 `Publisher<T>` / `Subscriber<T>` 模式中，即使使用 `intra://` 传输，消息也需要经过序列化（`T -> Bytes`）和反序列化（`Bytes -> T`）的过程。对于大型数据结构（图像、点云、Protobuf 消息），这会带来不必要的 CPU 开销。

`IntraData` 解决了这个问题：它将消息封装在 `shared_ptr` 中，发布者和订阅者直接共享同一个对象的引用，**完全跳过序列化/反序列化步骤**。

### 2.2 VLINK_INTRA_DATA_DECLARE 宏

```cpp
VLINK_INTRA_DATA_DECLARE(MyStruct, MyIntra)
```

这个宏生成两个类型：

| 生成类型 | 描述 |
|---------|------|
| `MyIntraType` | 继承 `IntraDataType` 的具体子类，包含 `MyStruct value` 成员 |
| `MyIntra` | `shared_ptr<MyIntraType>` 包装器，提供 `create()` 工厂方法 |

### 2.3 类型要求

目标类型必须是 VLink `Serializer` 支持的类型（用于跨传输回退序列化）：
- kStandardType（POD 类型）
- kCustomType（实现 `operator>>` / `operator<<`）
- kStringType（`std::string`）
- kBytesType（`Bytes`）

## 3. 关键代码解析

### 3.1 声明 IntraData 类型

```cpp
struct MyStruct {
  int32_t id{0};
  float temperature{0.0f};
  char label[32]{};

  void operator>>(Bytes& out) const { /* 序列化 */ }
  void operator<<(const Bytes& in)  { /* 反序列化 */ }
};

VLINK_INTRA_DATA_DECLARE(MyStruct, MyIntra)
```

`MyStruct` 实现了 `operator>>` 和 `operator<<`，使其被识别为 `kCustomType`。即使 `IntraData` 路径不需要序列化，这些方法仍然是必需的——它们在消息需要跨传输边界传递时提供回退支持。

### 3.2 创建和填充

```cpp
auto data = MyIntra::create();
data->value.id = 42;
data->value.temperature = 36.6f;
```

`create()` 在堆上分配一个 `MyIntraType` 实例，返回 `shared_ptr` 包装器。通过 `->value` 直接访问内嵌的 `MyStruct` 对象。

### 3.3 零拷贝发布 / 订阅

```cpp
Publisher<MyIntra> pub("intra://example/intra_data/struct#direct");
Subscriber<MyIntra> sub("intra://example/intra_data/struct#direct");

sub.listen([](const MyIntra& intra) {
  // 直接通过 shared_ptr 访问——零拷贝
  std::cout << intra->value.id << std::endl;
});

auto data = MyIntra::create();
data->value.id = 42;
pub.publish(data);
```

订阅者回调收到的 `MyIntra` 与发布者创建的是**同一个对象**（引用计数增加），内存零拷贝。

### 3.4 手动序列化（回退路径）

```cpp
Bytes wire;
(*original) >> wire;  // 序列化到 Bytes

auto restored = MyIntra::create();
(*restored) << wire;  // 从 Bytes 反序列化
```

当消息需要通过非 intra:// 传输发送时，`IntraDataType` 的 `operator>>` / `operator<<` 提供序列化支持。

## 4. 编译与运行

```bash
cmake -B build -S . -DCMAKE_PREFIX_PATH=/path/to/vlink/install
cmake --build build --target example_intra_data
./build/output/bin/example_intra_data
```

## 5. IntraData 与普通类型对比

| 特性 | 普通类型 | IntraData |
|------|---------|-----------|
| 发布时操作 | 序列化（`T -> Bytes`） | 零拷贝（传递 `shared_ptr`） |
| 接收时操作 | 反序列化（`Bytes -> T`） | 直接指针访问 |
| 适用传输 | 所有传输协议 | 仅 `intra://` |
| 语义 | 值语义 | 引用语义 |
| 性能 | 取决于类型大小 | 常数时间，与大小无关 |

## 6. 适用场景

- 发布者和订阅者在同一进程内
- 数据量大（图像帧、点云、大型 Protobuf 消息）
- 需要最低延迟和最小 CPU 开销
- 模块间解耦但不需要跨进程通信

## 7. 注意事项

- `IntraData` **仅支持 `intra://` 传输**。在非 intra:// 主题上调用 `publish(IntraData)` 会记录警告并返回 `false`
- 因为使用引用语义，发布后**不要修改** `IntraData` 中的值——订阅者可能同时在读取
- 如果需要跨进程通信，使用常规 `Publisher<MyStruct>` 配合 `shm://` 或 `dds://`

## 8. 要点总结

| 要点 | 说明 |
|------|------|
| 宏 | `VLINK_INTRA_DATA_DECLARE(TargetType, DeclareName)` |
| 工厂 | `DeclareName::create()` 返回 `shared_ptr` |
| 发布 | `Publisher<DeclareName>` + `pub.publish(data)` |
| 订阅 | `Subscriber<DeclareName>` + `sub.listen(callback)` |
| 传输限制 | 仅 `intra://` |
| 性能 | O(1) 发布/接收，与数据大小无关 |
