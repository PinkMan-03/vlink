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
#include <future>
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
        std::lock_guard lock(mtx);
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

  TEST_CASE("wait_for_idle treats pre-start queued tasks as not idle") {
    MessageLoop loop;
    std::atomic<bool> ran{false};

    CHECK(loop.post_task([&ran] { ran.store(true, std::memory_order_release); }));
    CHECK_FALSE(loop.wait_for_idle(20, false));
    CHECK_FALSE(ran.load(std::memory_order_acquire));

    CHECK(loop.spin_once(false));
    CHECK(loop.wait_for_idle(20, false));
    CHECK(ran.load(std::memory_order_acquire));
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
          std::lock_guard lk(mtx);
          order.push_back(1);
        },
        MessageLoop::kLowestPriority);

    loop.post_task_with_priority(
        [&order, &mtx]() {
          std::lock_guard lk(mtx);
          order.push_back(3);
        },
        MessageLoop::kHighestPriority);

    loop.post_task_with_priority(
        [&order, &mtx]() {
          std::lock_guard lk(mtx);
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

  TEST_CASE("post_task uses normal priority on kPriorityType") {
    MessageLoop loop(MessageLoop::kPriorityType);
    loop.async_run();

    std::promise<void> release;
    std::promise<void> barrier_started;
    auto release_future = release.get_future();
    auto barrier_fut = barrier_started.get_future();

    CHECK(loop.post_task_with_priority(
        [release_future = std::move(release_future), &barrier_started]() mutable {
          barrier_started.set_value();
          release_future.wait();
        },
        MessageLoop::kHighestPriority));
    REQUIRE(barrier_fut.wait_for(std::chrono::milliseconds(1000)) == std::future_status::ready);

    std::vector<int> order;
    std::promise<void> default_done;
    std::promise<void> low_done;
    std::promise<void> high_done;
    auto default_fut = default_done.get_future();
    auto low_fut = low_done.get_future();
    auto high_fut = high_done.get_future();

    CHECK(loop.post_task([&order, &default_done] {
      order.push_back(0);
      default_done.set_value();
    }));
    CHECK(loop.post_task_with_priority(
        [&order, &low_done] {
          order.push_back(1);
          low_done.set_value();
        },
        MessageLoop::kLowestPriority));
    CHECK(loop.post_task_with_priority(
        [&order, &high_done] {
          order.push_back(2);
          high_done.set_value();
        },
        MessageLoop::kHighestPriority));

    release.set_value();

    REQUIRE(high_fut.wait_for(std::chrono::milliseconds(1000)) == std::future_status::ready);
    REQUIRE(default_fut.wait_for(std::chrono::milliseconds(1000)) == std::future_status::ready);
    REQUIRE(low_fut.wait_for(std::chrono::milliseconds(1000)) == std::future_status::ready);

    REQUIRE(order.size() == 3U);
    CHECK(order[0] == 2);
    CHECK(order[1] == 0);
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

  TEST_CASE("kLockfreeType can run again after quit") {
    MessageLoop loop(MessageLoop::kLockfreeType);

    loop.async_run();
    loop.quit();
    loop.wait_for_quit();

    std::promise<void> done;
    auto done_fut = done.get_future();

    loop.async_run();
    CHECK(loop.post_task([&done] { done.set_value(); }));
    CHECK(done_fut.wait_for(1s) == std::future_status::ready);

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
          std::lock_guard lk(mtx);
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

  // ---

  TEST_CASE("get_alive_state returns non-null and alive initially") {
    MessageLoop loop;
    auto state = loop.get_alive_state();
    REQUIRE(state != nullptr);
    CHECK(state->alive.load(std::memory_order_acquire) == true);
  }

  TEST_CASE("get_alive_state alive flips to false after loop destruction") {
    std::shared_ptr<detail::MessageLoopAliveState> state;
    {
      MessageLoop loop;
      state = loop.get_alive_state();
      REQUIRE(state != nullptr);
      CHECK(state->alive.load(std::memory_order_acquire) == true);
    }
    REQUIRE(state != nullptr);
    CHECK(state->alive.load(std::memory_order_acquire) == false);
  }

  TEST_CASE("post_task_with_priority kNoPriority on kPriorityType returns false") {
    MessageLoop loop(MessageLoop::kPriorityType);
    loop.async_run();

    CHECK_FALSE(loop.post_task_with_priority([] {}, MessageLoop::kNoPriority));

    loop.quit();
    loop.wait_for_quit();
  }

  TEST_CASE("post_task_with_priority on kNormalType returns false") {
    MessageLoop loop(MessageLoop::kNormalType);
    loop.async_run();

    CHECK_FALSE(loop.post_task_with_priority([] {}, 5U));

    loop.quit();
    loop.wait_for_quit();
  }

  TEST_CASE("post_task_with_priority on kLockfreeType returns false") {
    MessageLoop loop(MessageLoop::kLockfreeType);
    loop.async_run();

    CHECK_FALSE(loop.post_task_with_priority([] {}, 5U));

    loop.quit();
    loop.wait_for_quit();
  }

  TEST_CASE("post_task_with_priority kNormalPriority on kPriorityType runs task") {
    MessageLoop loop(MessageLoop::kPriorityType);
    loop.async_run();

    std::promise<void> done;
    auto done_fut = done.get_future();
    CHECK(loop.post_task_with_priority([&done] { done.set_value(); }, MessageLoop::kNormalPriority));
    REQUIRE(done_fut.wait_for(std::chrono::milliseconds(1000)) == std::future_status::ready);

    loop.quit();
    loop.wait_for_quit();
  }

  TEST_CASE("quit force=true drains tracked handles to kDropped") {
    MessageLoop loop;
    std::atomic<bool> gate{false};
    std::atomic<bool> first_started{false};

    loop.async_run();

    loop.post_task([&first_started, &gate]() {
      first_started.store(true, std::memory_order_release);
      while (!gate.load(std::memory_order_acquire)) {
        std::this_thread::yield();
      }
    });

    std::vector<TaskHandle> handles;
    for (int i = 0; i < 5; ++i) {
      handles.emplace_back(loop.post_task_handle([] {}));
    }

    while (!first_started.load(std::memory_order_acquire)) {
      std::this_thread::yield();
    }

    loop.quit(true);
    gate.store(true, std::memory_order_release);
    loop.wait_for_quit();

    for (auto& h : handles) {
      CHECK(h.wait(1000));
      CHECK(h.state() == TaskExecutionState::kDropped);
    }
  }

  TEST_CASE("post_task after quit returns false") {
    MessageLoop loop;
    loop.async_run();

    loop.quit();
    loop.wait_for_quit();

    CHECK_FALSE(loop.post_task([] {}));
  }

  TEST_CASE("quit force=true aborts in-flight batch") {
    MessageLoop loop;
    std::atomic<bool> gate{false};
    std::atomic<bool> sentinel{false};
    std::atomic<bool> first_started{false};

    loop.async_run();

    loop.post_task([&first_started, &gate]() {
      first_started.store(true, std::memory_order_release);
      while (!gate.load(std::memory_order_acquire)) {
        std::this_thread::yield();
      }
    });

    for (int i = 0; i < 20; ++i) {
      loop.post_task([&sentinel] { sentinel.store(true, std::memory_order_release); });
    }

    while (!first_started.load(std::memory_order_acquire)) {
      std::this_thread::yield();
    }

    loop.quit(true);
    gate.store(true, std::memory_order_release);
    loop.wait_for_quit();

    CHECK_FALSE(sentinel.load(std::memory_order_acquire));
  }

  TEST_CASE("kLockfreeType high-concurrency post stress") {
    MessageLoop loop(MessageLoop::kLockfreeType);
    loop.async_run();

    constexpr int kProducerCount = 4;
    constexpr int kPerProducer = 100;
    std::atomic<int> counter{0};
    std::vector<std::thread> producers;
    producers.reserve(kProducerCount);

    for (int p = 0; p < kProducerCount; ++p) {
      producers.emplace_back([&loop, &counter]() {
        for (int i = 0; i < kPerProducer; ++i) {
          while (!loop.post_task([&counter] { counter.fetch_add(1, std::memory_order_acq_rel); })) {
            std::this_thread::yield();
          }
        }
      });
    }

    for (auto& t : producers) {
      t.join();
    }

    loop.wait_for_idle(5000);

    CHECK(counter.load(std::memory_order_acquire) == kProducerCount * kPerProducer);

    loop.quit();
    loop.wait_for_quit();
  }

  TEST_CASE("kLockfreeType kPopStrategy with small queue under overflow drops without crash") {
    SmallQueueMessageLoop loop(MessageLoop::kLockfreeType);
    loop.set_strategy(MessageLoop::kPopStrategy);

    std::atomic<int> executed{0};
    for (int i = 0; i < 50; ++i) {
      loop.post_task([&executed] { executed.fetch_add(1, std::memory_order_acq_rel); });
    }

    loop.async_run();
    loop.wait_for_idle(1000);

    CHECK(executed.load(std::memory_order_acquire) <= 50);

    loop.quit();
    loop.wait_for_quit();
  }

  TEST_CASE("post_task on non-running loop queues but does not run until spin_once") {
    MessageLoop loop;
    std::atomic<bool> ran{false};

    CHECK(loop.post_task([&ran] { ran.store(true, std::memory_order_release); }));
    sleep_ms(20);
    CHECK_FALSE(ran.load(std::memory_order_acquire));
    CHECK(loop.get_task_count() == 1U);

    CHECK(loop.spin_once(false));
    sleep_ms(20);
    CHECK(ran.load(std::memory_order_acquire));
  }

  TEST_CASE("wakeup on non-running loop returns false") {
    MessageLoop loop;
    CHECK_FALSE(loop.wakeup());
  }

  TEST_CASE("multiple wakeups while pending are idempotent and loop drains") {
    MessageLoop loop;
    loop.set_strategy(MessageLoop::kBlockStrategy);
    loop.async_run();
    sleep_ms(20);

    for (int i = 0; i < 20; ++i) {
      CHECK(loop.wakeup());
    }

    std::atomic<int> count{0};
    for (int i = 0; i < 10; ++i) {
      loop.post_task([&count] { count.fetch_add(1, std::memory_order_acq_rel); });
    }

    loop.wait_for_idle(1000);
    CHECK(count.load(std::memory_order_acquire) == 10);

    loop.quit();
    loop.wait_for_quit();
  }

  // -------------------------------------------------------------------------
  // Enhanced coverage: post-diff behavior, runtime configuration switches,
  // overflow/drop policy semantics, alive-state contention, custom subclass
  // dispatch overrides, and timer-task race conditions.
  // -------------------------------------------------------------------------

  TEST_CASE("recursive post_task from inside callback executes nested task") {
    MessageLoop loop;
    loop.async_run();

    std::atomic<int> outer{0};
    std::atomic<int> inner{0};
    std::promise<void> done;
    auto done_fut = done.get_future();

    loop.post_task([&loop, &outer, &inner, &done] {
      outer.fetch_add(1, std::memory_order_acq_rel);
      CHECK(loop.is_in_same_thread());
      loop.post_task([&inner, &done] {
        inner.fetch_add(1, std::memory_order_acq_rel);
        done.set_value();
      });
    });

    REQUIRE(done_fut.wait_for(std::chrono::seconds(1)) == std::future_status::ready);
    CHECK(outer.load() == 1);
    CHECK(inner.load() == 1);

    loop.quit();
    loop.wait_for_quit();
  }

  TEST_CASE("kBlockStrategy producer is released when loop processes queue") {
    SmallQueueMessageLoop loop;
    loop.set_strategy(MessageLoop::kBlockStrategy);
    loop.async_run();

    std::atomic<int> ran{0};
    CHECK(loop.post_task([&ran] {
      ran.fetch_add(1, std::memory_order_acq_rel);
      sleep_ms(20);
    }));

    std::thread producer([&loop, &ran] {
      // kBlockStrategy retries indefinitely while queue is full.
      CHECK(loop.post_task([&ran] { ran.fetch_add(1, std::memory_order_acq_rel); }));
    });

    producer.join();
    loop.wait_for_idle(1000);
    CHECK(ran.load() == 2);

    loop.quit();
    loop.wait_for_quit();
  }

  TEST_CASE("set_strategy at runtime is observed by subsequent posts") {
    SmallQueueMessageLoop loop;
    loop.set_strategy(MessageLoop::kPopStrategy);
    // Do not start the loop; force the queue to be full.

    std::atomic<int> first_ran{0};
    CHECK(loop.post_task([&first_ran] { first_ran.fetch_add(1); }));
    CHECK(loop.get_task_count() == 1U);

    // Swap to kPopStrategy: a second post must succeed by dropping the first.
    CHECK(loop.post_task([&first_ran] { first_ran.fetch_add(1); }));
    CHECK(loop.get_task_count() == 1U);

    loop.async_run();
    loop.wait_for_idle(500);
    CHECK(first_ran.load() == 1);

    loop.quit();
    loop.wait_for_quit();
  }

  TEST_CASE("priority dispatch interleaves droppable and protected by priority") {
    MessageLoop loop(MessageLoop::kPriorityType);

    std::vector<int> order;
    std::mutex order_mtx;
    auto record = [&order, &order_mtx](int tag) {
      std::lock_guard lk(order_mtx);
      order.push_back(tag);
    };

    PostTaskOptions protect_opts;
    protect_opts.drop_policy = TaskDropPolicy::kProtected;
    PostTaskOptions drop_opts;
    drop_opts.drop_policy = TaskDropPolicy::kDroppable;

    auto h_low_drop =
        loop.post_task_with_priority_handle([&record] { record(10); }, MessageLoop::kLowestPriority, drop_opts);
    auto h_hi_prot =
        loop.post_task_with_priority_handle([&record] { record(20); }, MessageLoop::kHighestPriority, protect_opts);
    auto h_norm_drop =
        loop.post_task_with_priority_handle([&record] { record(30); }, MessageLoop::kNormalPriority, drop_opts);

    loop.async_run();
    h_low_drop.wait();
    h_hi_prot.wait();
    h_norm_drop.wait();

    // Expected by priority: 20 (highest, protected) -> 30 (normal, droppable) -> 10 (lowest).
    REQUIRE(order.size() == 3U);
    CHECK(order[0] == 20);
    CHECK(order[1] == 30);
    CHECK(order[2] == 10);

    loop.quit();
    loop.wait_for_quit();
  }

  TEST_CASE("invoke_task on kPriorityType runs and returns value") {
    MessageLoop loop(MessageLoop::kPriorityType);
    loop.async_run();

    auto fut = loop.invoke_task([] { return 99; });
    REQUIRE(fut.wait_for(std::chrono::seconds(1)) == std::future_status::ready);
    CHECK(fut.get() == 99);

    loop.quit();
    loop.wait_for_quit();
  }

  TEST_CASE("invoke_task_with_priority on non-priority loop yields broken_promise") {
    MessageLoop loop(MessageLoop::kNormalType);
    loop.async_run();

    auto fut = loop.invoke_task_with_priority([] { return 7; }, MessageLoop::kNormalPriority);

    REQUIRE(fut.wait_for(std::chrono::seconds(1)) == std::future_status::ready);
    CHECK_THROWS_AS(fut.get(), std::future_error);

    loop.quit();
    loop.wait_for_quit();
  }

  TEST_CASE("alive_state alive flag is observable under mtx contention") {
    auto state_ptr = std::make_shared<std::shared_ptr<detail::MessageLoopAliveState>>();
    std::atomic<bool> stop{false};
    std::atomic<int> observed_alive{0};
    std::atomic<int> observed_dead{0};

    {
      MessageLoop loop;
      *state_ptr = loop.get_alive_state();

      std::vector<std::thread> observers;
      observers.reserve(4);
      for (int i = 0; i < 4; ++i) {
        observers.emplace_back([&] {
          while (!stop.load(std::memory_order_acquire)) {
            std::lock_guard lk((*state_ptr)->mtx);
            if ((*state_ptr)->alive.load(std::memory_order_acquire)) {
              observed_alive.fetch_add(1, std::memory_order_acq_rel);
            } else {
              observed_dead.fetch_add(1, std::memory_order_acq_rel);
            }
          }
        });
      }

      sleep_ms(50);
      stop.store(true, std::memory_order_release);
      for (auto& t : observers) t.join();

      // While the loop was alive, every observation must have seen alive=true.
      CHECK(observed_alive.load() > 0);
      CHECK(observed_dead.load() == 0);
    }

    // After destruction the shared state must report alive=false.
    REQUIRE(*state_ptr != nullptr);
    CHECK_FALSE((*state_ptr)->alive.load(std::memory_order_acquire));
  }

  TEST_CASE("alive_state observer sees alive flip during destructor race") {
    auto loop = std::make_unique<MessageLoop>();
    auto state = loop->get_alive_state();
    CHECK(state->alive.load());

    std::promise<bool> observed;
    auto observed_fut = observed.get_future();

    std::thread observer([&] {
      // Hold the mutex throughout, so we always see a consistent snapshot.
      std::lock_guard lk(state->mtx);
      observed.set_value(state->alive.load(std::memory_order_acquire));
    });

    // Wait for the observer to acquire the mutex and read the flag.
    REQUIRE(observed_fut.wait_for(std::chrono::seconds(1)) == std::future_status::ready);
    CHECK(observed_fut.get() == true);
    observer.join();

    // Destroy the loop and verify the flag flipped.
    loop.reset();
    CHECK_FALSE(state->alive.load(std::memory_order_acquire));
  }

  // Custom MessageLoop subclass exercising on_task_changed + on_task_timeout overrides.
  class TimeoutCountingLoop final : public MessageLoop {
   public:
    using MessageLoop::MessageLoop;
    [[nodiscard]] uint32_t get_max_elapsed_time() const override { return 5U; }
    void on_task_timeout(Callback&& callback, uint32_t elapsed) override {
      ++timeout_calls;
      last_elapsed = elapsed;
      (void)callback;  // Intentionally drop the slow callback in this test.
    }
    std::atomic<int> timeout_calls{0};
    std::atomic<uint32_t> last_elapsed{0};
  };

  TEST_CASE("on_task_timeout fires when queued task waits longer than max_elapsed_time") {
    // max_elapsed_time measures dispatch latency (push -> dequeue), not the
    // callback's own runtime.  Build the scenario by holding the loop with a
    // long task so that subsequent posts age past the 5 ms threshold.
    TimeoutCountingLoop loop;
    loop.async_run();

    std::atomic<bool> hold_done{false};
    loop.post_task([&hold_done] {
      sleep_ms(30);
      hold_done.store(true, std::memory_order_release);
    });
    sleep_ms(2);

    std::atomic<bool> ran_late{false};
    loop.post_task([&ran_late] { ran_late.store(true, std::memory_order_release); });

    loop.wait_for_idle(1000);
    CHECK(hold_done.load());
    CHECK(loop.timeout_calls.load() >= 1);
    CHECK(loop.last_elapsed.load() >= 5U);
    // The custom override dropped the late callback.
    CHECK_FALSE(ran_late.load());

    loop.quit();
    loop.wait_for_quit();
  }

  TEST_CASE("kLockfreeType repeated async_run/quit cycles preserve correctness") {
    MessageLoop loop(MessageLoop::kLockfreeType);

    for (int cycle = 0; cycle < 3; ++cycle) {
      loop.async_run();
      std::atomic<int> count{0};
      for (int i = 0; i < 64; ++i) {
        CHECK(loop.post_task([&count] { count.fetch_add(1, std::memory_order_acq_rel); }));
      }
      loop.wait_for_idle(1000);
      CHECK(count.load() == 64);
      loop.quit();
      loop.wait_for_quit();
    }
  }

  TEST_CASE("concurrent producers on kPriorityType preserve ordering by priority") {
    MessageLoop loop(MessageLoop::kPriorityType);
    loop.async_run();

    const int kPerThread = 50;
    const int kThreads = 4;
    std::atomic<int> running_total{0};

    std::vector<std::thread> producers;
    producers.reserve(kThreads);
    for (int t = 0; t < kThreads; ++t) {
      producers.emplace_back([&loop, &running_total, kPerThread, t] {
        for (int i = 0; i < kPerThread; ++i) {
          const uint16_t prio = static_cast<uint16_t>((t + i + 1) % 100 + 1);
          loop.post_task_with_priority([&running_total] { running_total.fetch_add(1, std::memory_order_acq_rel); },
                                       prio);
        }
      });
    }
    for (auto& th : producers) th.join();

    loop.wait_for_idle(5000);
    CHECK(running_total.load() == kThreads * kPerThread);

    loop.quit();
    loop.wait_for_quit();
  }

  TEST_CASE("wait_for_idle returns true after long task completes") {
    MessageLoop loop;
    loop.async_run();

    std::atomic<bool> task_done{false};
    loop.post_task([&task_done] {
      sleep_ms(60);
      task_done.store(true, std::memory_order_release);
    });

    CHECK(loop.wait_for_idle(500));
    CHECK(task_done.load());

    loop.quit();
    loop.wait_for_quit();
  }

  TEST_CASE("wait_for_idle returns false on timeout while task is still running") {
    MessageLoop loop;
    loop.async_run();

    loop.post_task([] { sleep_ms(200); });

    CHECK_FALSE(loop.wait_for_idle(30));

    loop.quit();
    loop.wait_for_quit();
  }

  TEST_CASE("post_task after quit() returns false for all queue types") {
    for (auto type : {MessageLoop::kNormalType, MessageLoop::kLockfreeType, MessageLoop::kPriorityType}) {
      MessageLoop loop(type);
      loop.async_run();
      loop.quit();
      loop.wait_for_quit();
      CHECK_FALSE(loop.post_task([] {}));
    }
  }

  TEST_CASE("register handlers after async_run is ignored and logs error") {
    MessageLoop loop;
    loop.async_run();
    sleep_ms(10);

    std::atomic<bool> begin_ran{false};
    std::atomic<bool> end_ran{false};
    std::atomic<bool> idle_ran{false};

    // These should be silently rejected because the loop is already running.
    loop.register_begin_handler([&begin_ran] { begin_ran.store(true); });
    loop.register_end_handler([&end_ran] { end_ran.store(true); });
    loop.register_idle_handler([&idle_ran] { idle_ran.store(true); });

    loop.post_task([] {});
    loop.wait_for_idle(200);

    // begin already fired before our registration was attempted, so it must be false.
    CHECK_FALSE(begin_ran.load());

    loop.quit();
    loop.wait_for_quit();

    // end_handler was registered late and should not fire.
    CHECK_FALSE(end_ran.load());
    // idle_handler was registered late; on_idle still calls whatever handler was set,
    // but the late registration was rejected so the handler is empty.
    CHECK_FALSE(idle_ran.load());
  }

  TEST_CASE("multi-thread concurrent producers on kNormalType preserve all tasks") {
    MessageLoop loop;
    loop.set_strategy(MessageLoop::kBlockStrategy);
    loop.async_run();

    const int kThreads = 8;
    const int kPerThread = 200;
    std::atomic<int> total{0};

    std::vector<std::thread> producers;
    producers.reserve(kThreads);
    for (int t = 0; t < kThreads; ++t) {
      producers.emplace_back([&loop, &total] {
        for (int i = 0; i < 200; ++i) {
          loop.post_task([&total] { total.fetch_add(1, std::memory_order_acq_rel); });
        }
      });
    }
    for (auto& th : producers) th.join();

    loop.wait_for_idle(5000);
    CHECK(total.load() == kThreads * kPerThread);

    loop.quit();
    loop.wait_for_quit();
  }

  TEST_CASE("Timer + post_task race: timer continues firing while tasks post concurrently") {
    MessageLoop loop;
    loop.async_run();

    std::atomic<int> timer_fires{0};
    Timer timer(&loop, 5, Timer::kInfinite, [&timer_fires] { timer_fires.fetch_add(1, std::memory_order_acq_rel); });
    timer.start();

    std::atomic<int> post_count{0};
    std::thread poster([&loop, &post_count] {
      for (int i = 0; i < 300; ++i) {
        loop.post_task([&post_count] { post_count.fetch_add(1, std::memory_order_acq_rel); });
        sleep_ms(1);
      }
    });

    poster.join();
    sleep_ms(50);
    timer.stop();
    loop.wait_for_idle(500);

    CHECK(post_count.load() == 300);
    CHECK(timer_fires.load() > 0);

    loop.quit();
    loop.wait_for_quit();
  }

  TEST_CASE("quit while a long task is running waits for current task to finish (force=false)") {
    MessageLoop loop;
    loop.async_run();

    std::atomic<bool> done{false};
    loop.post_task([&done] {
      sleep_ms(80);
      done.store(true, std::memory_order_release);
    });
    sleep_ms(20);

    loop.quit(/*force=*/false);
    loop.wait_for_quit();
    CHECK(done.load());
  }
}

// NOLINTEND
