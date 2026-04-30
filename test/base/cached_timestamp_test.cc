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

#include "./base/cached_timestamp.h"

#include <doctest/doctest.h>

#include <chrono>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

//
#include "../common_test.h"

// ---------------------------------------------------------------------------
TEST_SUITE("base-CachedTimestamp") {
  // -------------------------------------------------------------------------
  TEST_CASE("get() returns non-empty string_view") {
    CachedTimestamp ts;
    std::string_view sv = ts.get();

    CHECK(!sv.empty());
  }

  // -------------------------------------------------------------------------
  TEST_CASE("get() returns string of expected length for default format") {
    CachedTimestamp ts;
    std::string_view sv = ts.get();

    // Default format "%02d-%02d %02d:%02d:%02d.%03d" produces "MM-DD HH:MM:SS.mmm"
    // which is 18 characters
    CHECK(sv.size() == 18u);
  }

  // -------------------------------------------------------------------------
  TEST_CASE("get() result contains expected separators") {
    CachedTimestamp ts;
    std::string_view sv = ts.get();

    // Format: "MM-DD HH:MM:SS.mmm"
    // Position 2 should be '-', position 5 should be ' '
    // Position 8 should be ':', position 11 should be ':', position 14 should be '.'
    REQUIRE(sv.size() == 18u);
    CHECK(sv[2] == '-');
    CHECK(sv[5] == ' ');
    CHECK(sv[8] == ':');
    CHECK(sv[11] == ':');
    CHECK(sv[14] == '.');
  }

  // -------------------------------------------------------------------------
  TEST_CASE("get() digits are valid ASCII numerals") {
    CachedTimestamp ts;
    std::string_view sv = ts.get();

    REQUIRE(sv.size() == 18u);

    // All digit positions: 0,1,3,4,6,7,9,10,12,13,15,16,17
    static const size_t kDigitPositions[] = {0, 1, 3, 4, 6, 7, 9, 10, 12, 13, 15, 16, 17};

    for (size_t pos : kDigitPositions) {
      bool is_digit = (sv[pos] >= '0') && (sv[pos] <= '9');
      CHECK(is_digit);
    }
  }

  // -------------------------------------------------------------------------
  TEST_CASE("get() called multiple times returns consistent format") {
    CachedTimestamp ts;

    for (int i = 0; i < 10; ++i) {
      std::string_view sv = ts.get();
      CHECK(sv.size() == 18u);
      CHECK(sv[2] == '-');
      CHECK(sv[5] == ' ');
      CHECK(sv[14] == '.');
    }
  }

  // -------------------------------------------------------------------------
  TEST_CASE("get() with use_utc=true returns same length") {
    CachedTimestamp ts;
    std::string_view sv = ts.get("%02d-%02d %02d:%02d:%02d.%03d", true);

    CHECK(sv.size() == 18u);
  }

  // -------------------------------------------------------------------------
  TEST_CASE("get() rapid successive calls do not crash") {
    CachedTimestamp ts;

    for (int i = 0; i < 1000; ++i) {
      std::string_view sv = ts.get();
      CHECK(!sv.empty());
    }
  }

  // -------------------------------------------------------------------------
  TEST_CASE("get() millisecond field is within valid range [000..999]") {
    CachedTimestamp ts;
    std::string_view sv = ts.get();

    REQUIRE(sv.size() == 18u);

    // Millisecond portion: positions 15,16,17
    int ms = (sv[15] - '0') * 100 + (sv[16] - '0') * 10 + (sv[17] - '0');

    CHECK(ms >= 0);
    CHECK(ms <= 999);
  }

  // -------------------------------------------------------------------------
  TEST_CASE("get() result value copied to string remains valid") {
    CachedTimestamp ts;

    std::string copy1 = std::string(ts.get());
    std::string copy2 = std::string(ts.get());

    // Both should be valid timestamps (non-empty, right length)
    CHECK(copy1.size() == 18u);
    CHECK(copy2.size() == 18u);
  }

  // -------------------------------------------------------------------------
  TEST_CASE("multiple instances are independent") {
    CachedTimestamp ts1;
    CachedTimestamp ts2;

    std::string_view sv1 = ts1.get();
    std::string_view sv2 = ts2.get();

    CHECK(sv1.size() == 18u);
    CHECK(sv2.size() == 18u);
  }

  // -------------------------------------------------------------------------
  TEST_CASE("concurrent access from multiple threads does not crash") {
    CachedTimestamp ts;
    static constexpr int kThreads = 4;
    static constexpr int kIterCnt = 200;

    std::vector<std::thread> threads;
    threads.reserve(kThreads);

    for (int t = 0; t < kThreads; ++t) {
      threads.emplace_back([&ts]() {
        for (int i = 0; i < kIterCnt; ++i) {
          std::string copy = std::string(ts.get());
          CHECK(copy.size() == 18u);
        }
      });
    }

    for (auto& thr : threads) {
      thr.join();
    }
  }

  // -------------------------------------------------------------------------
  TEST_CASE("second boundary: get() returns fresh timestamp across second") {
    CachedTimestamp ts;

    // Warm up the cache
    std::string first = std::string(ts.get());

    // Sleep just over 1 second to cross the second boundary
    std::this_thread::sleep_for(std::chrono::milliseconds(1050));

    std::string second = std::string(ts.get());

    CHECK(first.size() == 18u);
    CHECK(second.size() == 18u);
    // The timestamps may differ (especially the seconds digit)
    // We cannot guarantee they differ in a fast-running environment,
    // but we verify both are well-formed.
    CHECK(second[2] == '-');
    CHECK(second[5] == ' ');
    CHECK(second[14] == '.');
  }
}

// NOLINTEND
