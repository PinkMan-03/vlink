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

// Example: SpinLock - lightweight lock for short critical sections

#include <vlink/base/elapsed_timer.h>
#include <vlink/base/logger.h>
#include <vlink/base/spin_lock.h>

#include <mutex>
#include <thread>
#include <vector>

int main() {
  // ---------------------------------------------------------------
  // 1. Basic lock/unlock.
  // ---------------------------------------------------------------
  {
    VLOG_I("=== Section 1: Basic lock/unlock ===");
    vlink::SpinLock lock;

    lock.lock();
    VLOG_I("  Critical section (manual lock)");
    lock.unlock();

    VLOG_I("  Lock released");
  }

  // ---------------------------------------------------------------
  // 2. RAII guard (SpinLockGuard).
  // ---------------------------------------------------------------
  {
    VLOG_I("=== Section 2: SpinLockGuard (RAII) ===");
    vlink::SpinLock lock;

    {
      vlink::SpinLockGuard guard(lock);
      VLOG_I("  Critical section (RAII guard)");
    }
    VLOG_I("  Lock automatically released by guard destructor");
  }

  // ---------------------------------------------------------------
  // 3. try_lock - non-blocking attempt.
  // ---------------------------------------------------------------
  {
    VLOG_I("=== Section 3: try_lock ===");
    vlink::SpinLock lock;

    bool acquired = lock.try_lock();
    MLOG_I("  First try_lock: {}", acquired);

    // Second attempt while held should fail.
    // Note: calling try_lock from the same thread while held is safe.
    bool second = lock.try_lock();
    MLOG_I("  Second try_lock (while held): {}", second);

    lock.unlock();

    bool third = lock.try_lock();
    MLOG_I("  Third try_lock (after unlock): {}", third);
    if (third) {
      lock.unlock();
    }
  }

  // ---------------------------------------------------------------
  // 4. Multi-threaded counter protection.
  // ---------------------------------------------------------------
  {
    VLOG_I("=== Section 4: Multi-threaded counter ===");
    vlink::SpinLock lock;
    int64_t counter = 0;
    constexpr int kThreads = 4;
    constexpr int kIterations = 100000;

    std::vector<std::thread> threads;
    for (int t = 0; t < kThreads; ++t) {
      threads.emplace_back([&]() {  // NOLINT(performance-inefficient-vector-operation)
        for (int i = 0; i < kIterations; ++i) {
          vlink::SpinLockGuard guard(lock);
          ++counter;
        }
      });
    }

    for (auto& t : threads) {
      t.join();
    }

    int64_t expected = static_cast<int64_t>(kThreads) * kIterations;
    MLOG_I("  Counter: {} (expected: {})", counter, expected);
    MLOG_I("  Correct: {}", counter == expected);
  }

  // ---------------------------------------------------------------
  // 5. SpinLock with std::lock_guard (satisfies Lockable).
  // ---------------------------------------------------------------
  {
    VLOG_I("=== Section 5: std::lock_guard compatibility ===");
    vlink::SpinLock lock;

    {
      std::lock_guard guard(lock);  // CTAD deduces std::lock_guard<vlink::SpinLock>
      VLOG_I("  Using std::lock_guard with SpinLock");
    }
    VLOG_I("  std::lock_guard released");
  }

  // ---------------------------------------------------------------
  // 6. Performance comparison with short critical section.
  // ---------------------------------------------------------------
  {
    VLOG_I("=== Section 6: Performance measurement ===");
    vlink::SpinLock spin;
    std::mutex mtx;
    constexpr int kOps = 1000000;

    // SpinLock performance.
    vlink::ElapsedTimer timer(vlink::ElapsedTimer::kCpuTimestamp, vlink::ElapsedTimer::kMicro);
    timer.start();
    for (int i = 0; i < kOps; ++i) {
      vlink::SpinLockGuard guard(spin);
    }
    int64_t spin_time = timer.get();
    timer.stop();

    // std::mutex performance.
    timer.start();
    for (int i = 0; i < kOps; ++i) {
      std::lock_guard guard(mtx);
    }
    int64_t mutex_time = timer.get();
    timer.stop();

    MLOG_I("  SpinLock: {}us for {}M ops", spin_time, kOps / 1000000);
    MLOG_I("  std::mutex: {}us for {}M ops", mutex_time, kOps / 1000000);
  }

  VLOG_I("SpinLock example finished.");
  return 0;
}
