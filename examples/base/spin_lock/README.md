# VLink SpinLock 示例

## 概述

本示例演示了 VLink `SpinLock` 的用法，包括手动加解锁、RAII 守卫、try_lock、多线程并发保护以及与 `std::mutex` 的性能对比。SpinLock 是为极短临界区设计的用户空间自旋锁。

## 文件说明

| 文件 | 说明 |
|------|------|
| `spin_lock.cc` | SpinLock 功能演示源码 |
| `CMakeLists.txt` | 构建配置，链接 `vlink::all` |

## 构建与运行

```bash
cmake --build . --target example_spin_lock
./examples/base/spin_lock/example_spin_lock
```

## 核心概念

### SpinLock vs std::mutex

| 特性 | SpinLock | std::mutex |
|------|----------|-----------|
| 等待方式 | 忙等自旋 | 内核上下文切换 |
| 适用场景 | 极短临界区（几条指令） | 长临界区或 I/O 操作 |
| CPU 开销 | 自旋期间消耗 CPU | 等待期间释放 CPU |
| 缓存行对齐 | `alignas(64)` 防止 false sharing | 不保证 |

### 自适应退避策略

`SpinLock::lock()` 内部采用指数退避策略减少总线竞争：

1. 尝试 `exchange(true, acquire)`，成功则返回
2. 以 `load(relaxed)` 自旋等待，直到看到锁被释放
3. 每 `backoff` 次自旋后调用 `Utils::yield_cpu()`（CPU PAUSE 指令）
4. `backoff` 每轮翻倍，上限 1024
5. 超过 50000 次自旋后，sleep 10 微秒并记录错误日志

### RAII 守卫

```cpp
SpinLock lock;
{
    SpinLockGuard guard(lock);  // 构造时加锁
    // 临界区
}  // 析构时自动解锁
```

`SpinLockGuard` 类似于 `std::lock_guard`，确保异常安全。由于 SpinLock 满足 `Lockable` 要求，也可以直接 `std::lock_guard guard(lock)`（C++17 CTAD 自动推导出 `std::lock_guard<vlink::SpinLock>`）。

## 代码执行流程

1. **手动加解锁**：基本的 lock/unlock 使用
2. **RAII 守卫**：SpinLockGuard 自动管理锁生命周期
3. **try_lock**：非阻塞尝试获取锁
4. **多线程计数器**：4 个线程各递增 100000 次，验证线程安全
5. **std::lock_guard 兼容**：演示与标准库的互操作性
6. **性能测量**：SpinLock 与 std::mutex 的单线程无竞争开销对比

## 退避策略详解

SpinLock 的 `lock()` 方法采用三级退避机制：

### 第一级：快速尝试

```
尝试 exchange(true, acquire)
  -> 成功：获取锁，返回
  -> 失败：进入自旋
```

### 第二级：自旋 + yield

```
while (flag.load(relaxed)):
    ++total_spin
    if ++spin_count >= backoff:
        yield_cpu()        // CPU PAUSE 指令
        backoff *= 2       // 指数增长，上限 1024
        spin_count = 0
```

`yield_cpu()` 在不同架构上发出不同的 CPU 暂停指令：
- x86/x86-64：`PAUSE`
- ARMv7/AArch64：`YIELD`
- RISC-V：`fence` hint
- 回退：`std::this_thread::yield()`

### 第三级：安全阀

```
if total_spin > 50000:
    VLOG_E("exceeded max spin count")
    sleep_for(10us)
```

超过最大自旋次数是病态竞争的信号，通常意味着应该改用 `std::mutex`。

## 适用场景与限制

### 适合使用 SpinLock 的场景

- 临界区只有几条指令（如递增计数器、修改指针）
- 多核系统上竞争不激烈
- 对延迟极度敏感的热路径

### 不适合使用 SpinLock 的场景

- 临界区包含 I/O 操作
- 临界区包含内存分配
- 临界区执行时间不确定
- 单核系统（自旋会阻止其他线程释放锁）

## 注意事项

- SpinLock 不是递归锁，同一线程连续 lock 两次会死锁
- 不要在长时间操作或 I/O 操作中使用 SpinLock
- SpinLock 是 header-only 实现，完全内联
- 64 字节对齐防止多个 SpinLock 对象之间的 false sharing
- 超过最大自旋次数后会 sleep 并记录错误日志，这是病态竞争的安全阀
- SpinLock 满足 `Lockable` 要求，可与 `std::lock_guard` 配合使用
