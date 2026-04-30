# URL SHM -- shm:// 共享内存传输详解

## 概述

`shm://` 传输基于 Eclipse Iceoryx，使用共享内存实现同机器上的零拷贝进程间通信 (IPC)。在所有跨进程传输中，它提供最低的延迟和最高的吞吐量。

```
shm://address[?event=name&domain=N&depth=N&history=N&wait=0|1]
```

## URL 参数详解

| 参数 | 位置 | 默认值 | 说明 |
|------|------|--------|------|
| address | host/path | (必填) | 主题名，最长 80 字符 |
| event | ?event= | (空) | 二级事件名，最长 80 字符 |
| domain | ?domain= | 0 | Iceoryx 域 ID，隔离共享内存段 |
| depth | ?depth= | 0 | 历史缓冲深度，0=不缓冲 |
| history | ?history= | 0 (pub/sub), 1 (field) | 迟到订阅者可接收的历史消息数 |
| wait | ?wait= | 0 | 阻塞等待模式：1=阻塞，0=非阻塞 |

## 前置条件

使用 `shm://` 前必须满足以下条件之一：
- 外部启动 Iceoryx RouDi 守护进程：`iox-roudi &`
- 进程内初始化 RouDi：`ShmConf::init_roudi()` + `ShmConf::init_runtime(name, true)`

## 关键代码分析

### 1. depth -- 缓冲深度

```
depth=0:   无缓冲，发布者直接覆盖单一槽位
depth=1-5: 适合低频控制信号
depth=16:  适合高频传感器数据（防突发丢失）
```

深度越大，占用的共享内存越多。建议根据消息大小和发布频率选择合适的值。

### 2. history -- 迟到加入

```
history=0: 迟到订阅者不收历史消息（pub/sub 默认）
history=1: 收到最后 1 条消息（field 模型默认）
history=5: 收到最后 5 条消息
```

### 3. wait -- 阻塞模式

```
wait=0: 非阻塞模式，适合 polling（默认）
wait=1: 阻塞模式，Subscriber 等待新消息到达
```

**注意**: wait 模式仅适用于 Publisher/Subscriber，不适用于 Server/Client/Getter/Setter。

### 4. 地址长度限制

Iceoryx 要求 address 和 event 字符串各不超过 80 字符。如果需要更长的标识符，建议使用 URL 重映射（`url_remap`）。

### 5. RouDi 初始化

```cpp
// 方式 A: 进程内 RouDi（适合测试）
ShmConf::init_roudi();
ShmConf::init_runtime("my_app", true);

// 方式 B: 连接外部 RouDi（推荐生产环境）
ShmConf::init_runtime("my_app");
```

## 编译与运行

```bash
# 先启动 RouDi 守护进程
iox-roudi &

mkdir build && cd build
cmake .. -DCMAKE_PREFIX_PATH=/path/to/vlink/install
make example_url_shm
./output/bin/example_url_shm
```

## 预期输出

```
[I] === Example 1: Basic shm:// address ===
[I]   transport:shm
[I]   host:vehicle
[I]   path:/speed
[I] === Example 3: Depth parameter ===
[I]   depth:16
[I] === Example 5: Wait (blocking) mode ===
[I]   wait:1
...
```

## 扩展思考

- `shm://` 的零拷贝特性意味着大消息（如图像、点云）传输时不产生额外内存拷贝，延迟恒定。
- 在自动驾驶系统中，摄像头到感知模块的数据传输通常使用 `shm://` + 较大的 `depth`。
- `domain` 参数可以在同一机器上隔离不同应用的共享内存，避免名称冲突。
- 如果 RouDi 崩溃，所有 `shm://` 节点都会失去连接。生产环境建议使用 RouDi 守护进程管理器确保自动重启。
