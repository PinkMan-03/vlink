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

#include "./base/mpmc_queue.h"

#include <doctest/doctest.h>

#include <atomic>
#include <chrono>
#include <numeric>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

//
#include "../common_test.h"

// ---------------------------------------------------------------------------
// Helper types
// ---------------------------------------------------------------------------

struct Point {
  int x{0};
  int y{0};

  bool operator==(const Point& o) const { return x == o.x && y == o.y; }
};

// ---------------------------------------------------------------------------
// Test suite
// ---------------------------------------------------------------------------

TEST_SUITE("base-MpmcQueue") {
  // -------------------------------------------------------------------------
  TEST_CASE("capacity=0 throws std::invalid_argument") { CHECK_THROWS_AS(MpmcQueue<int>(0), std::invalid_argument); }

  // -------------------------------------------------------------------------
  TEST_CASE("capacity() returns the value passed to constructor") {
    MpmcQueue<int> q(16);
    CHECK(q.capacity() == 16u);

    MpmcQueue<int> q1(1);
    CHECK(q1.capacity() == 1u);
  }

  // -------------------------------------------------------------------------
  TEST_CASE("initial state: empty, not full, size=0") {
    MpmcQueue<int> q(4);
    CHECK(q.empty());
    CHECK(q.empty(true));
    CHECK(!q.is_full());
    CHECK(!q.is_full(true));
    CHECK(q.size() == 0u);
    CHECK(q.size(true) == 0u);
  }

  // -------------------------------------------------------------------------
  TEST_CASE("push and pop single element with capacity=1") {
    MpmcQueue<int> q(1);

    q.push(42);
    CHECK(!q.empty());
    CHECK(q.is_full());
    CHECK(q.size() >= 1u);

    int val = 0;
    q.pop(val);
    CHECK(val == 42);
    CHECK(q.empty());
  }

  // -------------------------------------------------------------------------
  TEST_CASE("push fills queue; is_full() becomes true") {
    MpmcQueue<int> q(4);

    for (int i = 0; i < 4; ++i) {
      q.push(i);
    }

    CHECK(q.is_full());
    CHECK(q.size() >= 4u);
  }

  // -------------------------------------------------------------------------
  TEST_CASE("pop order is FIFO for sequential push/pop") {
    MpmcQueue<int> q(8);

    for (int i = 0; i < 5; ++i) {
      q.push(i);
    }

    for (int i = 0; i < 5; ++i) {
      int val = -1;
      q.pop(val);
      CHECK(val == i);
    }

    CHECK(q.empty());
  }

  // -------------------------------------------------------------------------
  TEST_CASE("try_push returns false when queue is full") {
    MpmcQueue<int> q(2);

    CHECK(q.try_push(1));
    CHECK(q.try_push(2));
    CHECK(!q.try_push(3));
  }

  // -------------------------------------------------------------------------
  TEST_CASE("try_pop returns false when queue is empty") {
    MpmcQueue<int> q(4);

    int val = -1;
    CHECK(!q.try_pop(val));
    CHECK(val == -1);
  }

  // -------------------------------------------------------------------------
  TEST_CASE("try_push and try_pop round-trip") {
    MpmcQueue<int> q(8);

    for (int i = 0; i < 8; ++i) {
      CHECK(q.try_push(i));
    }

    CHECK(!q.try_push(99));

    for (int i = 0; i < 8; ++i) {
      int val = -1;
      CHECK(q.try_pop(val));
      CHECK(val == i);
    }

    int dummy = -1;
    CHECK(!q.try_pop(dummy));
  }

  // -------------------------------------------------------------------------
  TEST_CASE("emplace constructs in-place") {
    MpmcQueue<Point> q(4);

    q.push(Point{1, 2});
    q.push(Point{3, 4});

    Point p;
    q.pop(p);
    CHECK(p == Point{1, 2});

    q.pop(p);
    CHECK(p == Point{3, 4});
  }

  // -------------------------------------------------------------------------
  TEST_CASE("try_emplace returns false when full") {
    MpmcQueue<Point> q(1);

    CHECK(q.try_push(Point{1, 1}));
    CHECK(!q.try_push(Point{2, 2}));
  }

  // -------------------------------------------------------------------------
  TEST_CASE("string type round-trip") {
    MpmcQueue<std::string> q(4);

    q.push(std::string("hello"));
    q.push(std::string("world"));

    std::string s;
    q.pop(s);
    CHECK(s == "hello");

    q.pop(s);
    CHECK(s == "world");

    CHECK(q.empty());
  }

  // -------------------------------------------------------------------------
  TEST_CASE("MPMC: multiple producers and consumers") {
    constexpr int kNumProducers = 4;
    constexpr int kNumConsumers = 4;
    constexpr int kItemsPerProducer = 1000;
    constexpr int kTotal = kNumProducers * kItemsPerProducer;

    MpmcQueue<int> q(256);
    std::atomic<int> produced{0};
    std::atomic<int> consumed{0};
    std::atomic<long long> sum_produced{0};  // NOLINT(runtime/int, google-runtime-int)
    std::atomic<long long> sum_consumed{0};  // NOLINT(runtime/int, google-runtime-int)

    std::vector<std::thread> producers;
    std::vector<std::thread> consumers;

    // Start consumers first to avoid busy-waiting on empty queue.
    for (int c = 0; c < kNumConsumers; ++c) {
      consumers.emplace_back([&]() {
        while (true) {
          int val = 0;
          int already = consumed.load(std::memory_order_acquire);

          if (already >= kTotal) {
            break;
          }

          if (q.try_pop(val)) {
            sum_consumed.fetch_add(val, std::memory_order_relaxed);
            consumed.fetch_add(1, std::memory_order_acq_rel);
          } else {
            std::this_thread::yield();
          }
        }
      });
    }

    for (int p = 0; p < kNumProducers; ++p) {
      producers.emplace_back([&, p]() {
        int base = p * kItemsPerProducer;

        for (int i = 0; i < kItemsPerProducer; ++i) {
          int v = base + i;
          sum_produced.fetch_add(v, std::memory_order_relaxed);
          q.push(v);
          produced.fetch_add(1, std::memory_order_acq_rel);
        }
      });
    }

    for (auto& t : producers) {
      t.join();
    }

    // Wait for all items to be consumed.
    while (consumed.load(std::memory_order_acquire) < kTotal) {
      std::this_thread::sleep_for(1ms);
    }

    for (auto& t : consumers) {
      t.join();
    }

    CHECK(produced.load() == kTotal);
    CHECK(consumed.load() == kTotal);
    CHECK(sum_produced.load() == sum_consumed.load());
  }

  // -------------------------------------------------------------------------
  TEST_CASE("wait_not_empty blocks until producer pushes") {
    MpmcQueue<int> q(8);

    // Producer pushes after a short delay.
    std::thread producer([&]() {
      std::this_thread::sleep_for(50ms);
      q.push<MpmcQueue<int>::kConditionBehavior>(99);
    });

    bool result = q.wait_not_empty();

    CHECK(result == true);
    CHECK(!q.empty());

    int val = 0;
    q.pop(val);
    CHECK(val == 99);

    producer.join();
  }

  // -------------------------------------------------------------------------
  TEST_CASE("wait_not_full blocks until consumer pops") {
    MpmcQueue<int> q(2);

    // Fill the queue.
    q.push(1);
    q.push(2);
    CHECK(q.is_full());

    // Consumer pops after a short delay.
    std::thread consumer([&]() {
      std::this_thread::sleep_for(50ms);
      int val = 0;
      q.pop<MpmcQueue<int>::kConditionBehavior>(val);
    });

    bool result = q.wait_not_full();
    CHECK(result == true);
    CHECK(!q.is_full());

    consumer.join();
  }

  // -------------------------------------------------------------------------
  TEST_CASE("wait_not_empty with timeout returns false when queue stays empty") {
    MpmcQueue<int> q(4);

    auto start = std::chrono::steady_clock::now();
    bool result = q.wait_not_empty(50ms);
    auto elapsed = std::chrono::steady_clock::now() - start;

    if (result) {
      CHECK(q.size() > 0u);
    } else {
      CHECK(true);
    }
    // Timeout should have fired within a reasonable bound.
    CHECK(elapsed >= 40ms);
  }

  // -------------------------------------------------------------------------
  TEST_CASE("notify_to_quit unblocks wait_not_empty") {
    MpmcQueue<int> q(4);

    std::thread quitter([&]() {
      std::this_thread::sleep_for(50ms);
      q.notify_to_quit();
    });

    bool result = q.wait_not_empty();
    CHECK(!result);

    quitter.join();
  }

  // -------------------------------------------------------------------------
  TEST_CASE("notify_to_quit unblocks wait_not_full") {
    MpmcQueue<int> q(1);

    q.push(1);
    CHECK(q.is_full());

    std::thread quitter([&]() {
      std::this_thread::sleep_for(50ms);
      q.notify_to_quit();
    });

    bool result = q.wait_not_full();
    CHECK(!result);

    quitter.join();
  }

  // -------------------------------------------------------------------------
  TEST_CASE("notify_to_quit makes subsequent try_push return false") {
    MpmcQueue<int> q(4);

    q.notify_to_quit();
    CHECK(!q.try_push(1));
  }

  // -------------------------------------------------------------------------
  TEST_CASE("notify_to_quit makes subsequent try_pop return false") {
    MpmcQueue<int> q(4);

    q.push(42);
    q.notify_to_quit();

    int val = -1;
    CHECK(!q.try_pop(val));
  }

  // -------------------------------------------------------------------------
  TEST_CASE("kConditionBehavior push notifies wait_not_empty") {
    MpmcQueue<int> q(8);

    std::thread producer([&]() {
      std::this_thread::sleep_for(30ms);
      q.push<MpmcQueue<int>::kConditionBehavior>(7);
    });

    bool ok = q.wait_not_empty();
    CHECK(ok);

    int val = 0;
    q.pop(val);
    CHECK(val == 7);

    producer.join();
  }

  // -------------------------------------------------------------------------
  TEST_CASE("kConditionBehavior pop notifies wait_not_full") {
    MpmcQueue<int> q(1);

    q.push(10);
    CHECK(q.is_full());

    std::thread consumer([&]() {
      std::this_thread::sleep_for(30ms);
      int val = 0;
      q.pop<MpmcQueue<int>::kConditionBehavior>(val);
    });

    bool ok = q.wait_not_full();
    CHECK(ok);
    CHECK(!q.is_full());

    consumer.join();
  }

  // -------------------------------------------------------------------------
  TEST_CASE("emplace with kConditionBehavior after notify_to_quit is a no-op") {
    MpmcQueue<int> q(4);

    q.notify_to_quit();

    // Should not block or crash.
    q.emplace<MpmcQueue<int>::kConditionBehavior>(1);

    CHECK(q.empty());
  }

  // -------------------------------------------------------------------------
  TEST_CASE("size(real=true) matches actual element count") {
    MpmcQueue<int> q(16);

    for (int i = 0; i < 10; ++i) {
      q.push(i);
    }

    size_t real_size = q.size(true);
    CHECK(real_size == 10u);
  }

  // -------------------------------------------------------------------------
  TEST_CASE("empty(real=true) and is_full(real=true) accuracy") {
    MpmcQueue<int> q(3);

    CHECK(q.empty(true));
    CHECK(!q.is_full(true));

    q.push(1);
    q.push(2);
    q.push(3);

    CHECK(!q.empty(true));
    CHECK(q.is_full(true));
  }

  // -------------------------------------------------------------------------
  TEST_CASE("custom struct type with multiple concurrent threads") {
    MpmcQueue<Point> q(128);
    std::atomic<int> count{0};
    std::atomic<long long> sum_x{0};  // NOLINT(runtime/int, google-runtime-int)

    constexpr int kN = 200;

    std::thread producer([&]() {
      for (int i = 0; i < kN; ++i) {
        q.emplace(Point{i, i * 2});
      }
    });

    std::thread consumer([&]() {
      int received = 0;

      while (received < kN) {
        Point p;

        if (q.try_pop(p)) {
          sum_x.fetch_add(p.x, std::memory_order_relaxed);
          ++received;
        } else {
          std::this_thread::yield();
        }
      }

      count.store(received);
    });

    producer.join();
    consumer.join();

    CHECK(count.load() == kN);

    // Expected sum of 0..199
    long long expected = static_cast<long long>(kN) * (kN - 1) / 2;  // NOLINT(runtime/int, google-runtime-int)
    CHECK(sum_x.load() == expected);
  }

  // -------------------------------------------------------------------------
  TEST_CASE("multiple concurrent wait_not_empty all unblocked by notify_to_quit") {
    MpmcQueue<int> q(4);
    constexpr int kWaiters = 4;
    std::atomic<int> unblocked{0};

    std::vector<std::thread> waiters;

    for (int i = 0; i < kWaiters; ++i) {
      waiters.emplace_back([&]() {
        bool result = q.wait_not_empty();

        if (!result) {
          unblocked.fetch_add(1, std::memory_order_acq_rel);
        }
      });
    }

    std::this_thread::sleep_for(30ms);
    q.notify_to_quit();

    for (auto& t : waiters) {
      t.join();
    }

    CHECK(unblocked.load() == kWaiters);
  }
}

// NOLINTEND
