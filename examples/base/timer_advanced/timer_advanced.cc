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

// Example: Timer advanced - strict mode, priority, multiple timers, attach/detach

#include <vlink/base/logger.h>
#include <vlink/base/message_loop.h>
#include <vlink/base/timer.h>

#include <atomic>
#include <thread>

#include "multi_timer_demo.h"

int main() {
  // ---------------------------------------------------------------
  // 1. Strict mode - catch-up missed ticks when the loop is busy.
  // ---------------------------------------------------------------
  {
    VLOG_I("=== Section 1: Strict mode ===");
    vlink::MessageLoop loop;
    loop.async_run();

    multi_timer_demo::demo_strict_mode(loop);

    loop.quit();
    loop.wait_for_quit();
  }

  // ---------------------------------------------------------------
  // 2. Timer with priority on a kPriorityType loop.
  // ---------------------------------------------------------------
  {
    VLOG_I("=== Section 2: Timer with priority ===");
    vlink::MessageLoop loop(vlink::MessageLoop::kPriorityType);
    loop.async_run();

    multi_timer_demo::demo_priority_timers(loop);

    loop.quit();
    loop.wait_for_quit();
  }

  // ---------------------------------------------------------------
  // 3. Multiple timers on the same loop with different intervals.
  // ---------------------------------------------------------------
  {
    VLOG_I("=== Section 3: Multiple timers ===");
    vlink::MessageLoop loop;
    loop.async_run();

    multi_timer_demo::demo_multiple_timers(loop);

    loop.quit();
    loop.wait_for_quit();
  }

  // ---------------------------------------------------------------
  // 4. Attach / detach between different loops.
  // ---------------------------------------------------------------
  {
    VLOG_I("=== Section 4: Attach/detach between loops ===");
    vlink::MessageLoop loop_a;
    vlink::MessageLoop loop_b;
    loop_a.set_name("loop_a");
    loop_b.set_name("loop_b");
    loop_a.async_run();
    loop_b.async_run();

    std::atomic<int> count_a{0};
    std::atomic<int> count_b{0};

    vlink::Timer timer(100, vlink::Timer::kInfinite);

    timer.attach(&loop_a);
    timer.set_callback([&count_a]() { count_a.fetch_add(1); });
    timer.start();
    std::this_thread::sleep_for(std::chrono::milliseconds(350));
    timer.stop();
    MLOG_I("  On loop_a: {} ticks", count_a.load());

    timer.detach();
    timer.attach(&loop_b);
    timer.set_callback([&count_b]() { count_b.fetch_add(1); });
    timer.start();
    std::this_thread::sleep_for(std::chrono::milliseconds(350));
    timer.stop();
    MLOG_I("  On loop_b: {} ticks", count_b.load());

    loop_a.quit();
    loop_b.quit();
    loop_a.wait_for_quit();
    loop_b.wait_for_quit();
  }

  // ---------------------------------------------------------------
  // 5. Query timer statistics.
  // ---------------------------------------------------------------
  {
    VLOG_I("=== Section 5: Timer statistics ===");
    vlink::MessageLoop loop;
    loop.async_run();

    multi_timer_demo::demo_timer_stats(loop);

    loop.quit();
    loop.wait_for_quit();
  }

  VLOG_I("Timer advanced example finished.");
  return 0;
}
