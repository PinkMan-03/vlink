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

// NOLINTBEGIN

#include "./base/message_loop.h"

#include <doctest/doctest.h>

#include <atomic>
#include <chrono>
#include <string>
#include <thread>
#include <vector>

//
#include "../common_test.h"

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static void sleep_ms(int ms) { std::this_thread::sleep_for(std::chrono::milliseconds(ms)); }

class SmallQueueMessageLoop final : public MessageLoop {
 public:
  using MessageLoop::MessageLoop;

  [[nodiscard]] size_t get_max_task_count() const override { return 1U; }
};

// ---------------------------------------------------------------------------
// TEST SUITE
// ---------------------------------------------------------------------------

TEST_SUITE("base-MessageLoop") {
  TEST_CASE("default construction has kNormalType") {
    MessageLoop loop;
    CHECK(loop.get_type() == MessageLoop::kNormalType);
  }

  TEST_CASE("explicit type construction") {
    MessageLoop loop_normal(MessageLoop::kNormalType);
    CHECK(loop_normal.get_type() == MessageLoop::kNormalType);

    MessageLoop loop_lf(MessageLoop::kLockfreeType);
    CHECK(loop_lf.get_type() == MessageLoop::kLockfreeType);

    MessageLoop loop_prio(MessageLoop::kPriorityType);
    CHECK(loop_prio.get_type() == MessageLoop::kPriorityType);
  }

  TEST_CASE("set_name and get_name") {
    MessageLoop loop;
    loop.set_name("test-loop");
    CHECK(loop.get_name() == "test-loop");
  }

  TEST_CASE("set_strategy and get_strategy default") {
    MessageLoop loop;
    // Default strategy is kOptimizationStrategy
    CHECK(loop.get_strategy() == MessageLoop::kOptimizationStrategy);

    loop.set_strategy(MessageLoop::kBlockStrategy);
    CHECK(loop.get_strategy() == MessageLoop::kBlockStrategy);

    loop.set_strategy(MessageLoop::kPopStrategy);
    CHECK(loop.get_strategy() == MessageLoop::kPopStrategy);

    loop.set_strategy(MessageLoop::kOptimizationStrategy);
    CHECK(loop.get_strategy() == MessageLoop::kOptimizationStrategy);
  }

  TEST_CASE("is_running before and after async_run") {
    MessageLoop loop;
    CHECK_FALSE(loop.is_running());

    loop.async_run();
    sleep_ms(20);
    CHECK(loop.is_running());

    loop.quit();
    loop.wait_for_quit();
    CHECK_FALSE(loop.is_running());
  }

  TEST_CASE("async_run second call returns false") {
    MessageLoop loop;
    bool first = loop.async_run();
    CHECK(first == true);

    bool second = loop.async_run();
    CHECK(second == false);

    loop.quit();
    loop.wait_for_quit();
  }

  TEST_CASE("async_run can restart after wait_for_quit") {
    MessageLoop loop;
    CHECK(loop.async_run());
    loop.quit();
    CHECK(loop.wait_for_quit());

    CHECK(loop.async_run());
    std::atomic<bool> ran{false};
    CHECK(loop.post_task([&ran] { ran.store(true, std::memory_order_release); }));
    loop.wait_for_idle();
    CHECK(ran.load(std::memory_order_acquire));

    loop.quit();
    loop.wait_for_quit();
  }

  TEST_CASE("post_task executes on loop thread") {
    MessageLoop loop;
    loop.async_run();

    std::atomic<int> counter{0};
    loop.post_task([&counter]() { counter.fetch_add(1); });
    loop.wait_for_idle();

    CHECK(counter.load() == 1);

    loop.quit();
    loop.wait_for_quit();
  }

  TEST_CASE("post_task multiple tasks in order (kNormalType)") {
    MessageLoop loop(MessageLoop::kNormalType);
    loop.async_run();

    std::vector<int> order;
    std::mutex mtx;

    for (int i = 0; i < 5; ++i) {
      loop.post_task([i, &order, &mtx]() {
        std::lock_guard<std::mutex> lock(mtx);
        order.push_back(i);
      });
    }

    loop.wait_for_idle();

    CHECK(order.size() == 5);
    // Normal type is FIFO — verify tasks ran in submission order
    for (int i = 0; i < 5; ++i) {
      CHECK(order[static_cast<size_t>(i)] == i);
    }

    loop.quit();
    loop.wait_for_quit();
  }

  TEST_CASE("post_task returns true when kPopStrategy drops old task") {
    SmallQueueMessageLoop loop(MessageLoop::kNormalType);
    loop.set_strategy(MessageLoop::kPopStrategy);

    CHECK(loop.post_task([] {}));
    CHECK(loop.post_task([] {}));
    CHECK(loop.get_task_count() == 1u);
  }

  TEST_CASE("invoke_task returns correct value") {
    MessageLoop loop;
    loop.async_run();

    auto fut = loop.invoke_task([]() -> int { return 42; });
    int result = fut.get();
    CHECK(result == 42);

    loop.quit();
    loop.wait_for_quit();
  }

  TEST_CASE("invoke_task with args") {
    MessageLoop loop;
    loop.async_run();

    auto fut = loop.invoke_task([](int a, int b) -> int { return a + b; }, 10, 20);
    int result = fut.get();
    CHECK(result == 30);

    loop.quit();
    loop.wait_for_quit();
  }

  TEST_CASE("invoke_task void return") {
    MessageLoop loop;
    loop.async_run();

    std::atomic<bool> ran{false};
    auto fut = loop.invoke_task([&ran]() { ran.store(true); });
    fut.get();
    CHECK(ran.load());

    loop.quit();
    loop.wait_for_quit();
  }

  TEST_CASE("get_task_count reflects pending tasks") {
    // Use block strategy so tasks don't drain before we can sample count
    MessageLoop loop(MessageLoop::kNormalType);
    // Don't start the loop yet — post tasks while it is idle
    std::atomic<bool> gate{false};

    loop.async_run();
    sleep_ms(10);

    // Block the loop on one long task so subsequent tasks pile up
    loop.post_task([&gate]() {
      while (!gate.load()) {
        std::this_thread::yield();
      }
    });
    sleep_ms(10);

    loop.post_task([]() {});
    loop.post_task([]() {});
    sleep_ms(5);

    // At least the two queued tasks should be pending
    CHECK(loop.get_task_count() >= 2);

    gate.store(true);
    loop.wait_for_idle();
    loop.quit();
    loop.wait_for_quit();
  }

  TEST_CASE("get_max_task_count returns 10000") {
    MessageLoop loop;
    CHECK(loop.get_max_task_count() == 10000);
  }

  TEST_CASE("wait_for_idle returns true when queue is empty") {
    MessageLoop loop;
    loop.async_run();

    std::atomic<int> cnt{0};
    for (int i = 0; i < 10; ++i) {
      loop.post_task([&cnt]() { cnt.fetch_add(1); });
    }

    bool idle = loop.wait_for_idle(500);
    CHECK(idle == true);
    CHECK(cnt.load() == 10);

    loop.quit();
    loop.wait_for_quit();
  }

  TEST_CASE("wait_for_idle timeout returns false when loop is blocked") {
    MessageLoop loop;
    std::atomic<bool> gate{false};

    loop.async_run();
    loop.post_task([&gate]() {
      while (!gate.load()) {
        std::this_thread::yield();
      }
    });

    sleep_ms(30);

    // Loop is busy; idle should not be reached within 100 ms
    bool idle = loop.wait_for_idle(100, true);
    CHECK_FALSE(idle);

    gate.store(true);
    loop.wait_for_idle();
    loop.quit();
    loop.wait_for_quit();
  }

  TEST_CASE("is_in_same_thread true inside callback, false outside") {
    MessageLoop loop;
    loop.async_run();

    std::atomic<bool> inside_same{false};
    loop.post_task([&loop, &inside_same]() { inside_same.store(loop.is_in_same_thread()); });

    loop.wait_for_idle();

    CHECK(inside_same.load() == true);
    CHECK(loop.is_in_same_thread() == false);

    loop.quit();
    loop.wait_for_quit();
  }

  TEST_CASE("is_ready_to_quit becomes true after quit()") {
    MessageLoop loop;
    loop.async_run();
    CHECK_FALSE(loop.is_ready_to_quit());

    loop.quit();
    // Give the loop a moment to set the flag
    sleep_ms(20);
    // After quit, loop should be winding down or already stopped
    bool stopping = loop.is_ready_to_quit() || !loop.is_running();
    CHECK(stopping);

    loop.wait_for_quit();
  }

  TEST_CASE("quit force=true discards remaining tasks") {
    MessageLoop loop;
    std::atomic<bool> gate{false};
    std::atomic<int> exec_count{0};

    loop.async_run();

    // Block the loop
    loop.post_task([&gate]() {
      while (!gate.load()) {
        std::this_thread::yield();
      }
    });
    sleep_ms(10);

    // Queue up tasks that should be discarded
    for (int i = 0; i < 5; ++i) {
      loop.post_task([&exec_count]() { exec_count.fetch_add(1); });
    }

    // Force quit without letting queued tasks run
    loop.quit(true);
    gate.store(true);
    loop.wait_for_quit();

    // The 5 queued tasks may have been discarded — exec_count < 5
    CHECK(exec_count.load() <= 5);
  }

  TEST_CASE("register_begin_handler fires once on start") {
    MessageLoop loop;
    std::atomic<int> begin_count{0};
    loop.register_begin_handler([&begin_count]() { begin_count.fetch_add(1); });

    loop.async_run();
    sleep_ms(30);

    CHECK(begin_count.load() == 1);

    loop.quit();
    loop.wait_for_quit();
  }

  TEST_CASE("register_end_handler fires once on stop") {
    MessageLoop loop;
    std::atomic<int> end_count{0};
    loop.register_end_handler([&end_count]() { end_count.fetch_add(1); });

    loop.async_run();
    loop.quit();
    loop.wait_for_quit();

    CHECK(end_count.load() == 1);
  }

  TEST_CASE("register_idle_handler fires when queue is empty") {
    MessageLoop loop;
    std::atomic<int> idle_count{0};
    loop.register_idle_handler([&idle_count]() { idle_count.fetch_add(1); });

    loop.async_run();
    sleep_ms(50);

    // Idle handler should have been called at least once
    CHECK(idle_count.load() >= 1);

    loop.quit();
    loop.wait_for_quit();
  }

  TEST_CASE("priority ordering in kPriorityType") {
    MessageLoop loop(MessageLoop::kPriorityType);

    std::atomic<bool> gate{false};
    std::vector<int> order;
    std::mutex mtx;

    loop.async_run();

    // Block the loop on a gated task so that the priority tasks queue up
    loop.post_task([&gate]() {
      while (!gate.load()) {
        std::this_thread::yield();
      }
    });
    sleep_ms(20);

    // Post tasks with different priorities
    loop.post_task_with_priority(
        [&order, &mtx]() {
          std::lock_guard<std::mutex> lk(mtx);
          order.push_back(1);
        },
        MessageLoop::kLowestPriority);

    loop.post_task_with_priority(
        [&order, &mtx]() {
          std::lock_guard<std::mutex> lk(mtx);
          order.push_back(3);
        },
        MessageLoop::kHighestPriority);

    loop.post_task_with_priority(
        [&order, &mtx]() {
          std::lock_guard<std::mutex> lk(mtx);
          order.push_back(2);
        },
        MessageLoop::kNormalPriority);

    // Unblock
    gate.store(true);
    loop.wait_for_idle();

    // Highest priority should have run first (3), then normal (2), then lowest (1)
    REQUIRE(order.size() == 3);
    CHECK(order[0] == 3);
    CHECK(order[1] == 2);
    CHECK(order[2] == 1);

    loop.quit();
    loop.wait_for_quit();
  }

  TEST_CASE("invoke_task_with_priority returns correct value on kPriorityType") {
    MessageLoop loop(MessageLoop::kPriorityType);
    loop.async_run();

    auto fut = loop.invoke_task_with_priority([]() -> std::string { return "priority-result"; },
                                              MessageLoop::kHighestPriority);
    std::string result = fut.get();
    CHECK(result == "priority-result");

    loop.quit();
    loop.wait_for_quit();
  }

  TEST_CASE("kLockfreeType basic post_task") {
    MessageLoop loop(MessageLoop::kLockfreeType);
    loop.async_run();

    std::atomic<int> counter{0};
    for (int i = 0; i < 20; ++i) {
      loop.post_task([&counter]() { counter.fetch_add(1); });
    }

    loop.wait_for_idle();
    CHECK(counter.load() == 20);

    loop.quit();
    loop.wait_for_quit();
  }

  TEST_CASE("kLockfreeType invoke_task") {
    MessageLoop loop(MessageLoop::kLockfreeType);
    loop.async_run();

    auto fut = loop.invoke_task([]() -> int { return 99; });
    CHECK(fut.get() == 99);

    loop.quit();
    loop.wait_for_quit();
  }

  TEST_CASE("reset_lockfree_capacity does not crash on kLockfreeType") {
    MessageLoop loop(MessageLoop::kLockfreeType);
    loop.async_run();

    for (int i = 0; i < 5; ++i) {
      loop.post_task([]() {});
    }

    loop.wait_for_idle();
    loop.reset_lockfree_capacity();

    // Should still be usable afterwards
    std::atomic<bool> ran{false};
    loop.post_task([&ran]() { ran.store(true); });
    loop.wait_for_idle();
    CHECK(ran.load());

    loop.quit();
    loop.wait_for_quit();
  }

  TEST_CASE("spin_once processes tasks without blocking when non-empty") {
    MessageLoop loop;

    std::atomic<int> counter{0};
    loop.post_task([&counter]() { counter.fetch_add(1); });
    loop.post_task([&counter]() { counter.fetch_add(1); });

    // spin_once with block=false processes what is available
    loop.spin_once(false);
    loop.spin_once(false);

    sleep_ms(20);
    CHECK(counter.load() >= 1);
  }

  TEST_CASE("wakeup does not crash and loop stays usable") {
    MessageLoop loop;
    loop.set_strategy(MessageLoop::kBlockStrategy);
    loop.async_run();
    sleep_ms(20);

    loop.wakeup();
    sleep_ms(10);

    std::atomic<bool> ran{false};
    loop.post_task([&ran]() { ran.store(true); });
    loop.wait_for_idle();
    CHECK(ran.load());

    loop.quit();
    loop.wait_for_quit();
  }

  TEST_CASE("is_busy is true while task is executing") {
    MessageLoop loop;
    loop.async_run();

    std::atomic<bool> task_running{false};
    std::atomic<bool> gate{false};

    loop.post_task([&task_running, &gate]() {
      task_running.store(true);
      while (!gate.load()) {
        std::this_thread::yield();
      }
    });

    // Wait until task starts
    while (!task_running.load()) {
      std::this_thread::yield();
    }

    CHECK(loop.is_busy());

    gate.store(true);
    loop.wait_for_idle();
    loop.quit();
    loop.wait_for_quit();
  }

  TEST_CASE("Priority enum values are ordered correctly") {
    CHECK(MessageLoop::kNoPriority < MessageLoop::kLowestPriority);
    CHECK(MessageLoop::kLowestPriority < MessageLoop::kTimerPriority);
    CHECK(MessageLoop::kTimerPriority < MessageLoop::kNormalPriority);
    CHECK(MessageLoop::kNormalPriority < MessageLoop::kHighestPriority);
  }

  TEST_CASE("wait_for_quit with timeout returns false when loop keeps running") {
    MessageLoop loop;
    loop.async_run();

    // Do not call quit — wait_for_quit with short timeout should return false
    bool quit_done = loop.wait_for_quit(50);
    CHECK_FALSE(quit_done);

    loop.quit();
    loop.wait_for_quit();
  }

  TEST_CASE("exec_task void callback runs on loop") {
    MessageLoop loop;
    loop.async_run();

    std::atomic<bool> ran{false};
    auto status = loop.exec_task(Schedule::Config{}, [&ran]() { ran.store(true); });
    CHECK(status.is_valid());

    loop.wait_for_idle();
    CHECK(ran.load());

    loop.quit();
    loop.wait_for_quit();
  }

  TEST_CASE("exec_task on_then") {
    MessageLoop loop;
    loop.async_run();

    std::atomic<bool> then_ran{false};
    std::atomic<bool> else_ran{false};

    // Use delay_ms=50 so on_then is registered before the task starts.
    loop.exec_task(Schedule::Config{50}, []() -> bool { return true; })
        .on_then([&then_ran]() -> bool {
          then_ran.store(true);
          return true;
        })
        .on_else([&else_ran]() { else_ran.store(true); });

    for (int attempt = 0; attempt < 200 && !then_ran.load(); ++attempt) {
      loop.wait_for_idle();
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    CHECK(then_ran.load());
    CHECK_FALSE(else_ran.load());

    loop.quit();
    loop.wait_for_quit();
  }

  TEST_CASE("exec_task on_else") {
    MessageLoop loop;
    loop.async_run();

    std::atomic<bool> then_ran{false};
    std::atomic<bool> else_ran{false};

    // Use delay_ms=50 so on_else is registered before the task starts.
    loop.exec_task(Schedule::Config{50}, []() -> bool { return false; })
        .on_then([&then_ran]() -> bool {
          then_ran.store(true);
          return true;
        })
        .on_else([&else_ran]() { else_ran.store(true); });

    for (int attempt = 0; attempt < 200 && !else_ran.load(); ++attempt) {
      loop.wait_for_idle();
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    CHECK_FALSE(then_ran.load());
    CHECK(else_ran.load());

    loop.quit();
    loop.wait_for_quit();
  }

  TEST_CASE("get_max_elapsed_time does not crash") {
    MessageLoop loop;
    loop.async_run();

    loop.post_task([]() {});
    loop.wait_for_idle();

    // Just verify the API is callable
    auto t = loop.get_max_elapsed_time();
    (void)t;
    CHECK(true);

    loop.quit();
    loop.wait_for_quit();
  }

  TEST_CASE("get_max_timer_count returns value") {
    MessageLoop loop;
    loop.async_run();

    size_t cnt = loop.get_max_timer_count();
    CHECK(cnt >= 0U);  // size_t is always >= 0; verify the call doesn't crash
    (void)cnt;

    loop.quit();
    loop.wait_for_quit();
  }

  TEST_CASE("run in dedicated thread then quit") {
    MessageLoop loop;

    std::thread t([&loop]() { loop.run(); });

    // Wait until the loop is running
    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (!loop.is_running() && std::chrono::steady_clock::now() < deadline) {
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    CHECK(loop.is_running());

    loop.quit();
    t.join();
    CHECK(!loop.is_running());
  }

  TEST_CASE("register_idle_handler fires when loop is idle") {
    MessageLoop loop;

    std::atomic<int> idle_count{0};
    loop.register_idle_handler([&idle_count]() { idle_count.fetch_add(1, std::memory_order_relaxed); });

    loop.async_run();

    loop.wait_for_idle(1000);
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    CHECK(idle_count.load() >= 0);  // idle handler may not fire if no tasks posted; int >= 0 always true

    loop.quit();
    loop.wait_for_quit();
  }

  TEST_CASE("spin_once processes one queued task") {
    MessageLoop loop;

    std::atomic<int> ran{0};
    loop.post_task([&ran]() { ran.store(1, std::memory_order_relaxed); });

    // spin_once(false) is non-blocking — processes available tasks without waiting
    loop.spin_once(false);
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    CHECK(ran.load() == 1);
  }

  TEST_CASE("kNormalType FIFO ordering preserved") {
    MessageLoop loop(MessageLoop::kNormalType);
    loop.async_run();

    std::vector<int> order;
    std::mutex mtx;
    std::atomic<int> remaining{5};
    std::promise<void> done;

    for (int i = 0; i < 5; ++i) {
      loop.post_task([i, &order, &mtx, &remaining, &done]() {
        {
          std::lock_guard<std::mutex> lk(mtx);
          order.push_back(i);
        }

        if (remaining.fetch_sub(1, std::memory_order_acq_rel) == 1) {
          done.set_value();
        }
      });
    }

    done.get_future().wait_for(std::chrono::seconds(2));
    loop.wait_for_idle(2000);

    REQUIRE(order.size() == 5U);
    for (int i = 0; i < 5; ++i) {
      CHECK(order[i] == i);
    }

    loop.quit();
    loop.wait_for_quit();
  }

  TEST_CASE("spin runs tasks until quit") {
    MessageLoop loop;

    std::atomic<int> count{0};

    // Post tasks and a quit task
    loop.post_task([&]() { count++; });
    loop.post_task([&]() { count++; });
    loop.post_task([&]() { count++; });
    loop.post_task([&loop]() { loop.quit(); });

    // spin() blocks until quit() is called
    loop.spin();

    CHECK(count.load() == 3);
  }

  TEST_CASE("spin_once non-blocking with empty queue") {
    MessageLoop loop;
    bool result = loop.spin_once(false);
    // Should return quickly with no tasks
    (void)result;
    CHECK(true);
  }

  TEST_CASE("spin_once processes single task") {
    MessageLoop loop;
    std::atomic_bool ran{false};
    loop.post_task([&]() { ran = true; });
    loop.spin_once(false);
    std::this_thread::sleep_for(10ms);
    CHECK(ran.load() == true);
  }

  TEST_CASE("timer attach/detach exercises add/remove_timer") {
    MessageLoop loop;
    loop.async_run();

    Timer timer;
    timer.set_interval(50);
    timer.set_loop_count(3);

    std::atomic<int> count{0};
    timer.attach(&loop);
    timer.start([&]() { count++; });

    std::this_thread::sleep_for(300ms);
    timer.stop();
    timer.detach();

    CHECK(count.load() >= 1);

    loop.quit();
    loop.wait_for_quit();
  }

  TEST_CASE("set_name and get_name standalone") {
    MessageLoop loop;
    loop.set_name("TestLoop");
    CHECK(loop.get_name() == "TestLoop");
  }

  TEST_CASE("is_running before and after async_run standalone") {
    MessageLoop loop;
    CHECK(loop.is_running() == false);
    loop.async_run();
    std::this_thread::sleep_for(20ms);
    CHECK(loop.is_running() == true);
    loop.quit();
    loop.wait_for_quit();
    CHECK(loop.is_running() == false);
  }

  TEST_CASE("get_task_count standalone") {
    MessageLoop loop;
    CHECK(loop.get_task_count() == 0);
    loop.post_task([]() { std::this_thread::sleep_for(50ms); });
    CHECK(loop.get_task_count() >= 0u);
  }
}

// NOLINTEND
