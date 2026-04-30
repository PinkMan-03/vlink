# Field Advanced -- VLink 字段模型进阶示例

## 通信模型概览

![通信模型概览](../event_basic/images/communication-models-overview.png)

## 概述

本示例在基础字段模型之上，演示高级功能：变更报告（重复值过滤）、迟到加入同步验证、多 Getter 扇出，以及 Getter 端的延迟/丢失跟踪。

```
Setter ──set(same_value)──> [change_reporting] ──> Getter 回调不触发（值未变）
Setter ──set(new_value)───> [change_reporting] ──> Getter 回调触发（值已变）
```

## 功能清单

| 功能 | API | 说明 |
|------|-----|------|
| 变更报告 | `set_change_reporting(true)` | 抑制重复值回调 |
| 查询变更报告状态 | `get_change_reporting()` | 返回是否启用变更报告 |
| 迟到加入同步 | 自动机制 | 新 Getter 自动收到 Setter 缓存值 |
| 多 Getter 扇出 | 多个 Getter 监听同一 URL | 各 Getter 独立接收更新 |
| Getter 延迟跟踪 | `set_latency_and_lost_enabled(true)` | 启用端到端延迟测量 |
| Getter 丢失统计 | `get_lost()` | 返回累积丢失信息 |

## 关键代码分析

### 1. 变更报告（Change Reporting）

```cpp
getter_cr.set_change_reporting(true);  // 必须在 listen() 之前调用
getter_cr.listen([](const BrightnessConfig& cfg) {
    // 仅在值实际变化时触发
});
```

启用变更报告后，Getter 内部在收到新数据时，会将序列化后的原始字节与上次缓存的字节进行比较。如果完全相同，则跳过回调和 `wait_for_value()` 通知。

**工作原理**:

```
Setter.set({50, false})  --> 序列化 -> Bytes_A
Setter.set({50, false})  --> 序列化 -> Bytes_B
  Bytes_A == Bytes_B  --> 跳过回调（值未变）

Setter.set({75, false})  --> 序列化 -> Bytes_C
  Bytes_B != Bytes_C  --> 触发回调（值已变）
```

**使用场景**:
- 传感器定期上报，但值经常不变（如温度传感器）
- 配置同步循环中减少不必要的处理
- 降低 CPU 开销

**注意**: 比较发生在原始字节级别，不是通过 `operator==`。因此，对于包含 padding 的结构体，即使逻辑值相同，如果 padding 字节不同，仍可能触发回调。建议对 POD 类型使用零初始化或确保所有字段都被显式赋值。

### 2. 迟到加入同步验证

```cpp
// Setter 已写入 {100, true}
Getter<BrightnessConfig> late_getter("ddsc://display/brightness");
late_getter.listen([](const BrightnessConfig& cfg) {
    // 自动收到 {100, true}
});
if (late_getter.wait_for_value(2000ms)) {
    auto val = late_getter.get();  // {100, true}
}
```

迟到加入（Late-Joiner Sync）机制的内部流程：

1. Setter 调用 `set()` 时，值被缓存在内部 `value_` 字段
2. 当新 Getter 通过传输层连接时，传输层触发 Setter 的内部 `sync()` 回调
3. `sync()` 回调重新发送缓存的值
4. 新 Getter 收到值，更新本地状态，触发 `listen` 回调

这确保了组件的启动顺序无关紧要。

### 3. 多 Getter 扇出

```cpp
Getter<int> g1("ddsc://config/volume");
Getter<int> g2("ddsc://config/volume");
Getter<int> g3("ddsc://config/volume");
```

多个 Getter 可以独立监听同一字段 URL。每个 Getter：
- 独立维护自己的 `value_` 缓存
- 独立触发 `listen` 回调
- 独立执行 `get()` / `wait_for_value()`
- 可以独立配置 `set_change_reporting` 和 `set_latency_and_lost_enabled`

### 4. Getter 端延迟/丢失跟踪

```cpp
g3.set_latency_and_lost_enabled(true);
g3.listen([&g3](const int& v) {
    int64_t latency = g3.get_latency();
    SampleLostInfo lost = g3.get_lost();
});
```

与 Subscriber 的延迟跟踪相同，Getter 也支持：
- `get_latency()`: 最近一次值更新的端到端延迟（微秒）
- `get_lost()`: 累积的消息统计（`total` 预期总数，`lost` 丢失数）

## 变更报告实验结果

```
set({50, false})  --> 初始值
set({50, false})  --> 抑制（字节相同）
set({50, false})  --> 抑制（字节相同）
set({75, false})  --> 触发回调（字节不同）
set({75, false})  --> 抑制（字节相同）
set({100, true})  --> 触发回调（字节不同）

回调实际触发次数: ~2 次（仅在值变化时）
```

## 编译与运行

```bash
mkdir build && cd build
cmake .. -DCMAKE_PREFIX_PATH=/path/to/vlink/install
make example_field_advanced
./output/bin/example_field_advanced
```

## 预期输出

```
[I] === VLink Field Advanced Example ===
[I] --- Section 1: Change reporting ---
[I] [CR-Getter] Changed: level=75 auto=0
[I] [CR-Getter] Changed: level=100 auto=1
[I] [CR-Getter] change_reporting enabled: 1
[I] [CR-Getter] Callback count (expect ~2 unique changes): 2
[I] --- Section 2: Late-joiner sync ---
[I] [LateGetter] Received via sync: level=100 auto=1
[I] [LateGetter] get(): level=100 auto=1
[I] --- Section 3: Multiple Getters ---
[I] [Getter1] volume=0
[I] [Getter3] volume=0 latency=3us
[I] [Getter1] volume=25
...
[I] [Getter1] received: 5
[I] [Getter2] received: 5
[I] [Getter3] received: 5
[I] === Example complete ===
```

## 文件结构

| 文件 | 说明 |
|------|------|
| `config_types.h` | POD 消息类型 `BrightnessConfig` 的定义 |
| `field_advanced.cc` | 主程序：Setter + 多 Getter 在同一进程 |
| `CMakeLists.txt` | 构建配置 |

## 扩展思考

- 变更报告对于高频定时上报的传感器尤为有用：如果车速长时间为 0（停车状态），回调只在值变化时触发，大幅降低 CPU 使用率。
- 多 Getter 模式可用于实现"观察者模式"：一个 Getter 用于 UI 显示，另一个 Getter 用于数据录制，第三个 Getter 用于健康监控。
- 在跨进程场景（`shm://`、`dds://`）中，迟到加入同步依赖传输层的 durability 特性，效果因传输协议而异。
- Getter 的 `set_change_reporting` 和 `set_latency_and_lost_enabled` 可以同时启用，互不冲突。

## 相关文档

详细原理参见 [doc/05-field-model.md](../../../doc/05-field-model.md)。
