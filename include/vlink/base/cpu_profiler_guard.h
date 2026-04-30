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
 * @file cpu_profiler_guard.h
 * @brief RAII guard that automatically calls CpuProfiler::begin() and CpuProfiler::end().
 *
 * @details
 * @c CpuProfilerGuard is a lightweight RAII wrapper that calls @c CpuProfiler::begin()
 * in its constructor and @c CpuProfiler::end() in its destructor.  This ensures the
 * active interval is always closed, even if an exception is thrown.
 *
 * @par Example
 * @code
 * vlink::CpuProfiler profiler;
 *
 * void process_frame() {
 *     vlink::CpuProfilerGuard guard(&profiler);
 *     // ... do work ...
 * }  // profiler.end() called here automatically
 *
 * double usage = profiler.get();
 * @endcode
 *
 * @note
 * - Passing @c nullptr as the profiler pointer is safe; both constructor and destructor
 *   check for @c nullptr before calling any method.
 * - The guard is non-copyable and non-movable; it must be used as a stack-allocated object.
 * - Use @c CpuProfiler::is_global_enabled() to skip guard construction when profiling
 *   is disabled globally, reducing overhead in hot paths.
 */

#pragma once

#include "./macros.h"

namespace vlink {

/**
 * @class CpuProfilerGuard
 * @brief RAII scope guard that brackets a @c CpuProfiler active interval.
 *
 * @details
 * Calls @c profiler->begin() on construction and @c profiler->end() on destruction.
 * Handles @c nullptr profiler gracefully (no-op).
 */
class VLINK_EXPORT CpuProfilerGuard final {
 public:
  /**
   * @brief Constructs the guard and calls @c profiler->begin().
   *
   * @param profiler  Pointer to the @c CpuProfiler to bracket.
   *                  If @c nullptr, both construction and destruction are no-ops.
   */
  explicit CpuProfilerGuard(class CpuProfiler* profiler) noexcept;

  /**
   * @brief Destructor -- calls @c profiler->end() to close the active interval.
   */
  ~CpuProfilerGuard() noexcept;

 private:
  class CpuProfiler* profiler_;

  VLINK_DISALLOW_COPY_AND_ASSIGN(CpuProfilerGuard)
};

}  // namespace vlink
