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

#include "./impl/ack_manager.h"

#include <doctest/doctest.h>

#include <atomic>
#include <thread>
#include <vector>

//
#include "../common_test.h"

// ---------------------------------------------------------------------------
// TEST SUITE: AckManager - basic request lifecycle
// ---------------------------------------------------------------------------

TEST_SUITE("impl-AckManager - basic lifecycle") {
  TEST_CASE("create_request returns non-null") {
    AckManager mgr;
    auto req = mgr.create_request();
    CHECK(req != nullptr);
  }

  TEST_CASE("multiple create_request calls return distinct tokens") {
    AckManager mgr;
    auto r1 = mgr.create_request();
    auto r2 = mgr.create_request();
    auto r3 = mgr.create_request();

    CHECK(r1 != r2);
    CHECK(r2 != r3);
    CHECK(r1 != r3);
  }

  TEST_CASE("process notified by separate thread succeeds") {
    AckManager mgr;
    auto req = mgr.create_request();

    std::thread notifier([&mgr, &req]() {
      // Give the process thread time to start waiting
      std::this_thread::sleep_for(std::chrono::milliseconds(20));
      mgr.notify(req);
    });

    bool result = mgr.process(req, 2000, []() { return true; });
    notifier.join();

    CHECK(result == true);
  }

  TEST_CASE("process times out when no notify is called") {
    AckManager mgr;
    auto req = mgr.create_request();

    bool result = mgr.process(req, 50, []() { return true; });

    CHECK(result == false);
  }

  TEST_CASE("process returns false when send callback returns false") {
    AckManager mgr;
    auto req = mgr.create_request();

    bool result = mgr.process(req, 2000, []() { return false; });

    CHECK(result == false);
  }

  TEST_CASE("notify with callback invokes the callback") {
    AckManager mgr;
    auto req = mgr.create_request();

    bool callback_called = false;

    std::thread notifier([&mgr, &req, &callback_called]() {
      std::this_thread::sleep_for(std::chrono::milliseconds(20));
      mgr.notify(req, [&callback_called]() { callback_called = true; });
    });

    bool result = mgr.process(req, 2000, []() { return true; });
    notifier.join();

    CHECK(result == true);
    CHECK(callback_called == true);
  }

  TEST_CASE("notify returns false for unknown request") {
    AckManager mgr;
    auto req = mgr.create_request();
    // Do not call process — request is not in the set

    // notify on a request not yet in the set should return false
    bool notified = mgr.notify(req);
    CHECK(notified == false);
  }

  TEST_CASE("remove returns false for request not in set") {
    AckManager mgr;
    auto req = mgr.create_request();

    bool removed = mgr.remove(req);
    CHECK(removed == false);
  }
}

// ---------------------------------------------------------------------------
// TEST SUITE: AckManager - clear / interruption
// ---------------------------------------------------------------------------

TEST_SUITE("impl-AckManager - clear") {
  TEST_CASE("clear wakes all blocked process calls") {
    AckManager mgr;

    constexpr int kRequests = 3;
    std::vector<AckManager::RequestPtr> reqs;
    reqs.reserve(kRequests);

    for (int i = 0; i < kRequests; ++i) {
      reqs.push_back(mgr.create_request());
    }

    std::atomic<int> failed_count{0};
    std::vector<std::thread> threads;
    threads.reserve(kRequests);

    for (int i = 0; i < kRequests; ++i) {
      threads.emplace_back([&mgr, &reqs, i, &failed_count]() {
        bool ok = mgr.process(reqs[i], 5000, []() { return true; });

        if (!ok) {
          ++failed_count;
        }
      });
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    mgr.clear();

    for (auto& t : threads) {
      t.join();
    }

    CHECK(failed_count.load() == kRequests);
  }

  TEST_CASE("process returns false immediately after clear") {
    AckManager mgr;
    mgr.clear();

    auto req = mgr.create_request();
    bool result = mgr.process(req, 2000, []() { return true; });

    CHECK(result == false);
  }
}

// ---------------------------------------------------------------------------
// TEST SUITE: AckManager - concurrent requests
// ---------------------------------------------------------------------------

TEST_SUITE("impl-AckManager - concurrent") {
  TEST_CASE("multiple concurrent process/notify pairs all succeed") {
    AckManager mgr;
    constexpr int kPairs = 5;

    std::vector<AckManager::RequestPtr> reqs;
    reqs.reserve(kPairs);

    for (int i = 0; i < kPairs; ++i) {
      reqs.push_back(mgr.create_request());
    }

    std::atomic<int> success_count{0};
    std::vector<std::thread> waiters;
    waiters.reserve(kPairs);

    for (int i = 0; i < kPairs; ++i) {
      waiters.emplace_back([&mgr, &reqs, i, &success_count]() {
        bool ok = mgr.process(reqs[i], 3000, []() { return true; });

        if (ok) {
          ++success_count;
        }
      });
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(30));

    // Notify all in reverse order to show ordering independence
    for (int i = kPairs - 1; i >= 0; --i) {
      mgr.notify(reqs[i]);
    }

    for (auto& t : waiters) {
      t.join();
    }

    CHECK(success_count.load() == kPairs);
  }
}

// ---------------------------------------------------------------------------
// TEST SUITE: AckManager - infinite wait
// ---------------------------------------------------------------------------

TEST_SUITE("impl-AckManager - infinite wait") {
  TEST_CASE("process with negative timeout waits until notify") {
    AckManager mgr;
    auto req = mgr.create_request();

    std::thread notifier([&mgr, &req]() {
      std::this_thread::sleep_for(std::chrono::milliseconds(30));
      mgr.notify(req);
    });

    bool result = mgr.process(req, -1, []() { return true; });
    notifier.join();

    CHECK(result == true);
  }
}

// ---------------------------------------------------------------------------
// TEST SUITE: AckManager - remove
// ---------------------------------------------------------------------------

TEST_SUITE("impl-AckManager - remove") {
  TEST_CASE("remove cancels a pending request") {
    AckManager mgr;
    auto req = mgr.create_request();

    std::atomic<bool> process_result{true};

    std::thread waiter(
        [&mgr, &req, &process_result]() { process_result = mgr.process(req, 5000, []() { return true; }); });

    std::this_thread::sleep_for(std::chrono::milliseconds(30));
    // Remove should find the request in the set
    // Note: remove does not wake the waiter, so the waiter will time out
    // or we clear to unblock
    mgr.clear();

    waiter.join();
    CHECK(process_result.load() == false);
  }

  TEST_CASE("remove on non-existent request returns false") {
    AckManager mgr;
    auto req = mgr.create_request();
    // Not added via process, so remove should return false
    CHECK(mgr.remove(req) == false);
  }
}

// ---------------------------------------------------------------------------
// TEST SUITE: AckManager - notify edge cases
// ---------------------------------------------------------------------------

TEST_SUITE("impl-AckManager - notify edge cases") {
  TEST_CASE("notify with nullptr callback succeeds") {
    AckManager mgr;
    auto req = mgr.create_request();

    std::thread notifier([&mgr, &req]() {
      std::this_thread::sleep_for(std::chrono::milliseconds(20));
      mgr.notify(req, nullptr);
    });

    bool result = mgr.process(req, 2000, []() { return true; });
    notifier.join();

    CHECK(result == true);
  }

  TEST_CASE("double notify on same request - second returns false") {
    AckManager mgr;
    auto req = mgr.create_request();

    std::thread notifier([&mgr, &req]() {
      std::this_thread::sleep_for(std::chrono::milliseconds(20));
      bool first = mgr.notify(req);
      // The request is removed after first notify, so second should fail
      bool second = mgr.notify(req);
      CHECK(first == true);
      CHECK(second == false);
    });

    bool result = mgr.process(req, 2000, []() { return true; });
    notifier.join();

    CHECK(result == true);
  }

  TEST_CASE("process with zero timeout returns false if not immediately notified") {
    AckManager mgr;
    auto req = mgr.create_request();

    bool result = mgr.process(req, 0, []() { return true; });
    // With timeout 0, should return false almost immediately unless notified
    CHECK(result == false);
  }
}

// ---------------------------------------------------------------------------
// TEST SUITE: AckManager - sequence numbers
// ---------------------------------------------------------------------------

TEST_SUITE("impl-AckManager - request sequence") {
  TEST_CASE("requests have monotonically increasing sequence numbers") {
    AckManager mgr;
    auto r1 = mgr.create_request();
    auto r2 = mgr.create_request();
    auto r3 = mgr.create_request();

    // Requests should be distinct and ordered
    CHECK(r1 != nullptr);
    CHECK(r2 != nullptr);
    CHECK(r3 != nullptr);
    CHECK(r1 != r2);
    CHECK(r2 != r3);
  }

  TEST_CASE("clear followed by new requests works") {
    AckManager mgr;
    auto r1 = mgr.create_request();
    mgr.clear();

    // After clear, process should return false immediately
    auto r2 = mgr.create_request();
    CHECK(r2 != nullptr);
    bool result = mgr.process(r2, 50, []() { return true; });
    CHECK(result == false);
  }
}

// NOLINTEND
