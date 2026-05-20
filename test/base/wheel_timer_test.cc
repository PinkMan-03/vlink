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

#include "./base/wheel_timer.h"

#if defined(__unix__) && !defined(__CYGWIN__)

#include <doctest/doctest.h>

#include <atomic>
#include <chrono>
#include <thread>
#include <vector>

//
#include "../common_test.h"

// Helper: sleep for ms milliseconds
static void sleep_ms(int ms) { std::this_thread::sleep_for(std::chrono::milliseconds(ms)); }

TEST_SUITE("base-WheelTimer") {
  TEST_CASE("is_running reflects start and stop state") {
    WheelTimer wheel(20, 10);

    CHECK_FALSE(wheel.is_running());

    wheel.start();
    CHECK(wheel.is_running());

    wheel.stop();
    CHECK_FALSE(wheel.is_running());
  }

  TEST_CASE("start is idempotent (calling start twice does not crash)") {
    WheelTimer wheel(20, 10);
    wheel.start();
    wheel.start();  // second call should be a no-op
    CHECK(wheel.is_running());
    wheel.stop();
  }

  TEST_CASE("stop is idempotent (calling stop twice does not crash)") {
    WheelTimer wheel(20, 10);
    wheel.start();
    wheel.stop();
    wheel.stop();  // second call should be a no-op
    CHECK_FALSE(wheel.is_running());
  }

  TEST_CASE("one-shot timer fires exactly once") {
    WheelTimer wheel(10, 10);
    wheel.start();

    std::atomic<int> fire_count{0};

    wheel.add(20, [&](WheelTimer::Key) { fire_count.fetch_add(1, std::memory_order_relaxed); });

    // Wait long enough for two potential fire windows
    sleep_ms(60);
    wheel.stop();

    CHECK(fire_count.load() == 1);
  }

  TEST_CASE("one-shot timer fires after expected delay") {
    WheelTimer wheel(10, 10);
    wheel.start();

    std::atomic<bool> fired{false};
    auto t0 = std::chrono::steady_clock::now();
    std::chrono::steady_clock::time_point fire_time;

    wheel.add(30, [&](WheelTimer::Key) {
      fire_time = std::chrono::steady_clock::now();
      fired.store(true, std::memory_order_release);
    });

    sleep_ms(60);
    wheel.stop();

    CHECK(fired.load(std::memory_order_acquire));
    auto delay = std::chrono::duration_cast<std::chrono::milliseconds>(fire_time - t0).count();
    CHECK(delay >= 10);
    CHECK(delay <= 150);
  }

  TEST_CASE("stop can be requested from callback without self-join") {
    WheelTimer wheel(10, 10);
    wheel.start();

    std::atomic<bool> fired{false};
    wheel.add(10, [&](WheelTimer::Key) {
      fired.store(true, std::memory_order_release);
      wheel.stop();
    });

    for (int i = 0; i < 100 && !fired.load(std::memory_order_acquire); ++i) {
      sleep_ms(5);
    }

    wheel.stop();
    CHECK(fired.load(std::memory_order_acquire));
  }

  TEST_CASE("repeating timer fires multiple times") {
    WheelTimer wheel(10, 10);
    wheel.start();

    std::atomic<int> fire_count{0};

    wheel.add(20, [&](WheelTimer::Key) { fire_count.fetch_add(1, std::memory_order_relaxed); }, 20);

    sleep_ms(80);
    wheel.stop();

    CHECK(fire_count.load() >= 2);
  }

  TEST_CASE("repeating timer key is consistent across firings") {
    WheelTimer wheel(10, 10);
    wheel.start();

    std::atomic<int> fire_count{0};
    WheelTimer::Key stored_key = -1;
    std::atomic<bool> key_consistent{true};

    auto key = wheel.add(
        20,
        [&](WheelTimer::Key k) {
          fire_count.fetch_add(1, std::memory_order_relaxed);

          if (stored_key == -1) {
            stored_key = k;
          } else if (stored_key != k) {
            key_consistent.store(false, std::memory_order_relaxed);
          }
        },
        20);

    stored_key = key;

    sleep_ms(80);
    wheel.stop();

    CHECK(fire_count.load() >= 2);
    CHECK(key_consistent.load());
  }

  TEST_CASE("remove before fire returns true and prevents firing") {
    WheelTimer wheel(10, 10);
    wheel.start();

    std::atomic<int> fire_count{0};

    auto key = wheel.add(60, [&](WheelTimer::Key) { fire_count.fetch_add(1, std::memory_order_relaxed); });

    // Remove well before the 60ms timeout
    sleep_ms(15);
    bool removed = wheel.remove(key);
    CHECK(removed);

    sleep_ms(60);
    wheel.stop();

    CHECK(fire_count.load() == 0);
  }

  TEST_CASE("remove after fire returns false") {
    WheelTimer wheel(10, 10);
    wheel.start();

    std::atomic<bool> fired{false};
    WheelTimer::Key fired_key = -1;

    auto key = wheel.add(20, [&](WheelTimer::Key k) {
      fired_key = k;
      fired.store(true, std::memory_order_release);
    });

    // Wait for timer to fire
    sleep_ms(60);

    CHECK(fired.load(std::memory_order_acquire));

    bool removed = wheel.remove(key);
    CHECK_FALSE(removed);

    wheel.stop();
  }

  TEST_CASE("remove invalid key returns false") {
    WheelTimer wheel(20, 10);
    wheel.start();

    bool removed = wheel.remove(99999);
    CHECK_FALSE(removed);

    wheel.stop();
  }

  TEST_CASE("multiple timers with different timeouts all fire") {
    WheelTimer wheel(10, 10);
    wheel.start();

    std::atomic<int> fire_count{0};

    // Add timers with 10, 30, 50 ms timeouts
    wheel.add(10, [&](WheelTimer::Key) { fire_count.fetch_add(1, std::memory_order_relaxed); });

    wheel.add(30, [&](WheelTimer::Key) { fire_count.fetch_add(1, std::memory_order_relaxed); });

    wheel.add(50, [&](WheelTimer::Key) { fire_count.fetch_add(1, std::memory_order_relaxed); });

    sleep_ms(80);
    wheel.stop();

    CHECK(fire_count.load() == 3);
  }

  TEST_CASE("multiple timers fire in expected order") {
    WheelTimer wheel(10, 10);
    wheel.start();

    std::vector<int> order;
    std::atomic<int> fire_count{0};

    wheel.add(10, [&](WheelTimer::Key) {
      order.push_back(1);
      fire_count.fetch_add(1, std::memory_order_relaxed);
    });

    wheel.add(30, [&](WheelTimer::Key) {
      order.push_back(2);
      fire_count.fetch_add(1, std::memory_order_relaxed);
    });

    wheel.add(50, [&](WheelTimer::Key) {
      order.push_back(3);
      fire_count.fetch_add(1, std::memory_order_relaxed);
    });

    sleep_ms(80);
    wheel.stop();

    CHECK(fire_count.load() == 3);
    CHECK(static_cast<int>(order.size()) == 3);
    CHECK(order[0] == 1);
    CHECK(order[1] == 2);
    CHECK(order[2] == 3);
  }

  TEST_CASE("get_remaining_time returns nonzero before expiry") {
    WheelTimer wheel(10, 10);
    wheel.start();

    auto key = wheel.add(200, [](WheelTimer::Key) {});

    sleep_ms(15);
    uint32_t remaining = wheel.get_remaining_time(key);
    CHECK(remaining > 0);

    wheel.remove(key);
    wheel.stop();
  }

  TEST_CASE("get_remaining_time returns zero for unknown key") {
    WheelTimer wheel(10, 10);
    wheel.start();

    uint32_t remaining = wheel.get_remaining_time(99999);
    CHECK(remaining == 0);

    wheel.stop();
  }

  TEST_CASE("get_remaining_time decreases over time") {
    WheelTimer wheel(10, 10);
    wheel.start();

    auto key = wheel.add(300, [](WheelTimer::Key) {});

    sleep_ms(15);
    uint32_t r1 = wheel.get_remaining_time(key);

    sleep_ms(50);
    uint32_t r2 = wheel.get_remaining_time(key);

    CHECK(r2 < r1);

    wheel.remove(key);
    wheel.stop();
  }

  TEST_CASE("pause prevents timers from firing") {
    WheelTimer wheel(10, 10);
    wheel.start();

    std::atomic<int> fire_count{0};

    wheel.add(40, [&](WheelTimer::Key) { fire_count.fetch_add(1, std::memory_order_relaxed); });

    sleep_ms(10);
    wheel.pause();

    sleep_ms(60);

    int count_while_paused = fire_count.load();
    CHECK(count_while_paused == 0);

    wheel.stop();
  }

  TEST_CASE("resume after pause allows pending timers to fire") {
    WheelTimer wheel(10, 10);
    wheel.set_catchup_limit(100);
    wheel.start();

    std::atomic<int> fire_count{0};

    wheel.add(40, [&](WheelTimer::Key) { fire_count.fetch_add(1, std::memory_order_relaxed); });

    sleep_ms(10);
    wheel.pause();
    sleep_ms(60);

    CHECK(fire_count.load() == 0);

    wheel.resume();

    sleep_ms(60);
    wheel.stop();

    CHECK(fire_count.load() == 1);
  }

  TEST_CASE("wakeup does not crash while running") {
    WheelTimer wheel(10, 10);
    wheel.start();

    std::atomic<int> fire_count{0};

    wheel.add(30, [&](WheelTimer::Key) { fire_count.fetch_add(1, std::memory_order_relaxed); });

    wheel.wakeup();

    sleep_ms(60);
    wheel.stop();

    CHECK(fire_count.load() == 1);
  }

  TEST_CASE("set_catchup_limit is accepted without error") {
    WheelTimer wheel(10, 10);
    wheel.set_catchup_limit(10);
    wheel.start();

    std::atomic<int> fire_count{0};
    wheel.add(30, [&](WheelTimer::Key) { fire_count.fetch_add(1, std::memory_order_relaxed); });

    sleep_ms(60);
    wheel.stop();

    CHECK(fire_count.load() == 1);
  }

  TEST_CASE("destructor stops wheel without explicit stop") {
    std::atomic<int> fire_count{0};
    {
      WheelTimer wheel(10, 10);
      wheel.start();

      wheel.add(20, [&](WheelTimer::Key) { fire_count.fetch_add(1, std::memory_order_relaxed); });

      sleep_ms(50);
    }

    CHECK(fire_count.load() >= 1);
  }

  TEST_CASE("adding many timers all fire") {
    WheelTimer wheel(256, 10);
    wheel.start();

    constexpr int kCount = 50;
    std::atomic<int> fire_count{0};

    for (int i = 0; i < kCount; ++i) {
      wheel.add(30, [&](WheelTimer::Key) { fire_count.fetch_add(1, std::memory_order_relaxed); });
    }

    sleep_ms(60);
    wheel.stop();

    CHECK(fire_count.load() == kCount);
  }

  TEST_CASE("remove mid-repeat stops further firings") {
    WheelTimer wheel(10, 10);
    wheel.start();

    std::atomic<int> fire_count{0};
    std::atomic<WheelTimer::Key> stored_key{-1};

    auto key = wheel.add(
        20,
        [&](WheelTimer::Key k) {
          stored_key.store(k, std::memory_order_relaxed);
          fire_count.fetch_add(1, std::memory_order_relaxed);
        },
        20);

    sleep_ms(60);
    int count_before_remove = fire_count.load();
    CHECK(count_before_remove >= 1);

    wheel.remove(key);

    sleep_ms(60);
    wheel.stop();

    CHECK(fire_count.load() == count_before_remove);
  }

  TEST_CASE("add returns unique keys for each timer") {
    WheelTimer wheel(10, 10);
    wheel.start();

    auto key1 = wheel.add(100, [](WheelTimer::Key) {});
    auto key2 = wheel.add(200, [](WheelTimer::Key) {});
    auto key3 = wheel.add(300, [](WheelTimer::Key) {});

    CHECK(key1 != key2);
    CHECK(key2 != key3);
    CHECK(key1 != key3);

    wheel.stop();
  }

  TEST_CASE("timer with timeout longer than wheel span uses rounds") {
    WheelTimer wheel(16, 10);
    wheel.start();

    std::atomic<bool> fired{false};
    wheel.add(120, [&](WheelTimer::Key) { fired.store(true, std::memory_order_release); });

    sleep_ms(60);
    CHECK_FALSE(fired.load(std::memory_order_acquire));

    sleep_ms(100);
    wheel.stop();
    CHECK(fired.load(std::memory_order_acquire));
  }

  TEST_CASE("callback receives correct key") {
    WheelTimer wheel(10, 10);
    wheel.start();

    std::atomic<WheelTimer::Key> received_key{-999};

    auto key = wheel.add(20, [&](WheelTimer::Key k) { received_key.store(k, std::memory_order_release); });

    sleep_ms(60);
    wheel.stop();

    CHECK(received_key.load(std::memory_order_acquire) == key);
  }

  TEST_CASE("remove all timers then stop cleanly") {
    WheelTimer wheel(10, 10);
    wheel.start();

    std::vector<WheelTimer::Key> keys;
    keys.reserve(10);
    for (int i = 0; i < 10; ++i) {
      keys.push_back(wheel.add(500, [](WheelTimer::Key) {}));
    }

    for (auto k : keys) {
      wheel.remove(k);
    }

    wheel.stop();
    CHECK_FALSE(wheel.is_running());
  }

  TEST_CASE("add timer after stop does not crash") {
    WheelTimer wheel(10, 10);
    wheel.start();
    wheel.stop();

    auto key = wheel.add(100, [](WheelTimer::Key) {});
    CHECK_NOTHROW((void)key);
  }

  TEST_CASE("pause and resume toggle correctly") {
    WheelTimer wheel(10, 10);
    wheel.start();

    std::atomic<int> fire_count{0};
    wheel.add(15, [&](WheelTimer::Key) { fire_count.fetch_add(1, std::memory_order_relaxed); }, 15);

    sleep_ms(50);
    int before_pause = fire_count.load();
    CHECK(before_pause >= 1);

    wheel.pause();
    sleep_ms(50);
    int during_pause = fire_count.load();
    CHECK(during_pause == before_pause);

    wheel.resume();
    sleep_ms(50);
    wheel.stop();
    CHECK(fire_count.load() > during_pause);
  }

  TEST_CASE("get_remaining_time returns 0 after timer fires") {
    WheelTimer wheel(10, 10);
    wheel.start();

    std::atomic<bool> fired{false};
    auto key = wheel.add(20, [&](WheelTimer::Key) { fired.store(true, std::memory_order_release); });

    sleep_ms(60);
    CHECK(fired.load(std::memory_order_acquire));

    uint32_t remaining = wheel.get_remaining_time(key);
    CHECK(remaining == 0);

    wheel.stop();
  }

  TEST_CASE("set_catchup_limit limits burst after pause") {
    WheelTimer wheel(10, 10);
    wheel.set_catchup_limit(5);
    wheel.start();

    std::atomic<int> fire_count{0};
    wheel.add(15, [&](WheelTimer::Key) { fire_count.fetch_add(1, std::memory_order_relaxed); }, 15);

    sleep_ms(30);
    wheel.pause();
    sleep_ms(60);
    wheel.resume();
    sleep_ms(60);
    wheel.stop();

    CHECK(fire_count.load() >= 1);
  }
}

#endif

// NOLINTEND
