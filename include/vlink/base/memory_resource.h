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
 * @file memory_resource.h
 * @brief @c std::pmr::memory_resource adapter over @c vlink::MemoryPool.
 *
 * @details
 * @c vlink::MemoryResource lets any pmr-aware container or allocator route its
 * raw byte allocations through a @c vlink::MemoryPool.  The adapter is a thin
 * forwarding shim -- every @c allocate / @c deallocate hops through one virtual
 * call to the underlying pool.
 *
 * Lifetime model:
 * - The @c Config, the @c int @p level, and the no-arg constructors each
 *   heap-allocate a private @c MemoryPool that the resource owns and destroys
 *   alongside itself.  The no-arg constructor builds a bypass pool (no tiers,
 *   every alloc goes through @c ::operator @c new).
 * - @c MemoryResource::global_instance() exposes a process-wide
 *   @c MemoryResource that wraps @c MemoryPool::global_instance() -- the
 *   static singleton is shared, never deleted by this resource.
 *
 * @par Example
 * @code
 * // 1. Drop-in process-wide handle (shares the global Bytes pool).
 * std::pmr::vector<int> v(&vlink::MemoryResource::global_instance());
 * v.reserve(1024);                                       // -> MemoryPool
 *
 * // 2. Private resource using the built-in level-3 (balanced) pyramid.
 * vlink::MemoryResource res(3);
 * std::pmr::polymorphic_allocator<char> alloc(&res);
 * std::pmr::string s(alloc);
 *
 * // 3. Private resource with custom tier configuration.
 * vlink::MemoryPool::Config cfg;
 * cfg.tiers = {{64, 16}, {1024, 4}};
 * cfg.prealloc = true;                                   // full-quota prealloc
 * vlink::MemoryResource custom(cfg);
 * std::pmr::vector<double> w(&custom);
 *
 * // 4. Private resource in bypass mode (no pool, raw new/delete).
 * vlink::MemoryResource bypass;                          // no-arg ctor = bypass
 * std::pmr::vector<int> direct(&bypass);
 * @endcode
 *
 * @note
 * - @c do_allocate throws @c std::bad_alloc when the underlying pool returns
 *   @c nullptr (pmr contract).  The underlying @c MemoryPool itself is noexcept.
 * - @c do_is_equal returns @c true iff @p other is also a @c MemoryResource
 *   bound to the same @c MemoryPool object.
 * - @c MemoryResource is non-copyable and non-movable, matching
 *   @c std::pmr::memory_resource's contract.
 */

#pragma once

#if defined(__linux__) && __has_include(<memory_resource>)
#include <memory_resource>
#if defined(__cpp_lib_memory_resource)
#define VLINK_ENABLE_MEMORY_RESOURCE
#endif
#endif

#ifdef VLINK_ENABLE_MEMORY_RESOURCE

#include <cstddef>

#include "./macros.h"
#include "./memory_pool.h"

namespace vlink {

/**
 * @class MemoryResource
 * @brief @c std::pmr::memory_resource adapter that delegates to a
 *        @c vlink::MemoryPool.
 */
class VLINK_EXPORT MemoryResource : public std::pmr::memory_resource {
 public:
  /**
   * @brief Constructs a bypass-mode resource owning a private bypass pool.
   *
   * @details
   * Equivalent to @c MemoryResource(MemoryPool::Config{}) -- the owned pool
   * has no tiers, so every allocation is forwarded to @c ::operator @c new.
   */
  MemoryResource();

  /**
   * @brief Owns a private @c MemoryPool using the built-in pyramid for
   *        @p level.
   *
   * @details
   * Forwards to @c MemoryPool(int, bool); see that constructor for
   * clamping and preallocation semantics.  In bypass mode (level @c 0)
   * @p prealloc is ignored.
   *
   * @param level     Built-in level in @c [0, 9].  Out-of-range values are
   *                  clamped.  Level @c 0 yields bypass mode (no pool).
   * @param prealloc  When @c true, eagerly fills every tier to its full
   *                  @c blocks_per_chunk quota with one upstream allocation
   *                  per tier (best-effort).  Defaults to @c false.
   */
  explicit MemoryResource(int level, bool prealloc = false);

  /**
   * @brief Owns a private @c MemoryPool built from the given config.
   *
   * @details
   * Empty @c config.tiers = bypass mode (every allocation goes straight to
   * @c ::operator @c new / @c delete).  Same validation and fallback rules
   * as the @c MemoryPool constructor apply, including @c config.prealloc.
   *
   * @param config  Tier configuration forwarded to the @c MemoryPool ctor.
   */
  explicit MemoryResource(const MemoryPool::Config& config);

  /**
   * @brief Destroys the resource and, if it owns one, the @c MemoryPool it
   *        was constructed with.
   *
   * @details
   * The @c global_instance() resource does NOT own its pool; only resources
   * built via the public constructors do, and their owned pool is destroyed
   * here.  The caller must guarantee that no live block obtained from this
   * resource is still outstanding and no other thread is calling
   * @c allocate / @c deallocate on this resource at destruction time.
   */
  ~MemoryResource() override;

  /**
   * @brief Returns the underlying @c MemoryPool the adapter forwards to.
   *
   * @details
   * For @c global_instance() this returns the same reference as
   * @c MemoryPool::global_instance().  For other constructors it returns
   * the private pool owned by this resource.
   */
  [[nodiscard]] MemoryPool& get_memory_pool() noexcept;

  /**
   * @brief Forwards to @c MemoryPool::trim() on the underlying pool.
   *
   * @details
   * Releases only fully-free chunks; chunks still backing a live allocation
   * are preserved.  Safe to call concurrently with @c allocate /
   * @c deallocate on this resource.  See @c MemoryPool::trim() for the full
   * complexity and concurrency contract.
   */
  void trim() noexcept;

  /**
   * @brief Returns a process-wide @c MemoryResource that wraps
   *        @c MemoryPool::global_instance().
   *
   * @details
   * Lazily constructs the singleton on first call.  The resource does NOT
   * own the underlying pool; deletion is a no-op.  The first call's
   * @p use_env_level value is forwarded to @c MemoryPool::global_instance()
   * and decides the underlying pool's configuration; subsequent calls
   * return the same resource and ignore the argument.
   *
   * @code
   * std::pmr::vector<T> v(&vlink::MemoryResource::global_instance());
   * @endcode
   *
   * @param use_env_level  Forwarded to @c MemoryPool::global_instance().
   *                       Default @c true honours @c VLINK_MEMORY_LEVEL and
   *                       @c VLINK_MEMORY_PREALLOC.
   * @return Reference to the global resource, valid for the program's lifetime.
   */
  static MemoryResource& global_instance(bool use_env_level = true);

 protected:
  void* do_allocate(size_t bytes, size_t alignment) override;

  void do_deallocate(void* p, size_t bytes, size_t alignment) override;

  bool do_is_equal(const std::pmr::memory_resource& other) const noexcept override;

 private:
  explicit MemoryResource(MemoryPool& global_pool) noexcept;

  MemoryPool* pool_{nullptr};
  bool owns_pool_{false};

  VLINK_DISALLOW_COPY_AND_ASSIGN(MemoryResource)
};

}  // namespace vlink

#endif
