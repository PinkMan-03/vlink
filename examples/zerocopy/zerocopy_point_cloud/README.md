# zerocopy_point_cloud — LiDAR 风格变长 schema 点云容器

`vlink::zerocopy::PointCloud` 是 vlink 内置的点云零拷贝容器，支持**变长 schema**：每个 point 的字段由模板参数定义（如 `<float, float, float>` 表示 XYZ，`<float, float, float, uint8_t>` 表示 XYZ + intensity）。容器在固定大小的 header 里嵌入字段类型列表，wire 格式自描述。

读完本示例你能掌握：

- `create<T...>` 与 `create_v3f` 两种模板构造方式。
- 按字段 push / set / get 的 API。
- schema 自描述格式（protocol type/size 字符串、key_map 字典）。
- 序列化 round-trip 行为（is_owner 翻转）。

## 背景与适用场景

适用：

- 多种 LiDAR 数据格式（XYZ、XYZI、XYZIRGB、自定义传感器）。
- 需要 schema 自描述、跨语言互通的点云通信。
- 高频大数据点云的 SHM 零拷贝传递。

不适合：

- 单一固定 schema 的项目（用普通 struct + Bytes 更简单）。
- 极高密度（百万点 @ 30Hz）必须用 SHM 后端，不要走 dds 序列化。

PointCloud 的字段 schema 编码进容器 header 的 256 字节里：通过 enum 标记每个字段的类型（Float / Double / Uint8 / …）和 name 字符串。接收端按 schema 自描述就能解析任意点云格式。

## 核心 API

| API | 签名 | 说明 |
|-----|------|------|
| `PointCloud::create` | `template <typename... T> void create(size_t max_points, std::initializer_list<std::string> names)` | 自定义 schema |
| `PointCloud::create_v3f` | `template <typename... Extra> void create_v3f(size_t, std::initializer_list<std::string>)` | XYZ + 额外字段 |
| `PointCloud::create_v3d` | 同上 | XYZ float64 + 额外字段 |
| `push_value<T...>` | `void` | 追加一个点 |
| `push_value_v3f` | `void (float x, float y, float z, ...)` | XYZ + extra |
| `set_value_v3f` | `void (size_t idx, ...)` | 覆盖指定索引 |
| `get_value_v3f` | `tuple<float,float,float,...> (size_t idx)` const | 读 XYZ + extra |
| `get_value<T>` | `T get_value(size_t idx, const key_map_t&, const std::string& name)` | 按字段名读 |
| `get_key_map` | `key_map_t get_key_map() const` | 字段名→索引字典 |
| `get_protocol_name_str` | `std::string` | 字段名 schema |
| `get_protocol_type_str` | `std::string` | 字段类型 schema |
| `size` / `capacity` / `resize` | const / mut | 当前 / 最大点数 |
| `operator>>` / `operator<<` | const / mut | 与 Bytes 互转 |
| `is_owner` | `bool` | 是否拥有底层内存 |

## 代码导读

### 1. 自定义 XYZ schema

```cpp
vlink::zerocopy::PointCloud cloud;
cloud.create<float, float, float>(/*max_points=*/100, {"x", "y", "z"});

cloud.push_value<float, float, float>(1.0F, 2.0F, 3.0F);
cloud.push_value<float, float, float>(4.0F, 5.0F, 6.0F);

VLOG_I("size=", cloud.size(), " capacity=", cloud.capacity());

auto [x, y, z] = cloud.get_value_v3f(0);
VLOG_I("point[0]: ", x, ",", y, ",", z);
```

### 2. XYZ + intensity

```cpp
cloud.create_v3f<float>(/*max_points=*/256, {"intensity"});
cloud.push_value_v3f(1.0F, 2.0F, 3.0F, /*intensity=*/0.5F);
cloud.push_value_v3f(4.0F, 5.0F, 6.0F, 0.8F);

auto [x, y, z, i] = cloud.get_value_v3f(0);
```

### 3. schema 自描述

```cpp
VLOG_I("protocol_name: ", cloud.get_protocol_name_str());   // "x,y,z,intensity"
VLOG_I("protocol_type: ", cloud.get_protocol_type_str());   // "f32,f32,f32,f32"

auto key_map = cloud.get_key_map();   // {"x":0, "y":1, "z":2, "intensity":3}
float i_value = cloud.get_value<float>(0, key_map, "intensity");
```

### 4. 序列化 round-trip

```cpp
vlink::Bytes wire;
cloud >> wire;

vlink::zerocopy::PointCloud restored;
restored << wire;
VLOG_I("restored size=", restored.size(), " owner=", restored.is_owner());
// is_owner == false: data borrows from wire
```

### 5. resize + 覆盖

```cpp
cloud.resize(10);
cloud.set_value_v3f(5, 99.0F, 88.0F, 77.0F, 0.9F);
```

### 6. pub/sub 跨进程

```cpp
vlink::Publisher<vlink::zerocopy::PointCloud> pub("dds://lidar/scan");
vlink::Subscriber<vlink::zerocopy::PointCloud> sub("dds://lidar/scan");
sub.listen([](const vlink::zerocopy::PointCloud& pc) {
  VLOG_I("got cloud size=", pc.size(), " protocol=", pc.get_protocol_name_str());
});
```

实际生产中用 `shm://` 后端才能享受零拷贝；本示例用 `dds://` 是为了脱离 RouDi 也能跑通。

### 7. double 精度版本

```cpp
vlink::zerocopy::PointCloud big;
big.create_v3d<double>(100, {"weight"});
big.push_value_v3d(1.0, 2.0, 3.0, 0.5);
auto [x, y, z, w] = big.get_value_v3d(0);
```

## 运行

```bash
./build/output/bin/example_zerocopy_point_cloud
```

预期输出（节选）：

```
=== XYZ schema ===
size=2 capacity=100
point[0]: 1,2,3
=== XYZ + intensity ===
intensity[0]=0.5
=== schema description ===
protocol_name: x,y,z,intensity
protocol_type: f32,f32,f32,f32
=== round-trip ===
restored size=2 owner=0
=== resize + set ===
ok
=== pub/sub ===
got cloud size=N protocol=x,y,z,intensity
=== v3d ===
double point: 1, 2, 3, 0.5
```

## 常见陷阱

1. **超过 max_points**：push_value 行为按实现可能拒绝或扩容；先 capacity() 检查。
2. **schema 不匹配的反序列化**：接收方 PointCloud 接收任意 schema；调用方按 key_map 拿字段才安全。
3. **字段名重复 / 超长**：name 字符串嵌入 header 总空间有限；保持简短（≤ 8 字符）。
4. **get_value_v3f 在 XYZ 之外的 schema**：只取前三字段；按模板参数推断的字段数量。
5. **resize 不扩容**：只能缩到 ≤ size；要扩容需要新 create。

## 设计要点

- header 内 schema 类型用 enum (`kFloat / kDouble / kUint8 / ...`) 紧凑编码；wire 格式自描述。
- push_value 按 schema 列字段顺序写入，避免对齐 padding。
- `is_owner == false` 在零拷贝路径下是常态；不要试图重新分配 / move。

## 配图

![PointCloud Layout](./images/point-cloud-zerocopy.png)

图中展示 PointCloud 的 256 字节 header（含 schema 描述）+ payload 区域的整体布局。

## 参考

- `../zerocopy_basic/` — loan + RawData 基础
- `../zerocopy_camera_frame/` — 摄像头帧
- `vlink/include/vlink/zerocopy/point_cloud.h` — PointCloud 接口
- 顶层 `doc/10-zerocopy.md` — 零拷贝机制
