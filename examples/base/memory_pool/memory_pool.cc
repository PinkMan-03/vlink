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

// Example: MemoryPool - tiered (pyramid) free-list allocator with size-class dispatch

#include <vlink/base/bytes.h>
#include <vlink/base/logger.h>
#include <vlink/base/memory_pool.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace {

void print_tier_stats(const vlink::MemoryPool& pool) {
  auto stats = pool.get_stats();
  MLOG_I("  tiers: {}", stats.size());
  for (size_t i = 0; i < stats.size(); ++i) {
    const auto& s = stats[i];
    MLOG_I(
        "    tier[{}]: max_size={} block_size={} blocks_per_chunk={} hits={} deallocs={} in_use={} chunks={} "
        "upstream_allocs={} upstream_bytes={}",
        i, s.max_size, s.block_size, s.blocks_per_chunk, s.hit_count, s.deallocate_count, s.in_use_blocks,
        s.chunk_count, s.upstream_alloc_count, s.upstream_alloc_bytes);
  }

  auto over = pool.get_oversized_stats();
  MLOG_I("  oversized: alloc_count={} alloc_bytes={} dealloc_count={}", over.alloc_count, over.alloc_bytes,
         over.dealloc_count);
}

}  // namespace

int main() {
  // ---------------------------------------------------------------
  // 1. Local pool with get_default_config() honours VLINK_MEMORY_LEVEL.
  // ---------------------------------------------------------------
  {
    VLOG_I("=== Section 1: get_default_config() ===");

    auto config = vlink::MemoryPool::get_default_config();
    MLOG_I("  VLINK_MEMORY_LEVEL produced {} tiers (prealloc={})", config.tiers.size(), config.prealloc);
    for (size_t i = 0; i < config.tiers.size(); ++i) {
      MLOG_I("    cfg[{}]: max_size={} blocks_per_chunk={}", i, config.tiers[i].max_size,
             config.tiers[i].blocks_per_chunk);
    }

    vlink::MemoryPool pool(config);
    MLOG_I("  pool.get_tier_count() = {}", pool.get_tier_count());

    // 2 KiB chunk header sized request + 1 KiB hot path + 1 MiB rare path.
    void* small_a = pool.allocate(48);                  // small header tier
    void* small_b = pool.allocate(96);                  // small header tier
    void* one_kib = pool.allocate(1024);                // 1 KiB tier
    void* one_mib = pool.allocate(1U * 1024U * 1024U);  // 1 MiB tier

    if (small_a == nullptr || small_b == nullptr || one_kib == nullptr || one_mib == nullptr) {
      VLOG_W("  allocate returned nullptr -- upstream OOM, skipping rest");
      pool.deallocate(small_a, 48);
      pool.deallocate(small_b, 96);
      pool.deallocate(one_kib, 1024);
      pool.deallocate(one_mib, 1U * 1024U * 1024U);
    } else {
      VLOG_I("  Allocated 48B / 96B / 1KiB / 1MiB blocks");
      print_tier_stats(pool);

      // Same bytes value MUST be passed back -- mismatched sizes route to the
      // wrong tier and corrupt that tier's free-list.
      pool.deallocate(small_a, 48);
      pool.deallocate(small_b, 96);
      pool.deallocate(one_kib, 1024);
      pool.deallocate(one_mib, 1U * 1024U * 1024U);

      VLOG_I("  After deallocate:");
      print_tier_stats(pool);
    }
  }

  // ---------------------------------------------------------------
  // 2. Oversized passthrough -- bypass the pool, hit ::operator new.
  // ---------------------------------------------------------------
  {
    VLOG_I("=== Section 2: Oversized passthrough ===");

    // Tiny pool whose biggest tier is only 1 KiB.
    vlink::MemoryPool::Config tiny_cfg;
    tiny_cfg.tiers = {
        {64U, 128U},
        {256U, 64U},
        {1024U, 16U},
    };
    vlink::MemoryPool tiny(tiny_cfg);

    constexpr size_t kHuge = 32U * 1024U * 1024U;  // 32 MiB -- way past the 1 KiB top tier
    void* huge = tiny.allocate(kHuge);

    if (huge == nullptr) {
      VLOG_W("  Huge allocation failed -- system OOM");
    } else {
      VLOG_I("  Huge 32 MiB allocation routed to oversized passthrough");
      tiny.deallocate(huge, kHuge);
      print_tier_stats(tiny);
    }
  }

  // ---------------------------------------------------------------
  // 3. Custom tier configuration tuned for fixed-size 4 KiB pages.
  // ---------------------------------------------------------------
  {
    VLOG_I("=== Section 3: Custom tier configuration ===");

    vlink::MemoryPool::Config page_cfg;
    page_cfg.tiers = {
        {64U, 256U},
        {4U * 1024U, 64U},
        {64U * 1024U, 8U},
    };
    page_cfg.prealloc = true;  // fill every tier to full blocks_per_chunk quota up-front
    vlink::MemoryPool page_pool(page_cfg);

    constexpr size_t kPageBytes = 4096U;
    constexpr size_t kPageCount = 32U;

    std::array<void*, kPageCount> pages{};
    for (size_t i = 0; i < kPageCount; ++i) {
      pages[i] = page_pool.allocate(kPageBytes);
    }
    VLOG_I("  Allocated 32 x 4 KiB pages from the 4 KiB tier");
    print_tier_stats(page_pool);

    for (size_t i = 0; i < kPageCount; ++i) {
      page_pool.deallocate(pages[i], kPageBytes);
    }
    VLOG_I("  Returned all pages to the free-list");
    print_tier_stats(page_pool);

    page_pool.reset_stats();
    VLOG_I("  After reset_stats():");
    print_tier_stats(page_pool);

    // clear() drops only chunks that are fully free; chunks still backing
    // a live block are kept (none here -- every page was returned above).
    // Safe to call concurrently with allocate / deallocate; lifetime
    // counters and the geometric growth state are preserved.
    page_pool.clear();
    VLOG_I("  After clear() -- fully-free chunks released, live blocks kept:");
    print_tier_stats(page_pool);
  }

  // ---------------------------------------------------------------
  // 4. Shared global instance -- the pool used by Bytes.
  // ---------------------------------------------------------------
  {
    VLOG_I("=== Section 4: global_instance() shared with Bytes ===");

    // First call wins: passing true makes the singleton honour VLINK_MEMORY_LEVEL.
    // Equivalent to vlink::Bytes::init_memory_pool().
    auto& global = vlink::MemoryPool::global_instance(true);
    MLOG_I("  global tier count = {}", global.get_tier_count());

    // Bytes::create() routes its heap allocation through the same singleton.
    auto buf_a = vlink::Bytes::create(2048);
    auto buf_b = vlink::Bytes::create(64U * 1024U);
    MLOG_I("  Bytes::create produced sizes {} and {}", buf_a.size(), buf_b.size());

    VLOG_I("  Stats after a few Bytes allocations:");
    print_tier_stats(global);
  }

  // ---------------------------------------------------------------
  // 5. Construct directly by level -- no need to spell out a tier array.
  // ---------------------------------------------------------------
  {
    VLOG_I("=== Section 5: MemoryPool(int level) ===");

    // Equivalent to MemoryPool(get_default_config row #5); out-of-range values
    // are clamped to [0, 9] with a warning.
    vlink::MemoryPool level5_pool(5);
    MLOG_I("  level5_pool tier count = {}", level5_pool.get_tier_count());

    void* p = level5_pool.allocate(96);
    if (p != nullptr) {
      level5_pool.deallocate(p, 96);
      VLOG_I("  level5_pool routed a 96B alloc to tier 0:");
      print_tier_stats(level5_pool);
    }
  }

  // ---------------------------------------------------------------
  // 6. Bypass mode -- empty tier list / level 0 / VLINK_MEMORY_LEVEL=0.
  //    Every allocate -> ::operator new, every deallocate -> ::operator delete.
  //    All tier counts stay 0; only the oversized counters move.
  // ---------------------------------------------------------------
  {
    VLOG_I("=== Section 6: Bypass mode (VLINK_MEMORY_LEVEL=0 / empty tiers) ===");

    vlink::MemoryPool bypass_pool;  // no-arg ctor = bypass; same as MemoryPool(0)
    MLOG_I("  bypass_pool tier count = {} (expect 0)", bypass_pool.get_tier_count());

    void* a = bypass_pool.allocate(48);
    void* b = bypass_pool.allocate(64U * 1024U);

    if (a != nullptr) bypass_pool.deallocate(a, 48);
    if (b != nullptr) bypass_pool.deallocate(b, 64U * 1024U);

    auto over = bypass_pool.get_oversized_stats();
    MLOG_I("  bypass oversized: alloc_count={} alloc_bytes={} dealloc_count={}", over.alloc_count, over.alloc_bytes,
           over.dealloc_count);
  }

  VLOG_I("MemoryPool example finished.");
  return 0;
}
