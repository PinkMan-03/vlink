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

// Example: MessageLoop advanced - 3 types, exec_task, Schedule::Config, chaining, invoke_task

#include <vlink/base/logger.h>
#include <vlink/base/message_loop.h>

#include "schedule_examples.h"

int main() {
  // ---------------------------------------------------------------
  // 1. Compare three queue types: Normal, Lockfree, Priority.
  // ---------------------------------------------------------------
  {
    VLOG_I("=== Section 1: Queue type comparison ===");

    // Normal type - mutex-protected std::queue (default).
    vlink::MessageLoop normal_loop(vlink::MessageLoop::kNormalType);
    normal_loop.set_name("normal");
    normal_loop.async_run();
    normal_loop.post_task([]() { VLOG_I("  [NormalType] Task executed"); });
    normal_loop.wait_for_idle();
    MLOG_I("  NormalType - strategy: {}", static_cast<int>(normal_loop.get_strategy()));
    normal_loop.quit();
    normal_loop.wait_for_quit();

    // Lockfree type - MPMC lock-free queue (fastest single-producer path).
    vlink::MessageLoop lockfree_loop(vlink::MessageLoop::kLockfreeType);
    lockfree_loop.set_name("lockfree");
    lockfree_loop.async_run();
    lockfree_loop.post_task([]() { VLOG_I("  [LockfreeType] Task executed"); });
    lockfree_loop.wait_for_idle();
    lockfree_loop.quit();
    lockfree_loop.wait_for_quit();

    // Priority type - priority queue, higher values dispatched first.
    vlink::MessageLoop priority_loop(vlink::MessageLoop::kPriorityType);
    priority_loop.set_name("priority");
    priority_loop.async_run();
    priority_loop.post_task_with_priority([]() { VLOG_I("  [PriorityType] Low priority task"); },
                                          vlink::MessageLoop::kLowestPriority);
    priority_loop.post_task_with_priority([]() { VLOG_I("  [PriorityType] High priority task"); },
                                          vlink::MessageLoop::kHighestPriority);
    priority_loop.post_task_with_priority([]() { VLOG_I("  [PriorityType] Normal priority task"); },
                                          vlink::MessageLoop::kNormalPriority);
    priority_loop.wait_for_idle();
    priority_loop.quit();
    priority_loop.wait_for_quit();
  }

  // ---------------------------------------------------------------
  // 2. Dispatch strategies comparison.
  // ---------------------------------------------------------------
  {
    VLOG_I("=== Section 2: Dispatch strategies ===");
    vlink::MessageLoop loop;
    loop.async_run();

    MLOG_I("  Current strategy: {}", static_cast<int>(loop.get_strategy()));

    loop.set_strategy(vlink::MessageLoop::kBlockStrategy);
    loop.post_task([]() { VLOG_I("  Running with kBlockStrategy"); });
    loop.wait_for_idle();

    loop.set_strategy(vlink::MessageLoop::kPopStrategy);
    loop.post_task([]() { VLOG_I("  Running with kPopStrategy"); });
    loop.wait_for_idle();

    loop.quit();
    loop.wait_for_quit();
  }

  // ---------------------------------------------------------------
  // 3. exec_task with Schedule::Config - void callback.
  // ---------------------------------------------------------------
  {
    VLOG_I("=== Section 3: Schedule::Config demos ===");
    vlink::MessageLoop loop(vlink::MessageLoop::kPriorityType);
    loop.async_run();

    schedule_examples::demo_exec_task_void(loop);

    loop.quit();
    loop.wait_for_quit();
  }

  // ---------------------------------------------------------------
  // 4. exec_task with on_then/on_else chaining - bool callback.
  // ---------------------------------------------------------------
  {
    VLOG_I("=== Section 4: Chaining demos ===");
    vlink::MessageLoop loop;
    loop.async_run();

    schedule_examples::demo_exec_task_chaining(loop);

    loop.quit();
    loop.wait_for_quit();
  }

  // ---------------------------------------------------------------
  // 5. invoke_task with std::future.
  // ---------------------------------------------------------------
  {
    VLOG_I("=== Section 5: invoke_task demos ===");
    vlink::MessageLoop loop;
    loop.async_run();

    schedule_examples::demo_invoke_task(loop);

    loop.quit();
    loop.wait_for_quit();
  }

  // ---------------------------------------------------------------
  // 6. Priority queue with invoke_task_with_priority.
  // ---------------------------------------------------------------
  {
    VLOG_I("=== Section 6: Priority invoke demos ===");
    vlink::MessageLoop loop(vlink::MessageLoop::kPriorityType);
    loop.async_run();

    schedule_examples::demo_priority_invoke(loop);

    loop.quit();
    loop.wait_for_quit();
  }

  VLOG_I("MessageLoop advanced example finished.");
  return 0;
}
