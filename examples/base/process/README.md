# VLink Process 示例

## 概述

本示例演示了 VLink `Process` 类的跨平台子进程管理功能，包括进程创建、标准输入输出管道通信、状态回调、进程终止与强制杀死等完整的进程生命周期管理。`Process` 的 API 设计灵感来自 Qt 的 `QProcess`，但采用纯 C++ 实现。

## 文件说明

| 文件 | 说明 |
|------|------|
| `process_demo.cc` | Process 全功能演示源码 |
| `CMakeLists.txt` | 构建配置，链接 `vlink::all` |

## 构建与运行

```bash
cmake --build . --target example_process
./examples/base/process/example_process
```

## 核心概念

### 进程生命周期状态

```
kNotRunningState -> kStartingState -> kRunningState -> kNotRunningState
     (创建)          (start调用)        (exec成功)       (进程退出)
```

| 状态 | 枚举值 | 说明 |
|------|--------|------|
| `kNotRunningState` | 0 | 未启动或已退出 |
| `kStartingState` | 1 | `start()` 已调用，等待 exec 完成 |
| `kRunningState` | 2 | 子进程正在运行 |

### I/O 通道模式

| 模式 | stdout | stderr | 适用场景 |
|------|--------|--------|---------|
| `kSeparateMode` | 缓冲管道 | 缓冲管道 | 需要分别读取 stdout/stderr |
| `kMergedMode` | 缓冲管道 | 合并到 stdout | 不区分输出来源 |
| `kForwardedMode` | 转发到父进程 | 转发到父进程 | 子进程直接输出到终端 |
| `kForwardedOutputMode` | 转发到父进程 | 缓冲管道 | stdout 直接显示，stderr 需要捕获 |
| `kForwardedErrorMode` | 缓冲管道 | 转发到父进程 | stdout 需要捕获，stderr 直接显示 |

### 基本使用模式

```cpp
Process proc;
proc.set_process_mode(Process::kSeparateMode);
proc.start("/usr/bin/ls", {"-la", "/tmp"});
proc.wait_for_started(3000);
proc.wait_for_finished(5000);

std::string output;
proc.read_all_output(output);
int exit_code = proc.get_exit_code();
```

## 代码详解

### 1. 同步执行

```cpp
int code = Process::execute("/bin/echo", {"hello"}, 5000);
```

`execute` 是静态方法，阻塞等待进程完成并返回退出码。适合简单的命令执行场景。超时或启动失败返回 `-1`。

### 2. 异步启动与输出捕获

```cpp
Process proc;
proc.set_process_mode(Process::kSeparateMode);
proc.start("/bin/echo", {"hello"});
proc.wait_for_started(3000);
proc.wait_for_finished(5000);

std::string output;
proc.read_all_output(output);
```

异步模式下，`start()` 立即返回，子进程在后台运行。通过 `wait_for_started` 和 `wait_for_finished` 等待状态变化。

### 3. 管道写入（stdin）

```cpp
proc.start("/bin/cat", {});
proc.wait_for_started(3000);
proc.write("Hello via pipe!\n");
proc.close_write_channel();  // 发送 EOF
proc.wait_for_finished(5000);
```

`write()` 将数据写入子进程的 stdin。`close_write_channel()` 关闭写端，向子进程发送 EOF 信号。这对于需要交互的程序（如 `cat`、`python`）至关重要。

### 4. 逐行读取

```cpp
while (proc.can_read_line_stdout()) {
    std::string line;
    proc.read_line_stdout(line);
    // 处理每一行
}
```

`can_read_line_stdout()` 检查缓冲区中是否有完整的一行（以换行符结尾）。`read_line_stdout()` 读取一行并去除换行符。

### 5. 进程终止

```cpp
proc.terminate();  // 发送 SIGTERM（优雅终止）
bool finished = proc.wait_for_finished(3000);
if (!finished) {
    proc.kill();   // 发送 SIGKILL（强制杀死）
}
```

`terminate()` 发送 SIGTERM，子进程可以选择忽略。`kill()` 发送 SIGKILL，不可被忽略。`close(true)` 是二合一方法：先 terminate，超时后自动 kill。

### 6. 状态与完成回调

```cpp
proc.register_state_changed_callback([](Process::State state) {
    // 在监控线程中被调用
});

proc.register_finished_callback([](int code, Process::ExitStatus status) {
    // code: 退出码, status: kNormalExitStatus 或 kCrashExitStatus
});

proc.register_ready_read_stdout_callback([&proc]() {
    std::string line;
    while (proc.can_read_line_stdout()) {
        proc.read_line_stdout(line);
    }
});
```

所有回调在内部监控线程中执行，访问共享数据时需要注意线程安全。

### 7. 分离进程

```cpp
bool ok = Process::start_detached("/usr/bin/my_daemon", {"--config", "/etc/app.conf"});
```

`start_detached` 启动一个完全独立于父进程的子进程。没有返回句柄，子进程独立运行直到自行退出。

## 错误处理

| 错误码 | 说明 |
|--------|------|
| `kNoError` | 无错误 |
| `kStartError` | 启动失败（文件不存在等） |
| `kCrashedError` | 进程崩溃（被信号杀死） |
| `kTimedOutError` | 等待操作超时 |
| `kWriteError` | stdin 写入失败 |
| `kReadError` | stdout/stderr 读取失败 |
| `kBufferOverflowError` | 输出超过 `max_buffer_size` 限制 |

通过 `register_error_callback` 注册错误回调，或通过 `get_error()` 查询最后的错误码。

## 高级配置

### 环境变量

```cpp
Process::EnvironmentMap env;
env["MY_VAR"] = "value";
proc.set_environment(env);
proc.set_inherit_environment(true); // 是否继承父进程环境
```

### 工作目录

```cpp
proc.set_working_directory("/tmp");
```

### 缓冲区限制

```cpp
proc.set_max_buffer_size(32 * 1024 * 1024); // 32MB 上限
```

默认缓冲区上限为 16MB，超出后触发 `kBufferOverflowError`。

## 退出状态

| 状态 | 说明 |
|------|------|
| `kNormalExitStatus` | 正常退出（通过 `exit()` 或 `main` 返回） |
| `kCrashExitStatus` | 被信号杀死或崩溃 |

## 常量说明

| 常量 | 默认值 | 说明 |
|------|--------|------|
| `kInfinite` | -1 | 无限等待 |
| `kDefaultWaitTimeoutMs` | 3000 | `wait_for_started/finished` 默认超时 |
| `kDefaultWriteTimeoutMs` | 5000 | `write()` 默认超时 |
| `kDefaultExecuteTimeoutMs` | 30000 | `execute()` 默认超时 |
| `kDestructorWaitTimeoutMs` | 5000 | 析构函数等待子进程退出的超时 |

## 注意事项

- `Process` 对象不可拷贝、不可移动
- 析构函数会等待最多 5 秒让子进程退出
- 所有回调在内部监控线程执行，避免在回调中进行阻塞操作
- `start()` 是异步的，必须调用 `wait_for_started()` 确认子进程已启动
- 在 `kForwardedMode` 下无法通过管道读取输出
- `execute()` 和 `start_detached()` 是静态方法，不需要实例
