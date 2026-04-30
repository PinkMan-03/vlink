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

#include "./impl/publisher_impl.h"

#include <doctest/doctest.h>

#include <atomic>
#include <chrono>
#include <thread>

#include "./base/bytes.h"
#include "./impl/types.h"

//
#include "../common_test.h"

// ---------------------------------------------------------------------------
// Helpers: concrete subclass to test PublisherImpl (which has pure virtuals)
// ---------------------------------------------------------------------------

namespace {

class TestPublisherImpl : public PublisherImpl {
 public:
  TestPublisherImpl() = default;
  ~TestPublisherImpl() override = default;

  void init() override {}
  void deinit() override {}

  bool has_subscribers() const override { return subscribers_present_.load(); }

  bool write(const Bytes& /*msg_data*/) override {
    ++write_count;
    return true;
  }

  void set_subscribers_present(bool present) { subscribers_present_ = present; }

  int write_count{0};

 private:
  std::atomic_bool subscribers_present_{false};
};

}  // namespace

// ---------------------------------------------------------------------------
// TEST SUITE: PublisherImpl - construction
// ---------------------------------------------------------------------------

TEST_SUITE("impl-PublisherImpl - construction") {
  TEST_CASE("constructor sets kPublisher impl_type") {
    TestPublisherImpl pub;
    CHECK(pub.impl_type == kPublisher);
  }

  TEST_CASE("has_subscribers returns false initially") {
    TestPublisherImpl pub;
    CHECK(pub.has_subscribers() == false);
  }
}

// ---------------------------------------------------------------------------
// TEST SUITE: PublisherImpl - detect_subscribers
// ---------------------------------------------------------------------------

TEST_SUITE("impl-PublisherImpl - detect_subscribers") {
  TEST_CASE("callback is stored and not called when no subscribers") {
    TestPublisherImpl pub;

    bool called = false;
    pub.detect_subscribers([&](bool) { called = true; });

    CHECK(called == false);
  }

  TEST_CASE("callback fires immediately if subscribers already present") {
    TestPublisherImpl pub;
    pub.set_subscribers_present(true);
    pub.update_subscribers();

    bool called = false;
    bool connected_value = false;
    pub.detect_subscribers([&](bool connected) {
      called = true;
      connected_value = connected;
    });

    CHECK(called == true);
    CHECK(connected_value == true);
  }

  TEST_CASE("callback fires when subscribers appear after registration") {
    TestPublisherImpl pub;

    std::atomic_bool called{false};
    std::atomic_bool connected_value{false};
    pub.detect_subscribers([&](bool connected) {
      called = true;
      connected_value = connected;
    });

    CHECK(called == false);

    pub.set_subscribers_present(true);
    pub.update_subscribers();

    CHECK(called == true);
    CHECK(connected_value == true);
  }

  TEST_CASE("callback fires on disconnect") {
    TestPublisherImpl pub;
    pub.set_subscribers_present(true);
    pub.update_subscribers();

    int call_count = 0;
    bool last_connected = false;
    pub.detect_subscribers([&](bool connected) {
      ++call_count;
      last_connected = connected;
    });

    // Initial callback for existing subscribers
    CHECK(call_count == 1);
    CHECK(last_connected == true);

    pub.set_subscribers_present(false);
    pub.update_subscribers();

    CHECK(call_count == 2);
    CHECK(last_connected == false);
  }
}

// ---------------------------------------------------------------------------
// TEST SUITE: PublisherImpl - wait_for_subscribers
// ---------------------------------------------------------------------------

TEST_SUITE("impl-PublisherImpl - wait_for_subscribers") {
  TEST_CASE("returns true immediately if subscribers are present") {
    TestPublisherImpl pub;
    pub.set_subscribers_present(true);

    CHECK(pub.wait_for_subscribers(100ms) == true);
  }

  TEST_CASE("returns false on timeout when no subscribers") {
    TestPublisherImpl pub;

    auto start = std::chrono::steady_clock::now();
    bool result = pub.wait_for_subscribers(50ms);
    auto elapsed = std::chrono::steady_clock::now() - start;

    CHECK(result == false);
    CHECK(elapsed >= 40ms);
  }

  TEST_CASE("returns true when subscriber appears during wait") {
    TestPublisherImpl pub;

    std::thread t([&]() {
      std::this_thread::sleep_for(20ms);
      pub.set_subscribers_present(true);
      pub.update_subscribers();
    });

    bool result = pub.wait_for_subscribers(500ms);
    t.join();

    CHECK(result == true);
  }

  TEST_CASE("interrupt unblocks wait") {
    TestPublisherImpl pub;

    std::thread t([&]() {
      std::this_thread::sleep_for(20ms);
      pub.interrupt();
    });

    bool result = pub.wait_for_subscribers(500ms);
    t.join();

    (void)result;

    CHECK(pub.is_interrupted() == true);
  }
}

// ---------------------------------------------------------------------------
// TEST SUITE: PublisherImpl - update_subscribers
// ---------------------------------------------------------------------------

TEST_SUITE("impl-PublisherImpl - update_subscribers") {
  TEST_CASE("no-op when state has not changed") {
    TestPublisherImpl pub;

    int call_count = 0;
    pub.detect_subscribers([&](bool) { ++call_count; });

    pub.update_subscribers();
    CHECK(call_count == 0);

    pub.update_subscribers();
    CHECK(call_count == 0);
  }

  TEST_CASE("fires callback on state change") {
    TestPublisherImpl pub;

    int call_count = 0;
    pub.detect_subscribers([&](bool) { ++call_count; });

    pub.set_subscribers_present(true);
    pub.update_subscribers();
    CHECK(call_count == 1);

    pub.set_subscribers_present(false);
    pub.update_subscribers();
    CHECK(call_count == 2);
  }

  TEST_CASE("no callback when state unchanged after toggle") {
    TestPublisherImpl pub;

    int call_count = 0;
    pub.detect_subscribers([&](bool) { ++call_count; });

    pub.set_subscribers_present(true);
    pub.update_subscribers();
    CHECK(call_count == 1);

    // State unchanged - no callback
    pub.update_subscribers();
    CHECK(call_count == 1);
  }
}

// ---------------------------------------------------------------------------
// TEST SUITE: PublisherImpl - write (IntraData)
// ---------------------------------------------------------------------------

TEST_SUITE("impl-PublisherImpl - write IntraData") {
  TEST_CASE("write(IntraData) returns false by default") {
    TestPublisherImpl pub;
    IntraData data = std::make_shared<IntraDataType>();
    // Call the base class IntraData overload, not the Bytes overload
    bool result = static_cast<PublisherImpl&>(pub).write(data);
    CHECK(result == false);
  }
}

// ---------------------------------------------------------------------------
// TEST SUITE: PublisherImpl - interrupt
// ---------------------------------------------------------------------------

TEST_SUITE("impl-PublisherImpl - interrupt") {
  TEST_CASE("interrupt sets flag and notifies cv") {
    TestPublisherImpl pub;
    pub.interrupt();
    CHECK(pub.is_interrupted() == true);
  }

  TEST_CASE("interrupt wakes blocked wait_for_subscribers") {
    TestPublisherImpl pub;

    std::atomic_bool wait_done{false};
    std::thread t([&]() {
      pub.wait_for_subscribers(-1ms);
      wait_done = true;
    });

    std::this_thread::sleep_for(20ms);
    pub.interrupt();

    t.join();
    CHECK(wait_done == true);
  }
}

// NOLINTEND
