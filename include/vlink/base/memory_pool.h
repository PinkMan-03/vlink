/*
 * Copyright (C) 2026 by Thun Lu. All rights reserved.
 * Author: Thun Lu <thun.lu@zohomail.cn>
 * Repo:   https://github.com/thun-res/vlink
 *  _    __   __      _           __
 * | |  / /  / /     (_) ____    / /__
 * | | / /  / /     / / / __ \  / //_/
 * | |/ /  / /___  / / / / / / / ,<
 * |___/  /_____/ /_/ /_/ /_/ /_/|_|
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

/**
 * @file memory_pool.h
 * @brief Tiered (pyramid) memory pool with size-class dispatch and per-tier statistics.
 *
 * @details
 * @c MemoryPool partitions allocation requests across a fixed pyramid of size
 * tiers.  Each tier owns a singly-linked free list of fixed-size blocks plus a
 * vector of upstream chunks; chunk capacity starts at a per-tier initial
 * count chosen to target a roughly 64 KiB first chunk and then doubles up to
 * the configured @c blocks_per_chunk.  Requests larger than the biggest tier
 * (or with caller alignment above @c alignof(std::max_align_t)) bypass the
 * pool and go straight to @c ::operator @c new / @c ::operator @c delete.
 *
 * Tier configuration:
 *
 * | Source                                | When used                                              |
 * | ------------------------------------- | ------------------------------------------------------ |
 * | Caller-supplied @c Config             | @c MemoryPool(const Config&) with non-empty tiers      |
 * | @c get_default_config() (level 0..9)  | @c global_instance(true)                               |
 * | Built-in level-3 balanced pyramid     | Malformed input fallback; @c global_instance(false)    |
 * | Built-in level @c N row               | @c MemoryPool(int level, bool prealloc) ctor           |
 *
 * Cleanup primitives:
 *
 * | Method            | What it releases                                | Concurrent traffic? |
 * | ----------------- | ----------------------------------------------- | ------------------- |
 * | @c clear / trim   | Only fully-free chunks; live blocks preserved   | Safe                |
 * | @c ~MemoryPool    | Every chunk, unconditionally                    | Caller must quiesce |
 *
 * @note
 * - All public methods are @c noexcept and safe to call from multiple threads.
 *   Lock granularity is per-tier, so traffic across different size classes
 *   does not contend.
 * - @c deallocate must be called with exactly the @c bytes value passed to
 *   @c allocate.  Mismatched sizes route the block to the wrong tier and
 *   corrupt that tier's free list.
 * - @c allocate returns @c nullptr on upstream OOM and never throws.  The
 *   constructor's only exception path is @c std::bad_alloc from internal
 *   vector growth.
 *
 * @par Example
 * @code
 * auto& pool = vlink::MemoryPool::global_instance();
 * void* p = pool.allocate(512);                  // routes to the 512 B tier
 * pool.deallocate(p, 512);                       // bytes MUST match
 *
 * pool.trim();                                   // periodic memory reclaim
 * @endcode
 */

#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

#include "./macros.h"

namespace vlink {

/**
 * @class MemoryPool
 * @brief Size-tiered memory pool with per-tier free list and statistics.
 *
 * @details
 * Thread-safe; per-tier spin lock serialises @c allocate, @c deallocate and
 * @c clear within a single tier.  Different tiers do not contend.
 */
class VLINK_EXPORT MemoryPool final {
 public:
  /**
   * @brief Default block alignment for every pooled allocation.
   *
   * @details
   * Equal to @c alignof(std::max_align_t).  Requests with a stricter
   * alignment bypass the pool and call @c ::operator @c new with
   * @c std::align_val_t directly.  Typical value on x86_64 / aarch64 Linux:
   * 16 bytes.
   */
  static constexpr size_t kBlockAlignment = alignof(std::max_align_t);

  /**
   * @brief Configuration of a single tier.
   */
  struct Tier final {
    size_t max_size{0};          ///< Inclusive byte upper bound for this tier.
    size_t blocks_per_chunk{0};  ///< Max blocks per upstream chunk.
  };

  /**
   * @brief Constructor configuration: tier list plus optional preallocation.
   *
   * @details
   * Wraps the @c Tier vector so additional knobs can be attached without
   * widening the @c MemoryPool constructor signature.
   *
   * @c prealloc controls whether the constructor eagerly fills every tier
   * to its full @c blocks_per_chunk quota on construction (one upstream
   * allocation per tier, sized for the entire quota).  Default @c false
   * keeps the lazy geometric-growth behaviour (idle pool footprint = 0).
   * Set to @c true for latency-critical paths that want to pay the entire
   * upstream allocation cost up-front instead of growing on the hot path.
   *
   * @note Preallocation is best-effort: if @c ::operator @c new fails for
   *       any tier, that tier remains in its lazy state and the constructor
   *       continues with the rest.
   */
  struct Config final {
    std::vector<Tier> tiers;  ///< Tier descriptors; empty or all-sentinel = bypass mode.
    bool prealloc{false};     ///< If @c true, eagerly fill each managed tier to its full quota.
  };

  /**
   * @brief Per-tier runtime statistics snapshot.
   *
   * @details
   * Counters use relaxed atomics; @c in_use_blocks and the lifetime upstream
   * fields are best-effort under concurrent traffic, not globally atomic.
   */
  struct TierStats final {
    size_t max_size{0};                ///< Tier's @c max_size from the configuration.
    size_t blocks_per_chunk{0};        ///< Tier's @c blocks_per_chunk from the configuration.
    size_t block_size{0};              ///< Effective block size after alignment rounding.
    uint64_t hit_count{0};             ///< Allocations dispatched to this tier (resettable).
    uint64_t deallocate_count{0};      ///< Deallocations dispatched to this tier (resettable).
    uint64_t in_use_blocks{0};         ///< Best-effort @c hit_count - @c deallocate_count.
    uint64_t chunk_count{0};           ///< Currently owned chunks; @c clear decrements by released count.
    uint64_t upstream_alloc_count{0};  ///< Lifetime chunks fully installed in this tier.
    uint64_t upstream_alloc_bytes{0};  ///< Lifetime bytes of those installed chunks.
  };

  /**
   * @brief Aggregate statistics for the oversized-passthrough path.
   *
   * @details
   * Tracks allocations that bypass the tier free lists (size > biggest tier
   * or alignment > @c kBlockAlignment).
   */
  struct OversizedStats final {
    uint64_t alloc_count{0};    ///< Oversized allocations forwarded to the system allocator.
    uint64_t alloc_bytes{0};    ///< Total bytes of oversized allocations.
    uint64_t dealloc_count{0};  ///< Oversized deallocations.
  };

  /**
   * @brief Constructs an empty pool in bypass mode.
   *
   * @details
   * Equivalent to @c MemoryPool(Config{}): every allocation routes through
   * @c ::operator @c new / @c ::operator @c delete (no tiered free lists,
   * no preallocation).
   */
  MemoryPool();

  /**
   * @brief Constructs a tiered pool with the built-in pyramid for the given
   *        level.
   *
   * @details
   * Selects a row from the built-in tier table.  Out-of-range values are
   * clamped to @c [0, 9] and a warning is logged.  Level @c 0 yields bypass
   * mode (no pool, every alloc goes through @c ::operator @c new); the
   * @p prealloc flag is ignored in bypass mode (no tiers to fill).
   * At level @c 9 the fully-saturated resident footprint is approximately
   * 704 MiB.
   *
   * Equivalent to @c MemoryPool(Config{tiers_for(level), prealloc}); see the
   * @c Config-based constructor for the full validation, fallback, and
   * preallocation contract.
   *
   * @param level     Built-in level in @c [0, 9].
   * @param prealloc  When @c true, eagerly fills every tier to its full
   *                  @c blocks_per_chunk quota with one upstream allocation
   *                  per tier (best-effort; failures revert that tier to
   *                  lazy growth).  Defaults to @c false (lazy / on-demand).
   */
  explicit MemoryPool(int level, bool prealloc = false);

  /**
   * @brief Constructs a tiered pool with the given configuration.
   *
   * @details
   * Empty @c config.tiers puts the pool in @b bypass mode -- every
   * @c allocate goes straight to @c ::operator @c new and every
   * @c deallocate to @c ::operator @c delete (equivalent to
   * @c VLINK_MEMORY_LEVEL=0).  Tier entries with @c blocks_per_chunk == 0
   * are sentinels: they declare a size ceiling but are stripped at
   * construction so allocations matching that range fall through to the
   * upstream allocator (an all-sentinel list is therefore equivalent to
   * bypass mode).  Malformed but non-empty input (non-monotonic ordering,
   * duplicate @c max_size, zero @c max_size, or @c max_size below the
   * minimum required block size) logs an error and silently falls back to
   * the level-3 default pyramid.  Validation never throws; @c std::bad_alloc
   * may still propagate from the internal vector growths.
   *
   * When @c config.prealloc is @c true, the constructor eagerly fills every
   * tier to its full @c blocks_per_chunk quota in a single upstream
   * allocation per tier (best-effort: tiers whose @c ::operator @c new
   * fails are left in lazy state).  Bypass mode ignores @c prealloc --
   * there are no tiers to fill.
   *
   * @param config  Tier descriptors and preallocation flag.
   */
  explicit MemoryPool(const Config& config);

  /**
   * @brief Releases every chunk owned by the pool, unconditionally.
   *
   * @warning The caller must guarantee no outstanding pooled block is in use
   *          and no other thread is calling @c allocate / @c deallocate /
   *          @c clear / @c trim on this instance.  Use @c clear() instead
   *          for a non-destructive trim.
   */
  ~MemoryPool();

  /**
   * @brief Allocates @p bytes of memory from the appropriate tier.
   *
   * @details
   * Routes to the first tier whose @c max_size is >= @p bytes.  Requests
   * larger than the biggest tier, or with @p alignment > @c kBlockAlignment,
   * bypass the pool and use @c ::operator @c new with @c std::align_val_t.
   * @p bytes == 0 routes to the smallest tier and returns a unique non-null
   * pointer (@p bytes still must be passed to @c deallocate).  This function
   * never throws.
   *
   * @param bytes      Requested size.
   * @param alignment  Required alignment (power of two).  Defaults to
   *                   @c kBlockAlignment; non-power-of-two is UB per
   *                   [new.delete.general].
   * @return Pointer to allocated memory, or @c nullptr if upstream
   *         allocation failed.
   */
  [[nodiscard]] void* allocate(size_t bytes, size_t alignment = kBlockAlignment) noexcept;

  /**
   * @brief Deallocates a pointer previously returned by @c allocate().
   *
   * @param p          Pointer returned by @c allocate.  @c nullptr is a no-op.
   * @param bytes      Original size passed to @c allocate -- MUST match.
   * @param alignment  Original alignment passed to @c allocate.
   */
  void deallocate(void* p, size_t bytes, size_t alignment = kBlockAlignment) noexcept;

  /**
   * @brief Returns the number of configured tiers.
   *
   * @return Tier count.  @c 0 indicates bypass mode (level @c 0, an empty
   *         caller-supplied tier list, or a list whose every entry is a
   *         @c blocks_per_chunk == 0 sentinel).  The count reflects only
   *         live (managed) tiers: sentinel entries are stripped at
   *         construction and do not appear here.  Built-in pyramid level
   *         @c 1 yields @c 14 live tiers (1 MiB / 4 MiB / 8 MiB / 16 MiB /
   *         32 MiB ceilings are sentinels); level @c 2 yields @c 15
   *         (4 MiB / 8 MiB / 16 MiB / 32 MiB sentinels); levels @c 3..4
   *         yield @c 16 (8 MiB / 16 MiB / 32 MiB sentinels); level @c 5
   *         yields @c 17 (16 MiB / 32 MiB sentinels); levels @c 6..7 yield
   *         @c 18 (32 MiB sentinel); levels @c 8..9 yield all @c 19 live
   *         tiers.
   */
  [[nodiscard]] size_t get_tier_count() const noexcept;

  /**
   * @brief Returns a snapshot of per-tier statistics.
   *
   * @return Vector of @c TierStats, one entry per configured tier.
   */
  [[nodiscard]] std::vector<TierStats> get_stats() const noexcept;

  /**
   * @brief Returns a snapshot of the oversized-passthrough statistics.
   *
   * @details
   * Counters are loaded with relaxed ordering and are not a globally atomic
   * snapshot; under heavy concurrent traffic @c dealloc_count may transiently
   * exceed @c alloc_count.
   *
   * @return Aggregated counts and bytes for the oversized path.
   */
  [[nodiscard]] OversizedStats get_oversized_stats() const noexcept;

  /**
   * @brief Resets per-call statistics counters to zero.
   *
   * @details
   * Clears @c hit_count and @c deallocate_count on every tier and the three
   * @c oversized_* counters.  Physical / lifetime state (@c chunk_count,
   * @c upstream_alloc_count, @c upstream_alloc_bytes) is preserved because it
   * describes memory the pool has actually committed -- not a per-call rate.
   * Use between benchmark phases, not as a memory-release primitive.
   */
  void reset_stats() noexcept;

  /**
   * @brief Releases only fully-free chunks; preserves any chunk that still
   *        backs a live allocation.
   *
   * @details
   * For each tier, walks the free list and partitions free nodes by their
   * owning chunk.  A chunk whose free-node count equals its block capacity
   * holds zero live allocations and is released; chunks containing one or
   * more live blocks (held by callers, not on the free list) stay intact and
   * keep their free nodes available for reuse.  @c chunk_count is decremented
   * by the number of released chunks.
   *
   * Per-call counters, lifetime upstream counters, @c next_chunk_blocks, and
   * oversized state are all preserved.  Per-tier work is
   * @c O(C @c log @c C @c + @c F @c log @c C) (chunks sorted by address once,
   * free-list scanned twice with binary lookup) under the spin lock; treat
   * as a maintenance call, not a hot-path primitive.
   *
   * @note
   * Safe to call concurrently with @c allocate / @c deallocate -- live blocks
   * held by other threads are never invalidated.  Under extreme memory
   * pressure individual tiers may be silently skipped; the pool stays valid
   * and a later call may succeed.
   */
  void clear() noexcept;

  /**
   * @brief Alias of @c clear() with a name that reads more naturally for
   *        periodic-trim use cases.
   *
   * @details
   * Behaviour, thread-safety, and complexity are identical to @c clear() --
   * a forwarding wrapper provided so that "trim cached memory" intent does
   * not have to use a name that historically suggested a destructive reset.
   */
  void trim() noexcept;

  /**
   * @brief Returns the default tier pyramid for the current
   *        @c VLINK_MEMORY_LEVEL and @c VLINK_MEMORY_PREALLOC.
   *
   * @details
   * Each level (0..9) maps to a hand-coded row of {max_size, blocks_per_chunk}
   * pairs -- nothing is computed at runtime.  Level @c 0 returns an
   * all-sentinel row (every entry has @c blocks_per_chunk == 0) which the
   * constructor then strips into bypass mode.  Levels @c 1..9 return 19
   * entries spanning 64 B .. 32 MiB; the 1 MiB / 4 MiB / 8 MiB / 16 MiB /
   * 32 MiB ceilings are sentinels at low levels (1 MiB activates at L2,
   * 4 MiB at L3, 8 MiB at L5, 16 MiB at L6, 32 MiB at L8) and yield
   * 14..19 live tiers post-construction depending on level.
   * Within each level, @c blocks_per_chunk halves cleanly across the
   * doubling-size tiers (64 B .. 512 KiB), so each tier holds a
   * roughly constant aggregate byte budget; the live 1 MiB / 4 MiB /
   * 8 MiB / 16 MiB / 32 MiB tail tiers grow per-level by doubling
   * from their activation count (= 1 block) within monotone constraints.
   * Non-numeric or out-of-range @c VLINK_MEMORY_LEVEL values are clamped to
   * @c [0, 9] with a warning.  At level @c 9 the fully-saturated footprint
   * is approximately 704 MiB.
   *
   * @c VLINK_MEMORY_PREALLOC populates the @c prealloc flag of the
   * returned config.  Only the literal value @c "1" enables preallocation;
   * any other value (including unset, @c "true", @c "yes", or whitespace)
   * keeps the default @c false.
   *
   * @return @c Config ready to pass to the constructor.
   */
  [[nodiscard]] static Config get_default_config();

  /**
   * @brief Returns the process-wide shared @c MemoryPool instance.
   *
   * @details
   * Lazily constructs a Meyers' singleton (thread-safe under C++11+).  Only
   * the very first call decides the configuration; subsequent calls return
   * the same instance and ignore @p use_env_level.
   *
   * @warning Whichever value @p use_env_level takes on the first call is
   *          baked in for the program's lifetime.
   *
   * @param use_env_level  @c true (default): build from @c get_default_config(),
   *                       honouring @c VLINK_MEMORY_LEVEL and
   *                       @c VLINK_MEMORY_PREALLOC.  @c false: use the
   *                       built-in level-3 pyramid (no env, no preallocation).
   * @return Reference to the global pool, valid for the program's lifetime.
   */
  static MemoryPool& global_instance(bool use_env_level = true);

 private:
  size_t find_tier(size_t bytes) const noexcept;

  struct Impl;
  std::unique_ptr<Impl> impl_;

  VLINK_DISALLOW_COPY_AND_ASSIGN(MemoryPool)
};

}  // namespace vlink
