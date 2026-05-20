# VLink Examples -- 示例项目总览

本目录包含 VLink 框架的全部官方示例代码，覆盖从入门到高级的各种使用场景。每个子目录聚焦于一个特定主题，提供可编译运行的完整工程。

> **注意**：本 README 关注代码层面的实践指导，与 `doc/` 目录下的框架原理文档互补，不做重复。如需了解 VLink 的架构设计、传输层原理、序列化机制等，请参阅根目录的 `README.md` 及 `doc/` 下各专题文档（白皮书在 `doc/00-whitepaper.md`）。

---

## 1. 目录总览

本示例集共包含 **14 个分类目录**、**89 个独立示例工程**。启用 `ENABLE_EXAMPLES=ON` 且保持 `ENABLE_WHOLE_EXAMPLES=OFF` 时，仅构建 `samples/` 中当前启用的综合示例；再启用 `ENABLE_WHOLE_EXAMPLES=ON` 时会覆盖其余分类。`samples/dds_idl` 在父级 `examples/samples/CMakeLists.txt:21` 注释禁用，需手动取消注释并准备 FastDDS IDL 工具链才会被加入构建。

| 目录 | 说明 | 工程数 | 前置知识 |
|------|------|--------|----------|
| `quickstart/` | 快速入门指南 | 3 | 无（使用 `intra://`） |
| `base/` | 基础库教程 | 23 | 无 |
| `serialization/` | 序列化类型 | 7 | quickstart |
| `communication/` | 通信模型 | 7 | quickstart |
| `url_guide/` | URL 书写指南 | 9 | communication |
| `qos/` | QoS 配置 | 3 | url_guide |
| `security/` | 加密与认证 | 4 | communication |
| `zerocopy/` | 零拷贝传输 | 4 | base/bytes |
| `recording/` | 数据录制与回放 | 5 | communication |
| `plugin/` | 插件系统 | 4 | base |
| `proxy/` | 代理 API/服务 | 3 | communication |
| `c_api/` | C 语言 API | 4 | quickstart |
| `node_features/` | 高级节点特性 | 4 | communication |
| `samples/` | 传输协议专项示例 | 9 | 视具体示例而定 |

---

## 2. 各目录详细说明

### 2.1 quickstart/ -- 快速入门（3 个工程）

面向零基础用户的三个入门示例，使用进程内传输（`intra://`），无需任何外部依赖即可运行。

| 工程 | 说明 |
|------|------|
| `hello_pubsub` | 发布/订阅（Event 模型）入门 -- Publisher 发布消息，Subscriber 接收回调 |
| `hello_rpc` | 请求/响应（Method 模型）入门 -- Client 发起调用，Server 处理并返回结果 |
| `hello_field` | 状态读写（Field 模型）入门 -- Setter 写入状态值，Getter 读取最新值 |

### 2.2 base/ -- 基础库教程（23 个工程）

VLink 基础工具库的全面教程，涵盖字节操作、日志、事件循环、定时器、线程池等基础设施。这些工具被 VLink 通信原语内部广泛使用，掌握它们有助于编写高质量的应用代码。

| 工程 | 说明 |
|------|------|
| `bytes_basic` | Bytes 类型基本操作（创建、访问、比较） |
| `bytes_advanced` | Bytes 高级用法（切片、拼接、from_string/to_string 转换） |
| `bytes_zerocopy` | Bytes 零拷贝语义（共享内存场景下的引用传递） |
| `memory_pool` | MemoryPool 分级金字塔内存池（tier 配置、统计、trim） |
| `logger_basic` | 日志系统基本配置（VLOG_I/D/W/E 和 CLOG_D printf 风格） |
| `logger_advanced` | 日志高级用法（级别过滤、自定义输出后端） |
| `message_loop_basic` | MessageLoop 事件循环入门（run/quit、任务投递） |
| `message_loop_advanced` | MessageLoop 高级用法（多循环协作、跨线程投递） |
| `message_loop_coroutine` | MessageLoop + C++20 协程（`ENABLE_CXX_STD_20=ON`；`Task<T>`、`co_spawn`、`schedule/yield/delay_ms`、`when_all/when_any/sequence`） |
| `timer_basic` | Timer 定时器入门（attach/set_interval/start） |
| `timer_advanced` | Timer 高级用法（单次触发、动态调整间隔、kInfinite） |
| `elapsed_timer` | ElapsedTimer 耗时测量工具 |
| `deadline_timer` | 截止时间检测器 |
| `thread_pool` | 线程池任务调度（含 `post_task_handle`、shutdown 自分离） |
| `spin_lock` | 自旋锁实现 |
| `graph_task` | 图任务（DAG 依赖执行；状态回调 snapshot 语义） |
| `cancellation` | 协作取消三件套（`CancellationSource` / `Token` / `Registration` + `OperationCancelled`） |
| `task_handle` | Tracked 任务投递（`post_task_handle` / `PostTaskOptions` / `TaskExecutionState` 全状态机覆盖） |
| `multi_loop` | 多事件循环协同 |
| `object_pool` | 对象池复用 |
| `process` | 进程信息查询 |
| `schedule` | 调度器用法 |
| `utils` | 工具函数（路径、线程、信号等） |

### 2.3 serialization/ -- 序列化类型（7 个工程）

演示 VLink 支持的各种序列化类型。VLink 的通信原语通过模板参数确定消息类型，框架根据类型自动选择序列化策略。

| 工程 | 说明 |
|------|------|
| `bytes_type` | 原始字节（Bytes）类型 -- 无序列化开销，适合自定义协议或性能敏感场景 |
| `pod_type` | POD 标量类型（int、float、uint64_t 等） -- 直接内存拷贝 |
| `string_type` | 字符串类型（std::string） -- 自动序列化/反序列化 |
| `custom_type` | 自定义类型 -- 需实现序列化接口 |
| `stream_type` | 流式序列化（Stream） |
| `dynamic_data` | DynamicData 动态类型 -- 运行时类型擦除容器，一个话题传多种类型 |
| `intra_data` | 进程内零拷贝传输类型 -- intra:// 专用的引用传递优化 |

### 2.4 communication/ -- 通信模型（7 个工程）

VLink 三种通信模型的完整教程，包括基础用法和高级特性。

| 工程 | 说明 |
|------|------|
| `event_basic` | Event 模型入门 -- Publisher.publish() + Subscriber.listen() 基本模式 |
| `event_advanced` | Event 高级用法 -- 过滤、批量发布、wait_for_subscribers 等 |
| `field_basic` | Field 模型入门 -- Setter.set() 写入 + Getter.get() 轮询读取 |
| `field_advanced` | Field 高级用法 -- 监听变化回调、缓存策略、std::optional 返回值处理 |
| `method_sync` | Method 同步调用 -- Client.invoke(req, resp, timeout) 阻塞等待 |
| `method_async` | Method 异步调用 -- 回调/Future 模式 |
| `method_fire_forget` | Method 单向调用 -- Client.send(req) 无返回值的 Fire & Forget 模式 |

### 2.5 url_guide/ -- URL 书写指南（9 个工程）

VLink URL 是连接通信原语与传输后端的纽带。URL 的 transport 字段决定使用哪个传输模块，host/path 部分标识话题，query 参数传递协议特定配置。

| 工程 | 说明 |
|------|------|
| `url_basics` | URL 基本结构 -- `transport://host/topic[?params]` 通用格式 |
| `url_intra` | 进程内传输 URL -- `intra://topic_name`，无依赖，最简配置 |
| `url_dds` | FastDDS 传输 URL -- `dds://topic_name`，支持 QoS 参数 |
| `url_shm` | 共享内存传输 URL -- `shm://group/topic`，需要 Iceoryx RouDi |
| `url_someip` | SOME/IP 传输 URL -- `someip://ServiceID/InstanceID?method=MethodID`，十六进制 ID |
| `url_zenoh` | Zenoh 传输 URL -- `zenoh://topic_name` |
| `url_mqtt` | MQTT 传输 URL -- `mqtt://topic_name` |
| `url_remap` | URL 重映射机制 -- 运行时替换 URL 的 transport 或 host 部分 |
| `url_environment` | 环境变量动态 URL 配置 -- Utils::get_env() 模式 |

### 2.6 qos/ -- QoS 配置（3 个工程）

服务质量（Quality of Service）参数配置教程。

| 工程 | 说明 |
|------|------|
| `qos_basics` | QoS 基本概念与默认配置 |
| `qos_history_depth` | 历史深度配置（消息队列长度） |
| `qos_profiles` | QoS 预设配置文件 |

### 2.7 security/ -- 加密与认证（4 个工程）

通信安全功能教程。VLink 提供 Security 前缀的通信原语变体（SecurityPublisher、SecurityClient 等），在构造时通过第二参数 `const Security::Config&` 一次性传入对称密钥、RSA 密钥或自定义回调；省略该参数时使用内置默认安全槽位，保护通信内容。

| 工程 | 说明 |
|------|------|
| `security_basic` | Security 变体基本用法 -- SecurityPublisher/SecuritySubscriber 等 |
| `security_custom` | 自定义加密回调 -- 通过 `Config::encrypt_callback` / `decrypt_callback` 绕过内置 AEAD |
| `security_rsa` | RSA-OAEP hybrid + 可选 RSA-PSS 签名认证；独立 `Security` 实例往返与错签名拒绝 |
| `security_ssl` | SSL/TLS 安全传输 |

### 2.8 zerocopy/ -- 零拷贝传输（4 个工程）

高性能场景下的零拷贝数据传输技术，主要用于 `shm://` 传输后端。

| 工程 | 说明 |
|------|------|
| `zerocopy_loan` | Loan-based 零拷贝 API |
| `zerocopy_camera_frame` | 摄像头帧零拷贝传输实例 |
| `zerocopy_point_cloud` | 点云数据零拷贝传输实例 |
| `zerocopy_raw_data` | 原始数据零拷贝传输 |

### 2.9 recording/ -- 数据录制与回放（5 个工程）

通信数据的录制、存储和回放功能。

| 工程 | 说明 |
|------|------|
| `record_basic` | 录制与回放入门 |
| `record_bag_writer` | Bag 文件写入 |
| `record_bag_reader` | Bag 文件读取 |
| `record_mcap` | MCAP 格式支持 |
| `record_compression` | 录制数据压缩 |

### 2.10 plugin/ -- 插件系统（4 个工程）

VLink 插件化扩展机制，支持运行时动态加载模块。

| 工程 | 说明 |
|------|------|
| `plugin_basic` | 插件基本加载与使用 |
| `plugin_create` | 自定义插件开发 |
| `plugin_runnable` | `RunablePluginInterface` runnable 插件模式 |
| `plugin_schema` | 统一 schema 插件（protobuf + flatbuffers） |

### 2.11 proxy/ -- 代理 API/服务（3 个工程）

通过代理层访问 VLink 通信节点，适用于跨语言或远程场景。

| 工程 | 说明 |
|------|------|
| `proxy_api_basic` | 代理 API 基本用法 |
| `proxy_server_basic` | 代理服务端搭建 |
| `proxy_runnable_plugin` | 可运行代理插件 |

### 2.12 c_api/ -- C 语言 API（4 个工程）

为 C 语言项目提供的 VLink 绑定接口，适用于嵌入式系统或 FFI 场景。

| 工程 | 说明 |
|------|------|
| `c_pubsub` | C API 发布/订阅 |
| `c_rpc` | C API 远程调用 |
| `c_field` | C API 字段读写 |
| `c_security` | C API 应用层加密（独立 `vlink_security_handle_t` + `vlink_create_secure_*` 一次性装入 `vlink_security_config_t`） |

### 2.13 node_features/ -- 高级节点特性（4 个工程）

VLink 节点的高级功能配置。

| 工程 | 说明 |
|------|------|
| `lifecycle` | 节点生命周期管理 |
| `message_loop_binding` | 节点绑定到 MessageLoop |
| `properties` | 节点属性系统 |
| `status_monitoring` | 节点状态监控 |

### 2.14 samples/ -- 传输协议专项示例（9 个工程）

针对各传输后端的完整可运行示例。当 `ENABLE_EXAMPLES=ON` 而 `ENABLE_WHOLE_EXAMPLES=OFF`（默认配置）时，**仅编译这一组**综合示例；其余 13 个分类要在 `ENABLE_WHOLE_EXAMPLES=ON` 时才会被编译。详见 [samples/README.md](samples/README.md)。

| 工程 | 传输协议 | 序列化格式 | 通信模型 | 进程模式 |
|------|----------|------------|----------|----------|
| `helloworld` | 多后端可切换 | Protobuf | Method + Event | 多进程（Server + Client） |
| `ping_pong` | 多后端可切换 | Bytes | Event（双向延迟测量） | 多进程（Ping + Pong） |
| `shm_raw` | `shm://` | Bytes | Method + Event + Field + Security | 单进程 |
| `dds_dynamic` | `dds://` | DynamicData + Protobuf | Method + Event | 单进程 |
| `dds_idl` | `dds://` | FastDDS IDL (CDR) | Method + Event | 单进程 |
| `ddsc_proto` | `ddsc://` | Protobuf | Method + Event | 单进程 |
| `pub_sub_fbs` | `ddsc://` | FlatBuffers | Event | 多进程（Pub + Sub） |
| `fdbus_proto` | `fdbus://` | Protobuf | Method + Event + Field | 单进程 |
| `someip_flat` | `someip://` | FlatBuffers | Method + Event + Field | 单进程 |

---

## 3. 推荐学习路径

根据目标和经验水平，选择合适的学习路径。所有分类都已经包含可运行源码；如果你只想先看默认构建产物，建议从 `samples/` 开始，再按主题回看对应分类。

### 3.1 路径 1：初学者入门路线

```
quickstart/ --> base/ --> serialization/ --> communication/
```

**目标读者**：首次接触 VLink 的开发者

**学习重点**：
1. **quickstart/**：通过三个最小示例（hello_pubsub / hello_rpc / hello_field）理解 VLink 的三种通信模型。使用 `intra://` 传输，无需部署外部中间件。
2. **base/**：掌握 VLink 基础设施 -- Bytes 字节操作、MessageLoop 事件循环、Timer 定时器、Logger 日志系统。这些组件在所有传输示例中被大量使用。
3. **serialization/**：学习 VLink 支持的各种数据类型 -- 原始字节、POD 标量、Protobuf、FlatBuffers、DynamicData。理解模板参数如何决定序列化策略。
4. **communication/**：深入三种通信模型的完整用法 -- 同步/异步 RPC、事件过滤、字段监听、Fire & Forget。

**实践建议**：在等待这些教程完成之前，可先阅读 `samples/helloworld/` 的源码，它已完整演示了 Method + Event 两种模型的实际用法，并展示了 MessageLoop、Timer、Utils 等基础设施的典型使用模式。

**建议耗时**：2-3 天

### 3.2 路径 2：URL 与传输协议路线

```
url_guide/ --> qos/ --> samples/
```

**目标读者**：需要对接不同传输后端的集成工程师

**学习重点**：
1. **url_guide/**：理解 VLink URL 的统一格式，掌握各传输协议的 URL 书写规则。特别注意 SOME/IP 的十六进制 ID 格式和 FDBus 的 `?event=` 参数格式。
2. **qos/**：根据业务场景配置 QoS 参数 -- 可靠性、历史深度、预设配置文件。
3. **samples/**（已实现）：在真实传输协议上运行完整示例。重点关注：
   - `helloworld/helloworld_common.h` -- 通过环境变量切换 6 种传输协议的 URL 映射实现
   - `ping_pong/ping_pong_common.h` -- 同样模式的延迟测量版本
   - 各协议专项示例的 URL 格式差异

**实践建议**：通过 `helloworld` 的环境变量切换机制，用同一套代码依次运行 DDS、SHM、DDSC、FDBus 等后端，对比各协议的部署差异和性能特征。

**建议耗时**：2-3 天

### 3.3 路径 3：高级功能路线

```
security/ --> zerocopy/ --> recording/ --> proxy/
```

**目标读者**：需要安全通信、高性能传输或数据分析的高级开发者

**学习重点**：
1. **security/**：通信加密 -- Security 变体原语、自定义安全密钥、SSL/TLS 传输加密。
2. **zerocopy/**：零拷贝高性能 -- Loan API、摄像头帧/点云等大数据量场景。
3. **recording/**：数据录制回放 -- Bag 文件读写、MCAP 格式、数据压缩。
4. **proxy/**：代理服务 -- 跨语言访问、远程代理。

**实践建议**：`samples/shm_raw/` 已完整演示了 Security 变体的六种通信原语，包括在 SecurityXxx 构造时通过第二参数传入 `Security::Config` 的对称密钥用法和 `SecurityServer/Client/Publisher/Subscriber/Setter/Getter` 的实际用法，是学习安全通信的最佳起点。

**建议耗时**：3-4 天

### 3.4 路径 4：系统集成路线

```
c_api/ --> plugin/ --> node_features/
```

**目标读者**：需要将 VLink 集成到现有系统中的架构师

**学习重点**：
1. **c_api/**：通过 C 语言绑定接入非 C++ 项目（如嵌入式系统、FFI 场景）。
2. **plugin/**：使用插件机制实现模块化架构，支持运行时动态加载。
3. **node_features/**：节点生命周期管理、属性配置、状态监控等高级特性。

**实践建议**：可以同时参考 `samples/` 中各示例的 CMakeLists.txt，了解 `find_package(vlink COMPONENTS ...)` 和 `vlink_generate_cpp()` 的标准用法。

**建议耗时**：2-3 天

---

## 4. 构建说明

### 4.1 构建全部示例

```bash
# 从 VLink 项目根目录构建（推荐方式，自动检测已安装的依赖）
cd /work/vlink

# 配置 Release 构建，并启用全量 examples
cmake -DCMAKE_BUILD_TYPE=Release -DENABLE_EXAMPLES=ON -DENABLE_WHOLE_EXAMPLES=ON -B build -S .

# 编译所有示例（缺少依赖的示例会自动跳过，不会报错）
cmake --build build -j$(nproc)

# 编译产物位于
ls build/output/bin/example_* build/output/bin/sample_*
```

如果系统中已安装过其他版本的 VLink，运行源码树 `build/output/bin` 下的示例前请先使用当前 build 的库路径，避免加载到 `/usr/local/lib` 中的旧库：

```bash
export LD_LIBRARY_PATH=$PWD/build/output/lib:$LD_LIBRARY_PATH
# 或 source build/output/vlink-setup.sh
```

> **提示**：每个 `samples/` 子工程的 CMakeLists.txt 都包含依赖检测逻辑。例如 `shm_raw` 会检测 `vlink::shm` 目标是否存在，不存在则打印 `Skip example_shm_raw.` 并跳过，不会导致构建失败。

### 4.2 独立构建 examples 目录

```bash
# 如果 VLink 已安装到系统路径或自定义路径
cd /work/vlink/examples
cmake -B build -S . -DCMAKE_PREFIX_PATH=<vlink安装路径> -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)
```

### 4.3 构建特定示例

```bash
# 直接构建单个示例工程
cd /work/vlink/examples/samples/helloworld
cmake -B build -S . -DCMAKE_PREFIX_PATH=<vlink安装路径>
cmake --build build
```

### 4.4 控制构建范围

顶层构建通过两个选项控制范围：`ENABLE_EXAMPLES=ON` 启用示例构建；`ENABLE_WHOLE_EXAMPLES=ON` 在默认 `samples/` 之外继续启用其余 13 个分类。若只想调整 `samples/` 内部的专项示例，可在 `examples/samples/CMakeLists.txt` 中注释/取消注释对应 `add_subdirectory()`：

```cmake
add_subdirectory(helloworld)
add_subdirectory(dds_dynamic)
# add_subdirectory(dds_idl)    # 默认注释 -- 需 FastDDS IDL 工具链
add_subdirectory(ddsc_proto)
add_subdirectory(pub_sub_fbs)
add_subdirectory(someip_flat)
add_subdirectory(shm_raw)
add_subdirectory(fdbus_proto)
add_subdirectory(ping_pong)
```

注意 `dds_idl` 默认被注释掉，因为它需要 FastDDS 的 `fastddsgen` IDL 代码生成工具链，需手动启用。

---

## 5. 运行示例时的环境变量

VLink 示例广泛使用环境变量来切换传输协议和配置运行参数：

### 5.1 helloworld 环境变量

| 环境变量 | 说明 | 可选值 | 默认值 |
|----------|------|--------|--------|
| `METHOD_TRANSPORT` | RPC 传输协议选择 | `dds`, `ddsc`, `shm`, `someip`, `fdbus`, `qnx` | `dds` |
| `EVENT_TRANSPORT` | 事件传输协议选择 | 同上 | `dds` |
| `METHOD_URL` | 直接指定完整 RPC URL（优先级高于 `METHOD_TRANSPORT`） | 任意合法 VLink URL | 无 |
| `EVENT_URL` | 直接指定完整事件 URL（优先级高于 `EVENT_TRANSPORT`） | 任意合法 VLink URL | 无 |

### 5.2 ping_pong 环境变量

| 环境变量 | 说明 | 可选值 | 默认值 |
|----------|------|--------|--------|
| `PING_TRANSPORT` | Ping 端传输协议选择 | `dds`, `ddsc`, `shm`, `someip`, `fdbus`, `qnx` | `dds` |
| `PONG_TRANSPORT` | Pong 端传输协议选择 | 同上 | `dds` |
| `PING_URL` | 直接指定 Ping URL（优先级高于 `PING_TRANSPORT`） | 任意合法 VLink URL | 无 |
| `PONG_URL` | 直接指定 Pong URL（优先级高于 `PONG_TRANSPORT`） | 任意合法 VLink URL | 无 |

### 5.3 使用方式

```bash
# 方式 1：命令行前缀（仅对当前命令生效）
METHOD_TRANSPORT=shm EVENT_TRANSPORT=shm ./build/output/bin/sample_helloworld_server

# 方式 2：export 预设（对当前终端所有后续命令生效）
export METHOD_TRANSPORT=ddsc
export EVENT_TRANSPORT=ddsc
./build/output/bin/sample_helloworld_server

# 方式 3：直接指定完整 URL（最高优先级，覆盖对应的 `*_TRANSPORT` 设置）
METHOD_URL="someip://0x01/0x02?method=0x1" \
EVENT_URL="someip://0x01/0x02?groups=0x1&event=0x2" \
    ./build/output/bin/sample_helloworld_server
```

---

## 6. 各传输后端的前置条件

| 传输后端 | URL 前缀 | 前置守护进程 | 额外依赖库 | CMake 组件名 |
|----------|-----------|-------------|-----------|-------------|
| 进程内 | `intra://` | 无 | 无 | `vlink::vlink` |
| FastDDS | `dds://` | 无 | FastDDS | `vlink::dds` |
| CycloneDDS | `ddsc://` | 无 | CycloneDDS | `vlink::ddsc` |
| RTI DDS | `ddsr://` | 无 | RTI Connext DDS（商用许可） | `vlink::ddsr` |
| TravoDDS | `ddst://` | 无 | TravoDDS | `vlink::ddst` |
| 共享内存（Iceoryx） | `shm://` | `iox-roudi`（Iceoryx RouDi 守护进程） | Iceoryx | `vlink::shm` |
| 共享内存（Iceoryx2） | `shm2://` | 无（Iceoryx2 无中央守护） | Iceoryx2 | `vlink::shm2` |
| SOME/IP | `someip://` | vsomeip routing manager（`vsomeipd`） | vsomeip + FlatBuffers | `vlink::someip` |
| FDBus | `fdbus://` | `fdb_name_server`（FDBus 名称服务） | FDBus | `vlink::fdbus` |
| QNX | `qnx://` | 无 | QNX OS | `vlink::qnx` |
| Zenoh | `zenoh://` | 无 | Zenoh | `vlink::zenoh` |
| MQTT | `mqtt://` | MQTT Broker | MQTT 客户端库 | `vlink::mqtt` |

---

## 7. VLink 六种通信原语速查

| 原语 | 通信模型 | 方向 | 创建方式 | 核心方法 |
|------|---------|------|---------|---------|
| `Publisher<T>` | Event | 发送方 | `Publisher<T> pub(url)` | `publish(msg)`, `wait_for_subscribers()` |
| `Subscriber<T>` | Event | 接收方 | `Subscriber<T> sub(url)` | `listen(callback)` |
| `Server<Req, Resp>` | Method | 服务端 | `Server<Req, Resp> srv(url)` | `listen(callback)` |
| `Client<Req, Resp>` | Method | 客户端 | `Client<Req, Resp> cli(url)` | `invoke(req)`, `send(req)`, `wait_for_connected()`, `detect_connected()` |
| `Setter<T>` | Field | 写入方 | `Setter<T> setter(url)` | `set(value)` |
| `Getter<T>` | Field | 读取方 | `Getter<T> getter(url)` | `get()` 返回 `std::optional<T>` |

每种原语都有对应的 Security 变体（如 `SecurityPublisher<T>`）。生产环境通常先构造 `Security::Config`（例如 `cfg.key = "..."`），然后作为 `SecurityXxx` 构造函数的第二参数传入，例如 `SecurityPublisher<T> pub(url, cfg)` 或 `SecurityPublisher<T> pub(url, cfg, InitType::kWithoutInit)`；省略该参数时使用内置默认安全槽位。运行时不再有公共 `enable_security()` / `security()` 接口。

---

## 8. 代码规范

所有示例遵循 VLink 项目的代码规范：

- **代码风格**：Google C++ Style，行宽上限 120 字符（`.clang-format`）
- **静态分析**：开启 clang-tidy 严格检查，警告视为错误（`.clang-tidy`）
- **C++ 标准**：C++17（部分功能使用 C++20 特性）
- **格式化工具**：`tools/format.sh`（clang-format + cmake-format）
- **代码检查**：`tools/check.sh`（cpplint，行宽 120）

---

## 9. 项目统计

| 指标 | 数值 |
|------|------|
| 分类目录总数 | 14 |
| 示例工程总数 | 89 |
| 全量构建覆盖工程数 | 88（`samples/dds_idl` 在 `samples/CMakeLists.txt:21` 注释禁用，需手动启用 FastDDS IDL 工具链） |
| 源文件数量 | 99 个 .cc + 4 个 .c + 33 个 .h + 4 个 .proto + 2 个 .fbs + 1 个 .idl |
| 覆盖传输协议 | intra, shm, shm2, dds, ddsc, ddsr, ddst, zenoh, someip, mqtt, fdbus, qnx（共 12 种 transport，部分依赖编译选项） |
| 覆盖序列化格式 | Protobuf, FlatBuffers, DDS IDL (CDR), Bytes, DynamicData |
| 覆盖通信模型 | Method (RPC), Event (Pub/Sub), Field (Get/Set), Fire & Forget |
