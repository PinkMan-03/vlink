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
 * @file schedule_examples.h
 * @brief Demonstrates Schedule::Config based task execution on MessageLoop.
 *
 * Schedule::Config provides fine-grained control over task scheduling:
 *   - delay_ms: postpone execution by N milliseconds
 *   - priority: dispatch order on kPriorityType loops
 *   - timeout callbacks: on_schedule_timeout / on_execution_timeout
 *   - chaining: on_then / on_else for bool-returning callbacks
 */

#include <vlink/base/logger.h>
#include <vlink/base/message_loop.h>

#include <string>
#include <thread>

namespace schedule_examples {

// Demonstrate exec_task with Schedule::Config for void callbacks.
// Supports delay, priority, and timeout configuration.
inline void demo_exec_task_void(vlink::MessageLoop& loop) {
  VLOG_I("=== exec_task with Schedule::Config (void) ===");

  // Immediate execution with priority 100.
  loop.exec_task(vlink::Schedule::Config{0, 100}, []() { VLOG_I("  Immediate task with priority=100"); });

  // Delayed execution: 200ms delay.
  loop.exec_task(vlink::Schedule::Config{200}, []() { VLOG_I("  Delayed task (200ms)"); })
      .on_schedule_timeout([]() { VLOG_W("  Schedule timeout!"); })
      .on_execution_timeout([]() { VLOG_W("  Execution timeout!"); })
      .on_catch([](std::exception& e) { MLOG_E("  Exception caught: {}", e.what()); });

  std::this_thread::sleep_for(std::chrono::milliseconds(300));
  loop.wait_for_idle();
}

// Demonstrate exec_task with on_then/on_else chaining for bool callbacks.
// The callback returns true/false to drive the chain.
inline void demo_exec_task_chaining(vlink::MessageLoop& loop) {
  VLOG_I("=== exec_task with on_then/on_else chaining ===");

  // Boolean callback returning true -> on_then is invoked.
  loop.exec_task(vlink::Schedule::Config{},
                 []() -> bool {
                   VLOG_I("  Bool task executing... returning true");
                   return true;
                 })
      .on_then([]() -> bool {
        VLOG_I("  on_then(1): first task succeeded");
        return true;
      })
      .on_then([]() -> bool {
        VLOG_I("  on_then(2): chained success handler");
        return true;
      })
      .on_else([]() { VLOG_I("  on_else: this should NOT be called"); });

  loop.wait_for_idle();

  // Boolean callback returning false -> on_else is invoked.
  loop.exec_task(vlink::Schedule::Config{},
                 []() -> bool {
                   VLOG_I("  Bool task executing... returning false");
                   return false;
                 })
      .on_then([]() -> bool {
        VLOG_I("  on_then: this should NOT be called");
        return true;
      })
      .on_else([]() { VLOG_I("  on_else: task returned false, handling failure"); });

  loop.wait_for_idle();
}

// Demonstrate invoke_task with std::future to get results from the loop thread.
inline void demo_invoke_task(vlink::MessageLoop& loop) {
  VLOG_I("=== invoke_task with future ===");

  auto future = loop.invoke_task([]() -> int {
    VLOG_I("  Computing on loop thread...");
    return 42;
  });

  int result = future.get();
  MLOG_I("  invoke_task result: {}", result);

  auto str_future = loop.invoke_task([]() -> std::string { return "hello from loop"; });
  MLOG_I("  invoke_task string result: {}", str_future.get());
}

// Demonstrate invoke_task_with_priority on a priority queue loop.
inline void demo_priority_invoke(vlink::MessageLoop& loop) {
  VLOG_I("=== invoke_task_with_priority ===");

  auto high_future = loop.invoke_task_with_priority(
      []() -> int {
        VLOG_I("  High priority invoke task");
        return 1;
      },
      vlink::MessageLoop::kHighestPriority);

  auto low_future = loop.invoke_task_with_priority(
      []() -> int {
        VLOG_I("  Low priority invoke task");
        return 2;
      },
      vlink::MessageLoop::kLowestPriority);

  MLOG_I("  High priority result: {}", high_future.get());
  MLOG_I("  Low priority result: {}", low_future.get());
}

}  // namespace schedule_examples
