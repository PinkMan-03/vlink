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

// Example: MessageLoop basic - run/quit, async_run, post_task, spin_once, handlers

#include <vlink/base/logger.h>
#include <vlink/base/message_loop.h>

#include <atomic>
#include <thread>

int main() {
  // ---------------------------------------------------------------
  // 1. Basic async_run and post_task usage.
  // ---------------------------------------------------------------
  {
    VLOG_I("=== Section 1: async_run + post_task ===");
    vlink::MessageLoop loop;
    loop.set_name("basic_loop");
    loop.async_run();

    // Post several tasks to be executed serially on the loop thread.
    for (int i = 0; i < 5; ++i) {
      loop.post_task([i]() { MLOG_I("Task {} executed on loop thread", i); });
    }

    // Wait until all tasks are processed.
    loop.wait_for_idle();
    VLOG_I("All tasks completed");

    loop.quit();
    loop.wait_for_quit();
    VLOG_I("Loop exited cleanly");
  }

  // ---------------------------------------------------------------
  // 2. Register begin/end/idle handlers.
  // ---------------------------------------------------------------
  {
    VLOG_I("=== Section 2: Lifecycle handlers ===");
    vlink::MessageLoop loop;
    loop.set_name("handler_loop");

    loop.register_begin_handler([]() { VLOG_I("  [begin_handler] Loop thread started"); });

    loop.register_end_handler([]() { VLOG_I("  [end_handler] Loop thread exiting"); });

    loop.register_idle_handler([]() {
      // Called each time the queue becomes empty.
      // Avoid logging here in production as it fires frequently.
    });

    loop.async_run();
    loop.post_task([]() { VLOG_I("  Task running between begin and end handlers"); });
    loop.wait_for_idle();

    loop.quit();
    loop.wait_for_quit();
  }

  // ---------------------------------------------------------------
  // 3. Blocking run() on the calling thread.
  //    Launch a helper thread to post tasks and then quit the loop.
  // ---------------------------------------------------------------
  {
    VLOG_I("=== Section 3: Blocking run() ===");
    vlink::MessageLoop loop;
    loop.set_name("blocking_loop");

    std::thread poster([&loop]() {
      // Give the loop thread a moment to start.
      std::this_thread::sleep_for(std::chrono::milliseconds(50));

      loop.post_task([]() { VLOG_I("  Task posted from helper thread"); });
      loop.post_task([]() { VLOG_I("  Second task from helper thread"); });

      std::this_thread::sleep_for(std::chrono::milliseconds(50));
      loop.quit();
    });

    // run() blocks until quit() is called.
    loop.run();
    poster.join();
    VLOG_I("Blocking run() returned after quit()");
  }

  // ---------------------------------------------------------------
  // 4. spin_once() for manual event processing.
  //    Useful when integrating with an external event loop.
  // ---------------------------------------------------------------
  {
    VLOG_I("=== Section 4: spin_once ===");
    vlink::MessageLoop loop;
    loop.set_name("spin_loop");

    std::atomic<int> processed{0};

    // Post tasks before starting to spin.
    for (int i = 0; i < 3; ++i) {
      loop.post_task([&processed, i]() {
        MLOG_I("  spin_once task {}", i);
        processed.fetch_add(1);
      });
    }

    // Process tasks one batch at a time using spin_once (non-blocking).
    while (processed.load() < 3) {
      loop.spin_once(false);
    }

    MLOG_I("spin_once processed {} tasks", processed.load());
  }

  // ---------------------------------------------------------------
  // 5. Query loop state.
  // ---------------------------------------------------------------
  {
    VLOG_I("=== Section 5: State queries ===");
    vlink::MessageLoop loop;

    MLOG_I("Before run - is_running: {}", loop.is_running());

    loop.async_run();
    MLOG_I("After async_run - is_running: {}", loop.is_running());
    MLOG_I("Task count: {}", loop.get_task_count());
    MLOG_I("Loop type: {}", static_cast<int>(loop.get_type()));

    loop.quit();
    loop.wait_for_quit();
    MLOG_I("After quit - is_running: {}", loop.is_running());
  }

  VLOG_I("MessageLoop basic example finished.");
  return 0;
}
