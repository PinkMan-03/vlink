# PointCloud 零拷贝点云容器示例

## 概述

本示例演示 VLink 的 `zerocopy::PointCloud` 容器——一个带有编译时 Schema 的零拷贝三维点云容器。`PointCloud` 支持自定义字段类型（float、double、int、uint8 等），提供类型安全的创建、填充和读取 API。

## 核心概念

### Schema 协议

每个 `PointCloud` 都包含一个嵌入式 Schema，描述每个点记录中每个字段的名称、类型和字节大小。Schema 编码在两个 `uint64_t` 值中：

```
size_num: 每个 nibble(4位) 编码一个字段的字节大小
type_num: 每个 nibble 编码一个字段的 Type 枚举值
names:    逗号分隔的字段名字符串（最多 160 字符，3-16 个字段）
```

### 支持的字段类型

| 枚举 | C++ 类型 | 字节数 |
|------|---------|--------|
| kBoolType(1) | bool | 1 |
| kInt8Type(2) | int8_t | 1 |
| kUint8Type(3) | uint8_t | 1 |
| kInt16Type(4) | int16_t | 2 |
| kUint16Type(5) | uint16_t | 2 |
| kInt32Type(6) | int32_t | 4 |
| kUint32Type(7) | uint32_t | 4 |
| kInt64Type(8) | int64_t | 8 |
| kUint64Type(9) | uint64_t | 8 |
| kFloatType(10) | float | 4 |
| kDoubleType(11) | double | 8 |

## 关键 API 解析

### 创建点云

```cpp
// 方式 1: 显式模板参数
pc.create<float, float, float>(1000, {"x", "y", "z"});

// 方式 2: 使用 v3f 便捷方法（自动添加 x,y,z）
pc.create_v3f<float>(500, {"intensity"});
// 等价于 create<float,float,float,float>(500, {"x","y","z","intensity"})

// 方式 3: 双精度
pc.create_v3d(100);  // double x,y,z
```

### 添加点（push_value_v3f）

```cpp
pc.push_value_v3f(1.0f, 2.0f, 3.0f, 0.8f);  // x, y, z, intensity
pc.push_value_v3f(4.0f, 5.0f, 6.0f, 0.5f);
```

每次 `push_value` 在缓冲区末尾追加一个点。返回 `false` 表示缓冲区已满。

### 读取点（get_value_v3f）

```cpp
// 方式 1: 输出参数
float x, y, z;
pc.get_value_v3f(x, y, z, index);

// 方式 2: 返回 Vector3f
auto v = pc.get_value_v3f(index);

// 方式 3: 通过 KeyMap 读取任意字段
auto key_map = pc.get_key_map();
float intensity = pc.get_value<float>(index, key_map, "intensity");
```

### Schema 检查

```cpp
std::cout << pc.get_protocol_name_str();  // "x,y,z,intensity"
std::cout << pc.get_protocol_size_str();  // "4,4,4,4"
std::cout << pc.get_protocol_type_str();  // "float,float,float,float"
std::cout << pc.pack_size();              // 16 (每个点 16 字节)
```

### 序列化 / 反序列化

```cpp
Bytes wire;
pc >> wire;  // 序列化

PointCloud restored;
restored << wire;  // 反序列化（零拷贝）
```

### resize + set_value（随机访问写入）

```cpp
pc.resize(100);  // 设置逻辑大小为 100
pc.set_value_v3f(50, 1.0f, 2.0f, 3.0f);  // 覆写第 50 个点
```

## 编译与运行

```bash
cd build
cmake .. && make example_zerocopy_point_cloud
./output/bin/example_zerocopy_point_cloud
```

## 二进制线格式

```
[ magic_begin(4) | PointCloud结构体(256) | 点数据(size*pack_size) | magic_end(4) ]
```

结构体在 64 位平台上恰好 256 字节。

## 点云操作流程

```
1. create<T...>(max_points, field_names)  -- 分配缓冲区，设置 Schema
2. push_value(...)  或  push_value_v3f(...)  -- 逐点追加
3. 或: fill_packed_data(src, count)  -- 批量填充预打包数据
4. pc >> wire  -- 序列化传输
5. restored << wire  -- 零拷贝反序列化
6. get_value_v3f(index)  -- 读取特定点
```

## 实际应用场景

- LiDAR 驱动发布点云到 `shm://lidar/front`
- 感知模块订阅并检索 XYZ + intensity
- 记录系统将点云序列化到 Bag 文件
- 支持 3-16 个自定义字段（ring、timestamp、label 等）

## 注意事项

- 字段数量限制：3 到 16 个
- 所有字段类型必须是基础类型（`is_fundamental_v<T> == true`）
- `resize()` 必须在 `set_value()` 之前调用
- `operator<<` 后数据借用 `Bytes` 的内存
- `clear(false)` 仅重置计数器，`clear(true)` 释放所有资源
