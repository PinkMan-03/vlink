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

#include "./base/condition_variable.h"

#if defined(__unix__) && !defined(__CYGWIN__)

#include <doctest/doctest.h>

#include <atomic>
#include <chrono>
#include <mutex>
#include <thread>
#include <vector>

//
#include "../common_test.h"

// ---------------------------------------------------------------------------
TEST_SUITE("base-ConditionVariable") {
  // -------------------------------------------------------------------------
  TEST_CASE("notify_one wakes a single waiting thread") {
    ConditionVariable cv;
    std::mutex mtx;
    bool ready = false;

    std::thread producer([&]() {
      std::this_thread::sleep_for(20ms);
      {
        std::lock_guard<std::mutex> lk(mtx);
        ready = true;
      }
      cv.notify_one();
    });

    std::unique_lock<std::mutex> lk(mtx);
    cv.wait(lk, [&] { return ready; });

    CHECK(ready);
    producer.join();
  }

  // -------------------------------------------------------------------------
  TEST_CASE("notify_all wakes all waiting threads") {
    ConditionVariable cv;
    std::mutex mtx;
    bool go = false;
    std::atomic<int> woken{0};
    constexpr int kThreads = 4;

    std::vector<std::thread> consumers;
    consumers.reserve(kThreads);

    for (int i = 0; i < kThreads; ++i) {
      consumers.emplace_back([&]() {
        std::unique_lock<std::mutex> lk(mtx);
        cv.wait(lk, [&] { return go; });
        woken.fetch_add(1, std::memory_order_relaxed);
      });
    }

    std::this_thread::sleep_for(20ms);
    {
      std::lock_guard<std::mutex> lk(mtx);
      go = true;
    }
    cv.notify_all();

    for (auto& t : consumers) {
      t.join();
    }

    CHECK(woken.load() == kThreads);
  }

  // -------------------------------------------------------------------------
  TEST_CASE("wait_for returns timeout when not notified") {
    ConditionVariable cv;
    std::mutex mtx;

    std::unique_lock<std::mutex> lk(mtx);
    auto status = cv.wait_for(lk, 30ms);

    CHECK(status == std::cv_status::timeout);
  }

  // -------------------------------------------------------------------------
  TEST_CASE("wait_for returns no_timeout when notified before deadline") {
    ConditionVariable cv;
    std::mutex mtx;
    bool ready = false;

    std::thread producer([&]() {
      std::this_thread::sleep_for(10ms);
      {
        std::lock_guard<std::mutex> lk(mtx);
        ready = true;
      }
      cv.notify_one();
    });

    std::unique_lock<std::mutex> lk(mtx);
    auto status = cv.wait_for(lk, 500ms);

    CHECK(status == std::cv_status::no_timeout);
    producer.join();
  }

  // -------------------------------------------------------------------------
  TEST_CASE("wait_for with predicate returns true when predicate satisfied") {
    ConditionVariable cv;
    std::mutex mtx;
    bool flag = false;

    std::thread producer([&]() {
      std::this_thread::sleep_for(10ms);
      {
        std::lock_guard<std::mutex> lk(mtx);
        flag = true;
      }
      cv.notify_one();
    });

    std::unique_lock<std::mutex> lk(mtx);
    bool result = cv.wait_for(lk, 500ms, [&] { return flag; });

    CHECK(result);
    producer.join();
  }

  // -------------------------------------------------------------------------
  TEST_CASE("wait_for with predicate returns false on timeout") {
    ConditionVariable cv;
    std::mutex mtx;

    std::unique_lock<std::mutex> lk(mtx);
    bool result = cv.wait_for(lk, 30ms, [] { return false; });

    CHECK(!result);
  }

  // -------------------------------------------------------------------------
  TEST_CASE("wait_until with steady_clock returns timeout") {
    ConditionVariable cv;
    std::mutex mtx;

    std::unique_lock<std::mutex> lk(mtx);
    auto deadline = std::chrono::steady_clock::now() + 30ms;
    auto status = cv.wait_until(lk, deadline);

    CHECK(status == std::cv_status::timeout);
  }

  // -------------------------------------------------------------------------
  TEST_CASE("wait_until with system_clock returns timeout") {
    ConditionVariable cv;
    std::mutex mtx;

    std::unique_lock<std::mutex> lk(mtx);
    auto deadline = std::chrono::system_clock::now() + 30ms;
    auto status = cv.wait_until(lk, deadline);

    CHECK(status == std::cv_status::timeout);
  }

  // -------------------------------------------------------------------------
  TEST_CASE("wait_until with predicate times out and returns false") {
    ConditionVariable cv;
    std::mutex mtx;

    std::unique_lock<std::mutex> lk(mtx);
    auto deadline = std::chrono::steady_clock::now() + 30ms;
    bool result = cv.wait_until(lk, deadline, [] { return false; });

    CHECK(!result);
  }

  // -------------------------------------------------------------------------
  TEST_CASE("wait_until with predicate succeeds before deadline") {
    ConditionVariable cv;
    std::mutex mtx;
    bool flag = false;

    std::thread producer([&]() {
      std::this_thread::sleep_for(10ms);
      {
        std::lock_guard<std::mutex> lk(mtx);
        flag = true;
      }
      cv.notify_one();
    });

    std::unique_lock<std::mutex> lk(mtx);
    auto deadline = std::chrono::steady_clock::now() + 500ms;
    bool result = cv.wait_until(lk, deadline, [&] { return flag; });

    CHECK(result);
    producer.join();
  }

  // -------------------------------------------------------------------------
  TEST_CASE("native_handle returns non-null") {
    ConditionVariable cv;
    CHECK(cv.native_handle() != nullptr);
  }

  // -------------------------------------------------------------------------
  TEST_CASE("simple producer-consumer ping-pong") {
    ConditionVariable cv;
    std::mutex mtx;
    int value = 0;
    constexpr int kRounds = 5;

    std::thread producer([&]() {
      for (int i = 1; i <= kRounds; ++i) {
        std::this_thread::sleep_for(5ms);
        {
          std::lock_guard<std::mutex> lk(mtx);
          value = i;
        }
        cv.notify_one();
      }
    });

    for (int expected = 1; expected <= kRounds; ++expected) {
      std::unique_lock<std::mutex> lk(mtx);
      cv.wait(lk, [&] { return value == expected; });
      CHECK(value == expected);
    }

    producer.join();
  }
}

// ---------------------------------------------------------------------------
TEST_SUITE("base-ConditionVariableAny") {
  // -------------------------------------------------------------------------
  TEST_CASE("notify_one wakes a waiting thread using std::mutex") {
    ConditionVariableAny cva;
    std::mutex mtx;
    bool ready = false;

    std::thread producer([&]() {
      std::this_thread::sleep_for(20ms);
      {
        std::lock_guard<std::mutex> lk(mtx);
        ready = true;
      }
      cva.notify_one();
    });

    std::unique_lock<std::mutex> lk(mtx);
    cva.wait(lk, [&] { return ready; });

    CHECK(ready);
    producer.join();
  }

  // -------------------------------------------------------------------------
  TEST_CASE("notify_all wakes all threads") {
    ConditionVariableAny cva;
    std::mutex mtx;
    bool go = false;
    std::atomic<int> woken{0};
    constexpr int kThreads = 3;

    std::vector<std::thread> consumers;
    consumers.reserve(kThreads);

    for (int i = 0; i < kThreads; ++i) {
      consumers.emplace_back([&]() {
        std::unique_lock<std::mutex> lk(mtx);
        cva.wait(lk, [&] { return go; });
        woken.fetch_add(1, std::memory_order_relaxed);
      });
    }

    std::this_thread::sleep_for(20ms);
    {
      std::lock_guard<std::mutex> lk(mtx);
      go = true;
    }
    cva.notify_all();

    for (auto& t : consumers) {
      t.join();
    }

    CHECK(woken.load() == kThreads);
  }

  // -------------------------------------------------------------------------
  TEST_CASE("wait_for times out") {
    ConditionVariableAny cva;
    std::mutex mtx;

    std::unique_lock<std::mutex> lk(mtx);
    auto status = cva.wait_for(lk, 30ms);

    CHECK(status == std::cv_status::timeout);
  }

  // -------------------------------------------------------------------------
  TEST_CASE("wait_for with predicate times out and returns false") {
    ConditionVariableAny cva;
    std::mutex mtx;

    std::unique_lock<std::mutex> lk(mtx);
    bool result = cva.wait_for(lk, 30ms, [] { return false; });

    CHECK(!result);
  }

  // -------------------------------------------------------------------------
  TEST_CASE("wait_for with predicate returns true when notified") {
    ConditionVariableAny cva;
    std::mutex mtx;
    bool flag = false;

    std::thread producer([&]() {
      std::this_thread::sleep_for(10ms);
      {
        std::lock_guard<std::mutex> lk(mtx);
        flag = true;
      }
      cva.notify_one();
    });

    std::unique_lock<std::mutex> lk(mtx);
    bool result = cva.wait_for(lk, 500ms, [&] { return flag; });

    CHECK(result);
    producer.join();
  }

  // -------------------------------------------------------------------------
  TEST_CASE("wait_until with steady_clock times out") {
    ConditionVariableAny cva;
    std::mutex mtx;

    std::unique_lock<std::mutex> lk(mtx);
    auto deadline = std::chrono::steady_clock::now() + 30ms;
    auto status = cva.wait_until(lk, deadline);

    CHECK(status == std::cv_status::timeout);
  }

  // -------------------------------------------------------------------------
  TEST_CASE("wait_until with system_clock times out") {
    ConditionVariableAny cva;
    std::mutex mtx;

    std::unique_lock<std::mutex> lk(mtx);
    auto deadline = std::chrono::system_clock::now() + 30ms;
    auto status = cva.wait_until(lk, deadline);

    CHECK(status == std::cv_status::timeout);
  }

  // -------------------------------------------------------------------------
  TEST_CASE("wait_until with predicate returns false on timeout") {
    ConditionVariableAny cva;
    std::mutex mtx;

    std::unique_lock<std::mutex> lk(mtx);
    auto deadline = std::chrono::steady_clock::now() + 30ms;
    bool result = cva.wait_until(lk, deadline, [] { return false; });

    CHECK(!result);
  }

  // -------------------------------------------------------------------------
  TEST_CASE("wait_until with predicate succeeds before deadline") {
    ConditionVariableAny cva;
    std::mutex mtx;
    bool flag = false;

    std::thread producer([&]() {
      std::this_thread::sleep_for(10ms);
      {
        std::lock_guard<std::mutex> lk(mtx);
        flag = true;
      }
      cva.notify_one();
    });

    std::unique_lock<std::mutex> lk(mtx);
    auto deadline = std::chrono::steady_clock::now() + 500ms;
    bool result = cva.wait_until(lk, deadline, [&] { return flag; });

    CHECK(result);
    producer.join();
  }

  // -------------------------------------------------------------------------
  TEST_CASE("type aliases resolve correctly") {
    // condition_variable and condition_variable_any are aliases
    vlink::condition_variable cv;
    vlink::condition_variable_any cva;

    // Just verify construction and destruction don't crash
    CHECK(cv.native_handle() != nullptr);
    (void)cva;
  }
}

#endif

// NOLINTEND
