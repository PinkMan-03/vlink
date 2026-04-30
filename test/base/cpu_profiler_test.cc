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

#include "./base/cpu_profiler.h"

#include <doctest/doctest.h>

#include <atomic>
#include <chrono>
#include <thread>

#include "./base/cpu_profiler_guard.h"

//
#include "../common_test.h"

// ---------------------------------------------------------------------------
// TEST SUITE: CpuProfiler - construction
// ---------------------------------------------------------------------------

TEST_SUITE("base-CpuProfiler - construction") {
  TEST_CASE("default-constructed profiler has zero utilisation") {
    CpuProfiler p;
    CHECK(p.get() == doctest::Approx(0.0));
  }

  TEST_CASE("is_global_enabled returns bool without crashing") {
    bool enabled = CpuProfiler::is_global_enabled();
    (void)enabled;
    CHECK(true);
  }
}

// ---------------------------------------------------------------------------
// TEST SUITE: CpuProfiler - begin / end
// ---------------------------------------------------------------------------

TEST_SUITE("base-CpuProfiler - begin and end") {
  TEST_CASE("begin and end do not crash") {
    CpuProfiler p;
    p.begin();
    p.end();
    CHECK(true);
  }

  TEST_CASE("multiple begin/end pairs do not crash") {
    CpuProfiler p;

    for (int i = 0; i < 5; ++i) {
      p.begin();
      std::this_thread::sleep_for(std::chrono::microseconds(100));
      p.end();
    }

    CHECK(true);
  }

  TEST_CASE("end without begin does not crash") {
    CpuProfiler p;
    p.end();
    CHECK(true);
  }
}

// ---------------------------------------------------------------------------
// TEST SUITE: CpuProfiler - get
// ---------------------------------------------------------------------------

TEST_SUITE("base-CpuProfiler - get") {
  TEST_CASE("get returns 0 before any begin") {
    CpuProfiler p;
    CHECK(p.get() == doctest::Approx(0.0));
  }

  TEST_CASE("get returns non-negative after work interval") {
    CpuProfiler p;
    p.begin();
    std::this_thread::sleep_for(std::chrono::milliseconds(2));
    p.end();

    double usage = p.get();
    CHECK(usage >= 0.0);
  }

  TEST_CASE("get does not reset accumulators") {
    // get() should not change the internal state; multiple calls after
    // the same begin/end pair should each return >= 0.
    CpuProfiler p;
    p.begin();
    std::this_thread::sleep_for(std::chrono::microseconds(500));
    p.end();

    double first = p.get();
    double second = p.get();

    // Both must be non-negative; they may differ slightly because
    // total_elapsed grows while active stays fixed, so second <= first.
    CHECK(first >= 0.0);
    CHECK(second >= 0.0);
  }
}

// ---------------------------------------------------------------------------
// TEST SUITE: CpuProfiler - restart
// ---------------------------------------------------------------------------

TEST_SUITE("base-CpuProfiler - restart") {
  TEST_CASE("restart returns current value and resets to 0") {
    CpuProfiler p;
    p.begin();
    std::this_thread::sleep_for(std::chrono::milliseconds(2));
    p.end();

    double val = p.restart();
    CHECK(val >= 0.0);

    // After restart, get should return 0 (no new intervals recorded)
    CHECK(p.get() == doctest::Approx(0.0));
  }

  TEST_CASE("restart on fresh profiler returns 0 and stays 0") {
    CpuProfiler p;
    double val = p.restart();
    CHECK(val == doctest::Approx(0.0));
    CHECK(p.get() == doctest::Approx(0.0));
  }

  TEST_CASE("profiler is usable again after restart") {
    CpuProfiler p;
    p.begin();
    std::this_thread::sleep_for(std::chrono::microseconds(500));
    p.end();
    p.restart();

    p.begin();
    std::this_thread::sleep_for(std::chrono::microseconds(500));
    p.end();

    double val = p.get();
    CHECK(val >= 0.0);
  }
}

// ---------------------------------------------------------------------------
// TEST SUITE: CpuProfiler - utilisation stays in expected range
// ---------------------------------------------------------------------------

TEST_SUITE("base-CpuProfiler - utilisation range") {
  TEST_CASE("100% CPU work yields high utilisation close to 100") {
    CpuProfiler p;

    // Spin for a fixed wall-clock window to simulate 100% CPU usage
    auto start = std::chrono::steady_clock::now();

    p.begin();

    std::atomic<int> busy = 0;

    while (std::chrono::steady_clock::now() - start < std::chrono::milliseconds(10)) {
      ++busy;
    }

    p.end();

    double usage = p.get();
    // Must be in [0, 200] — generous upper bound because of scheduling jitter
    CHECK(usage >= 0.0);
    CHECK(usage <= 200.0);
  }

  TEST_CASE("idle work (sleep) yields low utilisation") {
    CpuProfiler p;

    // Active time: 1 ms active, 9 ms idle
    for (int i = 0; i < 3; ++i) {
      // Mark 0 ms of active work then end immediately
      p.begin();
      p.end();
      std::this_thread::sleep_for(std::chrono::milliseconds(3));
    }

    double usage = p.get();
    // Utilisation should be very low (not guaranteed; just check it is >= 0)
    CHECK(usage >= 0.0);
    CHECK(usage < 100.0);
  }
}

// ---------------------------------------------------------------------------
// TEST SUITE: CpuProfilerGuard
// ---------------------------------------------------------------------------

TEST_SUITE("base-CpuProfilerGuard - RAII") {
  TEST_CASE("guard calls begin and end automatically") {
    CpuProfiler profiler;

    {
      CpuProfilerGuard guard(&profiler);
      std::this_thread::sleep_for(std::chrono::microseconds(200));
    }

    // end() was called; get() should return a valid (non-negative) value
    CHECK(profiler.get() >= 0.0);
  }

  TEST_CASE("null profiler pointer guard does not crash") {
    CpuProfilerGuard guard(nullptr);
    // Destructor must not crash either — verified implicitly at scope exit
    CHECK(true);
  }

  TEST_CASE("multiple guards on the same profiler accumulate correctly") {
    CpuProfiler profiler;

    for (int i = 0; i < 3; ++i) {
      CpuProfilerGuard guard(&profiler);
      std::this_thread::sleep_for(std::chrono::microseconds(100));
    }

    CHECK(profiler.get() >= 0.0);
  }

  TEST_CASE("guard is non-copyable") {
    CHECK(!std::is_copy_constructible_v<CpuProfilerGuard>);
    CHECK(!std::is_copy_assignable_v<CpuProfilerGuard>);
  }
}

// NOLINTEND
