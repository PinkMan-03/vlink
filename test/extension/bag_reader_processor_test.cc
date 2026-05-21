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

#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "../common_test.h"

TEST_SUITE("extension-BagReaderProcessor") {
  TEST_CASE("config default construction yields expected limits") {
    BagReaderProcessor::Config cfg;
    CHECK_EQ(cfg.min_cache_time, 500);
    CHECK_EQ(cfg.max_cache_size, 1024LL * 1024LL * 256);
  }

  TEST_CASE("config fields are mutable") {
    BagReaderProcessor::Config cfg;
    cfg.min_cache_time = 50;
    cfg.max_cache_size = 1024;
    CHECK_EQ(cfg.min_cache_time, 50);
    CHECK_EQ(cfg.max_cache_size, 1024);
  }

  TEST_CASE("default construction and destruction without input is safe") { BagReaderProcessor processor; }

  TEST_CASE("construction with custom config is safe") {
    BagReaderProcessor::Config cfg;
    cfg.min_cache_time = 10;
    cfg.max_cache_size = 1024;
    BagReaderProcessor processor(cfg);
  }

  TEST_CASE("registering output callback before destruction is safe") {
    BagReaderProcessor processor;
    processor.register_output_callback([](int64_t, const std::string&, ActionType, const Bytes&) {});
  }

  TEST_CASE("push without output callback does not crash") {
    BagReaderProcessor::Config cfg;
    cfg.min_cache_time = 10;
    BagReaderProcessor processor(cfg);

    Bytes data = Bytes::create(8u);
    processor.push(1, "intra://x", ActionType::kPublish, data);
    processor.push(2, "intra://y", ActionType::kPublish, data);
  }

  TEST_CASE("processor delivers all messages in ascending timestamp order on destruction") {
    std::vector<int64_t> received;
    std::mutex mtx;

    BagReaderProcessor::Config cfg;
    cfg.min_cache_time = 100;
    auto processor = std::make_unique<BagReaderProcessor>(cfg);

    processor->register_output_callback([&](int64_t ts, const std::string&, ActionType, const Bytes&) {
      std::lock_guard lock(mtx);
      received.push_back(ts);
    });

    Bytes payload = Bytes::create(4u);
    processor->push(1, "intra://a", ActionType::kPublish, payload);
    processor->push(2000, "intra://b", ActionType::kPublish, payload);
    processor->push(5001, "intra://c", ActionType::kPublish, payload);

    processor.reset();

    std::lock_guard lock(mtx);
    REQUIRE_EQ(received.size(), 3u);
    CHECK_EQ(received[0], 1);
    CHECK_EQ(received[1], 2000);
    CHECK_EQ(received[2], 5001);
  }

  TEST_CASE("out-of-order pushes are sorted by timestamp before delivery") {
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

    Bytes payload = Bytes::create(4u);
    processor->push(50'000'001, "intra://c", ActionType::kPublish, payload);
    processor->push(1, "intra://a", ActionType::kPublish, payload);
    processor->push(20'000'000, "intra://b", ActionType::kPublish, payload);
    processor->push(100'000'000, "intra://d", ActionType::kPublish, payload);

    {
      std::unique_lock lock(mtx);
      REQUIRE(cv.wait_for(lock, 2s, [&] { return received.size() >= 3u; }));
      CHECK_EQ(received[0], 1);
      CHECK_EQ(received[1], 20'000'000);
      CHECK_EQ(received[2], 50'000'001);
    }

    processor.reset();
  }

  TEST_CASE("processor flushes all cached messages on destruction after timeout") {
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

    Bytes payload = Bytes::create(4u);
    processor->push(1, "intra://a", ActionType::kPublish, payload);
    processor->push(2000, "intra://b", ActionType::kPublish, payload);
    processor->push(5001, "intra://c", ActionType::kPublish, payload);

    {
      std::unique_lock lock(mtx);
      REQUIRE(cv.wait_for(lock, 2s, [&] { return received.size() >= 3u; }));
      CHECK_EQ(received[0], 1);
      CHECK_EQ(received[1], 2000);
      CHECK_EQ(received[2], 5001);
    }

    processor.reset();
  }
}

// NOLINTEND
