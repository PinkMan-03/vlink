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

#include "./extension/bag_reader_processor.h"

#include <doctest/doctest.h>

#include <chrono>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

//
#include "../common_test.h"

// ---------------------------------------------------------------------------
// TEST SUITE: BagReaderProcessor::Config
// ---------------------------------------------------------------------------

TEST_SUITE("extension-BagReaderProcessor::Config") {
  TEST_CASE("default min_cache_time is 500 ms") {
    BagReaderProcessor::Config cfg;
    CHECK(cfg.min_cache_time == 500);
  }

  TEST_CASE("default max_cache_size is 256 MiB") {
    BagReaderProcessor::Config cfg;
    CHECK(cfg.max_cache_size == 1024LL * 1024LL * 256);
  }

  TEST_CASE("Config fields are settable") {
    BagReaderProcessor::Config cfg;
    cfg.min_cache_time = 50;
    cfg.max_cache_size = 1024;
    CHECK(cfg.min_cache_time == 50);
    CHECK(cfg.max_cache_size == 1024);
  }
}

// ---------------------------------------------------------------------------
// TEST SUITE: BagReaderProcessor - lifecycle
// ---------------------------------------------------------------------------

TEST_SUITE("extension-BagReaderProcessor - lifecycle") {
  TEST_CASE("default-construct and destroy without callback or input") {
    BagReaderProcessor processor;
    CHECK(true);
  }

  TEST_CASE("construct with custom Config") {
    BagReaderProcessor::Config cfg;
    cfg.min_cache_time = 10;
    cfg.max_cache_size = 1024;
    BagReaderProcessor processor(cfg);
    CHECK(true);
  }

  TEST_CASE("register_output_callback before destruction is safe") {
    BagReaderProcessor processor;
    processor.register_output_callback([](int64_t, const std::string&, ActionType, const Bytes&) {});
    CHECK(true);
  }

  TEST_CASE("push without an output callback does not crash") {
    BagReaderProcessor::Config cfg;
    cfg.min_cache_time = 10;
    BagReaderProcessor processor(cfg);

    Bytes data = Bytes::create(8U);
    processor.push(1, "intra://x", ActionType::kPublish, data);
    processor.push(2, "intra://x", ActionType::kPublish, data);
    CHECK(true);
  }
}

// ---------------------------------------------------------------------------
// TEST SUITE: BagReaderProcessor - time-ordered output
// ---------------------------------------------------------------------------

TEST_SUITE("extension-BagReaderProcessor - ordering") {
  TEST_CASE("worker delivers ascending-timestamp messages once the cache window elapses") {
    struct Captured {
      int64_t ts;
      std::string url;
      ActionType action;
      size_t size;
    };

    std::vector<Captured> received;
    std::mutex mtx;

    BagReaderProcessor::Config cfg;
    // 1 ms cache window: kept tiny so the test does not slow down CI runs.
    cfg.min_cache_time = 1;
    BagReaderProcessor processor(cfg);

    processor.register_output_callback([&](int64_t ts, const std::string& url, ActionType action, const Bytes& data) {
      std::lock_guard lock(mtx);
      received.push_back(Captured{ts, url, action, data.size()});
    });

    Bytes payload = Bytes::create(4U);
    // Timestamps are in microseconds and pushed in ascending order, as the
    // bag reader pipeline guarantees. on_check() compares back-front against
    // min_cache_time, so back - front (5000us) >= 1ms triggers the flush.
    processor.push(1, "intra://a", ActionType::kPublish, payload);
    processor.push(2000, "intra://b", ActionType::kPublish, payload);
    processor.push(5001, "intra://c", ActionType::kPublish, payload);

    // Poll for the worker thread to deliver all three messages.  The timeout
    // is generous enough to absorb scheduler jitter on a busy CI host.
    auto deadline = std::chrono::steady_clock::now() + 2s;
    while (std::chrono::steady_clock::now() < deadline) {
      {
        std::lock_guard lock(mtx);
        if (received.size() == 3U) {
          break;
        }
      }
      std::this_thread::sleep_for(5ms);
    }

    std::lock_guard lock(mtx);
    REQUIRE(received.size() == 3U);
    CHECK(received[0].ts == 1);
    CHECK(received[1].ts == 2000);
    CHECK(received[2].ts == 5001);
    CHECK(received[0].url == "intra://a");
    CHECK(received[1].url == "intra://b");
    CHECK(received[2].url == "intra://c");
  }
}

// NOLINTEND
