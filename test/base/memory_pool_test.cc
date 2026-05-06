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

using vlink::MemoryPool;

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static std::vector<MemoryPool::Tier> simple_tiers() {
  return {
      {64, 4},
      {256, 4},
      {1024, 2},
  };
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

  TEST_CASE("default_tiers (level 3) is well-formed and ends at 12 MiB") {
    auto tiers = MemoryPool::default_tiers();

    CHECK(tiers.size() == 8u);
    CHECK(tiers.front().max_size == 128u);
    CHECK(tiers.back().max_size == 12u * 1024u * 1024u);

    for (size_t i = 1; i < tiers.size(); ++i) {
      CHECK(tiers[i].max_size > tiers[i - 1].max_size);
      CHECK(tiers[i].blocks_per_chunk > 0u);
    }
  }

  TEST_CASE("construct from default_tiers") {
    auto tiers = MemoryPool::default_tiers();
    CHECK_NOTHROW(MemoryPool{tiers});
  }

  TEST_CASE("empty tier list falls back to level-3 defaults") {
    MemoryPool pool({});
    auto stats = pool.get_stats();
    CHECK(stats.size() >= 4u);
    CHECK(stats.front().max_size == 128u);
    CHECK(stats.back().max_size == 12u * 1024u * 1024u);
  }

  TEST_CASE("default-constructed pool uses level-3 defaults") {
    MemoryPool pool;
    CHECK(pool.get_tier_count() >= 4u);
  }

  TEST_CASE("zero max_size triggers default-pyramid fallback") {
    std::vector<MemoryPool::Tier> tiers = {{0, 4}};
    MemoryPool pool(tiers);
    CHECK(pool.get_tier_count() >= 4u);
  }

  TEST_CASE("zero blocks_per_chunk triggers default-pyramid fallback") {
    std::vector<MemoryPool::Tier> tiers = {{64, 0}};
    MemoryPool pool(tiers);
    CHECK(pool.get_tier_count() >= 4u);
  }

  TEST_CASE("non-monotonic ordering triggers default-pyramid fallback") {
    std::vector<MemoryPool::Tier> tiers = {{256, 4}, {64, 4}};
    MemoryPool pool(tiers);
    CHECK(pool.get_tier_count() >= 4u);
  }

  TEST_CASE("duplicate max_size triggers default-pyramid fallback") {
    std::vector<MemoryPool::Tier> tiers = {{64, 4}, {64, 4}};
    MemoryPool pool(tiers);
    CHECK(pool.get_tier_count() >= 4u);
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
    MemoryPool pool({{64, 4}});

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
    MemoryPool pool({{64, 4}});

    std::vector<void*> blocks;
    for (int i = 0; i < 3; ++i) {
      blocks.push_back(pool.allocate(64));
    }

    auto stats = pool.get_stats();
    CHECK(stats[0].upstream_alloc_count >= 2u);
    CHECK(stats[0].chunk_count == stats[0].upstream_alloc_count);

    for (void* p : blocks) pool.deallocate(p, 64);
  }

  TEST_CASE("blocks within a tier are unique pointers") {
    MemoryPool pool({{128, 64}});

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
    MemoryPool pool({{8, 4}, {24, 4}, {64, 4}});

    auto stats = pool.get_stats();
    CHECK(stats[0].block_size >= sizeof(void*));
    CHECK(stats[0].block_size % alignof(std::max_align_t) == 0u);
    CHECK(stats[1].block_size % alignof(std::max_align_t) == 0u);
    CHECK(stats[2].block_size == 64u);
  }

  TEST_CASE("upstream_alloc_bytes accumulates chunk sizes") {
    MemoryPool pool({{128, 4}});

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
    MemoryPool pool({{64, 4}});

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
// TEST SUITE: geometric chunk growth
// ---------------------------------------------------------------------------

TEST_SUITE("base-MemoryPool - geometric chunk growth") {
  TEST_CASE("upstream_alloc_count grows sub-linearly with block count") {
    // Cap at 32 -> chunks of 1,2,4,8,16,32 = 63 covers 63 blocks in 6 chunks.
    MemoryPool pool({{64, 32}});

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
    MemoryPool pool({{64, 4}});

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
  TEST_CASE("4 MiB-sized allocation routes to the largest pooled tier") {
    MemoryPool pool(MemoryPool::default_tiers());

    constexpr size_t k_size = 3u * 1024u * 1024u;  // 3 MiB, fits in the 4 MiB tier
    static_assert(k_size <= 4u * 1024u * 1024u, "must fit in 4 MiB tier");

    void* p = pool.allocate(k_size);
    CHECK(p != nullptr);

    std::memset(p, 0xAB, k_size);
    CHECK(static_cast<uint8_t*>(p)[k_size - 1] == 0xAB);

    CHECK(pool.get_oversized_stats().alloc_count == 0u);

    pool.deallocate(p, k_size);
  }

  TEST_CASE("8 MP NV12 image (~12 MB) fits in the largest tier") {
    MemoryPool pool(MemoryPool::default_tiers());

    constexpr size_t k8mp_nv12 = 8'000'000u * 3u / 2u;  // 12 MB
    static_assert(k8mp_nv12 < 12u * 1024u * 1024u, "must fit in the 12 MiB tier");

    void* p = pool.allocate(k8mp_nv12);
    CHECK(p != nullptr);
    std::memset(p, 0xCD, 16);

    CHECK(pool.get_oversized_stats().alloc_count == 0u);
    CHECK(pool.get_stats().back().hit_count == 1u);

    pool.deallocate(p, k8mp_nv12);
  }

  TEST_CASE("4K RGBA-sized request uses the oversized path") {
    MemoryPool pool(MemoryPool::default_tiers());

    constexpr size_t k4k_rgba = 3840u * 2160u * 4u;  // ~33 MiB
    static_assert(k4k_rgba > 12u * 1024u * 1024u, "test premise");

    void* p = pool.allocate(k4k_rgba);
    CHECK(p != nullptr);

    CHECK(pool.get_oversized_stats().alloc_count == 1u);
    CHECK(pool.get_oversized_stats().alloc_bytes == k4k_rgba);

    pool.deallocate(p, k4k_rgba);
  }
}

TEST_SUITE("base-MemoryPool - VLINK_MEMORY_LEVEL") {
  TEST_CASE("default_tiers respects the cached level (1..6 selects a known row)") {
    auto tiers = MemoryPool::default_tiers();

    REQUIRE(tiers.size() >= 4u);
    CHECK(tiers.front().max_size == 128u);
    CHECK(tiers.front().blocks_per_chunk > 0u);

    // Every level shares the same 8-tier shape; the largest tier is always 12 MiB.
    CHECK(tiers.size() == 8u);
    CHECK(tiers.back().max_size == 12u * 1024u * 1024u);
  }

  TEST_CASE("largest tier never produces a chunk above the 32 MiB cap") {
    auto tiers = MemoryPool::default_tiers();
    const auto& largest = tiers.back();
    constexpr size_t kChunkCap = 32u * 1024u * 1024u;
    CHECK(largest.blocks_per_chunk * largest.max_size <= kChunkCap);
  }
}

// ---------------------------------------------------------------------------
// TEST SUITE: concurrent stress
// ---------------------------------------------------------------------------

TEST_SUITE("base-MemoryPool - concurrency") {
  TEST_CASE("concurrent cold allocations all succeed and stay bounded") {
    MemoryPool pool({{64, 64}});

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
    CHECK(stats[0].chunk_count == stats[0].upstream_alloc_count);
    CHECK(stats[0].upstream_alloc_bytes >= 64u);

    for (void* p : blocks) {
      REQUIRE(p != nullptr);
      pool.deallocate(p, 64);
    }
  }

  TEST_CASE("many threads alloc/dealloc without races or corruption") {
    MemoryPool pool(MemoryPool::default_tiers());

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
