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

// NOLINTBEGIN

#include "./base/memory_pool.h"

#include <doctest/doctest.h>

#include <atomic>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <random>
#include <stdexcept>
#include <thread>
#include <unordered_set>
#include <vector>

//
#include "../common_test.h"

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static MemoryPool::Config simple_tiers() {
  MemoryPool::Config cfg;
  cfg.tiers = {
      {64, 4},
      {256, 4},
      {1024, 2},
  };
  return cfg;
}

static MemoryPool::Config make_config(std::vector<MemoryPool::Tier> tiers, bool prealloc = false) {
  MemoryPool::Config cfg;
  cfg.tiers = std::move(tiers);
  cfg.prealloc = prealloc;
  return cfg;
}

static uint64_t total_hits(const std::vector<MemoryPool::TierStats>& stats) {
  uint64_t sum = 0;
  for (const auto& s : stats) {
    sum += s.hit_count;
  }
  return sum;
}

// ---------------------------------------------------------------------------
// TEST SUITE: construction & validation
// ---------------------------------------------------------------------------

TEST_SUITE("base-MemoryPool - construction & validation") {
  TEST_CASE("construct with simple tiers") {
    MemoryPool pool(simple_tiers());

    CHECK(pool.get_tier_count() == 3u);
    CHECK(pool.get_stats().size() == 3u);
    CHECK(pool.get_oversized_stats().alloc_count == 0u);
  }

  TEST_CASE("get_default_config (level 3) is well-formed and ends at 16 MiB") {
    auto config = MemoryPool::get_default_config();
    const auto& tiers = config.tiers;

    CHECK(tiers.size() == 19u);
    CHECK(tiers.front().max_size == 32u);
    CHECK(tiers.back().max_size == 16u * 1024u * 1024u);

    for (size_t i = 1; i < tiers.size(); ++i) {
      CHECK(tiers[i].max_size > tiers[i - 1].max_size);
    }
  }

  TEST_CASE("construct from get_default_config") {
    auto config = MemoryPool::get_default_config();
    CHECK_NOTHROW(MemoryPool{config});
  }

  TEST_CASE("empty tier list selects bypass mode (every alloc -> ::operator new)") {
    MemoryPool pool({});
    CHECK(pool.get_tier_count() == 0u);
    CHECK(pool.get_stats().empty());

    void* p = pool.allocate(128);
    CHECK(p != nullptr);
    CHECK(pool.get_oversized_stats().alloc_count == 1u);
    pool.deallocate(p, 128);
    CHECK(pool.get_oversized_stats().dealloc_count == 1u);
  }

  TEST_CASE("default-constructed pool selects bypass mode") {
    MemoryPool pool;
    CHECK(pool.get_tier_count() == 0u);
  }

  TEST_CASE("MemoryPool(int level) — level 0 is bypass") {
    MemoryPool pool(0);
    CHECK(pool.get_tier_count() == 0u);
  }

  TEST_CASE("MemoryPool(int level) — level 3 yields the level-3 pyramid") {
    MemoryPool pool(3);
    auto stats = pool.get_stats();
    CHECK(stats.size() >= 4u);
    CHECK(stats.front().max_size == 32u);
    // L3 default leaves 4/8/16 MiB as sentinels; the top live tier is 1 MiB.
    CHECK(stats.back().max_size == 1u * 1024u * 1024u);
  }

  TEST_CASE("MemoryPool(int level) — out-of-range level is clamped") {
    MemoryPool pool(99);
    CHECK(pool.get_tier_count() >= 4u);  // clamped to L9
  }

  TEST_CASE("zero max_size triggers default-pyramid fallback") {
    MemoryPool pool(make_config({{0, 4}}));
    CHECK(pool.get_tier_count() >= 4u);
  }

  TEST_CASE("zero blocks_per_chunk is treated as a sentinel and stripped (bypass mode)") {
    MemoryPool pool(make_config({{64, 0}}));
    CHECK(pool.get_tier_count() == 0u);
  }

  TEST_CASE("non-monotonic ordering triggers default-pyramid fallback") {
    MemoryPool pool(make_config({{256, 4}, {64, 4}}));
    CHECK(pool.get_tier_count() >= 4u);
  }

  TEST_CASE("duplicate max_size triggers default-pyramid fallback") {
    MemoryPool pool(make_config({{64, 4}, {64, 4}}));
    CHECK(pool.get_tier_count() >= 4u);
  }

  TEST_CASE("prealloc=false leaves every tier lazy (no upstream alloc until first allocate)") {
    MemoryPool pool(make_config({{64, 8}, {256, 4}}, /*prealloc=*/false));

    auto stats = pool.get_stats();
    REQUIRE(stats.size() == 2u);
    CHECK(stats[0].chunk_count == 0u);
    CHECK(stats[0].upstream_alloc_count == 0u);
    CHECK(stats[1].chunk_count == 0u);
    CHECK(stats[1].upstream_alloc_count == 0u);
  }

  TEST_CASE("prealloc=true fills every tier to its full blocks_per_chunk in one upstream alloc") {
    constexpr size_t kBpc0 = 8;
    constexpr size_t kBpc1 = 4;
    MemoryPool pool(make_config({{64, kBpc0}, {256, kBpc1}}, /*prealloc=*/true));

    auto stats = pool.get_stats();
    REQUIRE(stats.size() == 2u);

    CHECK(stats[0].chunk_count == 1u);
    CHECK(stats[0].upstream_alloc_count == 1u);
    CHECK(stats[0].upstream_alloc_bytes == stats[0].block_size * kBpc0);

    CHECK(stats[1].chunk_count == 1u);
    CHECK(stats[1].upstream_alloc_count == 1u);
    CHECK(stats[1].upstream_alloc_bytes == stats[1].block_size * kBpc1);
  }

  TEST_CASE("prealloc=true serves blocks_per_chunk allocations without further upstream growth") {
    constexpr size_t kBpc = 6;
    MemoryPool pool(make_config({{64, kBpc}}, /*prealloc=*/true));

    std::vector<void*> blocks;
    blocks.reserve(kBpc);
    for (size_t i = 0; i < kBpc; ++i) {
      void* p = pool.allocate(64);
      REQUIRE(p != nullptr);
      blocks.push_back(p);
    }

    auto stats = pool.get_stats();
    REQUIRE(stats.size() == 1u);
    CHECK(stats[0].chunk_count == 1u);           // still the same prealloc chunk
    CHECK(stats[0].upstream_alloc_count == 1u);  // no extra upstream allocations
    CHECK(stats[0].hit_count == kBpc);

    for (void* p : blocks) {
      pool.deallocate(p, 64);
    }
  }

  TEST_CASE("after exhausting the prealloc chunk, subsequent grows preserve the full-chunk policy") {
    constexpr size_t kBpc = 8;
    MemoryPool pool(make_config({{64, kBpc}}, /*prealloc=*/true));

    std::vector<void*> blocks;
    blocks.reserve(kBpc + 1);
    for (size_t i = 0; i < kBpc; ++i) {
      blocks.push_back(pool.allocate(64));
    }

    void* extra = pool.allocate(64);
    REQUIRE(extra != nullptr);
    blocks.push_back(extra);

    auto stats = pool.get_stats();
    REQUIRE(stats.size() == 1u);
    CHECK(stats[0].chunk_count == 2u);
    CHECK(stats[0].upstream_alloc_count == 2u);

    // After a successful prealloc the tier keeps next_chunk_blocks at
    // blocks_per_chunk so the next lazy grow allocates another full quota
    // chunk (matching the prealloc "always allocate full chunks" intent);
    // the per-tier initial_chunk_blocks fallback only kicks in if prealloc
    // itself failed.
    const size_t prealloc_bytes = stats[0].block_size * kBpc;
    const size_t total = stats[0].upstream_alloc_bytes;
    CHECK(total == prealloc_bytes + stats[0].block_size * kBpc);

    for (void* p : blocks) {
      pool.deallocate(p, 64);
    }
  }

  TEST_CASE("prealloc=true on empty tiers (bypass) is a no-op") {
    MemoryPool pool(make_config({}, /*prealloc=*/true));
    CHECK(pool.get_tier_count() == 0u);
    CHECK(pool.get_oversized_stats().alloc_count == 0u);
  }
}

// ---------------------------------------------------------------------------
// TEST SUITE: tier dispatch
// ---------------------------------------------------------------------------

TEST_SUITE("base-MemoryPool - tier dispatch") {
  TEST_CASE("size dispatches to the smallest fitting tier") {
    MemoryPool pool(simple_tiers());

    void* a = pool.allocate(1);
    void* b = pool.allocate(64);
    void* c = pool.allocate(65);
    void* d = pool.allocate(256);
    void* e = pool.allocate(1024);

    auto stats = pool.get_stats();

    CHECK(stats[0].hit_count == 2u);  // <=64 covers 1 and 64
    CHECK(stats[1].hit_count == 2u);  // <=256 covers 65 and 256
    CHECK(stats[2].hit_count == 1u);  // <=1024 covers 1024

    pool.deallocate(a, 1);
    pool.deallocate(b, 64);
    pool.deallocate(c, 65);
    pool.deallocate(d, 256);
    pool.deallocate(e, 1024);
  }

  TEST_CASE("size larger than the last tier passes through to system") {
    MemoryPool pool(simple_tiers());

    const size_t big = 4096;
    void* p = pool.allocate(big);

    CHECK(p != nullptr);
    CHECK(total_hits(pool.get_stats()) == 0u);
    CHECK(pool.get_oversized_stats().alloc_count == 1u);
    CHECK(pool.get_oversized_stats().alloc_bytes == big);

    pool.deallocate(p, big);
    CHECK(pool.get_oversized_stats().dealloc_count == 1u);
  }

  TEST_CASE("sentinel tier keeps its size range on oversized path") {
    MemoryPool pool(make_config({{64, 0}, {128, 4}}));

    void* sentinel = pool.allocate(64);
    REQUIRE(sentinel != nullptr);

    auto stats = pool.get_stats();
    REQUIRE(stats.size() == 1u);
    CHECK(stats[0].max_size == 128u);
    CHECK(stats[0].hit_count == 0u);
    CHECK(pool.get_oversized_stats().alloc_count == 1u);

    void* pooled = pool.allocate(65);
    REQUIRE(pooled != nullptr);

    stats = pool.get_stats();
    CHECK(stats[0].hit_count == 1u);

    pool.deallocate(sentinel, 64);
    pool.deallocate(pooled, 65);
  }

  TEST_CASE("alignment > max_align_t falls back to oversized path") {
    MemoryPool pool(simple_tiers());

    constexpr size_t big_align = 64;
    static_assert(big_align > alignof(std::max_align_t), "test premise");

    void* p = pool.allocate(64, big_align);
    CHECK(reinterpret_cast<uintptr_t>(p) % big_align == 0u);
    CHECK(pool.get_oversized_stats().alloc_count == 1u);

    pool.deallocate(p, 64, big_align);
  }

  TEST_CASE("power-of-two alignment > kBlockAlignment routes to oversized path") {
    MemoryPool pool(simple_tiers());

    constexpr size_t requested_align = 32;
    static_assert(requested_align > alignof(std::max_align_t), "test premise");

    void* p = pool.allocate(64, requested_align);
    CHECK(p != nullptr);
    CHECK(reinterpret_cast<uintptr_t>(p) % requested_align == 0u);
    CHECK(pool.get_oversized_stats().alloc_count == 1u);

    pool.deallocate(p, 64, requested_align);
    CHECK(pool.get_oversized_stats().dealloc_count == 1u);
  }

  TEST_CASE("nullptr deallocate is a no-op") {
    MemoryPool pool(simple_tiers());
    pool.deallocate(nullptr, 64);
    pool.deallocate(nullptr, 100000);
    CHECK(true);
  }
}

// ---------------------------------------------------------------------------
// TEST SUITE: free-list reuse
// ---------------------------------------------------------------------------

TEST_SUITE("base-MemoryPool - free list reuse") {
  TEST_CASE("deallocated block is reused on the next allocation") {
    MemoryPool pool(simple_tiers());

    void* p = pool.allocate(64);
    pool.deallocate(p, 64);

    void* q = pool.allocate(64);
    CHECK(p == q);  // free list is LIFO.
    pool.deallocate(q, 64);
  }

  TEST_CASE("geometric growth: filling a tier needs sub-linear chunks") {
    // kInitialBlocksPerChunk=1, cap=4 -> chunks of 1,2,4 blocks = 7 total max.
    MemoryPool pool(make_config({{64, 4}}));

    std::vector<void*> blocks;
    for (int i = 0; i < 7; ++i) {
      blocks.push_back(pool.allocate(64));
    }

    auto stats = pool.get_stats();
    CHECK(stats[0].upstream_alloc_count <= 3u);
    CHECK(stats[0].chunk_count == stats[0].upstream_alloc_count);

    for (void* p : blocks) pool.deallocate(p, 64);
  }

  TEST_CASE("an additional alloc beyond the first chunk triggers a second chunk") {
    // The first chunk fills to its initial_chunk_blocks (capped at bpc=4); the
    // 5th allocation forces a second upstream chunk.
    MemoryPool pool(make_config({{64, 4}}));

    std::vector<void*> blocks;
    for (int i = 0; i < 5; ++i) {
      blocks.push_back(pool.allocate(64));
    }

    auto stats = pool.get_stats();
    CHECK(stats[0].upstream_alloc_count >= 2u);
    CHECK(stats[0].chunk_count == stats[0].upstream_alloc_count);

    for (void* p : blocks) pool.deallocate(p, 64);
  }

  TEST_CASE("blocks within a tier are unique pointers") {
    MemoryPool pool(make_config({{128, 64}}));

    std::unordered_set<void*> seen;
    std::vector<void*> blocks;

    constexpr int kCount = 64;
    for (int i = 0; i < kCount; ++i) {
      void* p = pool.allocate(64);
      CHECK(seen.insert(p).second);
      blocks.push_back(p);
    }

    for (void* p : blocks) pool.deallocate(p, 64);
  }
}

// ---------------------------------------------------------------------------
// TEST SUITE: stats & reset
// ---------------------------------------------------------------------------

TEST_SUITE("base-MemoryPool - stats") {
  TEST_CASE("hit_count, deallocate_count and in_use_blocks track allocations") {
    MemoryPool pool(simple_tiers());

    void* p = pool.allocate(64);
    void* q = pool.allocate(64);

    auto stats = pool.get_stats();
    CHECK(stats[0].hit_count == 2u);
    CHECK(stats[0].deallocate_count == 0u);
    CHECK(stats[0].in_use_blocks == 2u);

    pool.deallocate(p, 64);
    stats = pool.get_stats();
    CHECK(stats[0].deallocate_count == 1u);
    CHECK(stats[0].in_use_blocks == 1u);

    pool.deallocate(q, 64);
    stats = pool.get_stats();
    CHECK(stats[0].in_use_blocks == 0u);
  }

  TEST_CASE("block_size is rounded up to alignof(max_align_t)") {
    MemoryPool pool(make_config({{8, 4}, {24, 4}, {64, 4}}));

    auto stats = pool.get_stats();
    CHECK(stats[0].block_size >= sizeof(void*));
    CHECK(stats[0].block_size % alignof(std::max_align_t) == 0u);
    CHECK(stats[1].block_size % alignof(std::max_align_t) == 0u);
    CHECK(stats[2].block_size == 64u);
  }

  TEST_CASE("upstream_alloc_bytes accumulates chunk sizes") {
    MemoryPool pool(make_config({{128, 4}}));

    void* a = pool.allocate(128);
    void* b = pool.allocate(128);
    void* c = pool.allocate(128);
    void* d = pool.allocate(128);

    auto stats = pool.get_stats();
    CHECK(stats[0].upstream_alloc_count >= 1u);
    CHECK(stats[0].upstream_alloc_bytes >= stats[0].block_size * 4u);

    pool.deallocate(a, 128);
    pool.deallocate(b, 128);
    pool.deallocate(c, 128);
    pool.deallocate(d, 128);
  }

  TEST_CASE("reset_stats clears per-call counters but preserves physical pool state") {
    MemoryPool pool(make_config({{64, 4}}));

    void* p = pool.allocate(64);
    pool.deallocate(p, 64);

    auto before = pool.get_stats();
    REQUIRE(before[0].upstream_alloc_count > 0u);
    REQUIRE(before[0].hit_count > 0u);

    pool.reset_stats();

    auto after = pool.get_stats();
    CHECK(after[0].hit_count == 0u);
    CHECK(after[0].deallocate_count == 0u);
    CHECK(after[0].upstream_alloc_count == before[0].upstream_alloc_count);
    CHECK(after[0].upstream_alloc_bytes == before[0].upstream_alloc_bytes);
    CHECK(after[0].chunk_count == before[0].chunk_count);

    void* q = pool.allocate(64);
    pool.deallocate(q, 64);

    auto reused = pool.get_stats();
    // Served from existing chunk -- no new upstream allocation since the reset.
    CHECK(reused[0].upstream_alloc_count == before[0].upstream_alloc_count);
  }
}

// ---------------------------------------------------------------------------
// TEST SUITE: clear
// ---------------------------------------------------------------------------

TEST_SUITE("base-MemoryPool - clear") {
  TEST_CASE("clear with no live blocks releases every chunk") {
    MemoryPool pool(make_config({{64, 4}}));

    std::vector<void*> blocks;
    for (int i = 0; i < 7; ++i) {
      blocks.push_back(pool.allocate(64));
    }
    for (void* p : blocks) pool.deallocate(p, 64);

    auto before = pool.get_stats();
    REQUIRE(before[0].chunk_count > 0u);
    const uint64_t lifetime_allocs_before = before[0].upstream_alloc_count;
    const uint64_t lifetime_bytes_before = before[0].upstream_alloc_bytes;

    pool.clear();

    auto after = pool.get_stats();
    CHECK(after[0].chunk_count == 0u);
    CHECK(after[0].hit_count == before[0].hit_count);
    CHECK(after[0].deallocate_count == before[0].deallocate_count);
    CHECK(after[0].upstream_alloc_count == lifetime_allocs_before);
    CHECK(after[0].upstream_alloc_bytes == lifetime_bytes_before);

    // Pool is still usable: next allocation triggers a fresh chunk allocation
    // because all previous chunks were released.  next_chunk_blocks is NOT
    // reset, so the new chunk picks up at the geometric step the pool had
    // already reached (capped at blocks_per_chunk).
    void* q = pool.allocate(64);
    REQUIRE(q != nullptr);

    auto reused = pool.get_stats();
    CHECK(reused[0].chunk_count == 1u);
    CHECK(reused[0].upstream_alloc_count == lifetime_allocs_before + 1u);

    pool.deallocate(q, 64);
  }

  TEST_CASE("clear preserves chunks that still back a live allocation") {
    MemoryPool pool(make_config({{64, 4}}));

    // First fill several chunks, then return all but one block.
    std::vector<void*> blocks;
    for (int i = 0; i < 7; ++i) {
      blocks.push_back(pool.allocate(64));
    }

    void* live = blocks.back();
    blocks.pop_back();
    for (void* p : blocks) pool.deallocate(p, 64);

    const auto before = pool.get_stats();
    REQUIRE(before[0].chunk_count >= 1u);
    REQUIRE(before[0].in_use_blocks == 1u);
    const uint64_t chunks_before = before[0].chunk_count;

    pool.clear();

    const auto after = pool.get_stats();
    // Exactly one chunk -- the one backing 'live' -- must remain.
    CHECK(after[0].chunk_count >= 1u);
    CHECK(after[0].chunk_count < chunks_before);
    CHECK(after[0].in_use_blocks == 1u);
    CHECK(after[0].hit_count == before[0].hit_count);
    CHECK(after[0].deallocate_count == before[0].deallocate_count);
    // Lifetime upstream counters never decrease.
    CHECK(after[0].upstream_alloc_count == before[0].upstream_alloc_count);

    // The live pointer must still be writable -- its backing chunk was kept.
    std::memset(live, 0xEE, 64);
    pool.deallocate(live, 64);
  }

  TEST_CASE("clear leaves the surviving chunk's free nodes reusable") {
    // bpc=2 forces the third allocation to land in a second chunk: chunk #1
    // holds {a, b} and chunk #2 holds {c, free}.  Freeing 'c' empties chunk
    // #2 fully while 'b' keeps chunk #1 partially used; clear() must
    // therefore release chunk #2 and keep chunk #1 (with 'a's slot still
    // available for reuse).
    MemoryPool pool(make_config({{64, 2}}));

    void* a = pool.allocate(64);
    void* b = pool.allocate(64);
    void* c = pool.allocate(64);
    REQUIRE(a != nullptr);
    REQUIRE(b != nullptr);
    REQUIRE(c != nullptr);

    pool.deallocate(a, 64);  // chunk #1: 1 free, 1 live (b)
    pool.deallocate(c, 64);  // chunk #2: 2 free, 0 live -> fully free

    const auto before = pool.get_stats();
    REQUIRE(before[0].chunk_count >= 2u);
    REQUIRE(before[0].in_use_blocks == 1u);

    pool.clear();

    const auto after = pool.get_stats();
    // The fully-free 1-block chunk is released; the 2-block chunk stays.
    CHECK(after[0].chunk_count < before[0].chunk_count);
    CHECK(after[0].chunk_count >= 1u);
    CHECK(after[0].in_use_blocks == 1u);

    // The surviving chunk's free node must remain reusable.  We expect the
    // next allocate to NOT trigger a new chunk allocation, because the
    // surviving chunk still has a free slot.
    const uint64_t upstream_before = after[0].upstream_alloc_count;
    void* d = pool.allocate(64);
    REQUIRE(d != nullptr);

    const auto reused = pool.get_stats();
    CHECK(reused[0].upstream_alloc_count == upstream_before);

    pool.deallocate(b, 64);
    pool.deallocate(d, 64);
  }

  TEST_CASE("clear leaves oversized stats untouched") {
    MemoryPool pool(make_config({{64, 4}}));

    constexpr size_t kBig = 4096;
    void* p = pool.allocate(kBig);
    pool.deallocate(p, kBig);

    auto before = pool.get_oversized_stats();
    REQUIRE(before.alloc_count == 1u);
    REQUIRE(before.dealloc_count == 1u);

    pool.clear();

    auto after = pool.get_oversized_stats();
    CHECK(after.alloc_count == before.alloc_count);
    CHECK(after.alloc_bytes == before.alloc_bytes);
    CHECK(after.dealloc_count == before.dealloc_count);
  }

  TEST_CASE("clear on an empty pool is a no-op") {
    MemoryPool pool(make_config({{64, 4}}));
    pool.clear();
    pool.clear();

    auto stats = pool.get_stats();
    CHECK(stats[0].chunk_count == 0u);
    CHECK(stats[0].upstream_alloc_count == 0u);
  }

  TEST_CASE("clear with all blocks live keeps every chunk") {
    MemoryPool pool(make_config({{64, 4}}));

    std::vector<void*> blocks;
    for (int i = 0; i < 7; ++i) {
      blocks.push_back(pool.allocate(64));
    }

    const auto before = pool.get_stats();
    REQUIRE(before[0].in_use_blocks == 7u);
    const uint64_t chunks_before = before[0].chunk_count;

    pool.clear();

    const auto after = pool.get_stats();
    // Nothing released -- every chunk still holds at least one live block.
    CHECK(after[0].chunk_count == chunks_before);
    CHECK(after[0].in_use_blocks == 7u);

    // All seven pointers must still be safe to dereference and deallocate.
    for (void* p : blocks) {
      std::memset(p, 0xCC, 64);
      pool.deallocate(p, 64);
    }
  }

  TEST_CASE("clear with chunks count > stack buffer (heap spill path)") {
    // kStackSlots in clear() is 64; force >64 chunks via tight blocks_per_chunk.
    MemoryPool pool(make_config({{64, 1}}));

    constexpr int kAllocations = 200;
    std::vector<void*> blocks;
    blocks.reserve(kAllocations);
    for (int i = 0; i < kAllocations; ++i) {
      void* p = pool.allocate(64);
      REQUIRE(p != nullptr);
      blocks.push_back(p);
    }

    const auto before = pool.get_stats();
    REQUIRE(before[0].chunk_count > 64u);
    REQUIRE(before[0].in_use_blocks == static_cast<uint64_t>(kAllocations));

    // Free half: alternate keep / release so live and dead chunks interleave.
    for (size_t i = 0; i < blocks.size(); i += 2) {
      pool.deallocate(blocks[i], 64);
    }

    pool.clear();

    const auto after = pool.get_stats();
    // Released chunks are exactly the ones whose single block was freed.
    CHECK(after[0].chunk_count <= before[0].chunk_count);
    CHECK(after[0].chunk_count >= static_cast<uint64_t>(kAllocations) / 2u);
    CHECK(after[0].in_use_blocks == static_cast<uint64_t>(kAllocations) / 2u);

    for (size_t i = 1; i < blocks.size(); i += 2) {
      std::memset(blocks[i], 0x77, 64);
      pool.deallocate(blocks[i], 64);
    }
  }

  TEST_CASE("clear racing with concurrent allocate/deallocate keeps live blocks valid") {
    MemoryPool pool(MemoryPool::get_default_config());

    constexpr int kWorkers = 4;
    constexpr int kIterations = 2000;

    std::atomic<bool> stop{false};
    std::atomic<bool> error{false};

    auto worker = [&](int seed) {
      std::mt19937 rng(static_cast<uint32_t>(seed * 1234567u + 1u));
      std::uniform_int_distribution<size_t> size_dist(1, 4 * 1024);
      std::vector<std::pair<void*, size_t>> live;
      live.reserve(64);

      for (int i = 0; i < kIterations && !stop.load(std::memory_order_relaxed); ++i) {
        const size_t bytes = size_dist(rng);
        void* p = pool.allocate(bytes);
        if (p == nullptr) {
          error.store(true);
          return;
        }
        std::memset(p, static_cast<int>(seed & 0xFF), bytes);
        live.emplace_back(p, bytes);
        if (live.size() > 32 || (rng() & 1u)) {
          auto [ptr, sz] = live.back();
          live.pop_back();
          // Verify our pattern survived any concurrent clear() before freeing.
          for (size_t j = 0; j < sz; ++j) {
            if (static_cast<uint8_t*>(ptr)[j] != static_cast<uint8_t>(seed & 0xFF)) {
              error.store(true);
              break;
            }
          }
          pool.deallocate(ptr, sz);
        }
      }

      for (auto& [ptr, sz] : live) {
        pool.deallocate(ptr, sz);
      }
    };

    std::vector<std::thread> threads;
    threads.reserve(kWorkers);
    for (int i = 0; i < kWorkers; ++i) {
      threads.emplace_back(worker, i + 1);
    }

    // Periodically clear() while workers are running.
    for (int round = 0; round < 50; ++round) {
      std::this_thread::sleep_for(std::chrono::microseconds(200));
      pool.clear();
    }

    stop.store(true, std::memory_order_relaxed);
    for (auto& t : threads) t.join();

    CHECK_FALSE(error.load());

    auto stats = pool.get_stats();
    uint64_t hits = 0;
    uint64_t deallocs = 0;
    for (const auto& s : stats) {
      hits += s.hit_count;
      deallocs += s.deallocate_count;
    }
    CHECK(hits == deallocs);
  }
}

// ---------------------------------------------------------------------------
// TEST SUITE: geometric chunk growth
// ---------------------------------------------------------------------------

TEST_SUITE("base-MemoryPool - geometric chunk growth") {
  TEST_CASE("upstream_alloc_count grows sub-linearly with block count") {
    // Cap at 32 -> chunks of 1,2,4,8,16,32 = 63 covers 63 blocks in 6 chunks.
    MemoryPool pool(make_config({{64, 32}}));

    std::vector<void*> blocks;
    for (int i = 0; i < 63; ++i) {
      blocks.push_back(pool.allocate(64));
    }

    auto stats = pool.get_stats();
    CHECK(stats[0].upstream_alloc_count <= 6u);
    CHECK(stats[0].chunk_count == stats[0].upstream_alloc_count);

    for (void* p : blocks) pool.deallocate(p, 64);
  }

  TEST_CASE("blocks_per_chunk acts as a hard cap") {
    MemoryPool pool(make_config({{64, 4}}));

    std::vector<void*> blocks;
    for (int i = 0; i < 32; ++i) {
      blocks.push_back(pool.allocate(64));
    }

    auto stats = pool.get_stats();
    // chunks: 1, 2, 4, 4, 4, 4, 4, 4, 4 -> at least 8 chunks for 31 blocks at cap 4.
    CHECK(stats[0].upstream_alloc_count >= 8u);

    for (void* p : blocks) pool.deallocate(p, 64);
  }
}

// ---------------------------------------------------------------------------
// TEST SUITE: large-tier coverage (LiDAR / 8 MP NV12)
// ---------------------------------------------------------------------------

TEST_SUITE("base-MemoryPool - large frames") {
  TEST_CASE("4 MiB-sized allocation routes to the largest pooled tier at L4") {
    // L3 default leaves 4 MiB sentinel; L4 is the first level where the 4 MiB
    // tier becomes live (count=2), so a 3 MiB request actually hits it.
    MemoryPool pool(4);

    constexpr size_t k_size = 3u * 1024u * 1024u;  // 3 MiB, fits in the 4 MiB tier
    static_assert(k_size <= 4u * 1024u * 1024u, "must fit in 4 MiB tier");

    void* p = pool.allocate(k_size);
    CHECK(p != nullptr);

    std::memset(p, 0xAB, k_size);
    CHECK(static_cast<uint8_t*>(p)[k_size - 1] == 0xAB);

    CHECK(pool.get_oversized_stats().alloc_count == 0u);

    pool.deallocate(p, k_size);
  }

  TEST_CASE("1 MiB-sized allocation hits the top live tier at L3 default") {
    MemoryPool pool(3);

    constexpr size_t k_size = 1u * 1024u * 1024u;  // top live tier at L3
    void* p = pool.allocate(k_size);
    CHECK(p != nullptr);

    std::memset(p, 0xCD, k_size);
    CHECK(static_cast<uint8_t*>(p)[k_size - 1] == 0xCD);

    CHECK(pool.get_oversized_stats().alloc_count == 0u);

    pool.deallocate(p, k_size);
  }

  TEST_CASE("8 MP NV12 image (~12 MB) takes the oversized path at L3") {
    MemoryPool pool(3);

    constexpr size_t k8mp_nv12 = 8'000'000u * 3u / 2u;  // 12 MB
    static_assert(k8mp_nv12 > 1u * 1024u * 1024u, "must overflow the L3 1 MiB top live tier");

    void* p = pool.allocate(k8mp_nv12);
    CHECK(p != nullptr);

    CHECK(pool.get_oversized_stats().alloc_count == 1u);
    CHECK(pool.get_oversized_stats().alloc_bytes == k8mp_nv12);

    pool.deallocate(p, k8mp_nv12);
  }

  TEST_CASE("4K RGBA-sized request uses the oversized path at L3") {
    MemoryPool pool(3);

    constexpr size_t k4k_rgba = 3840u * 2160u * 4u;  // ~31.6 MiB
    static_assert(k4k_rgba > 1u * 1024u * 1024u, "test premise -- must overflow the L3 1 MiB top live tier");

    void* p = pool.allocate(k4k_rgba);
    CHECK(p != nullptr);

    CHECK(pool.get_oversized_stats().alloc_count == 1u);
    CHECK(pool.get_oversized_stats().alloc_bytes == k4k_rgba);

    pool.deallocate(p, k4k_rgba);
  }
}

TEST_SUITE("base-MemoryPool - VLINK_MEMORY_LEVEL") {
  TEST_CASE("get_default_config respects the cached level (1..6 selects a known row)") {
    const auto& tiers = MemoryPool::get_default_config().tiers;

    REQUIRE(tiers.size() >= 4u);
    CHECK(tiers.front().max_size == 32u);
    CHECK(tiers.front().blocks_per_chunk > 0u);

    CHECK(tiers.size() == 19u);
    CHECK(tiers.back().max_size == 16u * 1024u * 1024u);
  }

  TEST_CASE("32 B head tier carries exactly 2x the blocks_per_chunk of the 64 B tier") {
    // The 32 B tier is sized at double the 64 B count on every active level
    // to absorb high-density tiny allocations; verify the invariant on a few
    // representative non-bypass levels.
    for (int level : {1, 3, 5, 9}) {
      MemoryPool pool(level);
      auto stats = pool.get_stats();
      REQUIRE(stats.size() >= 2u);
      REQUIRE(stats[0].max_size == 32u);
      REQUIRE(stats[1].max_size == 64u);
      CHECK(stats[0].blocks_per_chunk == 2u * stats[1].blocks_per_chunk);
    }
  }

  TEST_CASE("L3 default leaves the 4/8/16 MiB tiers as sentinels (1 MiB top live tier)") {
    MemoryPool pool(3);
    auto stats = pool.get_stats();
    // 16 live tiers: 32 B .. 1 MiB.
    CHECK(stats.size() == 16u);
    CHECK(stats.back().max_size == 1u * 1024u * 1024u);
  }
}

// ---------------------------------------------------------------------------
// TEST SUITE: concurrent stress
// ---------------------------------------------------------------------------

TEST_SUITE("base-MemoryPool - concurrency") {
  TEST_CASE("concurrent cold allocations all succeed and stay bounded") {
    MemoryPool pool(make_config({{64, 64}}));

    constexpr int kThreads = 32;

    std::atomic<int> ready{0};
    std::atomic<bool> start{false};
    std::vector<void*> blocks(static_cast<size_t>(kThreads), nullptr);

    auto worker = [&](int index) {
      ready.fetch_add(1, std::memory_order_release);
      while (!start.load(std::memory_order_acquire)) {
        std::this_thread::yield();
      }

      blocks[static_cast<size_t>(index)] = pool.allocate(64);
    };

    std::vector<std::thread> threads;
    threads.reserve(kThreads);
    for (int i = 0; i < kThreads; ++i) {
      threads.emplace_back(worker, i);
    }

    while (ready.load(std::memory_order_acquire) != kThreads) {
      std::this_thread::yield();
    }

    start.store(true, std::memory_order_release);

    for (auto& t : threads) t.join();

    auto stats = pool.get_stats();

    CHECK(stats[0].upstream_alloc_count <= static_cast<uint64_t>(kThreads));
    // upstream_alloc_count counts every successful ::operator new (including
    // chunks discarded by the concurrent-grow race), whereas chunk_count is
    // the current owned chunks.  In a race the former can exceed the latter.
    CHECK(stats[0].chunk_count <= stats[0].upstream_alloc_count);
    CHECK(stats[0].chunk_count >= 1u);
    CHECK(stats[0].upstream_alloc_bytes >= 64u);

    for (void* p : blocks) {
      REQUIRE(p != nullptr);
      pool.deallocate(p, 64);
    }
  }

  TEST_CASE("many threads alloc/dealloc without races or corruption") {
    MemoryPool pool(MemoryPool::get_default_config());

    constexpr int kThreads = 8;
    constexpr int kIterations = 4'000;

    std::atomic<bool> error{false};

    auto worker = [&](int seed) {
      std::mt19937 rng(static_cast<uint32_t>(seed));
      std::uniform_int_distribution<size_t> size_dist(1, 8 * 1024);

      std::vector<std::pair<void*, size_t>> live;
      live.reserve(64);

      for (int i = 0; i < kIterations; ++i) {
        const size_t bytes = size_dist(rng);
        void* p = pool.allocate(bytes);

        if (p == nullptr) {
          error.store(true);
          return;
        }

        std::memset(p, static_cast<int>(seed & 0xFF), bytes);

        live.emplace_back(p, bytes);

        if (live.size() > 32 || (rng() & 1u)) {
          auto [ptr, sz] = live.back();
          live.pop_back();
          pool.deallocate(ptr, sz);
        }
      }

      for (auto& [ptr, sz] : live) {
        pool.deallocate(ptr, sz);
      }
    };

    std::vector<std::thread> threads;
    threads.reserve(kThreads);
    for (int i = 0; i < kThreads; ++i) {
      threads.emplace_back(worker, i + 1);
    }
    for (auto& t : threads) t.join();

    CHECK_FALSE(error.load());

    auto stats = pool.get_stats();
    uint64_t total_hits_v = 0;
    uint64_t total_deallocs = 0;
    for (const auto& s : stats) {
      total_hits_v += s.hit_count;
      total_deallocs += s.deallocate_count;
    }
    auto over = pool.get_oversized_stats();

    CHECK(total_hits_v + over.alloc_count >= static_cast<uint64_t>(kThreads * kIterations));
    CHECK(total_hits_v == total_deallocs);
    CHECK(over.alloc_count == over.dealloc_count);
  }
}

// NOLINTEND
