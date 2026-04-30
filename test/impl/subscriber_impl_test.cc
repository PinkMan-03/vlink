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

#include "./impl/subscriber_impl.h"

#include <doctest/doctest.h>

#include "./base/bytes.h"
#include "./impl/types.h"

//
#include "../common_test.h"

// ---------------------------------------------------------------------------
// Helpers: concrete subclass to test SubscriberImpl
// ---------------------------------------------------------------------------

namespace {

class TestSubscriberImpl : public SubscriberImpl {
 public:
  TestSubscriberImpl() = default;
  ~TestSubscriberImpl() override = default;

  void init() override {}
  void deinit() override {}

  using SubscriberImpl::listen;

  bool listen(MsgCallback&& callback) override {
    callback_ = std::move(callback);
    is_listened = true;
    return true;
  }

  void fire_message(const Bytes& data) {
    if (callback_) {
      callback_(data);
    }
  }

 private:
  MsgCallback callback_;
};

}  // namespace

// ---------------------------------------------------------------------------
// TEST SUITE: SubscriberImpl - construction
// ---------------------------------------------------------------------------

TEST_SUITE("impl-SubscriberImpl - construction") {
  TEST_CASE("constructor sets kSubscriber impl_type") {
    TestSubscriberImpl sub;
    CHECK(sub.impl_type == kSubscriber);
  }

  TEST_CASE("is_listened defaults to false") {
    TestSubscriberImpl sub;
    CHECK(sub.is_listened == false);
  }
}

// ---------------------------------------------------------------------------
// TEST SUITE: SubscriberImpl - listen
// ---------------------------------------------------------------------------

TEST_SUITE("impl-SubscriberImpl - listen") {
  TEST_CASE("listen(MsgCallback) sets is_listened") {
    TestSubscriberImpl sub;

    bool received = false;
    sub.listen([&](const Bytes&) { received = true; });

    CHECK(sub.is_listened == true);
  }

  TEST_CASE("listen(MsgCallback) callback fires on message") {
    TestSubscriberImpl sub;

    Bytes received_data;
    sub.listen([&](const Bytes& data) { received_data = data; });

    Bytes msg = {1, 2, 3};
    sub.fire_message(msg);

    CHECK(received_data.size() == 3);
  }

  TEST_CASE("listen(IntraMsgCallback) returns false by default") {
    TestSubscriberImpl sub;

    bool called = false;
    NodeImpl::IntraMsgCallback intra_cb = [&](const IntraData&) { called = true; };
    bool result = sub.listen(std::move(intra_cb));

    CHECK(result == false);
    CHECK(called == false);
  }
}

// ---------------------------------------------------------------------------
// TEST SUITE: SubscriberImpl - latency and lost tracking
// ---------------------------------------------------------------------------

TEST_SUITE("impl-SubscriberImpl - latency and lost") {
  TEST_CASE("set_latency_and_lost_enabled is no-op") {
    TestSubscriberImpl sub;
    sub.set_latency_and_lost_enabled(true);
    CHECK(sub.is_latency_and_lost_enabled() == false);
  }

  TEST_CASE("is_latency_and_lost_enabled returns false") {
    TestSubscriberImpl sub;
    CHECK(sub.is_latency_and_lost_enabled() == false);
  }

  TEST_CASE("get_latency returns 0") {
    TestSubscriberImpl sub;
    CHECK(sub.get_latency() == 0);
  }

  TEST_CASE("get_lost returns zeroed SampleLostInfo") {
    TestSubscriberImpl sub;
    auto info = sub.get_lost();
    CHECK(info.total == 0);
    CHECK(info.lost == 0);
  }
}

// NOLINTEND
