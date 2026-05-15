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

// Example: ObjectPool - pre-allocated object reuse with RAII and reset policies

#include <vlink/base/logger.h>
#include <vlink/base/object_pool.h>

#include <stdexcept>
#include <string>
#include <vector>

#include "pooled_buffer.h"

using pooled_buffer::Buffer;

int main() {
  // ---------------------------------------------------------------
  // 1. Basic pool with RAII (get / unique_ptr).
  // ---------------------------------------------------------------
  {
    VLOG_I("=== Section 1: RAII get() ===");
    auto pool = std::make_shared<vlink::ObjectPool<Buffer>>([]() { return std::make_unique<Buffer>(4096); },  // factory
                                                            2,                             // initial_size
                                                            8,                             // max_size
                                                            [](Buffer& b) { b.reset(); },  // reset callback
                                                            vlink::ObjectPool<Buffer>::kPolicyRelease);

    MLOG_I("  Initial: pool_size={}, borrowed={}, total={}", pool->size(), pool->borrowed(), pool->total_created());

    {
      auto buf = pool->get();
      buf->used = 100;
      MLOG_I("  Acquired buffer, used={}", buf->used);
      MLOG_I("  During use: pool_size={}, borrowed={}", pool->size(), pool->borrowed());
    }

    MLOG_I("  After return: pool_size={}, borrowed={}", pool->size(), pool->borrowed());
  }

  // ---------------------------------------------------------------
  // 2. Shared pointer mode (get_shared).
  // ---------------------------------------------------------------
  {
    VLOG_I("=== Section 2: get_shared() ===");
    auto pool = std::make_shared<vlink::ObjectPool<Buffer>>([]() { return std::make_unique<Buffer>(2048); }, 1, 4);

    {
      auto shared_buf = pool->get_shared();
      shared_buf->used = 50;

      auto copy = shared_buf;  // NOLINT(performance-unnecessary-copy-initialization)
      MLOG_I("  shared_ptr use_count: {}", copy.use_count());
    }

    MLOG_I("  After shared release: pool_size={}", pool->size());
  }

  // ---------------------------------------------------------------
  // 3. Manual borrow / give_back.
  // ---------------------------------------------------------------
  {
    VLOG_I("=== Section 3: borrow / give_back ===");
    auto pool = std::make_shared<vlink::ObjectPool<Buffer>>([]() { return std::make_unique<Buffer>(512); }, 0, 4);

    Buffer* raw = pool->borrow();
    raw->used = 256;
    MLOG_I("  Borrowed buffer, used={}", raw->used);
    MLOG_I("  During borrow: borrowed={}", pool->borrowed());

    pool->give_back(raw);
    MLOG_I("  After give_back: pool_size={}, borrowed={}", pool->size(), pool->borrowed());
  }

  // ---------------------------------------------------------------
  // 4. Reset policies.
  // ---------------------------------------------------------------
  {
    VLOG_I("=== Section 4: Reset policies ===");

    auto pool_acq = std::make_shared<vlink::ObjectPool<Buffer>>([]() { return std::make_unique<Buffer>(128); }, 1, 4,
                                                                [](Buffer& b) { b.reset(); },
                                                                vlink::ObjectPool<Buffer>::kPolicyAcquire);

    auto buf = pool_acq->get();
    MLOG_I("  kPolicyAcquire: buffer.used={} (reset on acquire)", buf->used);

    auto pool_both = std::make_shared<vlink::ObjectPool<Buffer>>([]() { return std::make_unique<Buffer>(128); }, 1, 4,
                                                                 [](Buffer& b) { b.reset(); },
                                                                 vlink::ObjectPool<Buffer>::kPolicyBoth);

    {
      auto buf2 = pool_both->get();
      buf2->used = 42;
    }
    MLOG_I("  kPolicyBoth: object reset on both acquire and release");

    auto pool_none = std::make_shared<vlink::ObjectPool<Buffer>>([]() { return std::make_unique<Buffer>(128); }, 1, 4,
                                                                 nullptr, vlink::ObjectPool<Buffer>::kPolicyNone);

    MLOG_I("  kPolicyNone: no reset callback invoked");
  }

  // ---------------------------------------------------------------
  // 5. Pool statistics.
  // ---------------------------------------------------------------
  {
    VLOG_I("=== Section 5: Pool statistics ===");
    auto pool = std::make_shared<vlink::ObjectPool<Buffer>>([]() { return std::make_unique<Buffer>(256); }, 3, 10);

    auto stats = pool->stats();
    MLOG_I("  pool_size: {}", stats.pool_size);
    MLOG_I("  borrowed: {}", stats.borrowed);
    MLOG_I("  total_created: {}", stats.total_created);
    MLOG_I("  max_size: {}", stats.max_size);

    std::vector<std::unique_ptr<Buffer, vlink::ObjectPool<Buffer>::PoolDeleter>> handles;
    for (int i = 0; i < 5; ++i) {
      // NOLINTNEXTLINE(performance-inefficient-vector-operation)
      handles.emplace_back(pool->get());
    }

    stats = pool->stats();
    MLOG_I("  After 5 borrows: pool_size={}, borrowed={}, total={}", stats.pool_size, stats.borrowed,
           stats.total_created);

    handles.clear();
    stats = pool->stats();
    MLOG_I("  After return all: pool_size={}, borrowed={}", stats.pool_size, stats.borrowed);
  }

  // ---------------------------------------------------------------
  // 6. Pool exhaustion.
  // ---------------------------------------------------------------
  {
    VLOG_I("=== Section 6: Pool exhaustion ===");
    auto pool = std::make_shared<vlink::ObjectPool<Buffer>>([]() { return std::make_unique<Buffer>(64); }, 0, 2);

    auto a = pool->get();
    auto b = pool->get();
    MLOG_I("  Borrowed 2 of max 2");

    try {
      auto c = pool->get();
    } catch (const std::runtime_error& e) {
      MLOG_I("  Pool exhausted: {}", e.what());
    }
  }

  VLOG_I("ObjectPool example finished.");
  return 0;
}
