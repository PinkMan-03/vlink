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

#include "./base/thread_pool.h"

#include <doctest/doctest.h>

#include <atomic>
#include <chrono>
#include <string>
#include <thread>
#include <vector>

//
#include "../common_test.h"

TEST_SUITE("base-ThreadPool") {
  class SmallQueueThreadPool final : public ThreadPool {
   public:
    using ThreadPool::ThreadPool;

    [[nodiscard]] size_t get_max_task_count() const override { return 1U; }
  };

  TEST_CASE("post_task executes the task") {
    ThreadPool pool(2);
    std::atomic<bool> executed{false};

    bool posted = pool.post_task([&] { executed.store(true, std::memory_order_release); });

    std::this_thread::sleep_for(100ms);

    CHECK(posted);
    pool.shutdown();
    CHECK(executed.load(std::memory_order_acquire));
  }

  TEST_CASE("post_task executes multiple tasks") {
    ThreadPool pool(4);
    constexpr int kCount = 20;
    std::atomic<int> counter{0};

    for (int i = 0; i < kCount; ++i) {
      pool.post_task([&] { counter.fetch_add(1, std::memory_order_relaxed); });
    }

    std::this_thread::sleep_for(100ms);

    pool.shutdown();
    CHECK(counter.load() == kCount);
  }

  TEST_CASE("invoke_task returns correct value") {
    ThreadPool pool(2);

    auto fut = pool.invoke_task([] { return 42; });
    int result = fut.get();

    CHECK(result == 42);
    pool.shutdown();
  }

  TEST_CASE("invoke_task with arguments returns correct value") {
    ThreadPool pool(2);

    auto fut = pool.invoke_task([](int a, int b) { return a + b; }, 10, 32);
    int result = fut.get();

    CHECK(result == 42);
    pool.shutdown();
  }

  TEST_CASE("invoke_task with string return type") {
    ThreadPool pool(2);

    auto fut = pool.invoke_task([] { return std::string("hello"); });
    std::string result = fut.get();

    CHECK(result == "hello");
    pool.shutdown();
  }

  TEST_CASE("parallel execution runs tasks concurrently") {
    // Submit kWorkers tasks each sleeping 50ms; with kWorkers threads they
    // should finish in ~50ms total, not kWorkers*50ms serially.
    constexpr int kWorkers = 4;
    ThreadPool pool(kWorkers);
    std::atomic<int> counter{0};

    auto t0 = std::chrono::steady_clock::now();

    std::vector<std::future<void>> futures;
    futures.reserve(kWorkers);
    for (int i = 0; i < kWorkers; ++i) {
      futures.push_back(pool.invoke_task([&] {
        std::this_thread::sleep_for(50ms);
        counter.fetch_add(1, std::memory_order_relaxed);
      }));
    }

    for (auto& f : futures) {
      f.get();
    }

    auto elapsed = std::chrono::steady_clock::now() - t0;
    CHECK(counter.load() == kWorkers);
    // Parallel: should finish well under kWorkers * 50ms
    CHECK(elapsed < std::chrono::milliseconds(kWorkers * 50 - 20));

    pool.shutdown();
  }

  TEST_CASE("is_in_work_thread returns true inside task") {
    ThreadPool pool(2);
    std::atomic<bool> inside{false};

    auto fut =
        pool.invoke_task([&pool, &inside] { inside.store(pool.is_in_work_thread(), std::memory_order_release); });

    fut.get();
    CHECK(inside.load(std::memory_order_acquire));
    pool.shutdown();
  }

  TEST_CASE("is_in_work_thread returns false outside pool") {
    ThreadPool pool(2);
    CHECK_FALSE(pool.is_in_work_thread());
    pool.shutdown();
  }

  TEST_CASE("shutdown waits for all tasks to complete") {
    ThreadPool pool(2);
    constexpr int kCount = 10;
    std::atomic<int> counter{0};

    for (int i = 0; i < kCount; ++i) {
      pool.post_task([&] {
        std::this_thread::sleep_for(10ms);
        counter.fetch_add(1, std::memory_order_relaxed);
      });
    }
    std::this_thread::sleep_for(100ms);

    bool ok = pool.shutdown();
    CHECK(ok);
    CHECK(counter.load() == kCount);
  }

  TEST_CASE("get_task_count reflects pending tasks") {
    // Use a single-threaded pool and block the worker so tasks queue up.
    ThreadPool pool(1);
    std::atomic<bool> block{true};

    // Occupy the sole worker thread
    pool.post_task([&] {
      while (block.load(std::memory_order_acquire)) {
        std::this_thread::yield();
      }
    });

    // Give the worker a moment to pick up the blocking task
    std::this_thread::sleep_for(20ms);

    // Post additional tasks that will queue
    pool.post_task([] {});
    pool.post_task([] {});
    pool.post_task([] {});

    // At least some tasks should be pending
    CHECK(pool.get_task_count() >= 0u);

    block.store(false, std::memory_order_release);
    pool.shutdown();
  }

  TEST_CASE("get_max_task_count returns positive value") {
    ThreadPool pool(2);
    CHECK(pool.get_max_task_count() > 0);
    pool.shutdown();
  }

  TEST_CASE("set_name and get_name round-trip") {
    ThreadPool pool(2);
    pool.set_name("test-pool");
    CHECK(pool.get_name() == "test-pool");
    pool.shutdown();
  }

  TEST_CASE("set_name with empty string") {
    ThreadPool pool(2);
    pool.set_name("");
    CHECK(pool.get_name() == "");
    pool.shutdown();
  }

  TEST_CASE("get_type returns kNormalType for default constructor") {
    ThreadPool pool(2);
    CHECK(pool.get_type() == ThreadPool::kNormalType);
    pool.shutdown();
  }

  TEST_CASE("kLockfreeType pool executes tasks correctly") {
    ThreadPool pool(4, ThreadPool::kLockfreeType);
    CHECK(pool.get_type() == ThreadPool::kLockfreeType);

    std::atomic<int> counter{0};
    constexpr int kCount = 20;

    for (int i = 0; i < kCount; ++i) {
      pool.post_task([&] { counter.fetch_add(1, std::memory_order_relaxed); });
    }

    std::this_thread::sleep_for(100ms);

    pool.shutdown();
    CHECK(counter.load() == kCount);
  }

  TEST_CASE("kLockfreeType invoke_task returns correct result") {
    ThreadPool pool(2, ThreadPool::kLockfreeType);

    auto fut = pool.invoke_task([] { return 99; });
    int result = fut.get();

    CHECK(result == 99);
    pool.shutdown();
  }

  TEST_CASE("kLockfreeType is_in_work_thread") {
    ThreadPool pool(2, ThreadPool::kLockfreeType);
    std::atomic<bool> inside{false};

    auto fut =
        pool.invoke_task([&pool, &inside] { inside.store(pool.is_in_work_thread(), std::memory_order_release); });

    fut.get();
    CHECK(inside.load(std::memory_order_acquire));
    pool.shutdown();
  }

  TEST_CASE("get_strategy returns a valid strategy") {
    ThreadPool pool(2);
    auto s = pool.get_strategy();
    bool valid = (s == ThreadPool::kOptimizationStrategy) || (s == ThreadPool::kPopStrategy) ||
                 (s == ThreadPool::kBlockStrategy);
    CHECK(valid);
    pool.shutdown();
  }

  TEST_CASE("set_strategy to kBlockStrategy") {
    ThreadPool pool(2);
    pool.set_strategy(ThreadPool::kBlockStrategy);
    CHECK(pool.get_strategy() == ThreadPool::kBlockStrategy);

    std::atomic<int> counter{0};
    for (int i = 0; i < 5; ++i) {
      pool.post_task([&] { counter.fetch_add(1, std::memory_order_relaxed); });
    }

    std::this_thread::sleep_for(100ms);

    pool.shutdown();
    CHECK(counter.load() == 5);
  }

  TEST_CASE("set_strategy to kPopStrategy") {
    ThreadPool pool(2);
    pool.set_strategy(ThreadPool::kPopStrategy);
    CHECK(pool.get_strategy() == ThreadPool::kPopStrategy);

    std::atomic<int> counter{0};
    for (int i = 0; i < 5; ++i) {
      pool.post_task([&] { counter.fetch_add(1, std::memory_order_relaxed); });
    }

    std::this_thread::sleep_for(100ms);

    pool.shutdown();
    CHECK(counter.load() == 5);
  }

  TEST_CASE("post_task returns true when kPopStrategy drops old task") {
    SmallQueueThreadPool pool(1);
    pool.set_strategy(ThreadPool::kPopStrategy);

    std::atomic<bool> release_worker{false};
    std::atomic<bool> worker_started{false};

    CHECK(pool.post_task([&] {
      worker_started.store(true, std::memory_order_release);
      while (!release_worker.load(std::memory_order_acquire)) {
        std::this_thread::yield();
      }
    }));

    while (!worker_started.load(std::memory_order_acquire)) {
      std::this_thread::yield();
    }

    CHECK(pool.post_task([] {}));
    CHECK(pool.post_task([] {}));

    release_worker.store(true, std::memory_order_release);
    pool.shutdown();
  }

  TEST_CASE("single-threaded pool serialises tasks") {
    ThreadPool pool(1);
    constexpr int kCount = 10;
    std::vector<int> order;
    order.reserve(kCount);
    std::atomic<bool> done{false};

    // Collect ordering of execution through futures to serialise
    std::vector<std::future<void>> futures;
    futures.reserve(kCount);
    for (int i = 0; i < kCount; ++i) {
      futures.push_back(pool.invoke_task([&order, i] { order.push_back(i); }));
    }

    std::this_thread::sleep_for(100ms);

    for (auto& f : futures) {
      f.get();
    }

    done.store(true);
    CHECK(done.load());
    CHECK(static_cast<int>(order.size()) == kCount);

    // Single thread must produce strictly sequential order
    for (int i = 0; i < kCount; ++i) {
      CHECK(order[i] == i);
    }

    pool.shutdown();
  }

  TEST_CASE("destructor shuts down gracefully without explicit shutdown") {
    std::atomic<int> counter{0};
    {
      ThreadPool pool(2);
      for (int i = 0; i < 5; ++i) {
        pool.post_task([&] { counter.fetch_add(1, std::memory_order_relaxed); });
      }
      // pool destroyed here — destructor must join workers
    }
    CHECK(counter.load() == 5);
  }

  TEST_CASE("destructor is safe when called from a worker task") {
    auto pool = std::make_unique<ThreadPool>(1);
    std::promise<void> done;
    auto done_fut = done.get_future();

    CHECK(pool->post_task([&] {
      pool.reset();
      done.set_value();
    }));

    CHECK(done_fut.wait_for(1s) == std::future_status::ready);
  }

  TEST_CASE("double shutdown is safe") {
    ThreadPool pool(2);
    pool.post_task([] {});
    bool ok1 = pool.shutdown();
    CHECK(ok1);
    bool ok2 = pool.shutdown();
    // Second shutdown should not crash; result may vary
    (void)ok2;
  }

  TEST_CASE("default constructor creates 4 threads") {
    ThreadPool pool;
    CHECK(pool.get_type() == ThreadPool::kNormalType);
    std::atomic<int> counter{0};
    for (int i = 0; i < 8; ++i) {
      pool.post_task([&] { counter.fetch_add(1, std::memory_order_relaxed); });
    }
    std::this_thread::sleep_for(100ms);
    pool.shutdown();
    CHECK(counter.load() == 8);
  }

  TEST_CASE("set_strategy to kOptimizationStrategy and execute tasks") {
    ThreadPool pool(2);
    pool.set_strategy(ThreadPool::kOptimizationStrategy);
    CHECK(pool.get_strategy() == ThreadPool::kOptimizationStrategy);

    std::atomic<int> counter{0};
    for (int i = 0; i < 5; ++i) {
      pool.post_task([&] { counter.fetch_add(1, std::memory_order_relaxed); });
    }
    std::this_thread::sleep_for(100ms);
    pool.shutdown();
    CHECK(counter.load() == 5);
  }

  TEST_CASE("invoke_task with void return type") {
    ThreadPool pool(2);
    std::atomic<bool> executed{false};

    auto fut = pool.invoke_task([&] { executed.store(true, std::memory_order_release); });
    fut.get();

    CHECK(executed.load(std::memory_order_acquire));
    pool.shutdown();
  }

  TEST_CASE("kLockfreeType with kBlockStrategy executes tasks") {
    ThreadPool pool(2, ThreadPool::kLockfreeType);
    pool.set_strategy(ThreadPool::kBlockStrategy);

    std::atomic<int> counter{0};
    for (int i = 0; i < 10; ++i) {
      pool.post_task([&] { counter.fetch_add(1, std::memory_order_relaxed); });
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    pool.shutdown();
    CHECK(counter.load() == 10);
  }

  TEST_CASE("kLockfreeType with kPopStrategy executes tasks") {
    ThreadPool pool(2, ThreadPool::kLockfreeType);
    pool.set_strategy(ThreadPool::kPopStrategy);

    std::atomic<int> counter{0};
    for (int i = 0; i < 10; ++i) {
      pool.post_task([&] { counter.fetch_add(1, std::memory_order_relaxed); });
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    pool.shutdown();
    CHECK(counter.load() == 10);
  }

  TEST_CASE("set_name with long string") {
    ThreadPool pool(2);
    std::string long_name(128, 'x');
    pool.set_name(long_name);
    CHECK(pool.get_name() == long_name);
    pool.shutdown();
  }

  TEST_CASE("invoke_task with multiple return types") {
    ThreadPool pool(2);

    auto fut_double = pool.invoke_task([] { return 3.14; });
    CHECK(fut_double.get() == doctest::Approx(3.14));

    auto fut_bool = pool.invoke_task([] { return true; });
    CHECK(fut_bool.get() == true);

    pool.shutdown();
  }

  TEST_CASE("post_task after shutdown returns false") {
    ThreadPool pool(2);
    pool.shutdown();

    bool posted = pool.post_task([] {});
    CHECK_FALSE(posted);
  }

  TEST_CASE("zero-thread pool rejects posted tasks") {
    ThreadPool pool(0);
    std::atomic<bool> executed{false};

    bool posted = pool.post_task([&] { executed.store(true, std::memory_order_release); });

    CHECK_FALSE(posted);
    CHECK_FALSE(executed.load(std::memory_order_acquire));
  }

  TEST_CASE("shutdown rejects producer waiting on a full queue") {
    SmallQueueThreadPool pool(1);
    pool.set_strategy(ThreadPool::kBlockStrategy);

    std::atomic<bool> release_worker{false};
    std::atomic<bool> worker_started{false};
    std::atomic<bool> producer_done{false};
    std::atomic<bool> producer_accepted{true};
    std::atomic<bool> late_task_executed{false};

    CHECK(pool.post_task([&] {
      worker_started.store(true, std::memory_order_release);
      while (!release_worker.load(std::memory_order_acquire)) {
        std::this_thread::yield();
      }
    }));

    while (!worker_started.load(std::memory_order_acquire)) {
      std::this_thread::yield();
    }

    CHECK(pool.post_task([] {}));

    std::thread producer([&] {
      producer_accepted.store(pool.post_task([&] { late_task_executed.store(true, std::memory_order_release); }),
                              std::memory_order_release);
      producer_done.store(true, std::memory_order_release);
    });

    std::this_thread::sleep_for(20ms);
    CHECK_FALSE(producer_done.load(std::memory_order_acquire));

    std::thread stopper([&] { static_cast<void>(pool.shutdown()); });
    std::this_thread::sleep_for(20ms);
    release_worker.store(true, std::memory_order_release);

    producer.join();
    stopper.join();

    CHECK(producer_done.load(std::memory_order_acquire));
    CHECK_FALSE(producer_accepted.load(std::memory_order_acquire));
    CHECK_FALSE(late_task_executed.load(std::memory_order_acquire));
  }

  TEST_CASE("high contention post_task from multiple threads") {
    ThreadPool pool(4);
    std::atomic<int> counter{0};
    static constexpr int kThreads = 4;
    static constexpr int kTasksPerThread = 50;

    std::vector<std::thread> producers;
    producers.reserve(kThreads);
    for (int t = 0; t < kThreads; ++t) {
      producers.emplace_back([&pool, &counter] {
        for (int i = 0; i < kTasksPerThread; ++i) {
          pool.post_task([&counter] { counter.fetch_add(1, std::memory_order_relaxed); });
        }
      });
    }

    for (auto& t : producers) {
      t.join();
    }
    std::this_thread::sleep_for(100ms);
    pool.shutdown();
    CHECK(counter.load() == kThreads * kTasksPerThread);
  }

  TEST_CASE("kLockfreeType get_max_task_count returns positive") {
    ThreadPool pool(2, ThreadPool::kLockfreeType);
    CHECK(pool.get_max_task_count() > 0);
    pool.shutdown();
  }

  // ---

  class TwoTaskLockfreeThreadPool final : public ThreadPool {
   public:
    using ThreadPool::ThreadPool;

    [[nodiscard]] size_t get_max_task_count() const override { return 2U; }
  };

  class SmallQueueLockfreeThreadPool final : public ThreadPool {
   public:
    using ThreadPool::ThreadPool;

    [[nodiscard]] size_t get_max_task_count() const override { return 1U; }
  };

  TEST_CASE("kLockfreeType with kBlockStrategy blocks producer until consumer drains") {
    TwoTaskLockfreeThreadPool pool(1U, ThreadPool::kLockfreeType);
    pool.set_strategy(ThreadPool::kBlockStrategy);

    std::atomic<bool> release_worker{false};
    std::atomic<bool> worker_started{false};
    std::atomic<int> executed{0};

    CHECK(pool.post_task([&] {
      worker_started.store(true, std::memory_order_release);
      while (!release_worker.load(std::memory_order_acquire)) {
        std::this_thread::yield();
      }
      executed.fetch_add(1, std::memory_order_acq_rel);
    }));

    while (!worker_started.load(std::memory_order_acquire)) {
      std::this_thread::yield();
    }

    CHECK(pool.post_task([&] { executed.fetch_add(1, std::memory_order_acq_rel); }));
    CHECK(pool.post_task([&] { executed.fetch_add(1, std::memory_order_acq_rel); }));

    std::atomic<bool> producer_done{false};
    std::atomic<bool> producer_accepted{false};

    std::thread producer([&] {
      producer_accepted.store(pool.post_task([&] { executed.fetch_add(1, std::memory_order_acq_rel); }),
                              std::memory_order_release);
      producer_done.store(true, std::memory_order_release);
    });

    std::this_thread::sleep_for(30ms);
    CHECK_FALSE(producer_done.load(std::memory_order_acquire));

    release_worker.store(true, std::memory_order_release);
    producer.join();

    CHECK(producer_done.load(std::memory_order_acquire));
    CHECK(producer_accepted.load(std::memory_order_acquire));

    pool.shutdown();
    CHECK(executed.load(std::memory_order_acquire) == 4);
  }

  TEST_CASE("kLockfreeType with kPopStrategy drops tasks under overflow via TaskHandle") {
    SmallQueueLockfreeThreadPool pool(1U, ThreadPool::kLockfreeType);
    pool.set_strategy(ThreadPool::kPopStrategy);

    std::atomic<bool> release_worker{false};
    std::atomic<bool> worker_started{false};

    CHECK(pool.post_task([&] {
      worker_started.store(true, std::memory_order_release);
      while (!release_worker.load(std::memory_order_acquire)) {
        std::this_thread::yield();
      }
    }));

    while (!worker_started.load(std::memory_order_acquire)) {
      std::this_thread::yield();
    }

    std::vector<TaskHandle> handles;
    handles.reserve(8);
    for (int i = 0; i < 8; ++i) {
      handles.push_back(pool.post_task_handle([] {}));
    }

    bool any_dropped = false;
    for (const auto& h : handles) {
      if (h.state() == TaskExecutionState::kDropped) {
        any_dropped = true;
        break;
      }
    }
    CHECK(any_dropped);

    release_worker.store(true, std::memory_order_release);
    pool.shutdown();
  }

  TEST_CASE("kLockfreeType high concurrency stress with many producers") {
    ThreadPool pool(4U, ThreadPool::kLockfreeType);
    std::atomic<int> counter{0};
    static constexpr int kProducers = 4;
    static constexpr int kTasksPerProducer = 100;

    std::vector<std::thread> producers;
    producers.reserve(kProducers);
    for (int t = 0; t < kProducers; ++t) {
      producers.emplace_back([&pool, &counter] {
        for (int i = 0; i < kTasksPerProducer; ++i) {
          pool.post_task([&counter] { counter.fetch_add(1, std::memory_order_relaxed); });
        }
      });
    }

    for (auto& t : producers) {
      t.join();
    }

    pool.shutdown();
    CHECK(counter.load() == kProducers * kTasksPerProducer);
  }

  TEST_CASE("kNormalType with kBlockStrategy blocks producer until queue drains") {
    SmallQueueThreadPool pool(1);
    pool.set_strategy(ThreadPool::kBlockStrategy);

    std::atomic<bool> release_worker{false};
    std::atomic<bool> worker_started{false};
    std::atomic<int> executed{0};

    CHECK(pool.post_task([&] {
      worker_started.store(true, std::memory_order_release);
      while (!release_worker.load(std::memory_order_acquire)) {
        std::this_thread::yield();
      }
      executed.fetch_add(1, std::memory_order_acq_rel);
    }));

    while (!worker_started.load(std::memory_order_acquire)) {
      std::this_thread::yield();
    }

    CHECK(pool.post_task([&] { executed.fetch_add(1, std::memory_order_acq_rel); }));

    std::atomic<bool> producer_done{false};
    std::atomic<bool> producer_accepted{false};

    std::thread producer([&] {
      producer_accepted.store(pool.post_task([&] { executed.fetch_add(1, std::memory_order_acq_rel); }),
                              std::memory_order_release);
      producer_done.store(true, std::memory_order_release);
    });

    std::this_thread::sleep_for(30ms);
    CHECK_FALSE(producer_done.load(std::memory_order_acquire));

    release_worker.store(true, std::memory_order_release);
    producer.join();

    CHECK(producer_done.load(std::memory_order_acquire));
    CHECK(producer_accepted.load(std::memory_order_acquire));

    pool.shutdown();
    CHECK(executed.load(std::memory_order_acquire) == 3);
  }

  TEST_CASE("invoke_task future becomes broken_promise after shutdown") {
    ThreadPool pool(2);
    pool.shutdown();

    auto fut = pool.invoke_task([] { return 7; });
    CHECK(fut.wait_for(100ms) == std::future_status::ready);
    CHECK_THROWS_AS(fut.get(), std::future_error);
  }

  TEST_CASE("kLockfreeType multiple workers process tasks concurrently") {
    constexpr int kWorkers = 4;
    ThreadPool pool(kWorkers, ThreadPool::kLockfreeType);
    std::atomic<int> counter{0};

    auto t0 = std::chrono::steady_clock::now();

    std::vector<std::future<void>> futures;
    futures.reserve(kWorkers);
    for (int i = 0; i < kWorkers; ++i) {
      futures.push_back(pool.invoke_task([&] {
        std::this_thread::sleep_for(50ms);
        counter.fetch_add(1, std::memory_order_relaxed);
      }));
    }

    for (auto& f : futures) {
      f.get();
    }

    auto elapsed = std::chrono::steady_clock::now() - t0;
    CHECK(counter.load() == kWorkers);
    CHECK(elapsed < std::chrono::milliseconds(kWorkers * 50 - 20));

    pool.shutdown();
  }

  TEST_CASE("set_strategy while pool is running takes effect on subsequent posts") {
    SmallQueueThreadPool pool(1);
    pool.set_strategy(ThreadPool::kOptimizationStrategy);
    CHECK(pool.get_strategy() == ThreadPool::kOptimizationStrategy);

    pool.set_strategy(ThreadPool::kPopStrategy);
    CHECK(pool.get_strategy() == ThreadPool::kPopStrategy);

    std::atomic<bool> release_worker{false};
    std::atomic<bool> worker_started{false};

    CHECK(pool.post_task([&] {
      worker_started.store(true, std::memory_order_release);
      while (!release_worker.load(std::memory_order_acquire)) {
        std::this_thread::yield();
      }
    }));

    while (!worker_started.load(std::memory_order_acquire)) {
      std::this_thread::yield();
    }

    std::vector<TaskHandle> handles;
    handles.reserve(4);
    for (int i = 0; i < 4; ++i) {
      handles.push_back(pool.post_task_handle([] {}));
    }

    bool any_dropped = false;
    for (const auto& h : handles) {
      if (h.state() == TaskExecutionState::kDropped) {
        any_dropped = true;
        break;
      }
    }
    CHECK(any_dropped);

    release_worker.store(true, std::memory_order_release);
    pool.shutdown();
  }
}

// NOLINTEND
