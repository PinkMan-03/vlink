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
 * tiers.  Each tier maintains a singly-linked free list of fixed-size blocks
 * plus a vector of upstream chunks (each chunk holds a number of consecutive
 * blocks).  Small high-frequency allocations land in tiers with many blocks
 * per chunk; larger and rarer allocations land in tiers with few blocks per
 * chunk.  Allocations larger than the biggest tier (or with caller alignment
 * above @c alignof(std::max_align_t)) bypass the pool and go straight to
 * @c ::operator @c new / @c ::operator @c delete.
 *
 * The implementation is fully self-contained -- no dependency on
 * @c \<memory_resource\> or any third-party allocator.  Chunk growth is
 * geometric: the first chunk in each tier holds a small number of blocks,
 * subsequent chunks double in size, capped at the configured
 * @c blocks_per_chunk.  This keeps idle tiers cheap while letting hot tiers
 * amortise upstream allocations.
 *
 * @c default_tiers() returns the 8-tier pyramid for the current
 * @c VLINK_MEMORY_LEVEL, taken from a hand-tuned 6-row lookup table:
 *
 * The @c blocks_per_chunk values are spelled out per level (1..6) so the
 * memory footprint is explicit and predictable; nothing is computed at
 * runtime.  Anything bigger than the 8 MiB tier (e.g. 4K RGBA frames) is
 * served by @c ::operator @c new / @c delete via the oversized passthrough
 * path -- correctness preserved, just no pooling for that size.
 *
 * @par Example
 * @code
 * auto& pool = vlink::MemoryPool::global_instance();   // shared singleton
 * void* p = pool.allocate(512);                        // 1 KiB tier
 * pool.deallocate(p, 512);                             // bytes MUST match
 * @endcode
 *
 * @note
 * - All public methods are noexcept and safe to call concurrently from
 *   multiple threads.  Lock contention is per-tier, so traffic across
 *   different size classes does not contend.
 * - @c deallocate must be called with the same @c bytes value that was
 *   passed to @c allocate.  Passing a mismatched size routes the block to a
 *   different tier and corrupts that tier's free list.
 * - @c allocate returns @c nullptr on upstream OOM; it never throws.
 * - The constructor never throws either: malformed tier configurations are
 *   logged and silently fall back to the level-3 default pyramid.
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
 * @brief Size-tiered memory pool with per-tier @c blocks_per_chunk and statistics.
 */
class VLINK_EXPORT MemoryPool final {
 public:
  /**
   * @brief Default block alignment used for every pooled allocation.
   *
   * @details
   * Equal to @c alignof(std::max_align_t) -- the alignment guaranteed by
   * @c ::operator @c new without explicit @c std::align_val_t.  Every block
   * returned by @c allocate() that lands on the pool path is aligned to this
   * boundary.  Requests with @p alignment greater than @c kBlockAlignment
   * bypass the pool and call @c ::operator @c new(bytes, std::align_val_t{...})
   * directly so the caller-requested alignment is still honoured.
   *
   * Typical value on x86_64 / aarch64 Linux: 16 bytes.
   */
  static constexpr size_t kBlockAlignment = alignof(std::max_align_t);

  /**
   * @brief Configuration for a single tier.
   */
  struct Tier final {
    size_t max_size{0};          ///< Inclusive upper bound (in bytes) for this tier.
    size_t blocks_per_chunk{0};  ///< Maximum blocks any single upstream chunk may hold.
  };

  /**
   * @brief Runtime statistics for a single tier.
   */
  struct TierStats final {
    size_t max_size{0};                ///< Tier's @c max_size from the configuration.
    size_t blocks_per_chunk{0};        ///< Tier's @c blocks_per_chunk from the configuration.
    size_t block_size{0};              ///< Effective block size after alignment rounding.
    uint64_t hit_count{0};             ///< Allocations dispatched to this tier (resettable).
    uint64_t deallocate_count{0};      ///< Deallocations dispatched to this tier (resettable).
    uint64_t in_use_blocks{0};         ///< Best-effort snapshot of @c hit_count - @c deallocate_count.
    uint64_t chunk_count{0};           ///< Number of upstream chunks currently owned by this tier.
    uint64_t upstream_alloc_count{0};  ///< Lifetime upstream chunk allocations (NOT reset by @c reset_stats).
    uint64_t upstream_alloc_bytes{0};  ///< Lifetime upstream chunk bytes (NOT reset by @c reset_stats).
  };

  /**
   * @brief Aggregate statistics for the oversized-passthrough path
   *        (size > biggest tier or alignment > @c alignof(std::max_align_t)).
   */
  struct OversizedStats final {
    uint64_t alloc_count{0};    ///< Oversized allocations forwarded to the system allocator.
    uint64_t alloc_bytes{0};    ///< Total bytes of oversized allocations.
    uint64_t dealloc_count{0};  ///< Oversized deallocations.
  };

  /**
   * @brief Returns the process-wide shared @c MemoryPool instance.
   *
   * @details
   * Lazily constructs the singleton on the first call (Meyers' singleton --
   * thread-safe construction under C++11+).  Use this accessor when a
   * component needs the same pool everyone else uses (e.g. @c Bytes), so
   * all subsystems share a single resident chunk pool.
   *
   * @warning Only the very first call decides the configuration.  Every
   *          subsequent call returns the same instance and ignores
   *          @p use_env_level.  If you need env-driven tuning, ensure your
   *          bootstrap code (e.g. @c Bytes::init_memory_pool) is the first
   *          caller in the process.  When the first call passes @c false,
   *          the pool falls back to the level-3 default pyramid regardless
   *          of any later @c true argument.
   *
   * @param use_env_level  If @c true, the pool is constructed from
   *                       @c default_tiers() and therefore honours
   *                       @c VLINK_MEMORY_LEVEL.  If @c false (default),
   *                       the pool is constructed with no caller-supplied
   *                       tiers, which falls back to the level-3 balanced
   *                       pyramid independent of the environment.
   *
   * @return Reference to the global pool, valid for the program's lifetime.
   */
  static MemoryPool& global_instance(bool use_env_level = false);

  /**
   * @brief Returns the 8-tier default pyramid for the current @c VLINK_MEMORY_LEVEL.
   *
   * @details
   * Each level maps to a hand-coded row of {max_size, blocks_per_chunk}
   * pairs -- nothing is computed at runtime.  When @c VLINK_MEMORY_LEVEL is
   * unset the function uses level 3 (balanced).  Non-numeric or
   * out-of-range values are clamped to [1, 6] and a warning is logged.
   *
   * @return An 8-tier configuration ready to pass to the @c MemoryPool constructor.
   */
  [[nodiscard]] static std::vector<Tier> default_tiers();

  /**
   * @brief Constructs a tiered pool with the given tier configuration.
   *
   * @details
   * On invalid or empty input (non-monotonic ordering, duplicate
   * @c max_size, zero @c max_size, zero @c blocks_per_chunk, or
   * @c max_size below the minimum required block size) the constructor
   * logs an error and silently falls back to the built-in level-3
   * (balanced) pyramid.  Construction never throws.
   *
   * @param tiers  Tier descriptors, optionally sorted strictly by ascending
   *               @c max_size with every field > 0.  When empty or
   *               malformed the level-3 default pyramid is used.
   */
  explicit MemoryPool(const std::vector<Tier>& tiers = {});

  /**
   * @brief Releases every upstream chunk owned by the pool.
   *
   * @details
   * Walks each tier, drops its free list, and pairs every aligned
   * @c ::operator @c new from chunk allocation with the matching sized
   * aligned @c ::operator @c delete.  Outstanding blocks held by callers
   * are invalidated -- use the pool's lifetime to ensure no live blocks
   * remain at destruction.
   */
  ~MemoryPool();

  /**
   * @brief Allocates @p bytes of memory.
   *
   * @details
   * Routes to the first tier whose @c max_size is >= @p bytes.  Requests
   * larger than the biggest tier, or with @p alignment greater than
   * @c alignof(std::max_align_t), bypass the pool and use the system
   * allocator directly.  This function never throws.
   *
   * Edge cases:
   * - @p bytes == 0 routes to the smallest tier and returns a unique
   *   non-null pointer (or @c nullptr on OOM).  The same @c bytes value
   *   must be passed to @c deallocate.
   * - @p alignment must be a power of two; non-power-of-two values are
   *   forwarded to @c ::operator @c new with @c std::align_val_t and
   *   trigger UB per [new.delete.general].
   *
   * @param bytes      Requested size.
   * @param alignment  Alignment in bytes (must be a power of two).
   *                   Defaults to @c kBlockAlignment.
   * @return Pointer to allocated memory, or @c nullptr if upstream
   *         allocation failed.
   */
  [[nodiscard]] void* allocate(size_t bytes, size_t alignment = kBlockAlignment) noexcept;

  /**
   * @brief Deallocates a pointer previously returned by @c allocate().
   *
   * @param p          Pointer returned by @c allocate.
   * @param bytes      Original size passed to @c allocate.  MUST match exactly.
   * @param alignment  Original alignment passed to @c allocate.
   */
  void deallocate(void* p, size_t bytes, size_t alignment = kBlockAlignment) noexcept;

  /**
   * @brief Returns the number of configured tiers.
   */
  [[nodiscard]] size_t get_tier_count() const noexcept;

  /**
   * @brief Returns a snapshot of per-tier statistics.
   */
  [[nodiscard]] std::vector<TierStats> get_stats() const noexcept;

  /**
   * @brief Returns a snapshot of the oversized-passthrough statistics.
   *
   * @note Counters are loaded with relaxed ordering and are NOT a globally
   *       atomic snapshot: under heavy concurrent traffic
   *       @c dealloc_count may transiently appear larger than
   *       @c alloc_count.  Treat the values as samples, not invariants.
   */
  [[nodiscard]] OversizedStats get_oversized_stats() const noexcept;

  /**
   * @brief Resets per-call statistics counters to zero.
   *
   * @details
   * Clears @c hit_count and @c deallocate_count on every tier and the
   * three @c oversized_* counters.  Physical pool state -- @c chunk_count,
   * @c upstream_alloc_count, @c upstream_alloc_bytes -- is preserved
   * because it describes how much memory the pool has actually committed,
   * not a per-call rate.  Use this between benchmark phases, not as a
   * memory-release primitive.
   */
  void reset_stats() noexcept;

 private:
  size_t find_tier(size_t bytes) const noexcept;

  struct Impl;
  std::unique_ptr<Impl> impl_;

  VLINK_DISALLOW_COPY_AND_ASSIGN(MemoryPool)
};

}  // namespace vlink
