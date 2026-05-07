/*
 * Copyright (C) 2026 by Thun Lu. All rights reserved.
 * Author: Thun Lu <thun.lu@zohomail.cn>
 * Repo:   https://github.com/thun-res/guidelink
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

#include <vlink/base/logger.h>
#include <vlink/base/memory_pool.h>
#include <vlink/base/memory_resource.h>

#include <cstdint>
#include <memory_resource>
#include <string>
#include <vector>

namespace {

void print_pool_summary(const char* tag, vlink::MemoryPool& pool) {
  uint64_t hits = 0;
  uint64_t deallocs = 0;
  for (const auto& s : pool.get_stats()) {
    hits += s.hit_count;
    deallocs += s.deallocate_count;
  }
  auto over = pool.get_oversized_stats();
  MLOG_I("  {}: tiers={} total_hits={} total_deallocs={} oversized_alloc={} oversized_dealloc={}", tag,
         pool.get_tier_count(), hits, deallocs, over.alloc_count, over.dealloc_count);
}

}  // namespace

int main() {
  // ---------------------------------------------------------------
  // 1. global_instance() shares the same backing pool as Bytes.
  // ---------------------------------------------------------------
  {
    VLOG_I("=== Section 1: MemoryResource::global_instance() ===");

    auto& res = vlink::MemoryResource::global_instance();
    auto& pool = vlink::MemoryPool::global_instance();
    MLOG_I("  shared with MemoryPool::global_instance(): {}", &res.get_memory_pool() == &pool);

    std::pmr::vector<int> v(&res);
    v.reserve(1024);
    for (int i = 0; i < 200; ++i) {
      v.push_back(i);
    }
    MLOG_I("  pmr::vector size={} front={} back={}", v.size(), v.front(), v.back());

    print_pool_summary("global_pool after pmr::vector", pool);
  }

  // ---------------------------------------------------------------
  // 2. Private resource owning a level-3 pyramid pool.
  // ---------------------------------------------------------------
  {
    VLOG_I("=== Section 2: MemoryResource(level=3) -- private pool ===");

    vlink::MemoryResource res(3);

    std::pmr::polymorphic_allocator<char> alloc(&res);
    std::pmr::string s("hello vlink memory resource", alloc);
    MLOG_I("  pmr::string = '{}'", std::string(s.data(), s.size()));

    print_pool_summary("private_level3_pool", res.get_memory_pool());
  }

  // ---------------------------------------------------------------
  // 3. Private resource with custom tier configuration.
  // ---------------------------------------------------------------
  {
    VLOG_I("=== Section 3: MemoryResource(custom tiers) ===");

    vlink::MemoryPool::Config cfg;
    cfg.tiers = {
        {64U, 16U},
        {1024U, 4U},
    };
    cfg.prealloc = true;  // fill every tier to full blocks_per_chunk quota up-front
    vlink::MemoryResource res(cfg);

    std::pmr::vector<double> w(&res);
    w.reserve(64);
    for (int i = 0; i < 32; ++i) {
      w.push_back(static_cast<double>(i));
    }
    MLOG_I("  pmr::vector<double> size={} back={}", w.size(), w.back());

    print_pool_summary("private_custom_pool", res.get_memory_pool());
  }

  // ---------------------------------------------------------------
  // 4. Bypass resource (default constructor / level 0).
  //    Every allocate -> ::operator new; tier counts stay 0.
  // ---------------------------------------------------------------
  {
    VLOG_I("=== Section 4: MemoryResource() -- bypass mode ===");

    vlink::MemoryResource res;  // no-arg ctor = bypass
    MLOG_I("  bypass tier_count = {} (expect 0)", res.get_memory_pool().get_tier_count());

    std::pmr::vector<int> v(&res);
    v.reserve(64);
    for (int i = 0; i < 32; ++i) {
      v.push_back(i);
    }

    print_pool_summary("bypass_pool", res.get_memory_pool());
  }

  // ---------------------------------------------------------------
  // 5. is_equal: each private resource owns a distinct underlying pool.
  // ---------------------------------------------------------------
  {
    VLOG_I("=== Section 5: do_is_equal across resources ===");

    vlink::MemoryResource a(3);
    vlink::MemoryResource b(3);
    auto& g = vlink::MemoryResource::global_instance();

    MLOG_I("  a.is_equal(a) = {}", a.is_equal(a));
    MLOG_I("  a.is_equal(b) = {} (private pools are distinct)", a.is_equal(b));
    MLOG_I("  a.is_equal(global) = {}", a.is_equal(g));
    MLOG_I("  global.is_equal(global) = {}", g.is_equal(g));
  }

  VLOG_I("MemoryResource example finished.");
  return 0;
}
