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
 * @file multi_timer_demo.h
 * @brief Demonstrates advanced timer patterns: strict mode, priority, multiple
 *        timers, attach/detach migration, and timer statistics.
 */

#include <vlink/base/logger.h>
#include <vlink/base/message_loop.h>
#include <vlink/base/timer.h>

#include <atomic>
#include <thread>

namespace multi_timer_demo {

// Demonstrate strict mode - catch-up missed ticks when the loop is busy.
// Strict timer fires missed ticks immediately on next iteration;
// non-strict timer drops missed ticks.
inline void demo_strict_mode(vlink::MessageLoop& loop) {
  VLOG_I("=== Strict mode ===");

  std::atomic<int> strict_count{0};
  std::atomic<int> normal_count{0};

  vlink::Timer strict_timer(&loop, 50, vlink::Timer::kInfinite, [&strict_count]() { strict_count.fetch_add(1); });
  strict_timer.set_strict(true);

  vlink::Timer normal_timer(&loop, 50, vlink::Timer::kInfinite, [&normal_count]() { normal_count.fetch_add(1); });

  strict_timer.start();
  normal_timer.start();

  // Simulate a busy loop by posting a blocking task.
  loop.post_task([]() {
    VLOG_I("  Simulating busy period (200ms)...");
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
  });

  std::this_thread::sleep_for(std::chrono::milliseconds(500));
  strict_timer.stop();
  normal_timer.stop();

  MLOG_I("  Strict timer: is_strict={}, ticks={}", strict_timer.is_strict(), strict_count.load());
  MLOG_I("  Normal timer: is_strict={}, ticks={}", normal_timer.is_strict(), normal_count.load());
}

// Demonstrate timers with priority on a kPriorityType loop.
inline void demo_priority_timers(vlink::MessageLoop& loop) {
  VLOG_I("=== Timer with priority ===");

  vlink::Timer high_timer(&loop, 100, 3);
  high_timer.set_priority(vlink::MessageLoop::kHighestPriority);
  high_timer.set_callback([]() { VLOG_I("  [HIGH priority timer] tick"); });

  vlink::Timer low_timer(&loop, 100, 3);
  low_timer.set_priority(vlink::MessageLoop::kLowestPriority);
  low_timer.set_callback([]() { VLOG_I("  [LOW priority timer] tick"); });

  high_timer.start();
  low_timer.start();

  MLOG_I("  High timer priority: {}", high_timer.get_priority());
  MLOG_I("  Low timer priority: {}", low_timer.get_priority());

  std::this_thread::sleep_for(std::chrono::milliseconds(500));
}

// Demonstrate multiple timers with different intervals on the same loop.
inline void demo_multiple_timers(vlink::MessageLoop& loop) {
  VLOG_I("=== Multiple timers ===");

  std::atomic<int> fast_count{0};
  std::atomic<int> slow_count{0};

  vlink::Timer fast_timer(&loop, 50, vlink::Timer::kInfinite, [&fast_count]() { fast_count.fetch_add(1); });
  vlink::Timer slow_timer(&loop, 200, vlink::Timer::kInfinite, [&slow_count]() { slow_count.fetch_add(1); });

  fast_timer.start();
  slow_timer.start();

  std::this_thread::sleep_for(std::chrono::milliseconds(600));
  fast_timer.stop();
  slow_timer.stop();

  MLOG_I("  Fast timer (50ms): {} ticks", fast_count.load());
  MLOG_I("  Slow timer (200ms): {} ticks", slow_count.load());
}

// Demonstrate timer statistics queries.
inline void demo_timer_stats(vlink::MessageLoop& loop) {
  VLOG_I("=== Timer statistics ===");

  vlink::Timer timer(&loop, 100, 5, []() {});
  timer.start();
  std::this_thread::sleep_for(std::chrono::milliseconds(350));

  MLOG_I("  interval: {}ms", timer.get_interval());
  MLOG_I("  loop_count: {}", timer.get_loop_count());
  MLOG_I("  remain_loop_count: {}", timer.get_remain_loop_count());
  MLOG_I("  invoke_count: {}", timer.get_invoke_count());
  MLOG_I("  is_active: {}", timer.is_active());
  MLOG_I("  message_loop: {}", timer.get_message_loop() != nullptr);

  std::this_thread::sleep_for(std::chrono::milliseconds(400));
  MLOG_I("  After all ticks - is_active: {}, invoke_count: {}", timer.is_active(), timer.get_invoke_count());
}

}  // namespace multi_timer_demo
