# VLink ObjectPool 示例

## 概述

本示例演示了 VLink `ObjectPool<T>` 的对象池功能，包括 RAII 自动回收、共享指针模式、手动借还、四种重置策略、统计信息查询以及池耗尽处理。对象池通过重用已分配的对象来减少热路径上的堆分配压力。

## 文件说明

| 文件 | 说明 |
|------|------|
| `object_pool.cc` | ObjectPool 功能演示源码 |
| `CMakeLists.txt` | 构建配置，链接 `vlink::all` |

## 构建与运行

```bash
cmake --build . --target example_object_pool
./examples/base/object_pool/example_object_pool
```

## 核心概念

### 获取 API

| 方法 | 返回类型 | 自动归还 | 用途 |
|------|---------|---------|------|
| `get()` | `unique_ptr<T, PoolDeleter>` | 是 | 最常用，RAII 自动归还 |
| `get_shared()` | `shared_ptr<T>` | 是 | 可复制，最后一个引用释放时归还 |
| `borrow()` | `T*` | 否 | 手动管理，必须调用 `give_back()` |

### 重置策略

```cpp
enum Policy {
    kPolicyNone = 0,     // 不调用重置回调
    kPolicyRelease = 1,  // 归还时重置（默认）
    kPolicyAcquire = 2,  // 获取时重置
    kPolicyBoth = 3,     // 获取和归还时都重置
};
```

| 策略 | 获取时重置 | 归还时重置 | 适用场景 |
|------|----------|----------|---------|
| `kPolicyNone` | 否 | 否 | 不可变或无状态对象 |
| `kPolicyRelease` | 否 | 是 | 归还前清理（默认） |
| `kPolicyAcquire` | 是 | 否 | 使用前初始化 |
| `kPolicyBoth` | 是 | 是 | 双重保证 |

### 构造

```cpp
auto pool = std::make_shared<ObjectPool<T>>(
    factory_callback,   // 创建新对象的工厂函数
    initial_size,       // 预分配对象数量
    max_size,           // 最大对象总数（0=无限）
    reset_callback,     // 重置回调（可选）
    policy              // 重置策略
);
```

**重要**：ObjectPool 必须通过 `std::make_shared` 创建，因为 `PoolDeleter` 内部持有 `weak_ptr` 指向池对象。

### PoolDeleter 机制

```cpp
auto buf = pool->get();
// buf 的类型是 unique_ptr<T, PoolDeleter>
// PoolDeleter 持有 weak_ptr<ObjectPool<T>>
// 析构时：
//   - 如果 pool 仍存活 -> 对象归还到池中
//   - 如果 pool 已销毁 -> 对象被 delete
```

## 代码执行流程

1. **RAII get()**：获取对象，使用后自动归还
2. **get_shared()**：获取共享指针，最后引用释放时归还
3. **borrow/give_back**：手动借还模式
4. **重置策略**：演示 kPolicyAcquire、kPolicyBoth、kPolicyNone
5. **统计信息**：查询 pool_size、borrowed、total_created、max_size
6. **池耗尽**：超过 max_size 时抛出 runtime_error

## 线程安全

所有公共方法通过内部互斥锁保护，可以从多个线程安全地并发调用 `get()`、`give_back()` 等方法。

## 统计信息

```cpp
auto stats = pool->stats();
stats.pool_size;      // 当前空闲对象数
stats.borrowed;       // 当前被持有的对象数
stats.total_created;  // 总共创建过的对象数
stats.max_size;       // 最大允许对象数
```

也可以单独查询：
```cpp
pool->size();           // 空闲对象数
pool->borrowed();       // 被持有对象数
pool->total_created();  // 总创建数
pool->max_size();       // 最大限制
```

## 对象生命周期

```
工厂创建 -> 首次 get() -> 使用中 -> unique_ptr 析构
                                         |
                                    PoolDeleter::operator()
                                         |
                              pool 存活？ -> 是 -> release() -> 入池等待重用
                                         |
                                         -> 否 -> delete
```

如果 ObjectPool 在 unique_ptr 之前被销毁，PoolDeleter 检测到 `weak_ptr` 失效，直接 `delete` 对象而非归还。

## 注意事项

- `max_size` 为 0 时池无限增长
- `initial_size` 不能超过 `max_size`（如果 max_size > 0）
- 工厂回调返回 nullptr 会抛出 `runtime_error`
- 重置回调抛出异常时，归还操作中对象会被丢弃而非归还到池
- `borrow()` 获取的对象**必须**通过 `give_back()` 归还，不可用于 `get()` 的返回值
- ObjectPool 必须通过 `std::make_shared` 创建（因为内部使用 `enable_shared_from_this`）
- 所有公共方法都是线程安全的
