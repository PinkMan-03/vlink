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

#pragma once

/**
 * @file parallel_tasks.h
 * @brief Task definitions for the ThreadPool example.
 *
 * Contains helper functions that demonstrate common parallel patterns:
 * basic parallel execution, future-based result collection, and
 * lockfree queue summation.
 */

#include <vlink/base/logger.h>
#include <vlink/base/thread_pool.h>

#include <atomic>
#include <chrono>
#include <thread>
#include <vector>

namespace parallel_tasks {

// Demonstrate basic parallel task execution: post 10 tasks to a 4-thread pool.
inline void demo_basic_parallel(vlink::ThreadPool& pool) {
  VLOG_I("=== Basic parallel execution ===");
  std::atomic<int> completed{0};

  for (int i = 0; i < 10; ++i) {
    pool.post_task([i, &completed]() {
      MLOG_I("  Task {} running on pool thread", i);
      std::this_thread::sleep_for(std::chrono::milliseconds(50));
      completed.fetch_add(1);
    });
  }

  while (completed.load() < 10) {
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
  MLOG_I("  All {} tasks completed", completed.load());
}

// Demonstrate invoke_task with std::future to retrieve results.
inline void demo_invoke_tasks(vlink::ThreadPool& pool) {
  VLOG_I("=== invoke_task with future ===");
  std::vector<std::future<int>> futures;
  for (int i = 0; i < 8; ++i) {
    futures.push_back(pool.invoke_task([i]() -> int {
      std::this_thread::sleep_for(std::chrono::milliseconds(30));
      return i * i;
    }));
  }

  for (int i = 0; i < 8; ++i) {
    MLOG_I("  invoke_task result[{}] = {}", i, futures[i].get());
  }
}

// Demonstrate lockfree queue type by computing sum 1..100.
inline void demo_lockfree_sum() {
  VLOG_I("=== Lockfree queue type ===");
  vlink::ThreadPool pool(4, vlink::ThreadPool::kLockfreeType);

  std::atomic<int> sum{0};
  for (int i = 1; i <= 100; ++i) {
    pool.post_task([i, &sum]() { sum.fetch_add(i); });
  }

  std::this_thread::sleep_for(std::chrono::milliseconds(200));
  MLOG_I("  Sum of 1..100 = {} (expected 5050)", sum.load());
}

}  // namespace parallel_tasks
