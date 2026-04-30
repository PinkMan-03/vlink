# VLink Utils 示例

## 概述

本示例全面演示了 VLink `Utils` 命名空间提供的平台无关系统工具函数。`Utils` 封装了底层操作系统调用，提供统一的跨平台 API，支持 Linux、macOS、Windows、QNX 和 Android。

## 文件说明

| 文件 | 说明 |
|------|------|
| `utils_demo.cc` | Utils 全功能演示源码 |
| `CMakeLists.txt` | 构建配置，链接 `vlink::all` |

## 构建与运行

```bash
cmake --build . --target example_utils
./examples/base/utils/example_utils
```

## 核心功能分类

### 1. 进程与路径信息

| 函数 | 返回类型 | 说明 |
|------|---------|------|
| `get_pid()` | `int32_t` | 当前进程 PID |
| `get_pid_str()` | `string` | PID 的字符串形式 |
| `get_app_path()` | `string` | 可执行文件的完整路径 |
| `get_app_dir()` | `string` | 可执行文件所在目录 |
| `get_app_name()` | `string` | 可执行文件名（不含目录） |
| `get_host_name()` | `string` | 主机名 |
| `get_tmp_dir()` | `string` | 系统临时目录（Linux: `/tmp`） |
| `get_machine_id()` | `string` | 机器唯一标识（Linux: `/etc/machine-id`） |

所有函数均为 `noexcept`，失败时返回空字符串或 `-1`。

### 2. 环境变量操作

```cpp
// 读取环境变量，不存在时返回默认值
std::string home = Utils::get_env("HOME", "/unknown");

// 设置环境变量（force=true 覆盖已有值）
Utils::set_env("MY_VAR", "value", true);

// 删除环境变量
Utils::unset_env("MY_VAR");
```

`set_env` 的第三个参数 `force` 控制是否覆盖已有变量。默认为 `true`，即强制覆盖。

### 3. 网络地址查询

| 函数 | 说明 |
|------|------|
| `get_all_ipv4_address(filter)` | 获取所有 IPv4 地址，`filter=true` 仅返回 UP 状态的接口 |
| `get_all_ipv6_address(filter)` | 获取所有 IPv6 地址 |
| `get_interface_name_by_ipv4(ip)` | 根据 IPv4 地址查询网卡名称 |
| `get_interface_name_by_ipv6(ip)` | 根据 IPv6 地址查询网卡名称 |
| `get_dds_default_address(filter, max)` | 获取适合 DDS 单播的地址（排除回环和链路本地地址） |

`get_dds_default_address` 专门为 DDS 参与者筛选可路由的单播地址，最多返回 `max_count` 个。

### 4. 单例检查

```cpp
bool is_unique = Utils::check_singleton("my_program");
```

使用锁文件或命名信号量实现进程间互斥。如果同名程序已经在运行，返回 `false`。常用于守护进程启动时防止重复实例。

### 5. 线程管理

| 函数 | 说明 |
|------|------|
| `set_thread_name(name, thread)` | 设置线程名称（Linux 最长 15 字符，用于 gdb/perf） |
| `set_thread_priority(level, policy, thread)` | 设置调度优先级和策略（需要 CAP_SYS_NICE） |
| `set_thread_stick(core_mask, thread)` | CPU 亲和性绑定（位掩码指定核心） |
| `get_native_thread_id()` | 获取操作系统原生线程 ID |
| `yield_cpu()` | CPU 暂停指令（x86: PAUSE, ARM: YIELD） |

`yield_cpu()` 是 `inline` 函数，在自旋等待循环中使用可以降低总线竞争，提高多核性能。它根据架构自动选择最优指令：
- x86/x86-64: `PAUSE`
- ARMv7/AArch64: `YIELD`
- RISC-V: `.word 0x0100000f`
- 其他: `std::this_thread::yield()`

### 6. 资源监控

```cpp
double cpu = Utils::get_cpu_usage();    // CPU 使用率 [0, 100*核心数]
double mem = Utils::get_memory_usage(); // RSS 内存占比 [0, 100]
bool running = Utils::is_process_running("my_app"); // 进程是否在运行
```

`get_cpu_usage()` 返回自上次调用以来的 CPU 使用率快照，首次调用可能返回 0。

### 7. 时间与时区

```cpp
int32_t tz = Utils::get_timezone_diff(); // 时区偏移（秒），如 UTC+8 返回 28800
```

### 8. 信号处理

```cpp
// 注册终止信号处理（SIGTERM, SIGINT）
Utils::register_terminate_signal([](int sig) {
    // 优雅关闭逻辑
}, false, false);

// 注册崩溃信号处理（SIGSEGV, SIGABRT, SIGFPE, SIGBUS）
Utils::register_crash_signal([](int sig) {
    // 紧急日志转储
});
```

`register_terminate_signal` 的参数：
- `is_async`：`true` 时回调在专用线程中异步执行
- `pass_through`：`true` 时回调后重新触发信号的默认行为（如 core dump）

### 9. 其他工具

| 函数 | 说明 |
|------|------|
| `set_console_utf8_output()` | Windows 控制台 UTF-8 设置（Linux 无操作） |
| `try_release_sys_memory()` | 提示系统回收未使用的堆内存（Linux: `malloc_trim`） |
| `get_terminal_size()` | 获取终端窗口尺寸（列数, 行数） |
| `wait_for_device(path, timeout, poll)` | 等待设备文件出现（如 `/dev/video0`） |

## 代码执行流程

1. **进程信息**：查询 PID、路径、主机名、机器 ID
2. **环境变量**：读取、设置、删除环境变量
3. **网络信息**：枚举 IPv4 地址、查询网卡名称、DDS 地址筛选
4. **单例检查**：验证当前是否为唯一实例
5. **线程管理**：设置线程名、优先级、yield 循环
6. **资源监控**：CPU/内存使用率、进程检测
7. **时区查询**：获取时区偏移和终端尺寸
8. **信号注册**：注册终止和崩溃信号回调

## 平台差异

| 特性 | Linux | Windows | macOS | QNX |
|------|-------|---------|-------|-----|
| `get_app_path` | `/proc/self/exe` | `GetModuleFileName` | `_NSGetExecutablePath` | `/proc/self/exefile` |
| `get_machine_id` | `/etc/machine-id` | 注册表 | `IOPlatformSerialNumber` | 主机名哈希 |
| `yield_cpu` | `PAUSE`/`YIELD` | `_mm_pause` | `YIELD` | `YIELD` |
| `try_release_sys_memory` | `malloc_trim(0)` | 无操作 | 无操作 | 无操作 |

## 线程安全

所有 `Utils` 函数都是 `noexcept` 的。大多数为只读查询，天然线程安全。`set_env`/`unset_env` 修改全局环境，在多线程环境下应注意同步。

## 注意事项

- `set_thread_name` 在 Linux 上线程名最长 15 字符（`pthread_setname_np` 限制）
- `set_thread_priority` 设置实时策略需要 `CAP_SYS_NICE` 权限或适当的 `RLIMIT_RTPRIO`
- `get_cpu_usage` 返回的是快照值，首次调用可能返回 0
- `check_singleton` 使用文件锁实现，进程异常退出后锁文件可能残留
- `register_terminate_signal` 回调应尽量简短，避免在信号处理上下文中执行复杂操作
