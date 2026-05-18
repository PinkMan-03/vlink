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

#include "./base/memory_resource.h"

#include <doctest/doctest.h>

#include <cstddef>
#include <cstdint>
#include <vector>

#include "./base/bytes.h"
#include "./base/memory_pool.h"

//
#include "../common_test.h"

#ifdef VLINK_ENABLE_BASE_MEMORY_RESOURCE

#include <memory_resource>

// ---------------------------------------------------------------------------
// TEST SUITE: construction
// ---------------------------------------------------------------------------

TEST_SUITE("base-MemoryResource - construction") {
  TEST_CASE("default constructor owns a bypass pool (every alloc -> oversized)") {
    MemoryResource res;

    MemoryPool& pool = res.get_memory_pool();
    CHECK(pool.get_tier_count() == 0u);

    void* p = res.allocate(64);
    REQUIRE(p != nullptr);

    auto over = pool.get_oversized_stats();
    CHECK(over.alloc_count == 1u);
    CHECK(over.alloc_bytes == 64u);

    res.deallocate(p, 64);
    over = pool.get_oversized_stats();
    CHECK(over.dealloc_count == 1u);
  }

  TEST_CASE("MemoryResource(level=3) owns the level-3 pyramid pool") {
    MemoryResource res(3);

    MemoryPool& pool = res.get_memory_pool();
    REQUIRE(pool.get_tier_count() >= 4u);

    auto stats_before = pool.get_stats();
    CHECK(stats_before.front().max_size == 32u);
    CHECK(stats_before.at(1).max_size == 64u);

    void* p = res.allocate(64);
    REQUIRE(p != nullptr);

    auto stats = pool.get_stats();
    CHECK(stats[0].hit_count == 0u);
    CHECK(stats[1].hit_count == 1u);
    CHECK(pool.get_oversized_stats().alloc_count == 0u);

    res.deallocate(p, 64);
  }

  TEST_CASE("MemoryResource(level=0) owns a bypass pool") {
    MemoryResource res(0);

    CHECK(res.get_memory_pool().get_tier_count() == 0u);

    void* p = res.allocate(64);
    REQUIRE(p != nullptr);
    CHECK(res.get_memory_pool().get_oversized_stats().alloc_count == 1u);
    res.deallocate(p, 64);
  }

  TEST_CASE("MemoryResource(custom config) routes to the matching tier") {
    MemoryPool::Config cfg;
    cfg.tiers = {{64, 4}, {256, 4}, {1024, 2}};
    MemoryResource res(cfg);

    void* a = res.allocate(48);
    void* b = res.allocate(200);
    void* c = res.allocate(900);

    auto stats = res.get_memory_pool().get_stats();
    CHECK(stats[0].hit_count == 1u);
    CHECK(stats[1].hit_count == 1u);
    CHECK(stats[2].hit_count == 1u);

    res.deallocate(a, 48);
    res.deallocate(b, 200);
    res.deallocate(c, 900);
  }
}

// ---------------------------------------------------------------------------
// TEST SUITE: global_instance() shares the Bytes pool
// ---------------------------------------------------------------------------

TEST_SUITE("base-MemoryResource - global instance") {
  TEST_CASE("global_instance() aliases MemoryPool::global_instance()") {
    MemoryResource& r1 = MemoryResource::global_instance();
    MemoryResource& r2 = MemoryResource::global_instance();

    CHECK(&r1 == &r2);
    CHECK(&r1.get_memory_pool() == &MemoryPool::global_instance());
  }

  TEST_CASE("allocations via global_instance() show on the global pool's stats") {
    MemoryResource& res = MemoryResource::global_instance();
    MemoryPool& pool = MemoryPool::global_instance();

    auto over_before = pool.get_oversized_stats();

    constexpr size_t kHuge = 32u * 1024u * 1024u;  // larger than the L3 default 1 MiB top live tier
    void* p = res.allocate(kHuge);
    REQUIRE(p != nullptr);

    auto over_after = pool.get_oversized_stats();
    CHECK(over_after.alloc_count == over_before.alloc_count + 1u);
    CHECK(over_after.alloc_bytes >= over_before.alloc_bytes + kHuge);

    res.deallocate(p, kHuge);
  }

  TEST_CASE("global_instance() shares the same pool used by Bytes::bytes_malloc") {
    MemoryResource& res = MemoryResource::global_instance();

    auto over_before = MemoryPool::global_instance().get_oversized_stats();

    constexpr size_t kHuge = 32u * 1024u * 1024u;
    uint8_t* via_bytes = vlink::Bytes::bytes_malloc(kHuge);
    REQUIRE(via_bytes != nullptr);

    auto over_after_bytes = MemoryPool::global_instance().get_oversized_stats();
    CHECK(over_after_bytes.alloc_count == over_before.alloc_count + 1u);

    vlink::Bytes::bytes_free(via_bytes, kHuge);

    void* via_res = res.allocate(kHuge);
    REQUIRE(via_res != nullptr);
    auto over_after_res = MemoryPool::global_instance().get_oversized_stats();
    CHECK(over_after_res.alloc_count == over_after_bytes.alloc_count + 1u);

    res.deallocate(via_res, kHuge);
  }
}

// ---------------------------------------------------------------------------
// TEST SUITE: pmr integration
// ---------------------------------------------------------------------------

TEST_SUITE("base-MemoryResource - pmr integration") {
  TEST_CASE("std::pmr::vector<int> routes through the resource") {
    MemoryResource res(3);
    MemoryPool& pool = res.get_memory_pool();

    {
      std::pmr::vector<int> v(&res);
      v.reserve(256);
      for (int i = 0; i < 200; ++i) {
        v.push_back(i);
      }

      uint64_t total_hits = 0;
      for (const auto& s : pool.get_stats()) {
        total_hits += s.hit_count;
      }
      CHECK(total_hits >= 1u);

      CHECK(v.front() == 0);
      CHECK(v.back() == 199);
    }

    uint64_t total_hits = 0;
    uint64_t total_deallocs = 0;
    for (const auto& s : pool.get_stats()) {
      total_hits += s.hit_count;
      total_deallocs += s.deallocate_count;
    }
    CHECK(total_hits == total_deallocs);
  }

  TEST_CASE("polymorphic_allocator works with the resource") {
    MemoryResource res(3);

    std::pmr::polymorphic_allocator<char> alloc(&res);
    char* p = alloc.allocate(128);
    REQUIRE(p != nullptr);

    for (size_t i = 0; i < 128; ++i) {
      p[i] = static_cast<char>(i);
    }
    CHECK(p[0] == 0);
    CHECK(p[127] == 127);

    alloc.deallocate(p, 128);
  }
}

// ---------------------------------------------------------------------------
// TEST SUITE: do_is_equal
// ---------------------------------------------------------------------------

TEST_SUITE("base-MemoryResource - is_equal") {
  TEST_CASE("two private resources are unequal (distinct underlying pools)") {
    MemoryResource a(3);
    MemoryResource b(3);

    CHECK_FALSE(a.is_equal(b));
    CHECK_FALSE(b.is_equal(a));
  }

  TEST_CASE("a resource is equal to itself") {
    MemoryResource a(3);
    CHECK(a.is_equal(a));
  }

  TEST_CASE("global_instance() is equal to itself") {
    MemoryResource& g1 = MemoryResource::global_instance();
    MemoryResource& g2 = MemoryResource::global_instance();
    CHECK(g1.is_equal(g2));
  }

  TEST_CASE("a private resource is not equal to global_instance()") {
    MemoryResource a(3);
    MemoryResource& g = MemoryResource::global_instance();
    CHECK_FALSE(a.is_equal(g));
  }

  TEST_CASE("not equal to a non-MemoryResource memory_resource") {
    MemoryResource a;
    auto* sys = std::pmr::new_delete_resource();
    CHECK_FALSE(a.is_equal(*sys));
  }
}

#endif

// ---------------------------------------------------------------------------
// NOTE: do_allocate's std::bad_alloc path is exercised only when the
// underlying MemoryPool returns nullptr, which requires the upstream
// ::operator new to OOM. Reproducing that reliably from a unit test would
// require either heap exhaustion or a mock allocator; both are intentionally
// skipped here. The behaviour is covered by the pmr contract assertion in
// the implementation.
// ---------------------------------------------------------------------------

// NOLINTEND
