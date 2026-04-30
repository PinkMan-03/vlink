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

#include "./base/spin_lock.h"

#include <doctest/doctest.h>

#include <atomic>
#include <thread>
#include <vector>

//
#include "../common_test.h"

// ---------------------------------------------------------------------------
TEST_SUITE("base-SpinLock") {
  // -------------------------------------------------------------------------
  TEST_CASE("default-constructed lock is unlocked - try_lock succeeds") {
    SpinLock lk;

    bool acquired = lk.try_lock();
    CHECK(acquired);

    // Clean up
    lk.unlock();
  }

  // -------------------------------------------------------------------------
  TEST_CASE("lock and unlock basic sequence") {
    SpinLock lk;

    lk.lock();
    // We are now inside the critical section.
    // Unlock must succeed without hanging.
    lk.unlock();

    // After unlock, try_lock must succeed again
    CHECK(lk.try_lock());
    lk.unlock();
  }

  // -------------------------------------------------------------------------
  TEST_CASE("try_lock returns false when lock is already held") {
    SpinLock lk;

    lk.lock();

    // The lock is currently held by this thread; try_lock from the same
    // context (without unlock) must fail.
    bool second = lk.try_lock();
    CHECK(!second);

    lk.unlock();
  }

  // -------------------------------------------------------------------------
  TEST_CASE("try_lock returns true on unlocked lock") {
    SpinLock lk;

    CHECK(lk.try_lock());
    lk.unlock();
  }

  // -------------------------------------------------------------------------
  TEST_CASE("repeated lock/unlock cycles work correctly") {
    SpinLock lk;

    for (int i = 0; i < 100; ++i) {
      lk.lock();
      lk.unlock();
    }

    // After 100 cycles the lock should still be acquirable
    CHECK(lk.try_lock());
    lk.unlock();
  }

  // -------------------------------------------------------------------------
  TEST_CASE("SpinLockGuard acquires on construction and releases on destruction") {
    SpinLock lk;

    {
      SpinLockGuard guard(lk);
      // Inside this scope the lock is held; try_lock from same thread
      // should fail
      CHECK(!lk.try_lock());
    }  // guard destructor releases the lock here

    // After scope the lock must be free again
    CHECK(lk.try_lock());
    lk.unlock();
  }

  // -------------------------------------------------------------------------
  TEST_CASE("SpinLockGuard - nested scope releases inner guard first") {
    SpinLock lk;

    lk.lock();
    lk.unlock();

    {
      SpinLockGuard g1(lk);
      // g1 holds the lock; a second try must fail
      CHECK(!lk.try_lock());
    }

    // g1 is destroyed; lock is free
    CHECK(lk.try_lock());
    lk.unlock();
  }

  // -------------------------------------------------------------------------
  TEST_CASE("concurrent increments with SpinLock produce correct total") {
    // Shared counter protected by a SpinLock.
    // Multiple threads each increment it N times; total must be threads * N.
    SpinLock lk;
    int counter = 0;

    constexpr int kThreads = 8;
    constexpr int kIncrementsPerThread = 10000;

    std::vector<std::thread> workers;
    workers.reserve(kThreads);

    for (int i = 0; i < kThreads; ++i) {
      workers.emplace_back([&]() {
        for (int j = 0; j < kIncrementsPerThread; ++j) {
          SpinLockGuard guard(lk);
          ++counter;
        }
      });
    }

    for (auto& w : workers) {
      w.join();
    }

    CHECK(counter == kThreads * kIncrementsPerThread);
  }

  // -------------------------------------------------------------------------
  TEST_CASE("concurrent increments with manual lock/unlock produce correct total") {
    SpinLock lk;
    int counter = 0;

    constexpr int kThreads = 4;
    constexpr int kIncrementsPerThread = 5000;

    std::vector<std::thread> workers;
    workers.reserve(kThreads);

    for (int i = 0; i < kThreads; ++i) {
      workers.emplace_back([&]() {
        for (int j = 0; j < kIncrementsPerThread; ++j) {
          lk.lock();
          ++counter;
          lk.unlock();
        }
      });
    }

    for (auto& w : workers) {
      w.join();
    }

    CHECK(counter == kThreads * kIncrementsPerThread);
  }

  // -------------------------------------------------------------------------
  TEST_CASE("try_lock under contention from another thread fails") {
    SpinLock lk;
    std::atomic<bool> lock_held{false};
    std::atomic<bool> can_release{false};

    // Thread A holds the lock and signals that it is held.
    std::thread holder([&]() {
      lk.lock();
      lock_held.store(true, std::memory_order_release);
      // Spin until the test signals it is OK to release
      while (!can_release.load(std::memory_order_acquire)) {
        std::this_thread::yield();
      }
      lk.unlock();
    });

    // Wait until the holder actually has the lock
    while (!lock_held.load(std::memory_order_acquire)) {
      std::this_thread::yield();
    }

    // Now try_lock from this thread must fail
    bool acquired = lk.try_lock();
    CHECK(!acquired);

    // Signal the holder to release
    can_release.store(true, std::memory_order_release);
    holder.join();

    // After the holder releases, try_lock must succeed
    CHECK(lk.try_lock());
    lk.unlock();
  }

  // -------------------------------------------------------------------------
  TEST_CASE("lock acquires after another thread releases") {
    SpinLock lk;
    std::atomic<bool> ready{false};
    std::atomic<bool> released{false};

    std::thread helper([&]() {
      lk.lock();
      ready.store(true, std::memory_order_release);
      std::this_thread::sleep_for(10ms);
      released.store(true, std::memory_order_release);
      lk.unlock();
    });

    // Wait until helper holds the lock
    while (!ready.load(std::memory_order_acquire)) {
      std::this_thread::yield();
    }

    // This will block until the helper releases
    lk.lock();
    // We got the lock; the helper must have released it by now
    CHECK(released.load(std::memory_order_acquire));
    lk.unlock();

    helper.join();
  }

  // -------------------------------------------------------------------------
  TEST_CASE("kInterferenceSize constant is 64") {
    // Verify the documented cache-line size constant via the header.
    // We access it through a local helper since it is private; the size of
    // the class itself is at least kInterferenceSize due to alignas(64).
    CHECK(alignof(SpinLock) == 64u);
  }

  // -------------------------------------------------------------------------
  TEST_CASE("kMaxSpinCount constant - no observable deadlock under normal contention") {
    // Stress-test: many short critical sections should complete without
    // hitting the 50000-spin safety valve in practice.
    SpinLock lk;
    std::atomic<int> total{0};

    constexpr int kThreads = 16;
    constexpr int kOps = 2000;

    std::vector<std::thread> workers;
    workers.reserve(kThreads);

    for (int i = 0; i < kThreads; ++i) {
      workers.emplace_back([&]() {
        for (int j = 0; j < kOps; ++j) {
          SpinLockGuard g(lk);
          total.fetch_add(1, std::memory_order_relaxed);
        }
      });
    }

    for (auto& w : workers) {
      w.join();
    }

    CHECK(total.load() == kThreads * kOps);
  }

  // -------------------------------------------------------------------------
  TEST_CASE("SpinLockGuard usable with std::lock_guard-like pattern in lambda") {
    SpinLock lk;
    int value = 0;

    auto inc = [&]() {
      SpinLockGuard guard(lk);
      ++value;
    };

    inc();
    inc();
    inc();

    CHECK(value == 3);
  }

  // -------------------------------------------------------------------------
  TEST_CASE("unlock without lock does not crash (implementation-defined but must not hang)") {
    SpinLock lk;
    // Calling unlock on an unlocked SpinLock is technically undefined in the
    // Lockable contract; however the implementation simply stores false, which
    // is idempotent and should not crash.
    lk.unlock();
    CHECK(lk.try_lock());
    lk.unlock();
  }
}

// NOLINTEND
