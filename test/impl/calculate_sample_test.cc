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

#include "./impl/calculate_sample.h"

#include <doctest/doctest.h>

#include <cstdint>
#include <thread>
#include <vector>

//
#include "../common_test.h"

// ---------------------------------------------------------------------------
// TEST SUITE: CalculateSample - initial state
// ---------------------------------------------------------------------------

TEST_SUITE("impl-CalculateSample - initial state") {
  TEST_CASE("default constructed: get_total returns 0") {
    CalculateSample cs;
    CHECK(cs.get_total() == 0);
  }

  TEST_CASE("default constructed: get_lost returns 0") {
    CalculateSample cs;
    CHECK(cs.get_lost() == 0);
  }
}

// ---------------------------------------------------------------------------
// TEST SUITE: CalculateSample - single sender (guid = 0)
// ---------------------------------------------------------------------------

TEST_SUITE("impl-CalculateSample - single sender") {
  TEST_CASE("first message initialises state, no loss") {
    CalculateSample cs;
    cs.update(1, 0);

    // first message: expected - first = 1 - 1 = 0 samples expected yet (since expected = seq+1 = 2, first = 1)
    // Actually total = expected - first = 2 - 1 = 1
    CHECK(cs.get_total() == 1);
    CHECK(cs.get_lost() == 0);
  }

  TEST_CASE("consecutive messages, no gap") {
    CalculateSample cs;
    cs.update(1, 0);
    cs.update(2, 0);
    cs.update(3, 0);

    CHECK(cs.get_total() == 3);
    CHECK(cs.get_lost() == 0);
  }

  TEST_CASE("single gap of 1 detected") {
    CalculateSample cs;
    cs.update(1, 0);
    cs.update(3, 0);  // gap: seq 2 missing

    CHECK(cs.get_total() == 3);
    CHECK(cs.get_lost() == 1);
  }

  TEST_CASE("single gap of 5 detected") {
    CalculateSample cs;
    cs.update(1, 0);
    cs.update(7, 0);  // gap: seq 2,3,4,5,6 missing => lost += 5

    CHECK(cs.get_lost() == 5);
  }

  TEST_CASE("multiple gaps accumulate") {
    CalculateSample cs;
    cs.update(1, 0);
    cs.update(3, 0);   // lost += 1
    cs.update(6, 0);   // lost += 2
    cs.update(10, 0);  // lost += 3

    CHECK(cs.get_lost() == 6);
  }

  TEST_CASE("no loss when seq is exactly expected") {
    CalculateSample cs;
    cs.update(10, 0);
    cs.update(11, 0);
    cs.update(12, 0);

    CHECK(cs.get_lost() == 0);
    CHECK(cs.get_total() == 3);
  }

  TEST_CASE("update with default guid argument") {
    CalculateSample cs;
    cs.update(5);  // guid defaults to 0
    cs.update(6);
    cs.update(7);

    CHECK(cs.get_total() == 3);
    CHECK(cs.get_lost() == 0);
  }

  TEST_CASE("second call with seq 0 treated as reset, no loss") {
    CalculateSample cs;
    cs.update(100, 0);
    // seq 0: gap would be (0 - 101) which wraps to a huge number => treated as reset
    cs.update(0, 0);

    // After reset, total = (0+1) - 0 = 1 for new entry, but original is also counted
    // The important thing: lost should NOT be huge
    CHECK(cs.get_lost() == 0);
  }
}

// ---------------------------------------------------------------------------
// TEST SUITE: CalculateSample - multiple senders (distinct guids)
// ---------------------------------------------------------------------------

TEST_SUITE("impl-CalculateSample - multiple senders") {
  TEST_CASE("two senders tracked independently") {
    CalculateSample cs;

    cs.update(1, 100);
    cs.update(2, 100);
    cs.update(1, 200);
    cs.update(3, 200);  // gap for guid 200: lost += 1

    CHECK(cs.get_lost() == 1);
    // guid100: expected=3, first=1 => contrib 2
    // guid200: expected=4, first=1 => contrib 3
    CHECK(cs.get_total() == 5);
  }

  TEST_CASE("three senders, each with gaps") {
    CalculateSample cs;

    cs.update(1, 1);
    cs.update(3, 1);  // lost 1 from guid 1

    cs.update(1, 2);
    cs.update(4, 2);  // lost 2 from guid 2

    cs.update(1, 3);
    cs.update(6, 3);  // lost 4 from guid 3

    CHECK(cs.get_lost() == 7);
  }

  TEST_CASE("senders do not interfere with each other's sequence") {
    CalculateSample cs;

    // guid 1: clean sequence
    cs.update(1, 1);
    cs.update(2, 1);
    cs.update(3, 1);

    // guid 2: gap in middle
    cs.update(10, 2);
    cs.update(15, 2);  // gap: 4 lost

    CHECK(cs.get_lost() == 4);
    // guid1: seq 1,2,3 => first=1, expected=4 => contrib 3
    // guid2: seq 10, 15 => first=10, expected=16, gap=4 => contrib 6
    CHECK(cs.get_total() == 9);
  }

  TEST_CASE("get_total sums all GUIDs") {
    CalculateSample cs;

    cs.update(1, 10);
    cs.update(2, 10);

    cs.update(1, 20);
    cs.update(2, 20);

    // total = (3 - 1) + (3 - 1) = 4
    CHECK(cs.get_total() == 4);
    CHECK(cs.get_lost() == 0);
  }
}

// ---------------------------------------------------------------------------
// TEST SUITE: CalculateSample - thread safety
// ---------------------------------------------------------------------------

TEST_SUITE("impl-CalculateSample - thread safety") {
  TEST_CASE("concurrent updates from multiple threads do not crash") {
    CalculateSample cs;
    static constexpr int kThreads = 4;
    static constexpr int kUpdates = 100;

    std::vector<std::thread> threads;
    threads.reserve(kThreads);

    for (int t = 0; t < kThreads; ++t) {
      threads.emplace_back([&cs, t]() {
        auto guid = static_cast<uint64_t>(t + 1);

        for (int i = 0; i < kUpdates; ++i) {
          cs.update(static_cast<uint64_t>(i + 1), guid);
        }
      });
    }

    for (auto& th : threads) {
      th.join();
    }

    // Lost should be 0 since each thread sends consecutive sequences
    CHECK(cs.get_lost() == 0);
    // Total = kThreads * kUpdates
    CHECK(cs.get_total() == static_cast<uint64_t>(kThreads * kUpdates));
  }

  TEST_CASE("concurrent reads and writes do not crash") {
    CalculateSample cs;
    std::thread writer([&cs]() {
      for (int i = 1; i <= 50; ++i) {
        cs.update(static_cast<uint64_t>(i), 0);
      }
    });

    std::thread reader([&cs]() {
      for (int i = 0; i < 50; ++i) {
        (void)cs.get_total();
        (void)cs.get_lost();
      }
    });

    writer.join();
    reader.join();
    // No assertion other than no crash/deadlock
    CHECK(true);
  }
}

// NOLINTEND
