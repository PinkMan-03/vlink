# VLink MemoryPool 示例

## 概述

本示例演示 `vlink::MemoryPool` 的用法：分级（金字塔）free-list 内存池，按 size class 分发请求；超过最大 tier 或对齐过严的请求走 oversized 直通路径，直接 `::operator new` / `::operator delete`。`MemoryPool` 是 `Bytes` 默认堆分配器。

## 文件说明

| 文件 | 说明 |
|------|------|
| `memory_pool.cc` | MemoryPool 功能演示源码 |
| `CMakeLists.txt` | 构建配置，链接 `vlink::all` |

## 构建与运行

```bash
cmake --build . --target example_memory_pool
./examples/base/memory_pool/example_memory_pool
```

## VLINK_MEMORY_LEVEL 环境变量

`MemoryPool::get_default_config()` 返回的 8 阶金字塔由 `VLINK_MEMORY_LEVEL`（整数 `0..9`，默认 `3`）从一张编译期常量表中按行索引。等级越高，每档 `blocks_per_chunk` 越大，常驻内存越多，但 upstream 分配次数越少。`0` 是 bypass 哨兵（不走池）。非数字或越界的取值会被钳到 `[0, 9]` 区间并打印 warning。

| 等级 | 风格      | 适用场景                                                                                        |
| ---- | --------- | ----------------------------------------------------------------------------------------------- |
| `0`  | Bypass    | 关闭池：每次 `allocate` 直接 `::operator new`、每次 `deallocate` 直接 `::operator delete`；oversized 计数照常增长，所有 tier 计数为 0 |
| `1`  | Tiny      | 端侧/嵌入式，最小驻留 chunk                                                                     |
| `2`  | Small     | 受限设备                                                                                        |
| `3`  | Balanced  | **默认**                                                                                        |
| `4`  | Large     | 服务器/高吞吐                                                                                   |
| `5`  | XLarge    | 大批量小消息                                                                                    |
| `6`  | Massive   | 重 IO/视频流；每档 block_per_chunk 翻倍                                                        |
| `7`  | Heavy     | 长时间高负载；小 tier 进一步倍增                                                                |
| `8`  | Saturate  | 接近饱和的多生产者场景                                                                          |
| `9`  | Peak      | 高吞吐峰值；满载占用上限不超过 500 MiB                                                          |

```bash
# 切到 Large 等级（每档 block 数比默认更多）
export VLINK_MEMORY_LEVEL=4
./examples/base/memory_pool/example_memory_pool

# Bypass 模式：所有分配跳过池
export VLINK_MEMORY_LEVEL=0
./examples/base/memory_pool/example_memory_pool
```

> 该变量仅在 **首次** 构造全局池时被读取（`MemoryPool::global_instance(true)` 或 `Bytes::init_memory_pool()`）；后续 `global_instance()` 调用始终返回同一个对象，不会再次读环境变量。

## VLINK_MEMORY_PREALLOC 环境变量

设为 `1` 时，`get_default_config()` 返回的 `Config` 会带上 `prealloc=true`，
构造函数据此为每个 tier 一次性预分配满额 `blocks_per_chunk` 的 chunk
（best-effort：单 tier 上游分配失败时该 tier 留作懒加载，构造继续）。
其它值或未设置保持懒加载（按几何增长）。

```bash
# 启用满额预分配，消除热路径上游分配延迟
export VLINK_MEMORY_PREALLOC=1
./examples/base/memory_pool/example_memory_pool
```

> 该变量同样仅在 **首次** 构造全局池时被读取。

## 核心 API

```cpp
namespace vlink {
class MemoryPool final {
 public:
  struct Tier { size_t max_size; size_t blocks_per_chunk; };
  struct Config { std::vector<Tier> tiers; bool prealloc{false}; };
  struct TierStats { /* hit_count, deallocate_count, in_use_blocks, ... */ };
  struct OversizedStats { uint64_t alloc_count, alloc_bytes, dealloc_count; };

  // 仅首次调用决定配置：
  //   use_env_level=true（默认）：按 VLINK_MEMORY_LEVEL 选 tier；env=0 时 bypass；
  //                              VLINK_MEMORY_PREALLOC=1 时同时启用满额预分配。
  //   use_env_level=false：使用 level-3 默认金字塔，不读环境变量、不预分配。
  static MemoryPool& global_instance(bool use_env_level = true);
  [[nodiscard]] static Config get_default_config();

  // 空 tiers = bypass；非空但格式非法 = 回退 level-3 + log error。
  // prealloc=true 时构造期为每个 tier 一次性预分配满额 blocks_per_chunk。
  explicit MemoryPool(const Config& config);

  // 无参构造 = bypass（等价 MemoryPool(Config{})）。
  MemoryPool();

  // 按 level 从内置 10 行表（L0..L9，L0 = bypass）取一行。越界值钳到 [0, 9]。
  // prealloc=true 时构造期为每个 tier 一次性预分配满额 blocks_per_chunk。
  explicit MemoryPool(int level, bool prealloc = false);

  [[nodiscard]] void* allocate(size_t bytes,
                               size_t alignment = alignof(std::max_align_t)) noexcept;
  void deallocate(void* p, size_t bytes,
                  size_t alignment = alignof(std::max_align_t)) noexcept;

  [[nodiscard]] size_t get_tier_count() const noexcept;   // bypass 时为 0
  [[nodiscard]] std::vector<TierStats> get_stats() const noexcept;
  [[nodiscard]] OversizedStats get_oversized_stats() const noexcept;
  void reset_stats() noexcept;
  void clear() noexcept;
  void trim() noexcept;   // alias of clear() — 语义更直观的别名
};
}  // namespace vlink
```

### 关键约束

- `deallocate(p, bytes, alignment)` 必须传入与 `allocate` **完全相同**的 `bytes`，否则会路由到错误 tier 损坏其 free-list。
- `allocate` 永不抛异常；upstream OOM 时返回 `nullptr`。
- `TierStats::upstream_alloc_count` / `upstream_alloc_bytes` 反映 **真实向 OS 申请的次数 / 字节**（成功保留入池的 chunk + 因并发竞态丢弃的 chunk + 因 `chunks.push_back` 失败丢弃的 chunk 都会计入；只有 `::operator new` 返回 `nullptr` 的 OOM 不计入）。`reset_stats()` 与 `clear()` 都不会清零这两个 lifetime 计数器。
- **进程向 OS 的总申请量**（次数 / 字节）= `Σ tier.upstream_alloc_count` + `OversizedStats::alloc_count` / `Σ tier.upstream_alloc_bytes` + `OversizedStats::alloc_bytes`；tier 与 oversized 是分离的两路计数，需要两边相加。
- `clear()` 仅释放**完全空闲**的 chunk —— 任何还含有 live block 的 chunk 会被保留，对应的 free 节点也保留在 free-list 上以便后续复用；`chunk_count` 按实际释放数量递减，lifetime 累计计数与 `next_chunk_blocks` 几何增长状态都保留。该方法在 per-tier spin lock 下执行，可以与 `allocate`/`deallocate` 并发调用而不会让 live block 失效；但 partition 阶段是 `O(N_freelist * N_chunks)`，**热路径并发场景下会显著拉锁**，请按"维护操作"使用而非高频原语。
- 析构函数（`~MemoryPool`）则**无条件**释放所有 chunk —— 这是 lifecycle end，需要调用方保证此时没有任何 live block，也没有其它线程仍在并发 `allocate`/`deallocate`/`clear`，否则 UB。
- 全局单例与进程同生共死。如需主动释放：对全局池调用 `clear()`（安全释放空闲 chunk，并发友好），或构造本地 `MemoryPool` 并在所有 block 归还后让其析构。

## 演示流程

1. **get_default_config()**：读取当前 `VLINK_MEMORY_LEVEL` 对应的 8 阶金字塔，构造本地池，分配 48B / 96B / 1 KiB / 1 MiB 四种典型尺寸，打印 per-tier 统计。
2. **Oversized passthrough**：构造一个最大 tier 仅 1 KiB 的小池子，请求 32 MiB，观察 oversized 计数。
3. **Custom tier configuration**：3 阶配置（64B / 4 KiB / 64 KiB）批量分配 32 个 4 KiB 页，演示 chunk 几何增长与 `reset_stats()`。
4. **global_instance(true)**：调用一次全局池（等价于 `Bytes::init_memory_pool()`），随后用 `Bytes::create()` 分配 2 KiB 与 64 KiB，验证 `Bytes` 的堆缓冲确实从同一个池里来。
5. **MemoryPool(int level)**：按 level（5）直接构造一个池，不需要拼写 tier 数组。
6. **Bypass 模式**：以 `MemoryPool()`（无参 = bypass）演示所有分配直通 `::operator new`，所有 tier 计数为 0、仅 oversized 计数增长。

## 与 Bytes 的关系

```
Bytes::create() / bytes_malloc()
   |
   v
MemoryPool::global_instance().allocate(size)
   |
   +-- size <= 最大 tier 的 max_size --> 命中某 tier 的 free-list
   |
   +-- 否则                              --> ::operator new 直通路径
```

`Bytes::init_memory_pool()` 内部就是 `MemoryPool::global_instance(true)`：在应用启动时调用一次，让单例按 `VLINK_MEMORY_LEVEL` 选择 tier；不显式调用也能工作，但会按 level-3 默认值懒加载。

## 注意事项

- 所有 `public` 方法 `noexcept`，可从多线程并发调用；锁粒度为 per-tier。
- `get_default_config()` 由查表得出，不在运行时计算，因此进程驻留内存大小可预测。
- 自定义 tier 数组若**非空但格式非法**（`max_size == 0`、不严格递增、`blocks_per_chunk == 0`、`max_size < sizeof(FreeNode)`），构造函数会打印 error 并回退到 level-3 默认金字塔。**空表**则被视为显式选择 bypass 模式，不会触发回退。
- Oversized 路径同样被独立计数，便于发现"漏过 tier"的热点尺寸。
