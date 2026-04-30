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

// Example: ElapsedTimer - high-resolution timing with configurable clock and precision

#include <vlink/base/elapsed_timer.h>
#include <vlink/base/logger.h>

#include <cmath>
#include <thread>

int main() {
  // ---------------------------------------------------------------
  // 1. Default timer (kCpuTimestamp, kMilli).
  // ---------------------------------------------------------------
  {
    VLOG_I("=== Section 1: Default timer (ms precision) ===");
    vlink::ElapsedTimer timer;
    timer.start();
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    int64_t elapsed = timer.get();
    timer.stop();
    MLOG_I("  Elapsed: {}ms (expected ~100)", elapsed);
    MLOG_I("  is_active after stop: {}", timer.is_active());
  }

  // ---------------------------------------------------------------
  // 2. Microsecond precision timer.
  // ---------------------------------------------------------------
  {
    VLOG_I("=== Section 2: Microsecond precision ===");
    vlink::ElapsedTimer timer(vlink::ElapsedTimer::kCpuTimestamp, vlink::ElapsedTimer::kMicro);
    timer.start();
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    int64_t elapsed = timer.get();
    MLOG_I("  Elapsed: {}us (expected ~50000)", elapsed);
    timer.stop();
  }

  // ---------------------------------------------------------------
  // 3. Nanosecond precision timer.
  // ---------------------------------------------------------------
  {
    VLOG_I("=== Section 3: Nanosecond precision ===");
    vlink::ElapsedTimer timer(vlink::ElapsedTimer::kCpuTimestamp, vlink::ElapsedTimer::kNano);
    timer.start();

    // Perform some CPU work to measure.
    volatile double sum = 0;
    for (int i = 0; i < 100000; ++i) {
      sum += std::sqrt(static_cast<double>(i));
    }

    int64_t elapsed = timer.get();
    MLOG_I("  CPU work elapsed: {}ns", elapsed);
    timer.stop();
  }

  // ---------------------------------------------------------------
  // 4. CPU active time measurement.
  // ---------------------------------------------------------------
  {
    VLOG_I("=== Section 4: CPU active time ===");
    vlink::ElapsedTimer timer(vlink::ElapsedTimer::kCpuActiveTime, vlink::ElapsedTimer::kMicro);
    timer.start();

    // CPU-bound work.
    volatile double sum = 0;
    for (int i = 0; i < 1000000; ++i) {
      sum += std::sqrt(static_cast<double>(i));
    }

    int64_t cpu_time = timer.get();
    MLOG_I("  CPU active time: {}us", cpu_time);
    timer.stop();
  }

  // ---------------------------------------------------------------
  // 5. restart() - atomically reset and get elapsed time.
  // ---------------------------------------------------------------
  {
    VLOG_I("=== Section 5: restart() ===");
    vlink::ElapsedTimer timer(vlink::ElapsedTimer::kCpuTimestamp, vlink::ElapsedTimer::kMilli);
    timer.start();
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    int64_t lap1 = timer.restart();  // Returns elapsed and resets.
    MLOG_I("  Lap 1: {}ms", lap1);

    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    int64_t lap2 = timer.restart();
    MLOG_I("  Lap 2: {}ms", lap2);

    timer.stop();
  }

  // ---------------------------------------------------------------
  // 6. Static timestamp utilities.
  // ---------------------------------------------------------------
  {
    VLOG_I("=== Section 6: Static utilities ===");

    uint64_t sys_ms = vlink::ElapsedTimer::get_sys_timestamp(vlink::ElapsedTimer::kMilli);
    MLOG_I("  System timestamp (ms): {}", sys_ms);

    uint64_t cpu_ms = vlink::ElapsedTimer::get_cpu_timestamp(vlink::ElapsedTimer::kMilli);
    MLOG_I("  CPU monotonic timestamp (ms): {}", cpu_ms);

    uint64_t cpu_active = vlink::ElapsedTimer::get_cpu_active_time(vlink::ElapsedTimer::kMicro);
    MLOG_I("  Process CPU active time (us): {}", cpu_active);

    uint64_t sys_ns = vlink::ElapsedTimer::get_sys_timestamp(vlink::ElapsedTimer::kNano);
    MLOG_I("  System timestamp (ns): {}", sys_ns);
  }

  // ---------------------------------------------------------------
  // 7. Query timer configuration.
  // ---------------------------------------------------------------
  {
    VLOG_I("=== Section 7: Configuration query ===");
    vlink::ElapsedTimer timer(vlink::ElapsedTimer::kCpuActiveTime, vlink::ElapsedTimer::kNano);
    MLOG_I("  Method: {} (0=CpuTimestamp, 1=CpuActiveTime)", static_cast<int>(timer.get_method()));
    MLOG_I("  Accuracy: {} (0=Milli, 1=Micro, 2=Nano)", static_cast<int>(timer.get_accuracy()));
    MLOG_I("  is_active before start: {}", timer.is_active());
    MLOG_I("  get() before start: {}", timer.get());
  }

  VLOG_I("ElapsedTimer example finished.");
  return 0;
}
