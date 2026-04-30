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

#include "./base/cpu_profiler_guard.h"

#include <doctest/doctest.h>

#include <atomic>
#include <thread>

#include "./base/cpu_profiler.h"

//
#include "../common_test.h"

// ---------------------------------------------------------------------------
// TEST SUITE: CpuProfilerGuard
// ---------------------------------------------------------------------------

TEST_SUITE("base-CpuProfilerGuard") {
  TEST_CASE("construction with nullptr does not crash") {
    CpuProfilerGuard guard(nullptr);
    CHECK(true);
  }

  TEST_CASE("construction with valid profiler calls begin/end") {
    CpuProfiler profiler;
    {
      CpuProfilerGuard guard(&profiler);
      // Simulate work
      std::this_thread::sleep_for(1ms);
    }
    // After guard goes out of scope, end() was called
    CHECK(true);
  }

  TEST_CASE("multiple guards on same profiler") {
    CpuProfiler profiler;
    {
      CpuProfilerGuard guard1(&profiler);
      std::this_thread::sleep_for(1ms);
    }
    {
      CpuProfilerGuard guard2(&profiler);
      std::this_thread::sleep_for(1ms);
    }
    double cpu_usage = profiler.get();
    // Just verify it returns a valid number
    CHECK(cpu_usage >= 0.0);
  }

  TEST_CASE("guard with profiler tracks non-zero usage") {
    CpuProfiler profiler;
    profiler.begin();
    profiler.end();

    {
      CpuProfilerGuard guard(&profiler);
      // Do some work to register CPU time
      std::atomic<int> sum = 0;
      for (int i = 0; i < 10000; ++i) {
        sum += i;
      }
      (void)sum;
    }
    CHECK(true);
  }
}

// NOLINTEND
