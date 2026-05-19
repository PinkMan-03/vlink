# URL SOME/IP -- someip:// 车载以太网传输详解

## 1. 概述

SOME/IP (Scalable service-Oriented MiddlewarE over IP) 是 AUTOSAR 标准的车载以太网通信协议。与其他 VLink 传输不同，`someip://` 使用数值 ID 而非字符串主题名。

```
RPC:   someip://service_id/instance_id?method=method_id
事件:  someip://service_id/instance_id?groups=g1,g2,...&event=event_id
字段:  someip://service_id/instance_id?groups=g1,g2,...&event=event_id&field=1
```

## 2. URL 参数详解

| 参数 | 位置 | 说明 |
|------|------|------|
| service_id | host | 16 位服务 ID，支持十进制或 0x 十六进制 |
| instance_id | path | 16 位实例 ID |
| method | ?method= | RPC 方法 ID（用于 Server/Client） |
| groups | ?groups= | 事件组 ID，逗号分隔（用于 Pub/Sub 和 Field） |
| event | ?event= | 事件 ID（配合 groups 使用） |
| field | ?field= | 1=字段模式（配合 groups+event） |

## 3. 三种通信模式

### 3.1 RPC 模式 (Server/Client)

```cpp
Server<Bytes, Bytes> server("someip://0x1234/0x5678?method=0x1");
Client<Bytes, Bytes> client("someip://0x1234/0x5678?method=0x1");
```

### 3.2 事件模式 (Publisher/Subscriber)

```cpp
Publisher<MyMsg> pub("someip://0x1234/0x5678?groups=1&event=16");
Subscriber<MyMsg> sub("someip://0x1234/0x5678?groups=1&event=16");
```

### 3.3 字段模式 (Setter/Getter)

```cpp
Setter<int> setter("someip://0x1234/0x5678?groups=1&event=20&field=1");
Getter<int> getter("someip://0x1234/0x5678?groups=1&event=20&field=1");
```

## 4. 关键代码分析

### 4.1 十六进制 ID 表示

```cpp
// 十进制
"someip://4660/22136?method=1"
// 十六进制（推荐，与 AUTOSAR 规范一致）
"someip://0x1234/0x5678?method=0x1"
```

SOME/IP 服务 ID 传统上使用十六进制。VLink URL 支持 `0x` 前缀。

### 4.2 多事件组

```cpp
"someip://0x1234/0x5678?groups=1,2,3&event=16"
```

订阅者可以同时加入多个事件组，逗号分隔。

### 4.3 SomeipConf 直接构造

```cpp
// RPC
SomeipConf rpc_conf(0x1234, 0x5678, 0x0001);
// 事件
SomeipConf event_conf(0x1234, 0x5678, {0x0001}, 0x0010);
// 字段
SomeipConf field_conf(0x1234, 0x5678, {0x0001}, 0x0020, true);
```

当服务 ID 来自配置文件时，直接构造 `SomeipConf` 比拼接 URL 字符串更安全。

### 4.4 典型车载服务布局

```
制动服务 (RPC):
  someip://0x0100/0x0001?method=0x01  -- getBrakePressure
  someip://0x0100/0x0001?method=0x02  -- setBrakeForce

制动服务 (事件):
  someip://0x0100/0x0001?groups=0x01&event=0x8001  -- brakePressureChanged

制动服务 (字段):
  someip://0x0100/0x0001?groups=0x02&event=0x8010&field=1  -- brakeWearLevel
```

## 5. 前置条件

- vsomeip 守护进程必须运行
- 建议提供 vsomeip JSON 配置文件
- 通过 `SomeipConf::load_global_config_file()` 加载配置

## 6. 编译与运行

```bash
cmake -B build -S . -DCMAKE_PREFIX_PATH=/path/to/vlink/install
cmake --build build --target example_url_someip
./build/output/bin/example_url_someip
```

## 7. 预期输出

```
[I] === Example 1: RPC (Method model) ===
[I]   Decimal URL:someip://4660/22136?method=1
[I]   Hex URL:    someip://0x1234/0x5678?method=0x1
[I]   host (service_id):  4660
[I]   path (instance_id): /22136
[I]   method:1
...
```

## 8. 扩展思考

- SOME/IP 的 ID 分配通常由 AUTOSAR 系统设计工具（如 Enterprise Architect + ARXML）生成。
- 在实际车载系统中，一个 ECU 上可能运行数十个 SOME/IP 服务，每个服务有多个方法和事件。
- `someip://` 不支持字符串主题名，所有寻址都基于数值 ID。
- 即发即忘 RPC：使用 `Client<Req>` + `send()` 发送无需确认的 SOME/IP 请求。
