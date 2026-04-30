# VLink Logger 高级示例

## 概述

本示例演示了 VLink 日志系统的高级功能，包括自定义控制台处理器、回溯缓冲区（backtrace）、Fatal 级别异常抛出机制、编译期过滤以及流格式化控制。这些功能在生产环境中的日志管理、问题排查和性能优化中非常重要。

## 文件说明

| 文件 | 说明 |
|------|------|
| `logger_advanced.cc` | 日志高级功能演示源码 |
| `CMakeLists.txt` | 构建配置，链接 `vlink::all` |

## 构建与运行

```bash
cmake --build . --target example_logger_advanced
./examples/base/logger_advanced/example_logger_advanced
```

## 核心功能详解

### 1. 自定义控制台处理器 (register_console_handler)

```cpp
Logger::register_console_handler([](Logger::Level level, std::string_view message) {
    // 自定义处理逻辑
});
```

通过 `register_console_handler()` 注册回调函数，可以拦截所有日志记录，实现自定义处理逻辑。典型应用场景：

- **远程遥测**：将日志转发到远程日志收集系统
- **日志聚合**：按自定义格式写入数据库或消息队列
- **监控告警**：对特定级别的日志触发告警通知
- **测试验证**：在单元测试中捕获并验证日志输出

回调函数签名为 `void(Level, std::string_view)`，其中 `string_view` 仅在回调执行期间有效。回调从日志线程同步调用，因此不应执行耗时操作。

同样可以通过 `register_file_handler()` 注册文件日志的自定义处理器。

### 2. 回溯缓冲区 (Backtrace)

```cpp
Logger::enable_backtrace(10);   // 启用，保留最近 10 条消息
// ... 产生日志 ...
Logger::dump_backtrace();       // 将缓冲区内容刷新到 sink
Logger::disable_backtrace();    // 禁用并释放缓冲区
```

回溯功能使用环形缓冲区保留最近 N 条日志消息，不受当前日志级别限制。这意味着即使控制台级别设置为 `kWarn`，Trace 和 Debug 级别的消息仍会被保留在缓冲区中。

当调用 `dump_backtrace()` 时，所有保留的消息会以其原始级别被刷新到活动的 sink（控制台/文件）。这在以下场景特别有用：

- **崩溃前诊断**：在捕获到异常或信号时，先 dump backtrace 再退出
- **条件性调试**：只在特定错误发生时才查看之前的详细日志
- **减少日志量**：正常运行时只显示警告以上级别，出问题时回溯查看详情

### 3. Fatal 级别与异常抛出

```cpp
try {
    VLOG_F("Fatal error: critical failure, code=", 0xDEAD);
} catch (const std::runtime_error& e) {
    // 处理异常
}
```

`kFatal` 级别的日志具有特殊行为：

1. 日志消息被写入所有活动 sink
2. 调用 `Logger::flush()` 确保所有缓冲消息被写出
3. 抛出 `Exception::RuntimeError`（继承自 `std::runtime_error`）

这意味着 `VLOG_F` / `MLOG_F` / `CLOG_F` / `SLOG_F` 之后的代码永远不会执行（除非在 try-catch 块中捕获了异常）。这种设计确保了致命错误不会被忽略。

### 4. 编译期过滤

VLink 日志支持在编译时完全剔除特定级别的日志代码：

```cpp
// 在编译时定义（CMake 或编译器选项）：
// -DVLINK_LOG_LEVEL=2    // 剔除 kTrace(0) 和 kDebug(1)
// -DVLINK_LOG_DETAIL_LEVEL=3  // 从 kWarn 开始附加文件名和行号
```

| 编译宏 | 作用 | 默认值 |
|--------|------|--------|
| `VLINK_LOG_LEVEL` | 低于此级别的日志宏变为空操作 | `kTrace`(0) |
| `VLINK_LOG_DETAIL_LEVEL` | 高于等于此级别时附加 `{file:line}` | `kWarn`(3) |
| `VLINK_LOG_DISABLE_SHORT` | 禁用短名称宏（VLOG_*等） | 未定义 |

编译期过滤的关键优势是**零运行时开销**：被剔除的日志宏在编译后不会产生任何代码，包括参数求值。这对性能敏感的代码路径至关重要。

### 5. is_writable 优化

```cpp
if (Logger::is_writable(Logger::kDebug)) {
    std::string expensive = build_debug_string();
    VLOG_D("debug: ", expensive);
}
```

在构造日志参数成本较高时，先调用 `is_writable()` 检查该级别是否会被输出。如果两个 sink（控制台和文件）的级别都高于查询级别，`is_writable()` 返回 `false`，从而避免不必要的字符串构建开销。

### 6. 控制台格式控制

```cpp
Logger::set_console_fmt_enable(false);  // 禁用 ANSI 颜色码
Logger::set_console_fmt_enable(true);   // 启用 ANSI 颜色码
```

在管道重定向或不支持 ANSI 转义序列的终端中，可以禁用颜色格式化输出。

### 7. 流格式化标志

```cpp
Logger::set_stream_precision(4);    // 设置浮点数精度
Logger::set_stream_flag(std::ios::hex);  // 设置十六进制输出
Logger::set_stream_width(8);        // 设置字段宽度
```

这些设置影响 Stream 风格（VLOG_*）中数值类型的输出格式。设置会应用到线程局部的 `FastStream` 对象上。

## 代码执行流程

1. **初始化与注册**：初始化 Logger，注册自定义控制台处理器捕获所有日志
2. **Backtrace 演示**：启用 10 条消息的回溯缓冲，产生多条日志后 dump
3. **Fatal 演示**：在 try-catch 中触发 VLOG_F 和 MLOG_F，捕获 RuntimeError
4. **编译期信息**：输出当前编译配置的最低级别和详情阈值
5. **优化技巧**：演示 is_writable 检查和格式控制
6. **汇总输出**：统计自定义处理器捕获的消息总数

## 后端支持

VLink Logger 根据编译时配置可以调度到不同的日志后端：

| 后端 | 适用平台 | 说明 |
|------|----------|------|
| spdlog | 通用 | 默认后端，高性能 |
| quill | 通用 | 低延迟异步后端 |
| DLT | 汽车电子 | AUTOSAR DLT 标准 |
| Android logcat | Android | 原生 Android 日志 |
| QNX slog2 | QNX | QNX 系统日志 |
| kmsg | Linux 内核 | 内核消息缓冲区 |

## 注意事项

- 自定义处理器从日志线程同步调用，避免在回调中执行阻塞操作
- Backtrace 缓冲区占用内存，使用完毕后应调用 `disable_backtrace()` 释放
- Fatal 日志总是被输出（`should_log<kFatal>()` 始终返回 true）
- `WrapperStream` 是模板类，未使用的日志级别在编译时被完全优化掉
