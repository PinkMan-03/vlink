# VLink Logger 基础示例 -- 深入解析

## 概述

VLink 的 `Logger` 是一个全局单例日志系统，通过 `Logger::init()` 初始化后可在任意位置通过宏输出日志。本示例深入演示日志初始化、四种输出风格、六种日志级别以及动态级别控制。

`Logger` 基于 spdlog 构建，但提供了 VLink 特有的增强功能：四种风格宏、编译时级别过滤、自动 `{file:line}` 注解以及零分配的 FastStream 风格。

## 文件说明

| 文件 | 说明 |
|------|------|
| `logger_basic.cc` | 日志基础功能演示源码 |
| `CMakeLists.txt` | 构建配置，链接 `vlink::all` |

## 构建与运行

```bash
cmake --build . --target example_logger_basic
./examples/base/logger_basic/example_logger_basic
```

## 四种输出风格详解

VLink 提供四种风格的日志宏，适用于不同的开发场景和个人偏好。每种风格在所有六种级别（T/D/I/W/E/F）上都有对应的宏。

### 1. Stream 风格 (VLOG_*)

```cpp
VLOG_I("message: key=", value, " name=", name);
```

**内部实现**：使用 `FastStream` 的 `operator<<` 进行流式拼接。`FastStream` 是 VLink 自研的线程局部流对象，使用固定大小的栈缓冲区（4096 字节），**完全零堆分配**。

**适用场景**：
- 简单的消息拼接
- 性能敏感的热路径
- 需要零分配保证的实时系统

**示例**：
```cpp
VLOG_T("Stream style [Trace]: counter=", 0, " name=", "alpha");
VLOG_D("Stream style [Debug]: temperature=", 23.5, " unit=C");
VLOG_I("Stream style [Info]: application started successfully");
VLOG_W("Stream style [Warn]: disk usage is high, used=", 91, "%");
VLOG_E("Stream style [Error]: failed to open config file");
```

### 2. Format 风格 (MLOG_*)

```cpp
MLOG_I("connected to host={}, port={}", host, port);
```

**内部实现**：使用 Python/fmt 风格的 `{}` 占位符，通过 `vlink::format::format_to_n` 格式化到 4096 字节的线程局部缓冲区。支持位置参数和格式说明符。

**适用场景**：
- 需要格式化控制的复杂消息
- 习惯 Python/fmt 风格的开发者
- 需要在消息中嵌入多个参数

**示例**：
```cpp
MLOG_T("Format style [Trace]: value={}, label={}", 42, "beta");
MLOG_D("Format style [Debug]: elapsed={}ms", 150);
MLOG_I("Format style [Info]: connected to host={}, port={}", "192.168.1.1", 8080);
MLOG_W("Format style [Warn]: retry attempt {}/{}", 3, 5);
MLOG_E("Format style [Error]: timeout after {}ms", 5000);
```

### 3. C 风格 (CLOG_*)

```cpp
CLOG_I("PID=%d, name=%s", pid, name);
```

**内部实现**：使用传统的 printf 风格 `%d/%s` 格式说明符，内部调用 `std::snprintf`。

**适用场景**：
- 从 C 代码迁移
- 习惯 printf 的开发者
- 需要精确的格式控制（如 `%.2f`）

**示例**：
```cpp
CLOG_T("C style [Trace]: index=%d, tag=%s", 7, "gamma");
CLOG_D("C style [Debug]: ratio=%.2f", 3.14);
CLOG_I("C style [Info]: PID=%d started", 12345);
CLOG_W("C style [Warn]: memory usage=%d%%", 85);
CLOG_E("C style [Error]: errno=%d (%s)", 2, "No such file or directory");
```

### 4. RAII Stream 风格 (SLOG_*)

```cpp
SLOG_I << "value=" << x << " status=" << status;
```

**内部实现**：使用 `WrapperStream` 模板类，支持标准 `<<` 操作符链式调用。临时对象析构时自动刷新消息到 sink。

**适用场景**：
- 需要条件性拼接的复杂表达式
- 习惯 iostream 风格的开发者
- 需要在 if 分支中逐步构建消息

**示例**：
```cpp
SLOG_T << "RAII stream [Trace]: id=" << 100 << " status=ok";
SLOG_D << "RAII stream [Debug]: x=" << 1.5 << " y=" << 2.5;
SLOG_I << "RAII stream [Info]: initialization complete";
SLOG_W << "RAII stream [Warn]: connection unstable";
SLOG_E << "RAII stream [Error]: data corruption detected";
```

### 四种风格对比

| 风格 | 宏前缀 | 语法 | 堆分配 | 类型安全 | 编译检查 |
|------|--------|------|--------|---------|---------|
| Stream | `VLOG_` | 逗号拼接 | 零分配 | 是 | 是 |
| Format | `MLOG_` | `{}` 占位符 | 零分配 | 是 | 部分 |
| C      | `CLOG_` | `%d/%s` 格式 | 零分配 | 否 | 否 |
| RAII   | `SLOG_` | `<<` 操作符 | 零分配 | 是 | 是 |

**推荐使用优先级**：`VLOG_`（最快、最安全） > `MLOG_`（最灵活） > `SLOG_`（特殊场景） > `CLOG_`（兼容旧代码）

## 六种日志级别

| 级别值 | 名称 | 宏后缀 | 用途 | 颜色 |
|--------|------|--------|------|------|
| 0 | kTrace | `_T` | 详细的内部追踪信息 | 灰色 |
| 1 | kDebug | `_D` | 开发调试诊断信息 | 青色 |
| 2 | kInfo | `_I` | 正常运行状态消息 | 绿色 |
| 3 | kWarn | `_W` | 异常但可恢复的状况 | 黄色 |
| 4 | kError | `_E` | 影响运行的错误 | 红色 |
| 5 | kFatal | `_F` | 不可恢复的致命错误 | 加粗红色 |

### 级别语义

- **kTrace**：函数进入/退出、循环迭代、帧时序等高频信息。生产环境应编译时剔除。
- **kDebug**：配置加载、连接建立、状态变化等开发调试信息。
- **kInfo**：正常运行里程碑：启动完成、连接成功、数据库就绪等。
- **kWarn**：可恢复的异常：重试、降级、超时等。
- **kError**：需要关注的错误：连接失败、数据校验失败等。
- **kFatal**：不可恢复的致命错误。**会抛出 `RuntimeError` 异常**。

### 自动 Detail 注解

当日志级别 >= `kDetailLevel`（默认 `kWarn`）时，宏自动在消息前添加 `{filename:line}` 信息：

```
[2026-03-28 10:00:00.123] [WARN]  {timer_basic.cc:42} This is a warning
[2026-03-28 10:00:00.124] [ERROR] {timer_basic.cc:43} This is an error
```

## 编译时级别过滤

通过定义 `VLINK_LOG_LEVEL=N` 宏，可以在编译时**完全剔除**低于 N 级别的日志代码：

```cmake
target_compile_definitions(myapp PRIVATE VLINK_LOG_LEVEL=2)  # 剔除 Trace 和 Debug
```

| VLINK_LOG_LEVEL | 剔除的级别 | 保留的级别 |
|-----------------|-----------|-----------|
| 0 | 无 | 全部 |
| 1 | Trace | Debug, Info, Warn, Error, Fatal |
| 2 | Trace, Debug | Info, Warn, Error, Fatal |
| 3 | Trace, Debug, Info | Warn, Error, Fatal |
| 4 | Trace, Debug, Info, Warn | Error, Fatal |
| 5 | Trace, Debug, Info, Warn, Error | Fatal |

被剔除的宏会被预处理器替换为空操作，**完全零开销**。这对性能敏感的嵌入式系统特别重要。

可以在运行时检查编译时最低级别：
```cpp
Logger::kMinimumLevel  // 编译时确定的最低级别
```

## 动态级别控制

```cpp
// 设置控制台和文件 sink 的最低输出级别
Logger::set_console_level(Logger::kWarn);   // 控制台只显示 Warn 及以上
Logger::set_file_level(Logger::kDebug);     // 文件记录 Debug 及以上

// 检查某个级别是否可写（用于避免构建昂贵的参数）
if (Logger::is_writable(Logger::kDebug)) {
    std::string expensive = build_debug_info();
    VLOG_D("Debug: ", expensive);
}
```

控制台和文件的输出级别可以独立配置，且可在运行时随时修改。

## 关键代码分析

### Logger 初始化

```cpp
Logger::init("app_name", "/path/to/logfile.log");
```

- `app_name`：嵌入日志输出中的标识，便于在多进程环境中区分来源
- `log_path`：日志文件路径（可选，为空则不启用文件输出）
- 必须在使用任何日志宏**之前**调用
- 可以多次调用以重新配置

### 日志刷新

```cpp
Logger::flush();
```

在异常终止前调用 `flush()` 确保缓冲的消息写入。`kFatal` 级别会自动触发 `flush()`。

## 常见错误

### 错误 1：在 Logger::init() 之前使用日志宏

```cpp
VLOG_I("This may crash or silently fail");
Logger::init("myapp");  // 太晚了！
```

### 错误 2：在 kFatal 之后期望继续执行

```cpp
VLOG_F("Fatal error: ", reason);
// 这一行永远不会执行！VLOG_F 抛出 RuntimeError
cleanup();
```

### 错误 3：混淆编译时过滤和运行时过滤

```cpp
// 编译时：-DVLINK_LOG_LEVEL=2
Logger::set_console_level(Logger::kTrace);  // 无效！Trace 已在编译时被剔除
VLOG_T("This is completely removed by preprocessor");  // 空操作
```

### 错误 4：在日志回调中递归使用日志

```cpp
Logger::register_console_handler([](Logger::Level, std::string_view msg) {
    VLOG_I("Received: ", msg);  // 可能递归！
});
```

## 相关示例

- [logger_advanced](../logger_advanced/) -- 自定义 handler、backtrace、fatal 异常捕获
-  -- VLink format 库的独立使用
-  -- VLOG_* 底层使用的 FastStream 零分配流
