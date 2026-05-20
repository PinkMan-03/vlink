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

// Example: DeadlineTimer - atomic absolute-deadline timer for lock-free timeout detection

#include <vlink/base/deadline_timer.h>
#include <vlink/base/logger.h>

#include <chrono>
#include <string>
#include <thread>

// Demonstrate default construction and validity check.
void demo_default_construction() {
  VLOG_I("=== Default Construction ===");

  vlink::DeadlineTimer timer;
  VLOG_I("  is_valid:    ", timer.is_valid() ? "true" : "false");
  VLOG_I("  has_expired: ", timer.has_expired() ? "true" : "false");
  VLOG_I("  deadline:    ", timer.deadline());
  VLOG_I("  A default timer is invalid (deadline=0), has_expired() returns false.");
}

// Demonstrate relative deadline with set_deadline().
void demo_relative_deadline() {
  VLOG_I("=== Relative Deadline (set_deadline) ===");

  // Create a timer with a 200ms deadline from now.
  vlink::DeadlineTimer timer(200, vlink::ElapsedTimer::kMilli);
  VLOG_I("  Created timer with 200ms deadline");
  VLOG_I("  is_valid: ", timer.is_valid() ? "true" : "false");
  VLOG_I("  has_expired: ", timer.has_expired() ? "true" : "false");
  VLOG_I("  remaining_time: ", timer.remaining_time(), " ms");

  // Wait 50ms -- should still be valid.
  std::this_thread::sleep_for(std::chrono::milliseconds(50));
  VLOG_I("  After 50ms sleep:");
  VLOG_I("    has_expired: ", timer.has_expired() ? "true" : "false");
  VLOG_I("    remaining_time: ", timer.remaining_time(), " ms");

  // Wait until expired.
  std::this_thread::sleep_for(std::chrono::milliseconds(200));
  VLOG_I("  After 250ms total:");
  VLOG_I("    has_expired: ", timer.has_expired() ? "true" : "false");
  VLOG_I("    remaining_time: ", timer.remaining_time(), " ms (0 = expired or invalid)");
}

// Demonstrate absolute deadline with set_deadline_abs().
void demo_absolute_deadline() {
  VLOG_I("=== Absolute Deadline (set_deadline_abs) ===");

  vlink::DeadlineTimer timer;

  // Get current monotonic timestamp in milliseconds.
  uint64_t now_ms = vlink::ElapsedTimer::get_cpu_timestamp(vlink::ElapsedTimer::kMilli);
  uint64_t deadline_ms = now_ms + 150;  // 150ms from now.

  timer.set_deadline_abs(deadline_ms);
  VLOG_I("  Current CPU timestamp: ", now_ms, " ms");
  VLOG_I("  Absolute deadline set: ", deadline_ms, " ms");
  VLOG_I("  remaining_time: ", timer.remaining_time(), " ms");
  VLOG_I("  has_expired: ", timer.has_expired() ? "true" : "false");

  // Wait for expiry.
  std::this_thread::sleep_for(std::chrono::milliseconds(200));
  VLOG_I("  After 200ms sleep:");
  VLOG_I("    has_expired: ", timer.has_expired() ? "true" : "false");
}

// Demonstrate resetting and reusing a timer.
void demo_reset_and_reuse() {
  VLOG_I("=== Reset and Reuse ===");

  vlink::DeadlineTimer timer(100);
  VLOG_I("  Initial: is_valid=", timer.is_valid() ? "true" : "false", " remaining=", timer.remaining_time(), " ms");

  // Reset makes the timer invalid again.
  timer.reset();
  VLOG_I("  After reset: is_valid=", timer.is_valid() ? "true" : "false", " deadline=", timer.deadline());

  // Set a new deadline via set_deadline.
  timer.set_deadline(300);
  VLOG_I("  After set_deadline(300): is_valid=", timer.is_valid() ? "true" : "false",
         " remaining=", timer.remaining_time(), " ms");
}

// Demonstrate microsecond accuracy mode.
void demo_microsecond_accuracy() {
  VLOG_I("=== Microsecond Accuracy ===");

  // 50000 us = 50 ms deadline.
  vlink::DeadlineTimer timer(50000, vlink::ElapsedTimer::kMicro);
  VLOG_I("  Accuracy: ", static_cast<int>(timer.get_accuracy()), " (0=ms, 1=us, 2=ns)");
  VLOG_I("  remaining_time: ", timer.remaining_time(), " us");
  VLOG_I("  has_expired: ", timer.has_expired() ? "true" : "false");

  // Busy-wait for a short period to see the remaining time decrease.
  std::this_thread::sleep_for(std::chrono::milliseconds(20));
  VLOG_I("  After 20ms: remaining_time=", timer.remaining_time(), " us");

  std::this_thread::sleep_for(std::chrono::milliseconds(40));
  VLOG_I("  After 60ms: has_expired=", timer.has_expired() ? "true" : "false");
}

// Demonstrate copy semantics and nanosecond accuracy.
void demo_copy_and_nano() {
  VLOG_I("=== Copy Semantics & Nanosecond Accuracy ===");

  // Nanosecond timer: 10ms = 10000000ns.
  vlink::DeadlineTimer ns_timer(10000000LL, vlink::ElapsedTimer::kNano);
  VLOG_I("  ns timer accuracy: ", static_cast<int>(ns_timer.get_accuracy()), " (2=ns)");
  std::this_thread::sleep_for(std::chrono::milliseconds(15));
  VLOG_I("  After 15ms: expired=", ns_timer.has_expired() ? "true" : "false");

  // Copy constructor and assignment.
  vlink::DeadlineTimer original(500);
  vlink::DeadlineTimer copy1(original);  // NOLINT(performance-unnecessary-copy-initialization)
  vlink::DeadlineTimer copy2;
  copy2 = original;
  VLOG_I("  Original remaining: ", original.remaining_time(), " ms");
  VLOG_I("  Same deadline? ", (copy1.deadline() == copy2.deadline()) ? "yes" : "no");
}

// Demonstrate a practical timeout detection pattern for real-time systems.
void demo_timeout_detection() {
  VLOG_I("=== Practical: Timeout Detection ===");

  // Simulate waiting for a response with a 100ms deadline.
  vlink::DeadlineTimer deadline(100);
  int iteration = 0;
  bool response_received = false;

  while (!deadline.has_expired()) {
    iteration++;
    // Simulate checking for a response.

    if (iteration == 5) {
      response_received = true;
      break;
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }

  if (response_received) {
    VLOG_I("  Response received after ", iteration, " iterations");
    VLOG_I("  Time remaining: ", deadline.remaining_time(), " ms");
  } else {
    VLOG_I("  Timeout! No response after ", iteration, " iterations");
  }
}

// Demonstrate a timed retry pattern.
void demo_retry_with_deadline() {
  VLOG_I("=== Practical: Retry with Deadline ===");

  vlink::DeadlineTimer overall(200);  // 200ms total budget.
  int attempt = 0;

  while (!overall.has_expired()) {
    attempt++;
    bool success = (attempt >= 4);

    if (success) {
      VLOG_I("  Success on attempt ", attempt);
      break;
    }

    VLOG_I("  Attempt ", attempt, " failed, remaining=", overall.remaining_time(), " ms");
    std::this_thread::sleep_for(std::chrono::milliseconds(30));
  }

  if (overall.has_expired()) {
    VLOG_W("  Retries exhausted within deadline");
  }
}

int main() {
  VLOG_I("===================================================");
  VLOG_I("  VLink DeadlineTimer Demo");
  VLOG_I("===================================================");

  demo_default_construction();
  demo_relative_deadline();
  demo_absolute_deadline();
  demo_reset_and_reuse();
  demo_microsecond_accuracy();
  demo_copy_and_nano();
  demo_timeout_detection();
  demo_retry_with_deadline();

  VLOG_I("===================================================");
  VLOG_I("  DeadlineTimer demo completed successfully");
  VLOG_I("===================================================");

  vlink::Logger::flush();
  return 0;
}
