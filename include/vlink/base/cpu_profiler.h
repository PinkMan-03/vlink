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
 * @file cpu_profiler.h
 * @brief Per-instance CPU utilisation profiler gated by a global environment variable.
 *
 * @details
 * @c CpuProfiler measures the fraction of wall-clock time that the CPU was actively
 * executing work between paired @c begin() / @c end() calls.  It uses two @c ElapsedTimer
 * instances internally:
 *
 * - @c cpu_active_timer_  -- measures only active time (CPU time spent in the section).
 * - @c cpu_timestamp_timer_ -- measures total elapsed wall-clock time since @c begin().
 *
 * The utilisation ratio is:
 * @code
 * utilisation (%) = (sum of active intervals / total elapsed time) * 100
 * @endcode
 *
 * @par Global enable gate
 * Profiling is gated by the environment variable @c VLINK_PROFILER_ENABLE.
 * Set it to @c "1" to enable; @c "0" (default) to disable.  The value is read once at
 * first call to @c is_global_enabled() and cached for the process lifetime.
 * @code
 * export VLINK_PROFILER_ENABLE=1
 * @endcode
 *
 * @par Typical usage
 * @code
 * vlink::CpuProfiler profiler;
 *
 * for (auto& item : work_items) {
 *     profiler.begin();
 *     process(item);
 *     profiler.end();
 * }
 *
 * double pct = profiler.get();  // CPU utilisation since first begin()
 * double reset_pct = profiler.restart();  // returns current value and resets
 * @endcode
 *
 * @note
 * - @c begin() and @c end() are guarded by an internal @c SpinLock and are safe to call
 *   from any thread, but each profiler instance measures a single logical stream of work.
 * - If profiling is globally disabled, @c begin() / @c end() still execute but should be
 *   short-circuited by the caller using @c is_global_enabled() for maximum performance.
 * - @c CpuProfilerGuard provides a convenient RAII wrapper that calls @c begin() and
 *   @c end() automatically.
 */

#pragma once

#include <atomic>
#include <cstdint>

#include "./elapsed_timer.h"
#include "./macros.h"
#include "./spin_lock.h"

namespace vlink {

/**
 * @class CpuProfiler
 * @brief Tracks CPU active time as a percentage of total elapsed wall-clock time.
 *
 * @details
 * Each @c begin() / @c end() pair contributes one active interval to the running total.
 * @c get() returns the cumulative utilisation ratio; @c restart() returns the ratio and
 * resets all accumulators.
 *
 * @note Copy and assignment are disabled.  Instances should be owned by a single component.
 */
class VLINK_EXPORT CpuProfiler final {
 public:
  /**
   * @brief Constructs a profiler with all accumulators initialised to zero.
   */
  CpuProfiler() noexcept;

  /**
   * @brief Destructor.
   */
  ~CpuProfiler() noexcept;

  /**
   * @brief Returns whether CPU profiling is globally enabled via environment variable.
   *
   * @details
   * Reads @c VLINK_PROFILER_ENABLE on the first call and caches the result.
   * Returns @c true if the variable is set to @c "1", @c false otherwise.
   *
   * @return @c true if profiling is enabled globally.
   */
  [[nodiscard]] static bool is_global_enabled() noexcept;

  /**
   * @brief Marks the start of an active CPU work section.
   *
   * @details
   * Restarts the active-time timer.  If this is the very first call, also starts the
   * wall-clock timestamp timer used as the denominator in @c get().
   * Safe to call multiple times; each call resets the active-timer baseline.
   *
   * @note This method acquires an internal @c SpinLock briefly.
   */
  void begin() noexcept;

  /**
   * @brief Marks the end of an active CPU work section and accumulates the elapsed time.
   *
   * @details
   * Reads the active-time elapsed since the last @c begin() and adds it to the running
   * total (@c total_active_).  Negative elapsed values (e.g., clock skew) are ignored.
   *
   * @note This method acquires an internal @c SpinLock briefly.
   */
  void end() noexcept;

  /**
   * @brief Returns the current CPU utilisation ratio as a percentage.
   *
   * @details
   * Computes:  @c (total_active_ns / total_elapsed_ns) * 100.0
   * Returns @c 0.0 if no time has elapsed since profiling started.
   *
   * @return CPU utilisation in the range [0.0, 100.0] (may exceed 100 on multi-core).
   */
  [[nodiscard]] double get() const noexcept;

  /**
   * @brief Returns the current utilisation and resets all accumulators to zero.
   *
   * @details
   * The return value is identical to calling @c get() before the reset.
   * After this call, @c get() returns @c 0.0 until the next @c begin() / @c end() pair.
   *
   * @note This method acquires an internal @c SpinLock briefly.
   *
   * @return CPU utilisation percentage accumulated since construction or the last @c restart().
   */
  double restart() noexcept;

 private:
  alignas(64) std::atomic<uint64_t> total_active_{0};
  ElapsedTimer cpu_active_timer_{ElapsedTimer::kCpuActiveTime, ElapsedTimer::kNano};
  ElapsedTimer cpu_timestamp_timer_{ElapsedTimer::kCpuTimestamp, ElapsedTimer::kNano};
  SpinLock spin_;

  VLINK_DISALLOW_COPY_AND_ASSIGN(CpuProfiler)
};

}  // namespace vlink
