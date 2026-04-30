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

#include "./impl/client_impl.h"

#include <doctest/doctest.h>

#include <atomic>
#include <chrono>
#include <thread>

#include "./base/bytes.h"
#include "./impl/types.h"

//
#include "../common_test.h"

// ---------------------------------------------------------------------------
// Helpers: concrete subclass to test ClientImpl (which has pure virtuals)
// ---------------------------------------------------------------------------

namespace {

class TestClientImpl : public ClientImpl {
 public:
  TestClientImpl() = default;
  ~TestClientImpl() override = default;

  void init() override {}
  void deinit() override {}

  bool is_connected() const override { return connected_.load(); }

  bool call(const Bytes& /*req_data*/, MsgCallback&& /*callback*/, std::chrono::milliseconds /*timeout*/) override {
    return false;
  }

  void set_connected(bool connected) { connected_ = connected; }

 private:
  std::atomic_bool connected_{false};
};

}  // namespace

// ---------------------------------------------------------------------------
// TEST SUITE: ClientImpl - construction
// ---------------------------------------------------------------------------

TEST_SUITE("impl-ClientImpl - construction") {
  TEST_CASE("constructor sets kClient impl_type") {
    TestClientImpl client;
    CHECK(client.impl_type == kClient);
  }

  TEST_CASE("is_connected returns false initially") {
    TestClientImpl client;
    CHECK(client.is_connected() == false);
  }

  TEST_CASE("is_resp_type defaults to false") {
    TestClientImpl client;
    CHECK(client.is_resp_type == false);
  }
}

// ---------------------------------------------------------------------------
// TEST SUITE: ClientImpl - detect_connected
// ---------------------------------------------------------------------------

TEST_SUITE("impl-ClientImpl - detect_connected") {
  TEST_CASE("callback is stored and not called when not connected") {
    TestClientImpl client;

    bool called = false;
    client.detect_connected([&](bool) { called = true; });

    CHECK(called == false);
  }

  TEST_CASE("callback fires immediately if already connected") {
    TestClientImpl client;
    client.set_connected(true);
    client.update_connected();

    bool called = false;
    bool connected_value = false;
    client.detect_connected([&](bool connected) {
      called = true;
      connected_value = connected;
    });

    CHECK(called == true);
    CHECK(connected_value == true);
  }

  TEST_CASE("callback fires when server appears after registration") {
    TestClientImpl client;

    std::atomic_bool called{false};
    std::atomic_bool connected_value{false};
    client.detect_connected([&](bool connected) {
      called = true;
      connected_value = connected;
    });

    CHECK(called == false);

    client.set_connected(true);
    client.update_connected();

    CHECK(called == true);
    CHECK(connected_value == true);
  }

  TEST_CASE("callback fires on disconnect") {
    TestClientImpl client;
    client.set_connected(true);
    client.update_connected();

    int call_count = 0;
    bool last_connected = false;
    client.detect_connected([&](bool connected) {
      ++call_count;
      last_connected = connected;
    });

    CHECK(call_count == 1);
    CHECK(last_connected == true);

    client.set_connected(false);
    client.update_connected();

    CHECK(call_count == 2);
    CHECK(last_connected == false);
  }
}

// ---------------------------------------------------------------------------
// TEST SUITE: ClientImpl - wait_for_connected
// ---------------------------------------------------------------------------

TEST_SUITE("impl-ClientImpl - wait_for_connected") {
  TEST_CASE("returns true immediately if connected") {
    TestClientImpl client;
    client.set_connected(true);

    CHECK(client.wait_for_connected(100ms) == true);
  }

  TEST_CASE("returns false on timeout when not connected") {
    TestClientImpl client;

    auto start = std::chrono::steady_clock::now();
    bool result = client.wait_for_connected(50ms);
    auto elapsed = std::chrono::steady_clock::now() - start;

    CHECK(result == false);
    CHECK(elapsed >= 40ms);
  }

  TEST_CASE("returns true when server appears during wait") {
    TestClientImpl client;

    std::thread t([&]() {
      std::this_thread::sleep_for(20ms);
      client.set_connected(true);
      client.update_connected();
    });

    bool result = client.wait_for_connected(500ms);
    t.join();

    CHECK(result == true);
  }

  TEST_CASE("interrupt unblocks wait") {
    TestClientImpl client;

    std::thread t([&]() {
      std::this_thread::sleep_for(20ms);
      client.interrupt();
    });

    bool result = client.wait_for_connected(500ms);
    t.join();

    (void)result;

    CHECK(client.is_interrupted() == true);
  }
}

// ---------------------------------------------------------------------------
// TEST SUITE: ClientImpl - update_connected
// ---------------------------------------------------------------------------

TEST_SUITE("impl-ClientImpl - update_connected") {
  TEST_CASE("no-op when state has not changed") {
    TestClientImpl client;

    int call_count = 0;
    client.detect_connected([&](bool) { ++call_count; });

    client.update_connected();
    CHECK(call_count == 0);
  }

  TEST_CASE("fires callback on state change") {
    TestClientImpl client;

    int call_count = 0;
    client.detect_connected([&](bool) { ++call_count; });

    client.set_connected(true);
    client.update_connected();
    CHECK(call_count == 1);

    client.set_connected(false);
    client.update_connected();
    CHECK(call_count == 2);
  }
}

// ---------------------------------------------------------------------------
// TEST SUITE: ClientImpl - interrupt
// ---------------------------------------------------------------------------

TEST_SUITE("impl-ClientImpl - interrupt") {
  TEST_CASE("interrupt sets flag and notifies cv") {
    TestClientImpl client;
    client.interrupt();
    CHECK(client.is_interrupted() == true);
  }

  TEST_CASE("interrupt wakes blocked wait_for_connected") {
    TestClientImpl client;

    std::atomic_bool wait_done{false};
    std::thread t([&]() {
      client.wait_for_connected(-1ms);
      wait_done = true;
    });

    std::this_thread::sleep_for(20ms);
    client.interrupt();

    t.join();
    CHECK(wait_done == true);
  }
}

// NOLINTEND
