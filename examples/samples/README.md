# samples/ -- VLink 传输协议专项示例

本目录包含 VLink 框架针对各传输后端的完整可运行示例。每个子目录聚焦于一种或多种传输协议，演示该协议下的通信原语用法、URL 格式、序列化配置和运行要求。

---

## 1. 示例总览

| 示例 | 传输协议 | 序列化格式 | 通信模型 | 进程模式 | 外部依赖 |
|------|----------|------------|----------|----------|----------|
| [helloworld](helloworld/) | 多后端可切换（dds/ddsc/shm/someip/fdbus/qnx） | Protobuf | Method + Event | 多进程 | Protobuf + 所选传输后端 |
| [ping_pong](ping_pong/) | 多后端可切换（同上） | Bytes（原始字节） | Event（双向） | 多进程 | 所选传输后端 |
| [shm_raw](shm_raw/) | `shm://`（共享内存） | Bytes（原始字节） | Method + Event + Field + Security | 单进程 | Iceoryx |
| [dds_dynamic](dds_dynamic/) | `dds://`（FastDDS） | DynamicData + Protobuf | Method + Event | 单进程 | FastDDS + Protobuf |
| [dds_idl](dds_idl/) | `dds://`（FastDDS） | FastDDS IDL (CDR) | Method + Event | 单进程 | FastDDS + IDL 工具链 |
| [ddsc_proto](ddsc_proto/) | `ddsc://`（CycloneDDS） | Protobuf | Method + Event | 单进程 | CycloneDDS + Protobuf |
| [fdbus_proto](fdbus_proto/) | `fdbus://`（FDBus） | Protobuf | Method + Event + Field | 单进程 | FDBus + Protobuf |
| [someip_flat](someip_flat/) | `someip://`（SOME/IP） | FlatBuffers | Method + Event + Field | 单进程 | vsomeip + FlatBuffers |
| [pub_sub_fbs](pub_sub_fbs/) | `ddsc://`（CycloneDDS） | FlatBuffers | Event（发布/订阅） | 多进程（Pub + Sub） | CycloneDDS + FlatBuffers |

---

## 2. 各示例关系与定位

![Samples Relationship Tree](images/samples-relationship.png)

- **helloworld** 和 **ping_pong** 是多协议通用示例，通过环境变量切换后端
- **shm_raw** 是功能最全面的单协议示例，覆盖全部 6 种通信原语 + Security 变体
- 其余 6 个示例各聚焦于一种特定传输协议的典型用法

---

## 3. 各协议 URL 格式速查

### 3.1 DDS / DDSC（话题路径模式）

```
dds://话题路径         例：dds://helloworld/method
ddsc://话题路径        例：ddsc://phone/event
```

话题路径即为 DDS Domain 中的 Topic 名称。同一路径的 Publisher/Subscriber 或 Server/Client 自动匹配。

### 3.2 SHM（分组/话题模式）

```
shm://分组名/话题名    例：shm://example_raw/method
```

分组名用于逻辑隔离，话题名标识具体通信通道。需要 Iceoryx RouDi 守护进程运行。

### 3.3 SOME/IP（服务 ID 模式）

```
someip://ServiceID/InstanceID?method=MethodID                         -- Method 模型
someip://ServiceID/InstanceID?groups=EventGroupID&event=EventID       -- Event 模型
someip://ServiceID/InstanceID?groups=EventGroupID&event=EventID&field=1  -- Field 模型
```

所有 ID 为十六进制格式（0x 前缀）。`groups` 支持逗号分隔的多个事件组：`groups=0x1,0x2`。

### 3.4 FDBus（服务名 + 事件参数模式）

```
fdbus://服务名?event=话题名    例：fdbus://phone?event=req
```

同一服务名下通过 `?event=` 参数区分不同的通信通道。

---

## 4. 构建全部传输示例

```bash
# 从 VLink 项目根目录
cd /work/vlink
cmake -DCMAKE_BUILD_TYPE=Release -DENABLE_EXAMPLES=ON -B build -S .
cmake --build build -j$(nproc)

# 查看编译产物
ls build/output/bin/sample_*
```

缺少依赖的示例会自动跳过（CMakeLists.txt 中有 `find_package` + `return()` 保护）。`dds_idl` 默认未加入 `examples/samples/CMakeLists.txt`，需要 FastDDS IDL 工具链时再手动启用。各示例的链接目标如下：

| 示例 | CMake 链接目标 | find_package 组件 |
|------|---------------|-------------------|
| helloworld | `helloworld_gen` + `vlink::vlink` | `vlink COMPONENTS ALL` + Protobuf |
| ping_pong | `vlink::vlink` | `vlink COMPONENTS ALL` |
| shm_raw | `vlink::shm` | `vlink COMPONENTS shm` |
| dds_dynamic | `dds_dynamic_gen` + `vlink::dds` | `vlink COMPONENTS dds` + Protobuf |
| dds_idl | `dds_idl_gen` + `vlink::dds` | `vlink COMPONENTS dds` |
| ddsc_proto | `ddsc_proto_gen` + `vlink::ddsc` | `vlink COMPONENTS ddsc` + Protobuf |
| fdbus_proto | `fdbus_proto_gen` + `vlink::fdbus` | `vlink COMPONENTS fdbus` + Protobuf |
| someip_flat | `someip_flat_gen` + `vlink::someip` | `vlink COMPONENTS someip` + Flatbuffers |
| pub_sub_fbs | `pub_sub_fbs_gen` + `vlink::ddsc` | `vlink COMPONENTS ddsc` + Flatbuffers |

`vlink_generate_cpp()` 是 VLink 提供的 CMake 辅助宏，支持从 `.proto`（Protobuf）、`.fbs`（FlatBuffers）、`.idl`（FastDDS IDL）文件自动生成 C++ 代码。

---

## 5. 推荐学习顺序

1. **helloworld** -- 最全面的入门示例，理解 VLink 的 Method/Event 模型、环境变量协议切换、Timer 定时发布
2. **ping_pong** -- 理解 Bytes 原始字节通信、延迟测量方法、可配置负载大小
3. **shm_raw** -- 学习全部 6 种通信原语和 Security 加密变体
4. **ddsc_proto** -- CycloneDDS + Protobuf 的标准用法，最简洁的协议专项示例
5. **dds_dynamic** -- DynamicData 动态类型系统，一个话题传多种消息
6. **fdbus_proto** -- FDBus IPC 协议，完整覆盖三种通信模型
7. **pub_sub_fbs** -- FlatBuffers 发布订阅最小范式，适合理解 `UserT` 和 `User*` 的配合方式
8. **someip_flat** -- SOME/IP 车载以太网场景 + FlatBuffers 序列化
9. **dds_idl** -- FastDDS 原生 IDL 类型（适合已有 IDL 定义的项目）

---

## 6. 运行前置条件速查

| 示例 | 启动命令 | 前置操作 |
|------|----------|----------|
| helloworld (dds, 默认) | `./build/output/bin/sample_helloworld_server` + `./build/output/bin/sample_helloworld_client sub` | 无 |
| helloworld (shm) | `METHOD_TRANSPORT=shm EVENT_TRANSPORT=shm ./build/output/bin/sample_helloworld_server` | `iox-roudi &` |
| helloworld (someip) | `METHOD_TRANSPORT=someip EVENT_TRANSPORT=someip ./build/output/bin/sample_helloworld_server` | 启动 vsomeip routing manager |
| ping_pong | `./build/output/bin/sample_pong` + `./build/output/bin/sample_ping [payload_size]` | 无（默认 dds） |
| shm_raw | `./build/output/bin/sample_shm_raw` | `iox-roudi &` |
| dds_dynamic | `./build/output/bin/sample_dds_dynamic` | 无 |
| dds_idl | `./build/output/bin/sample_dds_idl` | 无（需手动启用构建） |
| ddsc_proto | `./build/output/bin/sample_ddsc_proto` | 无 |
| pub_sub_fbs | `./build/output/bin/sample_pub_sub_fbs sub` + `./build/output/bin/sample_pub_sub_fbs pub` | 无 |
| fdbus_proto | `./build/output/bin/sample_fdbus_proto` | `fdb_name_server &` |
| someip_flat | `./build/output/bin/sample_someip_flat` | 启动 vsomeip routing manager |

## 7. 相关文档

详细原理参见 [doc/22-examples.md](../../doc/22-examples.md)。
