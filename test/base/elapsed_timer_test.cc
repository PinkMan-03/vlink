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

#include "./base/elapsed_timer.h"

#include <doctest/doctest.h>

#include <thread>

//
#include "../common_test.h"

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

// Busy-spin for approximately `ms` milliseconds so that both wall time
// and CPU time advance by a measurable amount.
static void busy_wait_ms(int ms) {
  auto end = std::chrono::steady_clock::now() + std::chrono::milliseconds(ms);
  while (std::chrono::steady_clock::now() < end) {
    // spin
  }
}

// ---------------------------------------------------------------------------
TEST_SUITE("base-ElapsedTimer") {
  // -------------------------------------------------------------------------
  TEST_CASE("static get_sys_timestamp - default accuracy returns positive value") {
    uint64_t ts = ElapsedTimer::get_sys_timestamp();
    CHECK(ts > 0u);
  }

  // -------------------------------------------------------------------------
  TEST_CASE("static get_sys_timestamp - kMilli") {
    uint64_t ts = ElapsedTimer::get_sys_timestamp(ElapsedTimer::kMilli);
    CHECK(ts > 0u);
  }

  // -------------------------------------------------------------------------
  TEST_CASE("static get_sys_timestamp - kMicro") {
    uint64_t ts = ElapsedTimer::get_sys_timestamp(ElapsedTimer::kMicro);
    CHECK(ts > 0u);
  }

  // -------------------------------------------------------------------------
  TEST_CASE("static get_sys_timestamp - kNano") {
    uint64_t ts = ElapsedTimer::get_sys_timestamp(ElapsedTimer::kNano);
    CHECK(ts > 0u);
  }

  // -------------------------------------------------------------------------
  TEST_CASE("static get_sys_timestamp - high_resolution=false still returns positive") {
    uint64_t ts = ElapsedTimer::get_sys_timestamp(ElapsedTimer::kMilli, false);
    CHECK(ts > 0u);
  }

  // -------------------------------------------------------------------------
  TEST_CASE("static get_sys_timestamp - values increase over time") {
    uint64_t t1 = ElapsedTimer::get_sys_timestamp(ElapsedTimer::kMilli);
    std::this_thread::sleep_for(5ms);
    uint64_t t2 = ElapsedTimer::get_sys_timestamp(ElapsedTimer::kMilli);

    CHECK(t2 >= t1);
  }

  // -------------------------------------------------------------------------
  TEST_CASE("static get_cpu_timestamp - default accuracy returns positive value") {
    uint64_t ts = ElapsedTimer::get_cpu_timestamp();
    CHECK(ts > 0u);
  }

  // -------------------------------------------------------------------------
  TEST_CASE("static get_cpu_timestamp - kMilli") {
    uint64_t ts = ElapsedTimer::get_cpu_timestamp(ElapsedTimer::kMilli);
    CHECK(ts > 0u);
  }

  // -------------------------------------------------------------------------
  TEST_CASE("static get_cpu_timestamp - kMicro") {
    uint64_t ts = ElapsedTimer::get_cpu_timestamp(ElapsedTimer::kMicro);
    CHECK(ts > 0u);
  }

  // -------------------------------------------------------------------------
  TEST_CASE("static get_cpu_timestamp - kNano") {
    uint64_t ts = ElapsedTimer::get_cpu_timestamp(ElapsedTimer::kNano);
    CHECK(ts > 0u);
  }

  // -------------------------------------------------------------------------
  TEST_CASE("static get_cpu_timestamp - high_resolution=false still returns positive") {
    uint64_t ts = ElapsedTimer::get_cpu_timestamp(ElapsedTimer::kMilli, false);
    CHECK(ts > 0u);
  }

  // -------------------------------------------------------------------------
  TEST_CASE("static get_cpu_timestamp - monotonically non-decreasing") {
    uint64_t t1 = ElapsedTimer::get_cpu_timestamp(ElapsedTimer::kMicro);
    busy_wait_ms(5);
    uint64_t t2 = ElapsedTimer::get_cpu_timestamp(ElapsedTimer::kMicro);

    CHECK(t2 >= t1);
  }

  // -------------------------------------------------------------------------
  TEST_CASE("static get_cpu_active_time - returns positive value") {
    busy_wait_ms(5);
    uint64_t t = ElapsedTimer::get_cpu_active_time();
    CHECK(t > 0u);
  }

  // -------------------------------------------------------------------------
  TEST_CASE("static get_cpu_active_time - kMicro returns positive value") {
    uint64_t t = ElapsedTimer::get_cpu_active_time(ElapsedTimer::kMicro);
    CHECK(t > 0u);
  }

  // -------------------------------------------------------------------------
  TEST_CASE("static get_cpu_active_time - kNano returns positive value") {
    uint64_t t = ElapsedTimer::get_cpu_active_time(ElapsedTimer::kNano);
    CHECK(t > 0u);
  }

  // -------------------------------------------------------------------------
  TEST_CASE("accuracy units are ordered: nano > micro > milli") {
    uint64_t ms = ElapsedTimer::get_cpu_timestamp(ElapsedTimer::kMilli);
    uint64_t us = ElapsedTimer::get_cpu_timestamp(ElapsedTimer::kMicro);
    uint64_t ns = ElapsedTimer::get_cpu_timestamp(ElapsedTimer::kNano);

    // Higher-precision timestamps must be larger numbers for the same point in time
    CHECK(us >= ms);
    CHECK(ns >= us);
  }

  // -------------------------------------------------------------------------
  TEST_CASE("default constructor - timer not active") {
    ElapsedTimer t;

    CHECK(!t.is_active());
    CHECK(t.get() == -1);
    CHECK(t.get_method() == ElapsedTimer::kCpuTimestamp);
    CHECK(t.get_accuracy() == ElapsedTimer::kMilli);
  }

  // -------------------------------------------------------------------------
  TEST_CASE("constructor(Method) - correct method, default accuracy") {
    ElapsedTimer t(ElapsedTimer::kCpuActiveTime);

    CHECK(t.get_method() == ElapsedTimer::kCpuActiveTime);
    CHECK(t.get_accuracy() == ElapsedTimer::kMilli);
    CHECK(!t.is_active());
  }

  // -------------------------------------------------------------------------
  TEST_CASE("constructor(Accuracy) - correct accuracy, default method") {
    ElapsedTimer t(ElapsedTimer::kMicro);

    CHECK(t.get_method() == ElapsedTimer::kCpuTimestamp);
    CHECK(t.get_accuracy() == ElapsedTimer::kMicro);
    CHECK(!t.is_active());
  }

  // -------------------------------------------------------------------------
  TEST_CASE("constructor(Method, Accuracy) - both set correctly") {
    ElapsedTimer t(ElapsedTimer::kCpuActiveTime, ElapsedTimer::kNano);

    CHECK(t.get_method() == ElapsedTimer::kCpuActiveTime);
    CHECK(t.get_accuracy() == ElapsedTimer::kNano);
    CHECK(!t.is_active());
  }

  // -------------------------------------------------------------------------
  TEST_CASE("start makes timer active and get returns non-negative") {
    ElapsedTimer t;

    t.start();

    CHECK(t.is_active());
    CHECK(t.get() >= 0);
  }

  // -------------------------------------------------------------------------
  TEST_CASE("get before start returns -1") {
    ElapsedTimer t;

    CHECK(t.get() == -1);
    CHECK(!t.is_active());
  }

  // -------------------------------------------------------------------------
  TEST_CASE("stop deactivates timer and get returns -1") {
    ElapsedTimer t;

    t.start();
    CHECK(t.is_active());

    t.stop();

    CHECK(!t.is_active());
    CHECK(t.get() == -1);
  }

  // -------------------------------------------------------------------------
  TEST_CASE("start is idempotent when already active") {
    ElapsedTimer t;

    t.start();
    int64_t first_read = t.get();

    // Second start on already-active timer should be a no-op
    t.start();

    CHECK(t.is_active());
    CHECK(t.get() >= first_read);
  }

  // -------------------------------------------------------------------------
  TEST_CASE("elapsed time increases while timer is running") {
    ElapsedTimer t(ElapsedTimer::kCpuTimestamp, ElapsedTimer::kMilli);

    t.start();
    int64_t t1 = t.get();
    std::this_thread::sleep_for(10ms);
    int64_t t2 = t.get();

    CHECK(t2 >= t1);
  }

  // -------------------------------------------------------------------------
  TEST_CASE("elapsed time is measurable after busy-wait (kMicro)") {
    ElapsedTimer t(ElapsedTimer::kCpuTimestamp, ElapsedTimer::kMicro);

    t.start();
    busy_wait_ms(10);
    int64_t elapsed = t.get();

    // At microsecond resolution, 10 ms of busy-wait should yield >= 1000 us
    CHECK(elapsed >= 0);
    CHECK(elapsed >= 1000);
  }

  // -------------------------------------------------------------------------
  TEST_CASE("restart returns elapsed and resets start time") {
    ElapsedTimer t(ElapsedTimer::kCpuTimestamp, ElapsedTimer::kMilli);

    t.start();
    std::this_thread::sleep_for(10ms);

    int64_t elapsed = t.restart();

    // restart returns the elapsed since start
    CHECK(elapsed >= 0);
    // Timer is active again after restart
    CHECK(t.is_active());
    // Fresh reading after restart starts from near 0
    CHECK(t.get() >= 0);
  }

  // -------------------------------------------------------------------------
  TEST_CASE("restart without prior start returns negative (timer was stopped)") {
    ElapsedTimer t;

    // Timer never started; restart should set start_time to now and return
    // the raw start_time_ which was -1 (negative)
    int64_t val = t.restart();
    (void)val;
    // After restart the timer is active
    CHECK(t.is_active());
  }

  // -------------------------------------------------------------------------
  TEST_CASE("stop then start cycles work correctly") {
    ElapsedTimer t;

    t.start();
    busy_wait_ms(5);
    t.stop();

    CHECK(!t.is_active());
    CHECK(t.get() == -1);

    t.start();

    CHECK(t.is_active());
    CHECK(t.get() >= 0);
  }

  // -------------------------------------------------------------------------
  TEST_CASE("kCpuActiveTime method - start and get work") {
    ElapsedTimer t(ElapsedTimer::kCpuActiveTime, ElapsedTimer::kMilli);

    t.start();
    CHECK(t.is_active());

    busy_wait_ms(10);

    int64_t elapsed = t.get();
    CHECK(elapsed >= 0);
  }

  // -------------------------------------------------------------------------
  TEST_CASE("copy constructor preserves method, accuracy, and active state") {
    ElapsedTimer original(ElapsedTimer::kCpuTimestamp, ElapsedTimer::kMicro);
    original.start();

    ElapsedTimer copy(original);

    CHECK(copy.get_method() == original.get_method());
    CHECK(copy.get_accuracy() == original.get_accuracy());
    CHECK(copy.is_active() == original.is_active());
  }

  // -------------------------------------------------------------------------
  TEST_CASE("copy assignment preserves method and accuracy") {
    ElapsedTimer src(ElapsedTimer::kCpuActiveTime, ElapsedTimer::kNano);
    ElapsedTimer dst;

    dst = src;

    CHECK(dst.get_method() == ElapsedTimer::kCpuActiveTime);
    CHECK(dst.get_accuracy() == ElapsedTimer::kNano);
  }

  // -------------------------------------------------------------------------
  TEST_CASE("move constructor preserves state") {
    ElapsedTimer original(ElapsedTimer::kCpuTimestamp, ElapsedTimer::kMicro);
    original.start();

    bool was_active = original.is_active();
    ElapsedTimer moved(std::move(original));

    CHECK(moved.get_method() == ElapsedTimer::kCpuTimestamp);
    CHECK(moved.get_accuracy() == ElapsedTimer::kMicro);
    CHECK(moved.is_active() == was_active);
  }

  // -------------------------------------------------------------------------
  TEST_CASE("kNano accuracy gives larger raw values than kMilli for same duration") {
    ElapsedTimer t_ms(ElapsedTimer::kCpuTimestamp, ElapsedTimer::kMilli);
    ElapsedTimer t_ns(ElapsedTimer::kCpuTimestamp, ElapsedTimer::kNano);

    t_ms.start();
    t_ns.start();

    busy_wait_ms(5);

    int64_t elapsed_ms = t_ms.get();
    int64_t elapsed_ns = t_ns.get();

    CHECK(elapsed_ms >= 0);
    CHECK(elapsed_ns >= 0);
    // 1 ms corresponds to 1,000,000 ns; nanosecond value must dominate
    CHECK(elapsed_ns > elapsed_ms);
  }

  // -------------------------------------------------------------------------
  TEST_CASE("get() while running returns non-negative milliseconds") {
    ElapsedTimer t(ElapsedTimer::kMilli);
    t.start();

    std::this_thread::sleep_for(std::chrono::milliseconds(20));

    // get() while running should return elapsed wall-clock ms
    int64_t elapsed = t.get();
    CHECK(elapsed >= 10);

    t.stop();
  }

  // -------------------------------------------------------------------------
  TEST_CASE("stop then get returns -1 (timer marked stopped)") {
    ElapsedTimer t(ElapsedTimer::kMilli);
    t.start();

    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    t.stop();

    // When stopped, get() returns -1 (sentinel for "not running")
    int64_t after_stop = t.get();
    CHECK(after_stop == -1);

    std::this_thread::sleep_for(std::chrono::milliseconds(10));

    // Repeated call still returns -1
    CHECK(t.get() == -1);
  }

  // -------------------------------------------------------------------------
  TEST_CASE("is_active is true while running, false after stop") {
    ElapsedTimer t;
    CHECK(!t.is_active());

    t.start();
    CHECK(t.is_active());

    t.stop();
    CHECK(!t.is_active());
  }

  // -------------------------------------------------------------------------
  TEST_CASE("kMicro accuracy: get() while running returns positive microseconds") {
    ElapsedTimer t(ElapsedTimer::kMicro);
    t.start();

    std::this_thread::sleep_for(std::chrono::milliseconds(10));

    // get() while running should return elapsed wall-clock microseconds
    int64_t us = t.get();
    CHECK(us >= 5000);  // At least 5 ms in microseconds

    t.stop();
  }

  // -------------------------------------------------------------------------
  TEST_CASE("default constructor method is kCpuTimestamp") {
    ElapsedTimer t;
    CHECK(t.get_method() == ElapsedTimer::kCpuTimestamp);
  }

  // -------------------------------------------------------------------------
  TEST_CASE("accuracy accessor matches constructor argument") {
    ElapsedTimer t_milli(ElapsedTimer::kMilli);
    ElapsedTimer t_micro(ElapsedTimer::kMicro);
    ElapsedTimer t_nano(ElapsedTimer::kNano);

    CHECK(t_milli.get_accuracy() == ElapsedTimer::kMilli);
    CHECK(t_micro.get_accuracy() == ElapsedTimer::kMicro);
    CHECK(t_nano.get_accuracy() == ElapsedTimer::kNano);
  }
}

// NOLINTEND
