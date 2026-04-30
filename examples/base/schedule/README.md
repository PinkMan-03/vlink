# VLink Schedule 示例

## 概述

本示例演示了 VLink `Schedule` 任务调度包装器的用法，包括 `Schedule::Config` 配置、`Status` 和 `RetStatus` RAII 句柄、`on_then`/`on_else` 结果链、异常捕获以及优先级调度。Schedule 提供了一种流式 API 来配置任务的延迟、优先级、超时和结果回调。

## 文件说明

| 文件 | 说明 |
|------|------|
| `schedule.cc` | Schedule 功能演示源码 |
| `CMakeLists.txt` | 构建配置，链接 `vlink::all` |

## 构建与运行

```bash
cmake --build . --target example_schedule
./examples/base/schedule/example_schedule
```

## 核心概念

### Schedule::Config

```cpp
Schedule::Config config(delay_ms, priority, schedule_timeout_ms, execution_timeout_ms);
```

| 字段 | 类型 | 含义 | 默认值 |
|------|------|------|--------|
| `delay_ms` | `uint32_t` | 投递前延迟（通过一次性 Timer 实现） | 0 |
| `priority` | `uint16_t` | 任务分发优先级 | 0 |
| `schedule_timeout_ms` | `uint32_t` | 任务开始前的最长等待 | 0（禁用） |
| `execution_timeout_ms` | `uint32_t` | 任务执行最长时间 | 0（禁用） |

### void 回调 -> Status

```cpp
loop.exec_task(config, []() { work(); })
    .on_schedule_timeout([]() { /* 任务未按时开始 */ })
    .on_execution_timeout([]() { /* 任务执行超时 */ })
    .on_catch([](std::exception& e) { /* 异常处理 */ });
```

`Status` 是移动语义的 RAII 对象，支持链式注册三种回调：
- `on_schedule_timeout`：任务在 `schedule_timeout_ms` 内未开始执行时触发
- `on_execution_timeout`：任务执行时间超过 `execution_timeout_ms` 时触发
- `on_catch`：任务回调抛出异常时触发，接收 `std::exception&` 参数

### bool 回调 -> RetStatus

```cpp
loop.exec_task(config, []() -> bool { return try_work(); })
    .on_then([]() -> bool { return next_step(); })
    .on_then([]() -> bool { return final_step(); })
    .on_else([]() { handle_failure(); });
```

`RetStatus` 继承自 `Status`，额外支持：
- `on_then`：当回调返回 `true` 时触发，可链式串联多个
- `on_else`：当回调返回 `false` 时触发

#### on_then 链式执行规则

1. 主回调返回 `true` -> 执行第一个 `on_then`
2. `on_then(1)` 返回 `true` -> 执行 `on_then(2)`
3. 任何 `on_then` 返回 `false` -> 停止链式执行，触发 `on_else`
4. 主回调返回 `false` -> 跳过所有 `on_then`，直接触发 `on_else`

### 异常处理

```cpp
loop.exec_task(config, []() { throw std::runtime_error("error"); })
    .on_catch([](std::exception& e) { log_error(e.what()); });
```

异常在任务包装器内部被捕获并传递给 `on_catch` 回调。任务在异常后被视为失败。

## 代码执行流程

1. **Config 构造**：演示默认、完整和仅延迟三种配置方式
2. **void Status 链**：立即和延迟执行，注册超时和异常回调
3. **bool RetStatus 链**：true 路径的 on_then 链和 false 路径的 on_else
4. **异常处理**：任务抛出异常后 on_catch 捕获
5. **优先级调度**：在 Priority 循环上使用不同优先级
6. **有效性检查**：查询 Status 的 is_valid 状态

## 内部机制

`Schedule::process()` 和 `Schedule::process_with_ret()` 是内部函数，由 `MessageLoop::exec_task()` 调用。它们：

1. 将用户回调包装在一个带超时检测的闭包中
2. 创建 `shared_ptr<StatusImpl>` 存储所有注册的回调
3. 返回 `Status`/`RetStatus` 对象供用户链式注册回调
4. 任务包装器和 Status 共享 `StatusImpl`，确保即使 Status 被销毁，回调仍有效

## 注意事项

- 所有回调都在 MessageLoop 线程上执行
- Status 是移动语义，不可拷贝
- 内部通过 `shared_ptr` 引用计数管理，Status 销毁后注册的回调仍有效
- `delay_ms > 0` 时通过内部一次性 Timer 实现延迟
- `is_valid()` 为 `false` 表示任务未能成功投递（如队列已满）
