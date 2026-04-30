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

#include "./base/multi_loop.h"

#include <doctest/doctest.h>

#include <atomic>
#include <chrono>
#include <future>
#include <mutex>
#include <thread>
#include <vector>

//
#include "../common_test.h"

// ---------------------------------------------------------------------------
// Each test is a separate TEST_CASE inside the TEST_SUITE.
// SUBCASE re-entry causes the TEST_CASE body to re-run for every SUBCASE,
// which leads to rapid MultiLoop (thread pool) creation/destruction and
// intermittent SIGSEGV from thread churn.
// ---------------------------------------------------------------------------

TEST_SUITE("base-MultiLoop") {
  TEST_CASE("default constructor") {
    MultiLoop loop;
    CHECK(!loop.is_running());
  }

  TEST_CASE("explicit thread_num constructor") {
    MultiLoop loop(2);
    CHECK(!loop.is_running());
  }

  TEST_CASE("constructor with Type overload") {
    MultiLoop loop_normal(2, MessageLoop::kNormalType);
    CHECK(!loop_normal.is_running());

    MultiLoop loop_lockfree(2, MessageLoop::kLockfreeType);
    CHECK(!loop_lockfree.is_running());
  }

  TEST_CASE("async_run starts the loop") {
    MultiLoop loop(2);

    bool started = loop.async_run();
    CHECK(started);
    CHECK(loop.is_running());

    loop.quit();
    loop.wait_for_quit(2000);
  }

  TEST_CASE("async_run returns false if already running") {
    MultiLoop loop(2);

    loop.async_run();
    bool second = loop.async_run();
    CHECK(!second);

    loop.quit();
    loop.wait_for_quit(2000);
  }

  TEST_CASE("post_task executes on a worker thread") {
    MultiLoop loop(2);
    loop.async_run();

    std::promise<int> promise;
    auto future = promise.get_future();

    loop.post_task([&promise]() { promise.set_value(42); });

    auto status = future.wait_for(2s);
    CHECK(status == std::future_status::ready);
    CHECK(future.get() == 42);

    loop.quit();
    loop.wait_for_quit(2000);
  }

  TEST_CASE("multiple post_task calls all execute") {
    constexpr int kCount = 100;

    MultiLoop loop(4);
    loop.async_run();

    std::atomic<int> counter{0};
    std::atomic<int> remaining{kCount};
    std::promise<void> all_done;
    auto future = all_done.get_future();

    for (int i = 0; i < kCount; ++i) {
      loop.post_task([&counter, &remaining, &all_done]() {
        counter.fetch_add(1, std::memory_order_relaxed);

        if (remaining.fetch_sub(1, std::memory_order_acq_rel) == 1) {
          all_done.set_value();
        }
      });
    }

    bool idle = loop.wait_for_idle(3000);
    CHECK(idle);
    auto status = future.wait_for(3s);
    CHECK(status == std::future_status::ready);
    CHECK(counter.load() == kCount);

    loop.quit();
    loop.wait_for_quit(2000);
  }

  TEST_CASE("wait_for_idle returns true after all tasks complete") {
    MultiLoop loop(2);
    loop.async_run();

    std::atomic<int> done{0};
    std::atomic<int> remaining{20};
    std::promise<void> all_done;
    auto future = all_done.get_future();

    for (int i = 0; i < 20; ++i) {
      loop.post_task([&done, &remaining, &all_done]() {
        std::this_thread::sleep_for(5ms);
        done.fetch_add(1, std::memory_order_relaxed);

        if (remaining.fetch_sub(1, std::memory_order_acq_rel) == 1) {
          all_done.set_value();
        }
      });
    }

    bool result = loop.wait_for_idle(5000);
    CHECK(result);
    auto status = future.wait_for(5s);
    CHECK(status == std::future_status::ready);
    CHECK(done.load() == 20);

    loop.quit();
    loop.wait_for_quit(2000);
  }

  TEST_CASE("quit stops the loop") {
    MultiLoop loop(2);
    loop.async_run();

    loop.quit();
    bool stopped = loop.wait_for_quit(2000);

    CHECK(stopped);
    CHECK(!loop.is_running());
  }

  TEST_CASE("is_in_same_thread returns true from worker") {
    MultiLoop loop(2);
    loop.async_run();

    std::promise<bool> promise;
    auto future = promise.get_future();

    loop.post_task([&loop, &promise]() { promise.set_value(loop.is_in_same_thread()); });

    auto status = future.wait_for(2s);
    CHECK(status == std::future_status::ready);
    CHECK(future.get() == true);

    loop.quit();
    loop.wait_for_quit(2000);
  }

  TEST_CASE("is_in_same_thread returns false from test thread") {
    MultiLoop loop(2);
    loop.async_run();

    CHECK(!loop.is_in_same_thread());

    loop.quit();
    loop.wait_for_quit(2000);
  }

  TEST_CASE("invoke_task returns a future") {
    MultiLoop loop(2);
    loop.async_run();

    auto future = loop.invoke_task([]() -> std::string { return "hello_from_worker"; });

    auto status = future.wait_for(2s);
    CHECK(status == std::future_status::ready);
    CHECK(future.get() == "hello_from_worker");

    loop.quit();
    loop.wait_for_quit(2000);
  }

  TEST_CASE("tasks run concurrently on multiple threads") {
    constexpr int kTasks = 4;

    MultiLoop loop(4);
    loop.async_run();

    std::atomic<int> running{0};
    std::atomic<int> max_concurrent{0};
    std::vector<std::future<void>> task_futures;

    std::atomic<int> latch{kTasks};

    for (int i = 0; i < kTasks; ++i) {
      auto fut = loop.invoke_task([&running, &max_concurrent, &latch]() {
        int cur = running.fetch_add(1, std::memory_order_acq_rel) + 1;

        int old_max = max_concurrent.load(std::memory_order_acquire);

        while (cur > old_max && !max_concurrent.compare_exchange_weak(old_max, cur, std::memory_order_acq_rel)) {
        }

        latch.fetch_sub(1, std::memory_order_acq_rel);

        while (latch.load(std::memory_order_acquire) > 0) {
          std::this_thread::yield();
        }

        running.fetch_sub(1, std::memory_order_acq_rel);
      });

      task_futures.push_back(std::move(fut));
    }

    for (auto& f : task_futures) {
      f.wait_for(5s);
    }

    CHECK(max_concurrent.load() > 1);

    loop.quit();
    loop.wait_for_quit(2000);
  }

  TEST_CASE("set_name and get_name") {
    MultiLoop loop(2);
    loop.set_name("test_loop");
    CHECK(loop.get_name() == "test_loop");
  }

  TEST_CASE("get_type reflects construction type") {
    MultiLoop normal_loop(2, MessageLoop::kNormalType);
    CHECK(normal_loop.get_type() == MessageLoop::kNormalType);

    MultiLoop lf_loop(2, MessageLoop::kLockfreeType);
    CHECK(lf_loop.get_type() == MessageLoop::kLockfreeType);
  }

  TEST_CASE("quit with force discards pending tasks") {
    MultiLoop loop(1);
    loop.async_run();

    std::atomic<int> counter{0};

    std::atomic<bool> release{false};
    loop.post_task([&release]() {
      while (!release.load(std::memory_order_acquire)) {
        std::this_thread::sleep_for(1ms);
      }
    });

    for (int i = 0; i < 50; ++i) {
      loop.post_task([&counter]() { counter.fetch_add(1, std::memory_order_relaxed); });
    }

    release.store(true, std::memory_order_release);
    loop.quit(true);
    loop.wait_for_quit(3000);

    CHECK(counter.load() <= 50);
  }

  TEST_CASE("post_task_with_priority on priority loop") {
    MultiLoop loop(2, MessageLoop::kPriorityType);
    loop.async_run();

    std::atomic<int> ran{0};

    loop.post_task_with_priority([&ran]() { ran.store(1); }, MessageLoop::kNormalPriority);

    loop.wait_for_idle(2000);
    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);

    while (ran.load() == 0 && std::chrono::steady_clock::now() < deadline) {
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    CHECK(ran.load() == 1);

    loop.quit();
    loop.wait_for_quit(2000);
  }

  TEST_CASE("destructor auto-quits without deadlock") {
    {
      MultiLoop loop(2);
      loop.async_run();

      std::atomic<bool> done{false};
      loop.post_task([&done] {
        std::this_thread::sleep_for(5ms);
        done.store(true);
      });

      // Wait for the task to finish before letting the destructor run,
      // otherwise the thread pool may be torn down while a worker is
      // still executing, causing SIGSEGV.
      auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);

      while (!done.load() && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(1ms);
      }
    }
  }

  TEST_CASE("register_begin_handler fires on start") {
    std::atomic<int> begin_count{0};

    MultiLoop loop(3);
    loop.register_begin_handler([&begin_count]() { begin_count.fetch_add(1, std::memory_order_relaxed); });

    loop.async_run();
    std::this_thread::sleep_for(50ms);

    CHECK(begin_count.load() == 1);

    loop.quit();
    loop.wait_for_quit(2000);
  }

  TEST_CASE("register_end_handler fires on exit") {
    std::atomic<int> end_count{0};

    MultiLoop loop(3);
    loop.register_end_handler([&end_count]() { end_count.fetch_add(1, std::memory_order_relaxed); });

    loop.async_run();
    loop.quit();
    loop.wait_for_quit(2000);

    CHECK(end_count.load() == 1);
  }

  TEST_CASE("get_task_count reflects pending tasks") {
    MultiLoop loop(1);

    std::atomic<bool> release{false};
    loop.async_run();

    loop.post_task([&release]() {
      while (!release.load(std::memory_order_acquire)) {
        std::this_thread::sleep_for(1ms);
      }
    });

    for (int i = 0; i < 5; ++i) {
      loop.post_task([] {});
    }

    std::this_thread::sleep_for(20ms);

    size_t queued = loop.get_task_count();
    CHECK(queued <= 5u);

    release.store(true, std::memory_order_release);
    loop.wait_for_idle(2000);
    loop.quit();
    loop.wait_for_quit(2000);
  }
}

// NOLINTEND
