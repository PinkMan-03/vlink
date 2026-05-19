# DynamicData 动态类型序列化示例

## 1. 类型检测优先级链

![类型检测优先级链](../bytes_type/images/serialization-type-chain.png)

## 2. 概述

`DynamicData` 是 VLink 提供的**类型擦除容器**（`kDynamicType`，编号 2），可以在一个 `Bytes` 缓冲区中存储任意可序列化类型的值和一个类型名称标签。这使得：

1. **多种不同类型共享同一个 topic URL**
2. 在运行时根据类型标签动态决定反序列化目标类型
3. 先传输原始数据，稍后再反序列化

## 3. 关键代码解析

### 3.1 内部存储布局

```
[类型名称（前 20 字节，含 NUL，实际写入长度 < 20）] [序列化后的有效载荷]
```

类型名称嵌入在 `Bytes` 缓冲区的前 20 字节（`kOffset = 20`），后面紧跟实际数据。
`load()` 的字符串字面量包括 NUL 终止符的长度必须小于 20（即字面量最多 18 个可见字符）。

### 3.2 基本用法：load / as / convert

```cpp
DynamicData dd;
dd.load("Temperature", Temperature{36.6, 42});
// load() 将 Temperature 序列化并附上 "Temperature" 类型标签

std::string type_name(dd.get_type());  // "Temperature"
bool empty = dd.is_empty();            // false

// 方式一：as<T>() 返回反序列化后的值
Temperature t = dd.as<Temperature>();

// 方式二：convert(T&) 填充已有对象
Temperature t2{};
dd.convert(t2);
```

`load()` 是模板函数，第一个参数是类型名称字符串字面量（编译期 `static_assert` 检查 `SizeT < kOffset`，即含 NUL 长度 < 20），第二个参数是要序列化的值。`as<T>()` 和 `convert(T&)` 用于反序列化。

### 3.3 多类型共享单一 topic

```cpp
// 订阅者：根据运行时类型标签分发
Subscriber<DynamicData> sub("dds://example/dynamic/multi");
sub.listen([](const DynamicData& dd) {
    std::string type_name(dd.get_type());

    if (type_name == "Temperature") {
        auto t = dd.as<Temperature>();
    } else if (type_name == "Pressure") {
        auto p = dd.as<Pressure>();
    }
});

// 发布者：在同一个 topic 上发送不同类型
Publisher<DynamicData> pub("dds://example/dynamic/multi");

DynamicData dd1;
dd1.load("Temperature", Temperature{22.5, 1});
pub.publish(dd1);

DynamicData dd2;
dd2.load("Pressure", Pressure{1013.25, 2});
pub.publish(dd2);
```

这是 `DynamicData` 最强大的功能：**同一个 topic URL 上可以传输不同类型的消息**。订阅者通过 `get_type()` 获取类型标签，然后动态决定调用哪个 `as<T>()` 进行反序列化。

### 3.4 线路格式：operator>> / operator<<

```cpp
DynamicData dd;
dd.load("TestType", Temperature{100.0, 99});

// 序列化到线路格式
Bytes wire_bytes;
dd >> wire_bytes;

// 从线路格式反序列化
DynamicData dd_from_wire;
dd_from_wire << wire_bytes;
```

`operator>>` 和 `operator<<` 用于 `DynamicData` 本身的序列化/反序列化（即将整个容器——包括类型标签和有效载荷——转换为/从 `Bytes`）。这是 VLink 传输层内部使用的接口，用户代码通常不需要直接调用。

## 4. 构建与运行

```bash
cmake -B build -S . -DCMAKE_PREFIX_PATH=/path/to/vlink/install
cmake --build build --target example_dynamic_data
./build/output/bin/example_dynamic_data
```

## 5. 要点总结

| 要点 | 说明 |
|------|------|
| 序列化类型 | `kDynamicType`（编号 2，优先级极高） |
| 核心功能 | 类型擦除 + 运行时类型标签 |
| 类型名长度限制 | 字符串字面量含 NUL 长度必须小于 `kOffset = 20`（即最多 18 个字符 + 1 NUL = 19 字节） |
| 多类型 topic | 支持——同一 URL 上发送不同类型的消息 |
| 限制 | 不支持序列化自身（`DynamicData`）和 CDR 类型 |
| 适用场景 | 通用数据总线、动态消息路由、异构类型聚合 |
