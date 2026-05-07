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

/**
 * @file spin_lock.h
 * @brief Adaptive, cache-line-aligned spin lock and RAII guard.
 *
 * @details
 * @c SpinLock implements a simple user-space mutex that spins instead of
 * sleeping.  It is intended for very short critical sections (a few CPU
 * instructions) where the overhead of a @c std::mutex context switch would
 * dominate.  It is used inside @c CpuProfiler and other hot-path VLink internals.
 *
 * Locking strategy inside @c lock():
 * -# Try to acquire with @c exchange(true, acquire); if successful, return.
 * -# Spin with @c load(relaxed) until the flag appears free, then retry.
 * -# Apply exponential back-off: after every @c backoff spins, call
 *    @c Utils::yield_cpu() (a CPU-pause instruction).
 * -# If @c kMaxSpinCount (50000) is exceeded, sleep for 10 us and log an
 *    error.  This is a safety valve for pathological contention.
 *
 * Cache-line alignment (@c alignas(64)) prevents false sharing when multiple
 * @c SpinLock objects reside in adjacent memory.
 *
 * @note
 * - @b Do not use this lock for long critical sections or I/O -- use a
 *   @c std::mutex instead.
 * - @c SpinLock is not recursive; a thread that calls @c lock() twice will
 *   deadlock.
 * - The lock is not @c VLINK_EXPORT because it is header-only and entirely
 *   inlined.
 *
 * @par Example
 * @code
 * vlink::SpinLock lock;
 *
 * // Manual lock/unlock:
 * lock.lock();
 * critical_section();
 * lock.unlock();
 *
 * // RAII guard (preferred):
 * {
 *   vlink::SpinLockGuard guard(lock);
 *   critical_section();
 * }  // automatically unlocked here
 * @endcode
 */

#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#include <thread>

#include "./logger.h"
#include "./macros.h"
#include "./utils.h"

#ifdef _MSC_VER
extern "C" void _mm_pause(void);
#endif

namespace vlink {

/**
 * @class SpinLock
 * @brief Adaptive, cache-line-aligned spin lock.
 *
 * @details
 * Implements the @c Lockable named requirement: @c lock(), @c try_lock(),
 * and @c unlock().  Can be used directly with @c std::lock_guard.
 */
class SpinLock final {
 public:
  /**
   * @brief Default constructor.  Initialises the flag to @c false (unlocked).
   */
  SpinLock() noexcept = default;

  /**
   * @brief Destructor.
   */
  ~SpinLock() noexcept = default;

  /**
   * @brief Acquires the lock, spinning until successful.
   *
   * @details
   * Uses an exponential back-off strategy to reduce bus contention:
   * - Spins with @c load(relaxed) for up to @c backoff iterations.
   * - Calls @c Utils::yield_cpu() (PAUSE/WFE/yield) when @c backoff is reached.
   * - Back-off doubles each round up to 1024.
   * - After 50000 total spins, logs an error and sleeps 10 us.
   *
   * @warning
   * This function must not be called recursively from the same thread; doing so
   * will cause an infinite deadlock.
   */
  void lock() noexcept;

  /**
   * @brief Attempts to acquire the lock without blocking.
   *
   * @details
   * Performs a single @c exchange(true, acquire).  Returns immediately
   * regardless of whether the lock was acquired.
   *
   * @return @c true if the lock was successfully acquired, @c false if it
   *         was already held by another thread.
   */
  bool try_lock() noexcept;

  /**
   * @brief Releases the lock.
   *
   * @details
   * Stores @c false with @c release memory order.  Must only be called by
   * the thread that successfully called @c lock() or @c try_lock().
   */
  void unlock() noexcept;

 private:
  static constexpr size_t kInterferenceSize = 64U;

  alignas(kInterferenceSize) std::atomic<bool> flag_{false};

  VLINK_DISALLOW_COPY_AND_ASSIGN(SpinLock)
};

/**
 * @class SpinLockGuard
 * @brief RAII guard that acquires a @c SpinLock on construction and releases it
 *        on destruction.
 *
 * @details
 * Analogous to @c std::lock_guard<SpinLock>.  Preferred over manual
 * @c lock() / @c unlock() because it is exception-safe.
 *
 * @par Example
 * @code
 * SpinLock my_lock;
 * {
 *   SpinLockGuard guard(my_lock);
 *   // critical section
 * }
 * @endcode
 */
class SpinLockGuard final {
 public:
  /**
   * @brief Acquires @p lock immediately.
   *
   * @param lock  The @c SpinLock to acquire.  Must outlive this guard.
   */
  explicit SpinLockGuard(SpinLock& lock) noexcept;

  /**
   * @brief Releases the lock held by this guard.
   */
  ~SpinLockGuard() noexcept;

 private:
  SpinLock& lock_;

  VLINK_DISALLOW_COPY_AND_ASSIGN(SpinLockGuard)
};

////////////////////////////////////////////////////////////////
/// Details
////////////////////////////////////////////////////////////////

inline void SpinLock::lock() noexcept {
  constexpr static uint32_t kMaxSpinCount = 50000U;

  constexpr static uint16_t kInitialBackoff = 8U;
  constexpr static uint16_t kMaxBackoff = 1024U;

  uint32_t total_spin = 0;
  uint16_t spin_count = 0;
  uint16_t backoff = kInitialBackoff;
  bool warned = false;

  for (;;) {
    if (!flag_.exchange(true, std::memory_order_acquire)) {
      return;
    }

    do {
      ++total_spin;

      if (++spin_count >= backoff) {
        if VUNLIKELY (total_spin > kMaxSpinCount) {
          // LCOV_EXCL_START
          // GCOVR_EXCL_START
          if (!warned) {
            VLOG_E("SpinLock: exceeded max spin count.");
            warned = true;
          }
          std::this_thread::sleep_for(std::chrono::microseconds(10));
          // GCOVR_EXCL_STOP
          // LCOV_EXCL_STOP
        } else {
          Utils::yield_cpu();
        }

        if (backoff < kMaxBackoff) {
          backoff = static_cast<uint16_t>(backoff * 2U);
        }
        spin_count = 0;
      }
    } while (flag_.load(std::memory_order_relaxed));
  }
}

inline bool SpinLock::try_lock() noexcept {
  if (flag_.load(std::memory_order_relaxed)) {
    return false;
  }

  return !flag_.exchange(true, std::memory_order_acquire);
}

inline void SpinLock::unlock() noexcept { flag_.store(false, std::memory_order_release); }

inline SpinLockGuard::SpinLockGuard(SpinLock& lock) noexcept : lock_(lock) { lock_.lock(); }

inline SpinLockGuard::~SpinLockGuard() noexcept { lock_.unlock(); }

}  // namespace vlink
