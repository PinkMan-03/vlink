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

// Example: Timer basic - construction, start/stop, loop_count, call_once

#include <vlink/base/logger.h>
#include <vlink/base/message_loop.h>
#include <vlink/base/timer.h>

#include <atomic>
#include <thread>

int main() {
  // ---------------------------------------------------------------
  // 1. Basic repeating timer with a fixed interval.
  // ---------------------------------------------------------------
  {
    VLOG_I("=== Section 1: Basic repeating timer ===");
    vlink::MessageLoop loop;
    loop.async_run();

    std::atomic<int> count{0};

    // Create a timer that fires every 100ms, indefinitely.
    vlink::Timer timer(&loop, 100, vlink::Timer::kInfinite, [&count]() {
      int c = count.fetch_add(1) + 1;
      MLOG_I("  Repeating timer tick #{}", c);
    });

    timer.start();
    MLOG_I("  Timer active: {}, interval: {}ms", timer.is_active(), timer.get_interval());

    // Let it tick a few times.
    std::this_thread::sleep_for(std::chrono::milliseconds(550));
    timer.stop();

    MLOG_I("  Timer stopped after {} ticks", count.load());

    loop.quit();
    loop.wait_for_quit();
  }

  // ---------------------------------------------------------------
  // 2. Timer with limited loop_count.
  //    Fires exactly N times then stops automatically.
  // ---------------------------------------------------------------
  {
    VLOG_I("=== Section 2: Timer with loop_count ===");
    vlink::MessageLoop loop;
    loop.async_run();

    std::atomic<int> count{0};

    // Fire exactly 3 times at 100ms interval.
    vlink::Timer timer(&loop, 100, 3, [&count]() {
      int c = count.fetch_add(1) + 1;
      MLOG_I("  Counted timer tick #{}", c);
    });

    timer.start();
    MLOG_I("  loop_count: {}, remain: {}", timer.get_loop_count(), timer.get_remain_loop_count());

    // Wait enough time for all 3 ticks.
    std::this_thread::sleep_for(std::chrono::milliseconds(500));

    MLOG_I("  Total ticks: {}, is_active: {}", count.load(), timer.is_active());

    loop.quit();
    loop.wait_for_quit();
  }

  // ---------------------------------------------------------------
  // 3. Timer start/stop/restart cycle.
  // ---------------------------------------------------------------
  {
    VLOG_I("=== Section 3: Start/stop/restart ===");
    vlink::MessageLoop loop;
    loop.async_run();

    std::atomic<int> count{0};

    vlink::Timer timer(&loop, 100, vlink::Timer::kInfinite, [&count]() { count.fetch_add(1); });

    timer.start();
    std::this_thread::sleep_for(std::chrono::milliseconds(250));
    timer.stop();
    int after_stop = count.load();
    MLOG_I("  After stop: {} ticks", after_stop);

    // restart() resets the invoke_count and re-arms the timer.
    count.store(0);
    timer.restart();
    std::this_thread::sleep_for(std::chrono::milliseconds(250));
    timer.stop();
    MLOG_I("  After restart: {} ticks, invoke_count: {}", count.load(), timer.get_invoke_count());

    loop.quit();
    loop.wait_for_quit();
  }

  // ---------------------------------------------------------------
  // 4. Timer::call_once - fire-and-forget one-shot delayed task.
  // ---------------------------------------------------------------
  {
    VLOG_I("=== Section 4: call_once ===");
    vlink::MessageLoop loop;
    loop.async_run();

    std::atomic<bool> fired{false};

    // Schedule a one-shot callback after 200ms.
    vlink::Timer::call_once(&loop, 200, [&fired]() {
      VLOG_I("  call_once fired after 200ms");
      fired.store(true);
    });

    MLOG_I("  call_once scheduled, waiting...");
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    MLOG_I("  call_once fired: {}", fired.load());

    loop.quit();
    loop.wait_for_quit();
  }

  // ---------------------------------------------------------------
  // 5. Timer constructed without a loop, then attached.
  // ---------------------------------------------------------------
  {
    VLOG_I("=== Section 5: Detached timer with attach ===");
    vlink::MessageLoop loop;
    loop.async_run();

    std::atomic<int> count{0};

    // Construct timer without a loop.
    vlink::Timer timer(100, 5, [&count]() { count.fetch_add(1); });

    // Attach to a loop and start.
    timer.attach(&loop);
    timer.start();

    std::this_thread::sleep_for(std::chrono::milliseconds(700));
    MLOG_I("  Detached timer ticks: {}", count.load());

    loop.quit();
    loop.wait_for_quit();
  }

  // ---------------------------------------------------------------
  // 6. Dynamic parameter changes.
  // ---------------------------------------------------------------
  {
    VLOG_I("=== Section 6: Dynamic parameter changes ===");
    vlink::MessageLoop loop;
    loop.async_run();

    std::atomic<int> count{0};

    vlink::Timer timer(&loop, 100, vlink::Timer::kInfinite, [&count]() { count.fetch_add(1); });

    timer.start();
    std::this_thread::sleep_for(std::chrono::milliseconds(250));
    timer.stop();
    MLOG_I("  Phase 1 (100ms interval): {} ticks", count.load());

    // Change interval and loop count, then restart.
    count.store(0);
    timer.set_interval(50);
    timer.set_loop_count(6);
    timer.restart();
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    MLOG_I("  Phase 2 (50ms interval, 6 ticks): {} ticks", count.load());

    loop.quit();
    loop.wait_for_quit();
  }

  VLOG_I("Timer basic example finished.");
  return 0;
}
