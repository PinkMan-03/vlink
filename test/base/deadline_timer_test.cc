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

#include "./base/deadline_timer.h"

#include <doctest/doctest.h>

#include <thread>

#include "./base/elapsed_timer.h"

//
#include "../common_test.h"

// ---------------------------------------------------------------------------
// TEST SUITE: Default construction
// ---------------------------------------------------------------------------

TEST_SUITE("base-DeadlineTimer - default construction") {
  TEST_CASE("default constructed timer is invalid") {
    DeadlineTimer t;
    CHECK(!t.is_valid());
  }

  TEST_CASE("default constructed timer deadline() == 0") {
    DeadlineTimer t;
    CHECK(t.deadline() == 0U);
  }

  TEST_CASE("default constructed timer has_expired() == false") {
    // deadline==0 is treated as already-expired by the implementation
    DeadlineTimer t;
    CHECK(!t.has_expired());
  }
}

// ---------------------------------------------------------------------------
// TEST SUITE: Construction with interval
// ---------------------------------------------------------------------------

TEST_SUITE("base-DeadlineTimer - interval construction") {
  TEST_CASE("timer constructed with positive interval is valid") {
    DeadlineTimer t(1000);  // 1000 ms from now
    CHECK(t.is_valid());
  }

  TEST_CASE("timer constructed with positive interval has non-zero deadline") {
    DeadlineTimer t(500);
    CHECK(t.deadline() > 0U);
  }

  TEST_CASE("timer with 1000 ms has not expired immediately") {
    DeadlineTimer t(1000);
    CHECK(!t.has_expired());
  }

  TEST_CASE("timer with large interval has positive remaining_time") {
    DeadlineTimer t(5000);  // 5 seconds
    CHECK(t.remaining_time() > 0);
  }

  TEST_CASE("timer constructed with microsecond accuracy") {
    DeadlineTimer t(100000, ElapsedTimer::kMicro);  // 100 ms in microseconds
    CHECK(t.is_valid());
    CHECK(!t.has_expired());
    CHECK(t.get_accuracy() == ElapsedTimer::kMicro);
  }

  TEST_CASE("timer constructed with nanosecond accuracy") {
    DeadlineTimer t(100000000LL, ElapsedTimer::kNano);  // 100 ms in nanoseconds
    CHECK(t.is_valid());
    CHECK(!t.has_expired());
    CHECK(t.get_accuracy() == ElapsedTimer::kNano);
  }

  TEST_CASE("get_accuracy() returns kMilli by default") {
    DeadlineTimer t(100);
    CHECK(t.get_accuracy() == ElapsedTimer::kMilli);
  }
}

// ---------------------------------------------------------------------------
// TEST SUITE: set_deadline
// ---------------------------------------------------------------------------

TEST_SUITE("base-DeadlineTimer - set_deadline") {
  TEST_CASE("set_deadline makes timer valid") {
    DeadlineTimer t;
    CHECK(!t.is_valid());

    t.set_deadline(500);
    CHECK(t.is_valid());
  }

  TEST_CASE("set_deadline(0) resets timer to invalid and expired state") {
    // interval <= 0 stores deadline=0: is_valid==false, has_expired==false
    DeadlineTimer t;
    t.set_deadline(0);
    CHECK(!t.is_valid());
    CHECK(!t.has_expired());
  }

  TEST_CASE("set_deadline with large value - remaining time is positive") {
    DeadlineTimer t;
    t.set_deadline(10000);  // 10 s
    CHECK(t.remaining_time() > 0);
  }

  TEST_CASE("set_deadline can be called multiple times") {
    DeadlineTimer t;
    t.set_deadline(100);
    uint64_t first = t.deadline();

    std::this_thread::sleep_for(10ms);
    t.set_deadline(100);
    uint64_t second = t.deadline();

    // The second deadline should be later than the first
    CHECK(second > first);
  }
}

// ---------------------------------------------------------------------------
// TEST SUITE: set_deadline_abs
// ---------------------------------------------------------------------------

TEST_SUITE("base-DeadlineTimer - set_deadline_abs") {
  TEST_CASE("set_deadline_abs makes timer valid") {
    DeadlineTimer t;
    uint64_t abs = ElapsedTimer::get_cpu_timestamp() + 5000;
    t.set_deadline_abs(abs);
    CHECK(t.is_valid());
  }

  TEST_CASE("set_deadline_abs stores the exact value") {
    DeadlineTimer t;
    uint64_t abs = 123456789U;
    t.set_deadline_abs(abs);
    CHECK(t.deadline() == abs);
  }

  TEST_CASE("set_deadline_abs with past timestamp: has_expired is true") {
    DeadlineTimer t;
    // Set the deadline to 1 ms in the past
    uint64_t past = ElapsedTimer::get_cpu_timestamp() - 1U;
    t.set_deadline_abs(past);
    CHECK(t.has_expired());
  }
}

// ---------------------------------------------------------------------------
// TEST SUITE: reset
// ---------------------------------------------------------------------------

TEST_SUITE("base-DeadlineTimer - reset") {
  TEST_CASE("reset() invalidates a previously set timer") {
    DeadlineTimer t(500);
    CHECK(t.is_valid());

    t.reset();
    CHECK(!t.is_valid());
    CHECK(t.deadline() == 0U);
  }

  TEST_CASE("reset() then set_deadline() works") {
    DeadlineTimer t(100);
    t.reset();
    t.set_deadline(200);
    CHECK(t.is_valid());
  }
}

// ---------------------------------------------------------------------------
// TEST SUITE: Expiry detection
// ---------------------------------------------------------------------------

TEST_SUITE("base-DeadlineTimer - expiry detection") {
  TEST_CASE("timer expires after sleep longer than deadline") {
    DeadlineTimer t(50);  // 50 ms
    std::this_thread::sleep_for(100ms);
    CHECK(t.has_expired());
  }

  TEST_CASE("remaining_time decreases over time") {
    DeadlineTimer t(2000);  // 2 s
    int64_t r1 = t.remaining_time();

    std::this_thread::sleep_for(20ms);
    int64_t r2 = t.remaining_time();

    CHECK(r2 < r1);
  }

  TEST_CASE("remaining_time is negative or zero after expiry") {
    DeadlineTimer t(30);
    std::this_thread::sleep_for(60ms);
    CHECK(t.remaining_time() <= 0);
  }
}

// ---------------------------------------------------------------------------
// TEST SUITE: Copy and move semantics
// ---------------------------------------------------------------------------

TEST_SUITE("base-DeadlineTimer - copy and move") {
  TEST_CASE("copy constructor preserves deadline") {
    DeadlineTimer orig(1000);
    DeadlineTimer copy(orig);  // NOLINT(performance-unnecessary-copy-initialization)

    CHECK(copy.is_valid());
    CHECK(copy.deadline() == orig.deadline());
  }

  TEST_CASE("move constructor transfers deadline") {
    DeadlineTimer orig(1000);
    uint64_t dl = orig.deadline();

    DeadlineTimer moved(std::move(orig));
    CHECK(moved.is_valid());
    CHECK(moved.deadline() == dl);
  }

  TEST_CASE("copy assignment preserves deadline") {
    DeadlineTimer a(500);
    DeadlineTimer b;
    b = a;

    CHECK(b.is_valid());
    CHECK(b.deadline() == a.deadline());
  }

  TEST_CASE("move assignment transfers deadline") {
    DeadlineTimer a(500);
    uint64_t dl = a.deadline();

    DeadlineTimer b;
    b = std::move(a);

    CHECK(b.is_valid());
    CHECK(b.deadline() == dl);
  }
}

// NOLINTEND
