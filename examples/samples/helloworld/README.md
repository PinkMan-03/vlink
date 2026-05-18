# helloworld -- VLink 多协议入门示例

## 1. 概述

这是 VLink 框架最全面的入门示例，在一个项目中同时演示了两种核心通信模型：
- **Method 模型**（Server / Client）：RPC 请求-响应，客户端发送两个整数，服务端返回它们的和
- **Event 模型**（Publisher / Subscriber）：事件发布-订阅，服务端每 100ms 发布一条递增消息

本示例使用 Protobuf 作为序列化格式，并通过环境变量实现运行时传输协议切换，**同一套代码可在 6 种传输后端上运行**。

---

## 2. 目录结构

```
helloworld/
  CMakeLists.txt             -- 顶层构建配置，生成 Protobuf 代码
  helloworld_common.h        -- 公共配置：URL 定义 + 环境变量读取逻辑
  proto/
    helloworld.proto          -- Protobuf 消息定义（Request, Response, Message）
  server/
    CMakeLists.txt            -- 服务端构建配置
    helloworld_server.cc      -- 服务端程序（Server + Publisher + Timer）
  client/
    CMakeLists.txt            -- 客户端构建配置
    helloworld_client.cc      -- 客户端程序（Client + Subscriber，支持两种运行模式）
```

---

## 3. 消息定义分析

`helloworld.proto` 定义了三种消息类型，对应两种通信模型的数据结构：

```protobuf
package Helloworld;

message Request {
  uint64 left = 1;     // RPC 请求：加数
  uint64 right = 2;    // RPC 请求：被加数
}

message Response {
  uint64 sum = 1;      // RPC 响应：计算结果
}

message Message {
  string detail = 1;   // 事件消息：字符串内容
}
```

使用 `option optimize_for = LITE_RUNTIME;` 减小生成代码体积，适合嵌入式场景。

---

## 4. 服务端代码分析（helloworld_server.cc）

服务端程序在一个进程中同时承担 RPC 服务端和事件发布者两个角色。

### 4.1 关键代码段解析

**1. 单例保护**

```cpp
if (!Utils::check_singleton("helloworld_server")) {
    std::cerr << "Program has started." << std::endl;
    return 1;
}
```

`Utils::check_singleton()` 通过锁文件机制确保同一主机上不会重复启动同名程序。这在多协议测试时特别有用，避免端口或资源冲突。

**2. 事件循环与信号处理**

```cpp
MessageLoop message_loop;
Utils::register_terminate_signal([&message_loop](int) { message_loop.quit(); });
```

`MessageLoop` 是 VLink 的核心事件循环，所有 Timer 回调在其线程中执行。`register_terminate_signal()` 捕获 SIGTERM/SIGINT（Ctrl+C），调用 `quit()` 从任意线程安全退出事件循环。

**3. Method 模型 -- RPC 服务端**

```cpp
Server<Helloworld::Request, Helloworld::Response> server(Common::get_method_url());
server.listen([](const Helloworld::Request& req, Helloworld::Response& resp) {
    resp.set_sum(req.left() + req.right());
});
```

- `Server<Req, Resp>` 的模板参数确定请求和响应类型
- `listen()` 注册同步回调：`req` 为输入（const 引用），`resp` 为输出（引用传参，直接修改）
- URL 由 `Common::get_method_url()` 从环境变量动态获取

**4. Event 模型 -- 定时发布**

```cpp
Publisher<Helloworld::Message> pub(Common::get_event_url());

Timer timer;
timer.attach(&message_loop);
timer.set_interval(100);                  // 每 100ms 触发
timer.set_loop_count(Timer::kInfinite);   // 无限循环

int index = 0;
timer.start([&pub, &index]() {
    index++;
    Helloworld::Message msg;
    msg.set_detail("hello_world_" + std::to_string(index));
    pub.publish(msg);                     // 异步、非阻塞
});
```

Timer 必须 `attach()` 到 MessageLoop 才能工作。`publish()` 是非阻塞调用，消息通过传输层异步发送。

---

## 5. 客户端代码分析（helloworld_client.cc）

客户端支持两种运行模式，通过命令行参数选择：

### 5.1 模式 1：RPC 调用（`set` 命令）

```cpp
Client<Helloworld::Request, Helloworld::Response> client(Common::get_method_url());

// 阻塞等待服务端上线，超时 1 秒
if (!client.wait_for_connected(1s)) {
    VLOG_W("[Client] Server not ready.");
    return -1;
}

// 同步 RPC 调用，超时 3 秒
Helloworld::Response resp;
bool ret = client.invoke(req, resp, 3s);
```

关键点：
- `wait_for_connected(1s)` 是阻塞等待，在服务端未启动时返回 false，避免无限等待
- `invoke(req, resp, timeout)` 是带超时的同步调用，返回 `bool` 表示成功/失败
- 这里使用的是双参数 invoke（req 输入 + resp 输出引用），另一种写法是 `auto resp = client.invoke(req)` 返回 `std::optional`

### 5.2 模式 2：事件订阅（`sub` 命令）

```cpp
Subscriber<Helloworld::Message> sub(Common::get_event_url());
sub.listen([](const Helloworld::Message& msg) {
    CLOG_D("[Client] Receive event: %s.", msg.detail().c_str());
});

// 用 condition_variable 阻塞主线程
vlink::ConditionVariable cv;
Utils::register_terminate_signal([&cv](int) { cv.notify_one(); });
cv.wait(lock);
```

关键点：
- `listen()` 注册的回调在 VLink 内部线程中异步触发，无需事件循环
- 使用 `vlink::ConditionVariable`（而非 `std::condition_variable`）是为了跨平台兼容性
- 主线程通过 `cv.wait()` 阻塞，直到收到 Ctrl+C 信号

---

## 6. 环境变量协议切换机制

`helloworld_common.h` 实现了基于环境变量的传输协议动态选择。核心逻辑：

1. **优先级 1**：检查 `METHOD_URL` / `EVENT_URL` 环境变量，如果设置了则直接使用完整 URL
2. **优先级 2**：检查 `METHOD_TRANSPORT` / `EVENT_TRANSPORT` 环境变量，根据 transport 值映射到预定义 URL
3. **默认值**：如果都未设置，使用 `dds://helloworld/method` 和 `dds://helloworld/event`

### 6.1 支持的协议和对应 URL

| 环境变量值 | Method URL | Event URL |
|-----------|-----------|-----------|
| `dds`（默认） | `dds://helloworld/method` | `dds://helloworld/event` |
| `ddsc` | `ddsc://helloworld/method` | `ddsc://helloworld/event` |
| `shm` | `shm://helloworld/method` | `shm://helloworld/event` |
| `someip` | `someip://0x01/0x02?method=0x1` | `someip://0x01/0x02?groups=0x1&event=0x2` |
| `fdbus` | `fdbus://helloworld/method` | `fdbus://helloworld/event` |
| `qnx` | `qnx://helloworld/method` | `qnx://helloworld/event` |

注意 SOME/IP 的 URL 格式与其他协议截然不同：使用十六进制服务 ID / 实例 ID / 方法 ID，而非路径风格的话题名。

---

## 7. 构建配置分析

顶层 `CMakeLists.txt` 的关键逻辑：

```cmake
# 查找 Protobuf（CONFIG 模式，静默失败）
find_package(Protobuf CONFIG QUIET)

# 查找 VLink（ALL 组件 = 所有可用传输后端）
find_package(vlink QUIET COMPONENTS ALL)

# 如果没有 Protobuf 则跳过
if(NOT Protobuf_FOUND)
  message(STATUS "Skip ${PROJECT_NAME}.")
  return()
endif()

# 使用 vlink_generate_cpp 从 .proto 生成 C++ 代码
vlink_generate_cpp(TARGET helloworld_gen PROTO ${PROTO_SRCS})
```

`find_package(vlink COMPONENTS ALL)` 会加载所有已安装的传输模块，使得同一二进制文件可以通过 URL 中的 transport 字段在运行时选择任意后端。

---

## 8. 构建与运行

```bash
# 构建
cd /work/vlink
cmake -DCMAKE_BUILD_TYPE=Release -B build -S .
cmake --build build -j$(nproc)

# 运行服务端（终端 1，默认使用 dds://）
./build/output/bin/sample_helloworld_server

# 订阅事件（终端 2）
./build/output/bin/sample_helloworld_client sub

# 发起 RPC 调用（终端 3）
./build/output/bin/sample_helloworld_client set 10 20
# 预期输出：[Client] Receive sum: 30

# 切换到共享内存传输
iox-roudi &    # 先启动 Iceoryx 守护进程
METHOD_TRANSPORT=shm EVENT_TRANSPORT=shm ./build/output/bin/sample_helloworld_server
EVENT_TRANSPORT=shm ./build/output/bin/sample_helloworld_client sub
METHOD_TRANSPORT=shm ./build/output/bin/sample_helloworld_client set 3 4

# 切换到 CycloneDDS
METHOD_TRANSPORT=ddsc EVENT_TRANSPORT=ddsc ./build/output/bin/sample_helloworld_server
EVENT_TRANSPORT=ddsc ./build/output/bin/sample_helloworld_client sub
```

---

## 9. 依赖

- VLink 库（支持所选传输协议的全部组件）
- Protobuf（用于消息序列化）
- 所选传输后端的运行时依赖（如 Iceoryx RouDi、vsomeip routing manager 等）
