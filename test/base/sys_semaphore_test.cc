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

#include "./base/sys_semaphore.h"

#ifdef __linux__

#include <doctest/doctest.h>
#include <unistd.h>

#include <atomic>
#include <string>
#include <thread>
#define VLINK_GETPID() ::getpid()

//
#include "../common_test.h"

// Build a unique semaphore name using the current PID to avoid cross-run
// collisions left over by a previous crashed test process.
static std::string make_sem_name(const char* tag) {
  return std::string("/vlink_test_") + tag + "_" + std::to_string(VLINK_GETPID());
}

// ---------------------------------------------------------------------------
// Helpers: ensure semaphore is cleaned up even when a CHECK fires
// ---------------------------------------------------------------------------

struct SemGuard {
  SysSemaphore& sem;

  ~SemGuard() {
    if (sem.is_attached()) {
      sem.detach(true);
    }
  }
};

// ---------------------------------------------------------------------------
// TEST SUITE
// ---------------------------------------------------------------------------

TEST_SUITE("base-SysSemaphore") {
  // ---------------------------------------------------------------------------
  // TEST: initial state before attach
  // ---------------------------------------------------------------------------

  TEST_CASE("initial-state") {
    SysSemaphore sem;

    CHECK_FALSE(sem.is_attached());
    CHECK(sem.get_count() == 0);
  }

  // ---------------------------------------------------------------------------
  // TEST: attach creates a new semaphore and is_attached returns true
  // ---------------------------------------------------------------------------

  TEST_CASE("attach-detach") {
    const std::string name = make_sem_name("attach");
    SysSemaphore sem(0);
    SemGuard guard{sem};

    bool ok = sem.attach(name);
    CHECK(ok);
    CHECK(sem.is_attached());

    bool d = sem.detach(true);
    CHECK(d);
    CHECK_FALSE(sem.is_attached());
  }

  // ---------------------------------------------------------------------------
  // TEST: detach without attach returns false
  // ---------------------------------------------------------------------------

  TEST_CASE("detach-not-attached") {
    SysSemaphore sem;

    bool d = sem.detach(true);
    CHECK_FALSE(d);
  }

  // ---------------------------------------------------------------------------
  // TEST: basic release/acquire round-trip (count starts at 0, release then acquire)
  // ---------------------------------------------------------------------------

  TEST_CASE("release-acquire") {
    const std::string name = make_sem_name("rel_acq");
    SysSemaphore sem(0);
    SemGuard guard{sem};

    REQUIRE(sem.attach(name));

    // Count is 0 — a non-blocking acquire should fail immediately
    bool non_blocking = sem.acquire(1, 0);
    CHECK_FALSE(non_blocking);

    // Release one permit
    sem.release(1);
    CHECK(sem.get_count() >= 1);

    // Now acquire should succeed without blocking
    bool ok = sem.acquire(1, 0);
    CHECK(ok);
  }

  // ---------------------------------------------------------------------------
  // TEST: release multiple permits at once
  // ---------------------------------------------------------------------------

  TEST_CASE("release-multiple") {
    const std::string name = make_sem_name("rel_multi");
    SysSemaphore sem(0);
    SemGuard guard{sem};

    REQUIRE(sem.attach(name));

    sem.release(3);
    CHECK(sem.get_count() == 3);

    CHECK(sem.acquire(1, 0));
    CHECK(sem.acquire(1, 0));
    CHECK(sem.acquire(1, 0));

    // All permits consumed — next acquire should time-out immediately
    CHECK_FALSE(sem.acquire(1, 0));
  }

  // ---------------------------------------------------------------------------
  // TEST: initial count > 0 is honoured when creating a new semaphore
  // ---------------------------------------------------------------------------

  TEST_CASE("initial-count") {
    const std::string name = make_sem_name("init_cnt");
    SysSemaphore sem(2);
    SemGuard guard{sem};

    REQUIRE(sem.attach(name));
    CHECK(sem.get_count() == 2);

    CHECK(sem.acquire(1, 0));
    CHECK(sem.acquire(1, 0));
    CHECK_FALSE(sem.acquire(1, 0));
  }

  // ---------------------------------------------------------------------------
  // TEST: timeout — acquire blocks for at most timeout_ms ms when count == 0
  // ---------------------------------------------------------------------------

  TEST_CASE("timeout") {
    const std::string name = make_sem_name("timeout");
    SysSemaphore sem(0);
    SemGuard guard{sem};

    REQUIRE(sem.attach(name));

    auto start = std::chrono::steady_clock::now();
    bool ok = sem.acquire(1, 100);  // 100 ms timeout
    auto elapsed = std::chrono::steady_clock::now() - start;

    CHECK_FALSE(ok);
    // Must have waited at least 50 ms (give generous lower bound for slow CI)
    CHECK(elapsed >= 50ms);
    // Must not have waited more than 2 s (semaphore should not hang)
    CHECK(elapsed < 2000ms);
  }

  // ---------------------------------------------------------------------------
  // TEST: cross-thread signalling — producer thread releases, consumer acquires
  // ---------------------------------------------------------------------------

  TEST_CASE("cross-thread") {
    const std::string name = make_sem_name("xthread");
    SysSemaphore sem(0);
    SemGuard guard{sem};

    REQUIRE(sem.attach(name));

    std::atomic<bool> produced{false};
    std::atomic<bool> consumed{false};

    std::thread producer([&]() {
      std::this_thread::sleep_for(50ms);
      produced = true;
      sem.release(1);
    });

    // Wait up to 3 s for the producer to signal
    bool ok = sem.acquire(1, 3000);
    consumed = true;

    producer.join();

    CHECK(ok);
    CHECK(produced.load());
    CHECK(consumed.load());
  }

  // ---------------------------------------------------------------------------
  // TEST: destructor silently detaches (no crash / no assert)
  // ---------------------------------------------------------------------------

  TEST_CASE("destructor-detaches") {
    const std::string name = make_sem_name("dtor");

    {
      SysSemaphore sem(0);
      REQUIRE(sem.attach(name));
      // sem goes out of scope here — destructor must detach cleanly
    }

    // Destructor calls detach(false), so the named semaphore remains until explicit cleanup.
    SysSemaphore sem2(1);
    bool ok = sem2.attach(name);
    CHECK(ok);
    if (ok) {
      CHECK(sem2.get_count() == 0);
      sem2.detach(true);
    }
  }

  // ---------------------------------------------------------------------------
  // TEST: kInfinite sentinel value is negative (documented contract)
  // ---------------------------------------------------------------------------

  TEST_CASE("infinite-sentinel") { CHECK(SysSemaphore::kInfinite == -1); }

  // ---------------------------------------------------------------------------
  // TEST: acquire(n>1) blocks until n permits are available
  // ---------------------------------------------------------------------------

  TEST_CASE("acquire-multi") {
    const std::string name = make_sem_name("multi_acq");
    SysSemaphore sem(0);
    SemGuard guard{sem};

    REQUIRE(sem.attach(name));

    // Release 3 permits, then acquire 3 at once
    sem.release(3);
    CHECK(sem.get_count() == 3);

    bool ok = sem.acquire(3, 500);
    CHECK(ok);
    CHECK(sem.get_count() == 0);
  }

  // ---------------------------------------------------------------------------
  // TEST: acquire(n>available) rolls back permits acquired before timeout
  // ---------------------------------------------------------------------------

  TEST_CASE("acquire-multi-timeout-rolls-back") {
    const std::string name = make_sem_name("multi_rollback");
    SysSemaphore sem(0);
    SemGuard guard{sem};

    REQUIRE(sem.attach(name));
    sem.release(1);
    CHECK(sem.get_count() == 1);

    CHECK_FALSE(sem.acquire(2, 50));
    CHECK(sem.get_count() == 1);
  }

  // ---------------------------------------------------------------------------
  // TEST: release(0) does not change count
  // ---------------------------------------------------------------------------

  TEST_CASE("release-zero") {
    const std::string name = make_sem_name("rel_zero");
    SysSemaphore sem(2);
    SemGuard guard{sem};

    REQUIRE(sem.attach(name));
    CHECK(sem.get_count() == 2);

    sem.release(0);
    CHECK(sem.get_count() == 2);
  }

  // ---------------------------------------------------------------------------
  // TEST: get_count returns 0 before attach
  // ---------------------------------------------------------------------------

  TEST_CASE("get-count-unattached") {
    SysSemaphore sem(5);
    // Not attached yet; get_count should return 0
    CHECK(sem.get_count() == 0);
  }

  // ---------------------------------------------------------------------------
  // TEST: detach(force=false) closes handle but leaves semaphore in namespace
  // ---------------------------------------------------------------------------

  TEST_CASE("detach-no-force") {
    const std::string name = make_sem_name("det_nof");
    SysSemaphore sem(1);

    REQUIRE(sem.attach(name));
    CHECK(sem.is_attached());

    // Detach without removing from namespace
    bool d = sem.detach(false);
    CHECK(d);
    CHECK_FALSE(sem.is_attached());

    // The semaphore should still exist; re-attach should succeed
    SysSemaphore sem2(0);
    bool ok = sem2.attach(name);
    CHECK(ok);

    if (ok) {
      CHECK(sem2.get_count() == 1);
      sem2.detach(true);  // Cleanup
    }
  }
}

#endif

// NOLINTEND
