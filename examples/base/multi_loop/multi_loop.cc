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

// Example: MultiLoop - multi-threaded message dispatch

#include <vlink/base/logger.h>
#include <vlink/base/multi_loop.h>

#include <atomic>
#include <thread>

int main() {
  // ---------------------------------------------------------------
  // 1. Basic multi-threaded task execution.
  // ---------------------------------------------------------------
  {
    VLOG_I("=== Section 1: Basic multi-threaded dispatch ===");
    vlink::MultiLoop loop(4);
    loop.set_name("multi_4");
    loop.async_run();

    std::atomic<int> completed{0};

    for (int i = 0; i < 20; ++i) {
      loop.post_task([i, &completed]() {
        MLOG_I("  Task {} executing", i);
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        completed.fetch_add(1);
      });
    }

    loop.wait_for_idle();
    MLOG_I("  Completed: {}/20 tasks", completed.load());

    loop.quit();
    loop.wait_for_quit();
  }

  // ---------------------------------------------------------------
  // 2. is_in_same_thread check.
  // ---------------------------------------------------------------
  {
    VLOG_I("=== Section 2: Thread identity ===");
    vlink::MultiLoop loop(2);
    loop.async_run();

    MLOG_I("  Main thread - is_in_same_thread: {}", loop.is_in_same_thread());

    loop.post_task([&loop]() { MLOG_I("  Worker thread - is_in_same_thread: {}", loop.is_in_same_thread()); });

    loop.wait_for_idle();
    loop.quit();
    loop.wait_for_quit();
  }

  // ---------------------------------------------------------------
  // 3. MultiLoop with priority queue.
  // ---------------------------------------------------------------
  {
    VLOG_I("=== Section 3: Priority queue ===");
    vlink::MultiLoop loop(2, vlink::MultiLoop::kPriorityType);
    loop.async_run();

    loop.post_task_with_priority([]() { VLOG_I("  [LOW] priority task"); }, vlink::MultiLoop::kLowestPriority);
    loop.post_task_with_priority([]() { VLOG_I("  [HIGH] priority task"); }, vlink::MultiLoop::kHighestPriority);
    loop.post_task_with_priority([]() { VLOG_I("  [NORMAL] priority task"); }, vlink::MultiLoop::kNormalPriority);

    loop.wait_for_idle();
    loop.quit();
    loop.wait_for_quit();
  }

  // ---------------------------------------------------------------
  // 4. invoke_task on MultiLoop.
  // ---------------------------------------------------------------
  {
    VLOG_I("=== Section 4: invoke_task ===");
    vlink::MultiLoop loop(4);
    loop.async_run();

    auto future = loop.invoke_task([]() -> int {
      std::this_thread::sleep_for(std::chrono::milliseconds(30));
      return 99;
    });

    MLOG_I("  invoke_task result: {}", future.get());

    loop.quit();
    loop.wait_for_quit();
  }

  // ---------------------------------------------------------------
  // 5. exec_task with Schedule::Config on MultiLoop.
  // ---------------------------------------------------------------
  {
    VLOG_I("=== Section 5: exec_task with Schedule ===");
    vlink::MultiLoop loop(2);
    loop.async_run();

    loop.exec_task(vlink::Schedule::Config{0}, []() { VLOG_I("  Immediate exec_task on MultiLoop"); })
        .on_catch([](std::exception& e) { MLOG_E("  Exception: {}", e.what()); });

    loop.exec_task(vlink::Schedule::Config{100}, []() { VLOG_I("  Delayed exec_task (100ms) on MultiLoop"); });

    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    loop.wait_for_idle();

    loop.quit();
    loop.wait_for_quit();
  }

  // ---------------------------------------------------------------
  // 6. Parallel computation with atomic accumulation.
  // ---------------------------------------------------------------
  {
    VLOG_I("=== Section 6: Parallel computation ===");
    vlink::MultiLoop loop(4);
    loop.async_run();

    std::atomic<int64_t> sum{0};
    constexpr int kTasks = 100;

    for (int i = 1; i <= kTasks; ++i) {
      loop.post_task([i, &sum]() { sum.fetch_add(i); });
    }

    loop.wait_for_idle();
    MLOG_I("  Sum of 1..{}: {} (expected {})", kTasks, sum.load(), kTasks * (kTasks + 1) / 2);

    loop.quit();
    loop.wait_for_quit();
  }

  VLOG_I("MultiLoop example finished.");
  return 0;
}
