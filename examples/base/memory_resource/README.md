# VLink MemoryResource 示例

## 概述

`vlink::MemoryResource` 是 `std::pmr::memory_resource` 的适配器，让任何 pmr-aware 容器（`std::pmr::vector`、`std::pmr::string`、`std::pmr::polymorphic_allocator<T>` 等）通过 `vlink::MemoryPool` 完成底层字节分配。本示例演示四种典型用法：进程级共享、私有 level 池、私有自定义 tier 池、bypass 模式。

## 文件说明

| 文件 | 说明 |
|------|------|
| `memory_resource.cc` | MemoryResource + pmr 容器集成演示源码 |
| `CMakeLists.txt` | 构建配置，链接 `vlink::all` |

## 构建与运行

```bash
cmake --build . --target example_memory_resource
./examples/base/memory_resource/example_memory_resource
```

## 核心 API

```cpp
namespace vlink {
class MemoryResource : public std::pmr::memory_resource {
 public:
  // 进程级共享 resource，包装 MemoryPool::global_instance()，不持有底层池。
  // 与 Bytes 共享同一个全局池。use_env_level 默认 true，仅首次调用生效。
  static MemoryResource& global_instance(bool use_env_level = true);

  // 私有池，按 Config 构造（tiers + prealloc）。
  //   空 tiers = bypass：每次 allocate 直接 ::operator new。
  //   非空但格式非法 = 回退 level-3 默认金字塔（同 MemoryPool 构造规则）。
  //   prealloc=true 时构造期为每个 tier 一次性预分配满额 blocks_per_chunk。
  explicit MemoryResource(const MemoryPool::Config& config);

  // 无参构造 = bypass。
  MemoryResource();

  // 私有池，按 level 构造。level=0 = bypass，1..9 = 内置金字塔某一行。
  explicit MemoryResource(int level);

  ~MemoryResource() override;   // 私有池在此销毁；global_instance 不销毁

  [[nodiscard]] MemoryPool& get_memory_pool() noexcept;
};
}  // namespace vlink
```

### 关键约束

- `do_allocate` 在底层 `MemoryPool::allocate` 返回 `nullptr` 时抛 `std::bad_alloc`（pmr 契约）。其余路径不抛。底层 `MemoryPool` 本身永不抛。
- `do_is_equal` 仅当两个 resource 指向**同一个底层 `MemoryPool` 对象**时返回 `true`。两个用相同 tier 数组分别构造的私有 resource 拥有各自的私有池，因此不相等。
- `global_instance()` 返回的 resource 是全局池的别名（不拥有），析构时不释放底层池；其他构造路径产生的 resource 在析构时同时销毁自己拥有的池。
- `MemoryResource` 不可拷贝、不可移动，与 `std::pmr::memory_resource` 契约一致。
- 该类不带 `final`，可被继承。

## 演示流程

1. **global_instance()**：用 `std::pmr::vector<int>` 绑定全局 resource，验证它与 `MemoryPool::global_instance()` 是同一底层池。
2. **MemoryResource(level=3)**：私有 level-3 金字塔池 + `std::pmr::polymorphic_allocator<char>` + `std::pmr::string`。
3. **MemoryResource(custom Config)**：自定义 2 阶 tier 池 + `prealloc=true` 满额预分配 + `std::pmr::vector<double>`。
4. **Bypass 模式**：`MemoryResource()`（无参）下，所有分配直通 `::operator new`，tier 计数为 0、仅 oversized 计数增长。
5. **`do_is_equal`**：演示同一 resource 的等价、不同私有 resource 的不等价、全局 resource 与私有 resource 的不等价。

## 与 MemoryPool 的关系

```
std::pmr 容器 / polymorphic_allocator
   |
   v
MemoryResource::do_allocate(bytes, alignment)
   |
   v
MemoryPool::allocate(bytes, alignment)   // 全局池或私有池
```

- 选 `global_instance()`：与 `Bytes`、所有内部容器共享同一底层池，无需额外内存。
- 选私有 resource：当某个子系统希望统计/限额/隔离自己的内存使用时，构造一个私有 resource 并传给所有 pmr 容器即可。

## 注意事项

- `std::pmr::vector::reserve()` 等接口可能多次调用 resource，按容量增长策略可能产生多次 allocate/deallocate。
- 底层池为 bypass（空表 / level 0）时不影响 pmr 行为：每次 alloc 走 `::operator new`，统计上仅 `OversizedStats` 增长。
- pmr 契约要求 `do_allocate` 在失败时抛 `std::bad_alloc`，因此**不要**在 `noexcept` 上下文中假设 `MemoryResource` 永不抛。
