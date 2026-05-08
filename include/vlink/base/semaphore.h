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
 * @file semaphore.h
 * @brief In-process counting semaphore with optional timeout.
 *
 * @details
 * @c Semaphore provides classic P/V (acquire/release) semaphore semantics
 * within a single process.  It is built on @c std::mutex and @c std::condition_variable
 * (via the VLink @c ConditionVariable, which uses @c CLOCK_MONOTONIC to avoid NTP
 * clock-jump issues on Linux).
 *
 * Typical use cases:
 * - Limiting concurrent access to a resource pool.
 * - Signalling between a producer and one or more consumers.
 * - Throttling task submission to a bounded work queue.
 *
 * @note
 * - @c acquire() blocks the caller until at least @p n permits are available.
 * - @c release() is safe to call from any thread.  It is NOT
 *   async-signal-safe because it acquires @c std::mutex internally; do not
 *   call it from a signal handler.
 * - @c reset() with @p interrupt_waiters == @c true is a disruptive operation
 *   that wakes all blocked @c acquire() callers and returns @c false to them.
 *   Use it only during controlled shutdown.
 * - This is an in-process semaphore.  For cross-process synchronisation, use
 *   @c SysSemaphore.
 *
 * @par Example
 * @code
 * vlink::Semaphore sem(0);  // start at 0
 *
 * // Producer:
 * do_work();
 * sem.release();  // signal one consumer
 *
 * // Consumer:
 * if (sem.acquire(1, 100)) {  // wait up to 100 ms
 *   consume_result();
 * } else {
 *   handle_timeout();
 * }
 * @endcode
 */

#pragma once

#include <memory>

#include "./macros.h"

namespace vlink {

/**
 * @class Semaphore
 * @brief In-process counting semaphore with optional acquire timeout.
 *
 * @details
 * The internal counter is initialised via the constructor and can be
 * atomically decremented (@c acquire) or incremented (@c release).
 */
class VLINK_EXPORT Semaphore final {
 public:
  /**
   * @brief Sentinel value for @c acquire() meaning "wait indefinitely".
   */
  static constexpr int kInfinite{-1};

  /**
   * @brief Constructs a @c Semaphore with an initial counter value.
   *
   * @param count  Initial number of available permits (default: 0).
   */
  explicit Semaphore(size_t count = 0) noexcept;

  /**
   * @brief Destructor.  Wakes all blocked acquirers before destruction.
   */
  ~Semaphore() noexcept;

  /**
   * @brief Decrements the semaphore counter by @p n, blocking until permits
   *        are available.
   *
   * @details
   * If the counter is less than @p n, the caller is blocked until enough
   * @c release() calls bring the counter up.  A timeout causes the function
   * to return @c false without decrementing the counter.
   *
   * @param n           Number of permits to acquire (default: 1).
   * @param timeout_ms  Maximum time to wait in milliseconds.
   *                    Use @c kInfinite (-1) to wait indefinitely (default).
   * @return @c true if the permits were acquired, @c false on timeout or
   *         if the semaphore was reset with @p interrupt_waiters == @c true.
   */
  bool acquire(size_t n = 1, int timeout_ms = kInfinite) noexcept;

  /**
   * @brief Increments the semaphore counter by @p n, waking blocked acquirers.
   *
   * @details
   * Increments the counter under the mutex and then broadcasts to all blocked
   * @c acquire() callers; each will re-check whether enough permits are available.
   *
   * @param n  Number of permits to release (default: 1).
   */
  void release(size_t n = 1) noexcept;

  /**
   * @brief Resets the semaphore counter to its initial (constructor-provided) value.
   *
   * @details
   * If @p interrupt_waiters is @c true, all threads blocked in @c acquire()
   * are woken and their calls return @c false.  Use this during shutdown to
   * unblock consumers cleanly.
   *
   * @param interrupt_waiters  If @c true, wake all blocked acquirers (default: false).
   */
  void reset(bool interrupt_waiters = false) noexcept;

  /**
   * @brief Returns the current value of the semaphore counter.
   *
   * @details
   * The returned value is a snapshot; it may change before the caller can use it.
   * Intended for diagnostic and logging purposes only.
   *
   * @return Current counter value.
   */
  [[nodiscard]] size_t get_count() const noexcept;

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;

  VLINK_DISALLOW_COPY_AND_ASSIGN(Semaphore)
};

}  // namespace vlink
