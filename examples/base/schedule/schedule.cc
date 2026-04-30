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

// Example: Schedule - Config, Status, RetStatus, chaining callbacks

#include <vlink/base/logger.h>
#include <vlink/base/message_loop.h>

#include <thread>

int main() {
  // ---------------------------------------------------------------
  // 1. Schedule::Config construction.
  // ---------------------------------------------------------------
  {
    VLOG_I("=== Section 1: Schedule::Config ===");

    // Default config (all zeros).
    vlink::Schedule::Config default_cfg;
    MLOG_I("  Default: delay={}ms, priority={}, schedule_timeout={}ms, execution_timeout={}ms", default_cfg.delay_ms,
           default_cfg.priority, default_cfg.schedule_timeout_ms, default_cfg.execution_timeout_ms);

    // Full config.
    vlink::Schedule::Config full_cfg(100, 200, 5000, 3000);
    MLOG_I("  Full: delay={}ms, priority={}, schedule_timeout={}ms, execution_timeout={}ms", full_cfg.delay_ms,
           full_cfg.priority, full_cfg.schedule_timeout_ms, full_cfg.execution_timeout_ms);

    // Delay only.
    vlink::Schedule::Config delay_cfg(50);
    MLOG_I("  Delay only: delay={}ms", delay_cfg.delay_ms);
  }

  // ---------------------------------------------------------------
  // 2. Void callback with Status chaining.
  // ---------------------------------------------------------------
  {
    VLOG_I("=== Section 2: Void callback (Status) ===");
    vlink::MessageLoop loop;
    loop.async_run();

    // Immediate execution.
    loop.exec_task(vlink::Schedule::Config{}, []() { VLOG_I("  Immediate void task executed"); })
        .on_catch([](std::exception& e) { MLOG_E("  Exception: {}", e.what()); });

    loop.wait_for_idle();

    // Delayed execution with timeout callbacks.
    loop.exec_task(vlink::Schedule::Config{100, 0, 500, 500},
                   []() { VLOG_I("  Delayed void task executed (100ms delay)"); })
        .on_schedule_timeout([]() { VLOG_W("  Schedule timeout fired"); })
        .on_execution_timeout([]() { VLOG_W("  Execution timeout fired"); })
        .on_catch([](std::exception& e) { MLOG_E("  Caught exception: {}", e.what()); });

    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    loop.wait_for_idle();

    loop.quit();
    loop.wait_for_quit();
  }

  // ---------------------------------------------------------------
  // 3. Bool callback with RetStatus (on_then / on_else).
  // ---------------------------------------------------------------
  {
    VLOG_I("=== Section 3: Bool callback (RetStatus) ===");
    vlink::MessageLoop loop;
    loop.async_run();

    // Case A: callback returns true -> on_then chain fires.
    VLOG_I("  --- Case A: Returns true ---");
    loop.exec_task(vlink::Schedule::Config{},
                   []() -> bool {
                     VLOG_I("  Bool task: returning true");
                     return true;
                   })
        .on_then([]() -> bool {
          VLOG_I("  on_then(1): processing success");
          return true;
        })
        .on_then([]() -> bool {
          VLOG_I("  on_then(2): continued success");
          return false;  // Returning false stops the chain.
        })
        .on_then([]() -> bool {
          VLOG_I("  on_then(3): this should NOT execute");
          return true;
        })
        .on_else([]() { VLOG_I("  on_else: triggered because on_then(2) returned false"); });

    loop.wait_for_idle();

    // Case B: callback returns false -> on_else fires directly.
    VLOG_I("  --- Case B: Returns false ---");
    loop.exec_task(vlink::Schedule::Config{},
                   []() -> bool {
                     VLOG_I("  Bool task: returning false");
                     return false;
                   })
        .on_then([]() -> bool {
          VLOG_I("  on_then: this should NOT execute");
          return true;
        })
        .on_else([]() { VLOG_I("  on_else: handling failure path"); });

    loop.wait_for_idle();

    loop.quit();
    loop.wait_for_quit();
  }

  // ---------------------------------------------------------------
  // 4. Exception handling with on_catch.
  // ---------------------------------------------------------------
  {
    VLOG_I("=== Section 4: Exception handling ===");
    vlink::MessageLoop loop;
    loop.async_run();

    loop.exec_task(vlink::Schedule::Config{}, []() { throw std::runtime_error("simulated error"); })
        .on_catch([](std::exception& e) { MLOG_I("  on_catch: caught '{}'", e.what()); });

    loop.wait_for_idle();

    loop.quit();
    loop.wait_for_quit();
  }

  // ---------------------------------------------------------------
  // 5. Priority-aware scheduling.
  // ---------------------------------------------------------------
  {
    VLOG_I("=== Section 5: Priority scheduling ===");
    vlink::MessageLoop loop(vlink::MessageLoop::kPriorityType);
    loop.async_run();

    loop.exec_task(vlink::Schedule::Config{0, vlink::MessageLoop::kLowestPriority},
                   []() { VLOG_I("  [LOW] priority scheduled task"); });

    loop.exec_task(vlink::Schedule::Config{0, vlink::MessageLoop::kHighestPriority},
                   []() { VLOG_I("  [HIGH] priority scheduled task"); });

    loop.exec_task(vlink::Schedule::Config{0, vlink::MessageLoop::kNormalPriority},
                   []() { VLOG_I("  [NORMAL] priority scheduled task"); });

    loop.wait_for_idle();

    loop.quit();
    loop.wait_for_quit();
  }

  // ---------------------------------------------------------------
  // 6. Status validity check.
  // ---------------------------------------------------------------
  {
    VLOG_I("=== Section 6: Status validity ===");
    vlink::MessageLoop loop;
    loop.async_run();

    auto status = loop.exec_task(vlink::Schedule::Config{}, []() { VLOG_I("  Task for validity check"); });

    MLOG_I("  Status is_valid: {}", status.is_valid());

    loop.wait_for_idle();
    loop.quit();
    loop.wait_for_quit();
  }

  VLOG_I("Schedule example finished.");
  return 0;
}
