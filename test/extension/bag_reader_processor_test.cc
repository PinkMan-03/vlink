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

#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
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
    cfg.min_cache_time = 100;
    auto processor = std::make_unique<BagReaderProcessor>(cfg);

    processor->register_output_callback([&](int64_t ts, const std::string& url, ActionType action, const Bytes& data) {
      std::lock_guard lock(mtx);
      received.push_back(Captured{ts, url, action, data.size()});
    });

    Bytes payload = Bytes::create(4U);
    processor->push(1, "intra://a", ActionType::kPublish, payload);
    processor->push(2000, "intra://b", ActionType::kPublish, payload);
    processor->push(5001, "intra://c", ActionType::kPublish, payload);

    processor.reset();

    std::lock_guard lock(mtx);
    REQUIRE(received.size() == 3U);
    CHECK(received[0].ts == 1);
    CHECK(received[1].ts == 2000);
    CHECK(received[2].ts == 5001);
    CHECK(received[0].url == "intra://a");
    CHECK(received[1].url == "intra://b");
    CHECK(received[2].url == "intra://c");
  }

  TEST_CASE("worker keeps out-of-order input sorted by timestamp") {
    std::vector<int64_t> received;
    std::mutex mtx;
    ConditionVariable cv;

    BagReaderProcessor::Config cfg;
    cfg.min_cache_time = 5000;
    auto processor = std::make_unique<BagReaderProcessor>(cfg);

    processor->register_output_callback([&](int64_t ts, const std::string&, ActionType, const Bytes&) {
      std::lock_guard lock(mtx);
      received.push_back(ts);
      cv.notify_all();
    });

    Bytes payload = Bytes::create(4U);
    processor->push(50000001, "intra://c", ActionType::kPublish, payload);
    processor->push(1, "intra://a", ActionType::kPublish, payload);
    processor->push(20000000, "intra://b", ActionType::kPublish, payload);
    processor->push(100000000, "intra://d", ActionType::kPublish, payload);

    {
      std::unique_lock lock(mtx);
      REQUIRE(cv.wait_for(lock, 2s, [&received]() { return received.size() == 3U; }));
      CHECK(received[0] == 1);
      CHECK(received[1] == 20000000);
      CHECK(received[2] == 50000001);
    }

    processor.reset();
  }

  TEST_CASE("worker flushes cached tail after the cache timeout") {
    std::vector<int64_t> received;
    std::mutex mtx;
    ConditionVariable cv;

    BagReaderProcessor::Config cfg;
    cfg.min_cache_time = 1;
    auto processor = std::make_unique<BagReaderProcessor>(cfg);

    processor->register_output_callback([&](int64_t ts, const std::string&, ActionType, const Bytes&) {
      std::lock_guard lock(mtx);
      received.push_back(ts);
      cv.notify_all();
    });

    Bytes payload = Bytes::create(4U);
    processor->push(1, "intra://a", ActionType::kPublish, payload);
    processor->push(2000, "intra://b", ActionType::kPublish, payload);
    processor->push(5001, "intra://c", ActionType::kPublish, payload);

    {
      std::unique_lock lock(mtx);
      REQUIRE(cv.wait_for(lock, 2s, [&received]() { return received.size() == 3U; }));
      CHECK(received[0] == 1);
      CHECK(received[1] == 2000);
      CHECK(received[2] == 5001);
    }

    processor.reset();
  }
}

// NOLINTEND
