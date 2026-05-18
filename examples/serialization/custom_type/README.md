# Custom 自定义类型序列化示例

## 1. 类型检测优先级链

![类型检测优先级链](../bytes_type/images/serialization-type-chain.png)

## 2. 概述

本示例演示如何为自定义 C++ 类型实现 VLink 序列化接口，使其被识别为 `kCustomType`（编号 3）。用户只需为类型定义两个运算符重载即可接入 VLink 序列化体系：

- `void operator>>(Bytes& out) const` — 序列化（将对象写入 `Bytes`）
- `void operator<<(const Bytes& in)` — 反序列化（从 `Bytes` 读取到对象）

## 3. 关键代码解析

### 3.1 固定长度自定义类型

```cpp
struct Vec3 {
  float x = 0.0f, y = 0.0f, z = 0.0f;

  void operator>>(Bytes& out) const {
    out = Bytes::create(sizeof(float) * 3);
    std::memcpy(out.data(), &x, sizeof(float));
    std::memcpy(out.data() + sizeof(float), &y, sizeof(float));
    std::memcpy(out.data() + sizeof(float) * 2, &z, sizeof(float));
  }

  void operator<<(const Bytes& in) {
    if (in.size() >= sizeof(float) * 3) {
      std::memcpy(&x, in.data(), sizeof(float));
      std::memcpy(&y, in.data() + sizeof(float), sizeof(float));
      std::memcpy(&z, in.data() + sizeof(float) * 2, sizeof(float));
    }
  }
};
```

`Vec3` 虽然满足 POD 条件（trivial + standard-layout），但因为它定义了 `operator>>` 和 `operator<<`，在检测链中 `kCustomType`（第 9 位）排在 `kStandardType`（第 13 位）之前，所以会被优先识别为 `kCustomType`。

### 3.2 变长自定义类型

```cpp
struct NamedValue {
  std::string name;
  int32_t code = 0;
  double value = 0.0;

  void operator>>(Bytes& out) const {
    uint32_t name_len = static_cast<uint32_t>(name.size());
    size_t total = sizeof(uint32_t) + name_len + sizeof(int32_t) + sizeof(double);
    out = Bytes::create(total);
    // ... 写入 [长度前缀] [字符串内容] [固定字段]
  }

  void operator<<(const Bytes& in) {
    // ... 读取 [长度前缀] [字符串内容] [固定字段]
  }
};
```

`NamedValue` 包含 `std::string` 成员，不满足 POD 条件。序列化时使用**长度前缀编码**：先写入 4 字节的字符串长度，再写入字符串内容，最后写入固定大小的字段。这是变长数据的经典编码方式。

### 3.3 序列化流程

```
publish(msg)
  -> Serializer 检测到 kCustomType
  -> 调用 msg.operator>>(Bytes& out) 进行序列化
  -> 传输层投递 Bytes 数据
  -> Serializer 调用 msg.operator<<(const Bytes& in) 进行反序列化
  -> 订阅者回调收到反序列化后的对象
```

整个过程对用户完全透明。用户只需定义序列化/反序列化逻辑，VLink 框架会在 `publish()` 和 `listen()` 时自动调用。

### 3.4 类型检测优先级

```cpp
// Vec3 是 POD，但因为有 operator>>/<<，被识别为 kCustomType 而非 kStandardType
static_assert(Serializer::get_type_of<Vec3>() == Serializer::kCustomType, "...");
```

检测链中 `kCustomType`（位置 9）优先于 `kStandardType`（位置 13）。这意味着如果你为一个 POD 类型添加了自定义序列化运算符，它会使用自定义编码而不是原始内存拷贝。

## 4. 构建与运行

```bash
cd build
cmake .. && make example_custom_type
./output/bin/example_custom_type
```

## 5. 文件结构

| 文件 | 说明 |
|------|------|
| `custom_types.h` | 自定义序列化类型定义：`Vec3`（固定长度）、`NamedValue`（变长） |
| `custom_type.cc` | 主程序：pub/sub、序列化流程展示、往返验证 |
| `CMakeLists.txt` | 构建配置 |

## 6. 要点总结

| 要点 | 说明 |
|------|------|
| 序列化类型 | `kCustomType`（编号 3） |
| 所需接口 | `operator>>(Bytes&) const` + `operator<<(const Bytes&)` |
| 检测优先级 | 高于 String、Stream、Standard（排第 9 位） |
| 适用场景 | 需要自定义编码格式的复杂类型 |
| 注意事项 | 序列化/反序列化逻辑需确保对称一致 |
