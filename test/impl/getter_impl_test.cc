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

#include "./impl/getter_impl.h"

#include <doctest/doctest.h>

#include "./base/bytes.h"
#include "./impl/types.h"

//
#include "../common_test.h"

// ---------------------------------------------------------------------------
// Helpers: concrete subclass to test GetterImpl
// ---------------------------------------------------------------------------

namespace {

class TestGetterImpl : public GetterImpl {
 public:
  TestGetterImpl() = default;
  ~TestGetterImpl() override = default;

  void init() override {}
  void deinit() override {}

  bool listen(MsgCallback&& callback) override {
    callback_ = std::move(callback);
    is_listened = true;
    return true;
  }

  void fire_value(const Bytes& data) {
    if (callback_) {
      callback_(data);
    }
  }

 private:
  MsgCallback callback_;
};

}  // namespace

// ---------------------------------------------------------------------------
// TEST SUITE: GetterImpl - construction
// ---------------------------------------------------------------------------

TEST_SUITE("impl-GetterImpl - construction") {
  TEST_CASE("constructor sets kGetter impl_type") {
    TestGetterImpl getter;
    CHECK(getter.impl_type == kGetter);
  }

  TEST_CASE("is_listened defaults to false") {
    TestGetterImpl getter;
    CHECK(getter.is_listened == false);
  }
}

// ---------------------------------------------------------------------------
// TEST SUITE: GetterImpl - listen
// ---------------------------------------------------------------------------

TEST_SUITE("impl-GetterImpl - listen") {
  TEST_CASE("listen sets is_listened") {
    TestGetterImpl getter;

    getter.listen([](const Bytes&) {});

    CHECK(getter.is_listened == true);
  }

  TEST_CASE("listen callback fires on value update") {
    TestGetterImpl getter;

    bool received = false;
    getter.listen([&](const Bytes&) { received = true; });

    Bytes data;
    getter.fire_value(data);

    CHECK(received == true);
  }

  TEST_CASE("listen callback receives correct data") {
    TestGetterImpl getter;

    Bytes received_data;
    getter.listen([&](const Bytes& data) { received_data = data; });

    Bytes msg = {10, 20, 30};
    getter.fire_value(msg);

    CHECK(received_data.size() == 3);
  }
}

// ---------------------------------------------------------------------------
// TEST SUITE: GetterImpl - latency and lost tracking
// ---------------------------------------------------------------------------

TEST_SUITE("impl-GetterImpl - latency and lost") {
  TEST_CASE("set_latency_and_lost_enabled is no-op") {
    TestGetterImpl getter;
    getter.set_latency_and_lost_enabled(true);
    CHECK(getter.is_latency_and_lost_enabled() == false);
  }

  TEST_CASE("is_latency_and_lost_enabled returns false") {
    TestGetterImpl getter;
    CHECK(getter.is_latency_and_lost_enabled() == false);
  }

  TEST_CASE("get_latency returns 0") {
    TestGetterImpl getter;
    CHECK(getter.get_latency() == 0);
  }

  TEST_CASE("get_lost returns zeroed SampleLostInfo") {
    TestGetterImpl getter;
    auto info = getter.get_lost();
    CHECK(info.total == 0);
    CHECK(info.lost == 0);
  }

  TEST_CASE("set_latency_and_lost_enabled false is also no-op") {
    TestGetterImpl getter;
    getter.set_latency_and_lost_enabled(false);
    CHECK(getter.is_latency_and_lost_enabled() == false);
  }
}

// NOLINTEND
