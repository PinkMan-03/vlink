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
 * @file elapsed_timer.h
 * @brief High-resolution elapsed-time measurement with configurable clock source and precision.
 *
 * @details
 * @c ElapsedTimer measures how much time (wall-clock or CPU-active time) has elapsed
 * since @c start() was called.  It is the lowest-level timing primitive in VLink and
 * is used internally by @c DeadlineTimer, @c CpuProfiler, and the message-loop task
 * latency tracking.
 *
 * Two independent axes of configuration are available:
 *
 * @par Clock source (Method)
 * | Value              | Clock source                        | Notes                              |
 * | ------------------ | ----------------------------------- | ---------------------------------- |
 * | kCpuTimestamp      | steady_clock (instances)            | Monotonic wall time, never jumps   |
 * | kCpuActiveTime     | getrusage / GetProcessTimes         | CPU user+kernel time consumed      |
 *
 * @par Precision (Accuracy)
 * | Value   | Unit         | Range (64-bit)          |
 * | ------- | ------------ | ----------------------- |
 * | kMilli  | milliseconds | ~292 million years      |
 * | kMicro  | microseconds | ~292 thousand years     |
 * | kNano   | nanoseconds  | ~292 years              |
 *
 * @note
 * - The timer is @b not started on construction; call @c start() explicitly.
 * - @c get() returns @c -1 when the timer has not been started or has been stopped.
 * - The internal @c start_time_ is stored as a @c std::atomic<int64_t>, making
 *   concurrent reads from multiple threads safe, though concurrent @c start()/stop()
 *   calls from different threads lead to a data race on the "is started" semantic.
 * - On Linux, @c get_sys_timestamp() uses @c CLOCK_REALTIME via @c clock_gettime for
 *   nanosecond resolution; on Windows it falls back to @c std::chrono::system_clock.
 * - Instance timing uses @c std::chrono::steady_clock for @c kCpuTimestamp.
 *   The static @c get_cpu_timestamp(..., true) helper may use
 *   @c CLOCK_MONOTONIC_RAW on Linux for a high-resolution monotonic timestamp.
 *
 * @par Example
 * @code
 * vlink::ElapsedTimer t(vlink::ElapsedTimer::kCpuTimestamp,
 *                       vlink::ElapsedTimer::kMicro);
 * t.start();
 * do_work();
 * int64_t us = t.get();   // microseconds elapsed; -1 if not started
 * t.stop();
 *
 * // One-liner: restart returns elapsed and resets the timer atomically
 * int64_t delta = t.restart();
 * @endcode
 */

#pragma once

#include <atomic>
#include <cstdint>

#include "./macros.h"

namespace vlink {

/**
 * @class ElapsedTimer
 * @brief Atomic, high-resolution elapsed-time timer.
 *
 * @details
 * Measures elapsed time since @c start() using either a monotonic wall-clock
 * (@c kCpuTimestamp) or the process CPU-active time (@c kCpuActiveTime).
 * The class is @c final and not copyable (copy/move constructors are provided
 * but only copy the snapshot value, not ownership semantics).
 */
class VLINK_EXPORT ElapsedTimer final {
 public:
  /**
   * @brief Selects the underlying clock source for time measurement.
   */
  enum Method : uint8_t {
    kCpuTimestamp = 0,  ///< Monotonic wall-clock (steady_clock for instance timing)
    kCpuActiveTime = 1  ///< Process CPU time (user + kernel, via getrusage)
  };

  /**
   * @brief Selects the precision (resolution) of the timer values returned.
   */
  enum Accuracy : uint8_t {
    kMilli = 0,  ///< Millisecond precision
    kMicro = 1,  ///< Microsecond precision
    kNano = 2    ///< Nanosecond precision
  };

  /**
   * @brief Constructs an @c ElapsedTimer with default method (@c kCpuTimestamp)
   *        and default accuracy (@c kMilli).
   *
   * @details The timer is @b not started after construction.
   */
  ElapsedTimer() noexcept;

  /**
   * @brief Constructs an @c ElapsedTimer with the specified clock source.
   *
   * @param method  The clock source to use (@c kCpuTimestamp or @c kCpuActiveTime).
   */
  explicit ElapsedTimer(Method method) noexcept;

  /**
   * @brief Constructs an @c ElapsedTimer with the specified precision.
   *
   * @param accuracy  The precision of time values (@c kMilli, @c kMicro, or @c kNano).
   */
  explicit ElapsedTimer(Accuracy accuracy) noexcept;

  /**
   * @brief Constructs an @c ElapsedTimer with the specified clock source and precision.
   *
   * @param method    The clock source to use.
   * @param accuracy  The precision of time values.
   */
  explicit ElapsedTimer(Method method, Accuracy accuracy) noexcept;

  /**
   * @brief Copy constructor.  Copies the current start-time snapshot, method, and accuracy.
   *
   * @param target  The source timer.
   */
  ElapsedTimer(const ElapsedTimer& target) noexcept;

  /**
   * @brief Move constructor.  Behaves identically to the copy constructor.
   *
   * @param target  The source timer.
   */
  ElapsedTimer(ElapsedTimer&& target) noexcept;

  /**
   * @brief Destructor.
   */
  ~ElapsedTimer() noexcept;

  /**
   * @brief Copy-assignment operator.
   *
   * @param target  The source timer.
   * @return A reference to @c *this.
   */
  ElapsedTimer& operator=(const ElapsedTimer& target) noexcept;

  /**
   * @brief Move-assignment operator.
   *
   * The source timer is unnamed in the declaration and is moved from.
   * @return A reference to @c *this.
   */
  ElapsedTimer& operator=(ElapsedTimer&&) noexcept;

  /**
   * @brief Returns the current wall-clock (system) timestamp.
   *
   * @details
   * On Linux uses @c CLOCK_REALTIME via @c clock_gettime for maximum precision
   * when @p high_resolution is @c true; otherwise uses @c std::chrono::system_clock.
   * On Windows always uses @c std::chrono::system_clock.
   *
   * @param accuracy         Desired precision (default: @c kMilli).
   * @param high_resolution  Whether to use @c clock_gettime (Linux only, default: @c true).
   * @return Current system timestamp in the requested unit.
   */
  [[nodiscard]] static uint64_t get_sys_timestamp(Accuracy accuracy = kMilli, bool high_resolution = true) noexcept;

  /**
   * @brief Returns the current monotonic CPU timestamp.
   *
   * @details
   * On Linux uses @c CLOCK_MONOTONIC_RAW (unaffected by NTP) when
   * @p high_resolution is @c true; otherwise uses @c std::chrono::steady_clock.
   * On Windows always uses @c std::chrono::steady_clock.
   *
   * @param accuracy         Desired precision (default: @c kMilli).
   * @param high_resolution  Whether to use @c clock_gettime (Linux only, default: @c true).
   * @return Current monotonic timestamp in the requested unit.
   */
  [[nodiscard]] static uint64_t get_cpu_timestamp(Accuracy accuracy = kMilli, bool high_resolution = true) noexcept;

  /**
   * @brief Returns the accumulated CPU time (user + kernel) consumed by the current process.
   *
   * @details
   * Uses @c getrusage(RUSAGE_SELF) on POSIX or @c GetProcessTimes on Windows.
   * The returned value is the total CPU time used so far in the process lifetime,
   * not a delta.  Use @c ElapsedTimer with @c kCpuActiveTime to measure deltas.
   *
   * @param accuracy  Desired precision (default: @c kMilli).
   * @return Cumulative process CPU time in the requested unit.
   */
  [[nodiscard]] static uint64_t get_cpu_active_time(Accuracy accuracy = kMilli) noexcept;

  /**
   * @brief Returns the clock source configured for this timer.
   *
   * @return The @c Method value set at construction.
   */
  [[nodiscard]] Method get_method() const noexcept;

  /**
   * @brief Returns the precision configured for this timer.
   *
   * @return The @c Accuracy value set at construction.
   */
  [[nodiscard]] Accuracy get_accuracy() const noexcept;

  /**
   * @brief Starts the timer if it is not already active.
   *
   * @details
   * Records the current time as the start reference using a CAS on the
   * internal atomic.  If the timer is already active this call is a no-op.
   * Call @c stop() first to reset and then @c start() again.
   */
  void start() noexcept;

  /**
   * @brief Stops the timer, setting the internal start time to @c -1.
   *
   * @details
   * After @c stop(), @c is_active() returns @c false and @c get() returns @c -1.
   */
  void stop() noexcept;

  /**
   * @brief Atomically resets the start time to now and returns the elapsed time
   *        since the previous @c start() / @c restart() call.
   *
   * @details
   * This reads the elapsed time and resets the start reference to now in one
   * atomic exchange.  Unlike @c start(), it resets an already-active timer.
   * If the timer was not active, returns a negative value (the raw
   * @c start_time_ value, which is -1 when stopped) and starts timing from now.
   *
   * @return
   * - Elapsed time in the configured units since the last start/restart, or
   * - A negative value if the timer was not started before this call.
   */
  int64_t restart() noexcept;

  /**
   * @brief Returns @c true when the timer has been started and not yet stopped.
   *
   * @return @c true if active (start_time_ >= 0), @c false otherwise.
   */
  [[nodiscard]] bool is_active() const noexcept;

  /**
   * @brief Returns the elapsed time since @c start() was called.
   *
   * @details
   * The value is computed as @c (current_time - start_time_) in the configured
   * precision units.  Returns @c -1 if the timer has not been started (or has
   * been stopped).
   *
   * @return
   * - Elapsed time in the configured units (>= 0) when active.
   * - @c -1 when the timer is not active.
   */
  [[nodiscard]] int64_t get() const noexcept;

 private:
  alignas(64) std::atomic<int64_t> start_time_{-1};
  Method method_{kCpuTimestamp};
  Accuracy accuracy_{kMilli};
};

}  // namespace vlink
