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

// Example: ThreadPool - parallel task execution, invoke_task with future

#include <vlink/base/logger.h>
#include <vlink/base/thread_pool.h>

#include <thread>

#include "parallel_tasks.h"

int main() {
  // ---------------------------------------------------------------
  // 1. Basic parallel task execution.
  // ---------------------------------------------------------------
  {
    VLOG_I("=== Section 1: Basic parallel execution ===");
    vlink::ThreadPool pool(4);
    pool.set_name("worker_pool");

    parallel_tasks::demo_basic_parallel(pool);
  }

  // ---------------------------------------------------------------
  // 2. invoke_task with std::future to retrieve results.
  // ---------------------------------------------------------------
  {
    VLOG_I("=== Section 2: invoke_task with future ===");
    vlink::ThreadPool pool(4);

    parallel_tasks::demo_invoke_tasks(pool);
  }

  // ---------------------------------------------------------------
  // 3. Lockfree queue type.
  // ---------------------------------------------------------------
  {
    VLOG_I("=== Section 3: Lockfree queue type ===");
    parallel_tasks::demo_lockfree_sum();
  }

  // ---------------------------------------------------------------
  // 4. Strategy control and state queries.
  // ---------------------------------------------------------------
  {
    VLOG_I("=== Section 4: Strategy and state ===");
    vlink::ThreadPool pool(2);
    pool.set_name("query_pool");

    MLOG_I("  Pool name: {}", pool.get_name());
    MLOG_I("  Pool type: {}", static_cast<int>(pool.get_type()));
    MLOG_I("  Pool strategy: {}", static_cast<int>(pool.get_strategy()));
    MLOG_I("  Max task count: {}", pool.get_max_task_count());

    pool.set_strategy(vlink::ThreadPool::kBlockStrategy);
    MLOG_I("  Strategy after change: {}", static_cast<int>(pool.get_strategy()));

    pool.post_task([]() { VLOG_I("  Task on block-strategy pool"); });
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    MLOG_I("  is_in_work_thread (from main): {}", pool.is_in_work_thread());
    pool.post_task([&pool]() { MLOG_I("  is_in_work_thread (from worker): {}", pool.is_in_work_thread()); });
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    pool.shutdown();
    VLOG_I("  Pool shut down");
  }

  VLOG_I("ThreadPool example finished.");
  return 0;
}
