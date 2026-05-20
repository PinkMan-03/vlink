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

#include "./base/cancellation.h"

#include <doctest/doctest.h>

#include <atomic>
#include <future>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

//
#include "../common_test.h"

TEST_SUITE("base-Cancellation") {
  TEST_CASE("request_cancel fires registered callbacks once") {
    CancellationSource source;
    auto token = source.token();

    std::atomic<int> count{0};
    auto registration = token.register_callback([&count] { count.fetch_add(1, std::memory_order_relaxed); });

    CHECK(registration.valid());
    CHECK_FALSE(token.is_cancellation_requested());
    CHECK(source.request_cancel());
    CHECK_FALSE(registration.valid());
    CHECK_FALSE(source.request_cancel());
    CHECK(token.is_cancellation_requested());
    CHECK(count.load(std::memory_order_relaxed) == 1);
  }

  TEST_CASE("copied tokens observe the same cancellation state") {
    CancellationSource source;
    auto token_a = source.token();
    auto token_b = token_a;

    CHECK(token_a.valid());
    CHECK(token_b.valid());
    CHECK_FALSE(token_a.is_cancellation_requested());
    CHECK_FALSE(token_b.is_cancellation_requested());

    CHECK(source.request_cancel());
    CHECK(token_a.is_cancellation_requested());
    CHECK(token_b.is_cancellation_requested());
  }

  TEST_CASE("reset registration prevents callback") {
    CancellationSource source;
    auto token = source.token();

    std::atomic<int> count{0};
    auto registration = token.register_callback([&count] { count.fetch_add(1, std::memory_order_relaxed); });
    registration.reset();

    CHECK_FALSE(registration.valid());
    CHECK(source.request_cancel());
    CHECK(count.load(std::memory_order_relaxed) == 0);
  }

  TEST_CASE("registering after cancellation invokes callback immediately") {
    CancellationSource source;
    auto token = source.token();
    std::atomic<bool> fired{false};

    CHECK(source.request_cancel());
    auto registration = token.register_callback([&fired] { fired.store(true, std::memory_order_release); });

    CHECK_FALSE(registration.valid());
    CHECK(fired.load(std::memory_order_acquire));
  }

  TEST_CASE("moved registration keeps callback ownership") {
    CancellationSource source;
    auto token = source.token();

    std::atomic<int> count{0};
    auto registration_a = token.register_callback([&count] { count.fetch_add(1, std::memory_order_relaxed); });
    auto registration_b = std::move(registration_a);

    CHECK_FALSE(registration_a.valid());
    CHECK(registration_b.valid());
    CHECK(source.request_cancel());
    CHECK(count.load(std::memory_order_relaxed) == 1);
  }

  TEST_CASE("callback can query token without deadlocking") {
    CancellationSource source;
    auto token = source.token();

    std::atomic<bool> observed{false};
    auto registration = token.register_callback(
        [token, &observed] { observed.store(token.is_cancellation_requested(), std::memory_order_release); });

    CHECK(source.request_cancel());
    CHECK(observed.load(std::memory_order_acquire));
  }

  TEST_CASE("callback exceptions do not prevent later callbacks") {
    CancellationSource source;
    auto token = source.token();

    auto throwing = token.register_callback([] { throw std::runtime_error("cancel callback"); });
    std::atomic<int> count{0};
    auto counting = token.register_callback([&count] { count.fetch_add(1, std::memory_order_relaxed); });

    CHECK_NOTHROW(source.request_cancel());
    CHECK(count.load(std::memory_order_relaxed) == 1);
    CHECK_FALSE(throwing.valid());
    CHECK_FALSE(counting.valid());
  }

  TEST_CASE("throw_if_cancellation_requested throws Exception::OperationCancelled") {
    CancellationSource source;
    source.request_cancel();

    CHECK_THROWS_AS(source.token().throw_if_cancellation_requested(), Exception::OperationCancelled);

    try {
      source.token().throw_if_cancellation_requested();
      FAIL("expected Exception::OperationCancelled");
    } catch (const Exception::OperationCancelled& e) {
      CHECK(std::string(e.what()) == "vlink operation cancelled");
    }
  }

  // ---

  TEST_CASE("default constructed token is invalid and inert") {
    CancellationToken token;

    CHECK_FALSE(token.valid());
    CHECK_FALSE(token.is_cancellation_requested());

    auto registration = token.register_callback([] {});
    CHECK_FALSE(registration.valid());

    CHECK_NOTHROW(token.throw_if_cancellation_requested());
  }

  // ---

  TEST_CASE("default constructed registration is invalid and resettable") {
    CancellationRegistration registration;

    CHECK_FALSE(registration.valid());
    CHECK_NOTHROW(registration.reset());
    CHECK_FALSE(registration.valid());
  }

  // ---

  TEST_CASE("request_cancel returns false on second call") {
    CancellationSource source;

    CHECK_FALSE(source.is_cancellation_requested());
    CHECK(source.request_cancel());
    CHECK(source.is_cancellation_requested());
    CHECK_FALSE(source.request_cancel());
    CHECK_FALSE(source.request_cancel());
    CHECK(source.is_cancellation_requested());
  }

  // ---

  TEST_CASE("source token mints distinct tokens sharing state") {
    CancellationSource source;
    auto token_a = source.token();
    auto token_b = source.token();
    auto token_c = source.token();

    CHECK(token_a.valid());
    CHECK(token_b.valid());
    CHECK(token_c.valid());
    CHECK_FALSE(token_a.is_cancellation_requested());
    CHECK_FALSE(token_b.is_cancellation_requested());
    CHECK_FALSE(token_c.is_cancellation_requested());

    CHECK(source.request_cancel());

    CHECK(token_a.is_cancellation_requested());
    CHECK(token_b.is_cancellation_requested());
    CHECK(token_c.is_cancellation_requested());
  }

  // ---

  TEST_CASE("all registered callbacks fire on cancel") {
    CancellationSource source;
    auto token = source.token();

    constexpr int kCallbackCount = 16;
    std::atomic<int> count{0};
    std::vector<CancellationRegistration> registrations;
    registrations.reserve(kCallbackCount);

    for (int i = 0; i < kCallbackCount; ++i) {
      registrations.emplace_back(token.register_callback([&count] { count.fetch_add(1, std::memory_order_relaxed); }));
      CHECK(registrations.back().valid());
    }

    CHECK(source.request_cancel());
    CHECK(count.load(std::memory_order_relaxed) == kCallbackCount);

    for (auto& registration : registrations) {
      CHECK_FALSE(registration.valid());
    }
  }

  // ---

  TEST_CASE("concurrent register and cancel is safe") {
    CancellationSource source;
    auto token = source.token();

    constexpr int kWorkers = 8;
    constexpr int kPerWorker = 64;

    std::atomic<int> count{0};
    std::atomic<bool> start{false};
    std::vector<std::thread> workers;
    workers.reserve(kWorkers);

    std::promise<void> ready;
    auto ready_future = ready.get_future();
    std::atomic<int> arrived{0};

    for (int i = 0; i < kWorkers; ++i) {
      workers.emplace_back([&] {
        if (arrived.fetch_add(1, std::memory_order_acq_rel) + 1 == kWorkers) {
          ready.set_value();
        }

        while (!start.load(std::memory_order_acquire)) {
        }
        for (int j = 0; j < kPerWorker; ++j) {
          auto registration = token.register_callback([&count] { count.fetch_add(1, std::memory_order_relaxed); });
          (void)registration;
        }
      });
    }

    ready_future.wait();
    start.store(true, std::memory_order_release);

    std::this_thread::yield();
    CHECK(source.request_cancel());

    for (auto& worker : workers) {
      worker.join();
    }

    CHECK(source.is_cancellation_requested());
    CHECK(count.load(std::memory_order_relaxed) <= kWorkers * kPerWorker);
  }

  // ---

  TEST_CASE("registration reset is idempotent") {
    CancellationSource source;
    auto token = source.token();

    std::atomic<int> count{0};
    auto registration = token.register_callback([&count] { count.fetch_add(1, std::memory_order_relaxed); });

    CHECK(registration.valid());
    registration.reset();
    CHECK_FALSE(registration.valid());
    CHECK_NOTHROW(registration.reset());
    CHECK_NOTHROW(registration.reset());
    CHECK_FALSE(registration.valid());

    CHECK(source.request_cancel());
    CHECK(count.load(std::memory_order_relaxed) == 0);
  }

  // ---

  TEST_CASE("registration self move-assignment is safe") {
    CancellationSource source;
    auto token = source.token();

    std::atomic<int> count{0};
    auto registration = token.register_callback([&count] { count.fetch_add(1, std::memory_order_relaxed); });

    CHECK(registration.valid());

    auto& alias = registration;
    registration = std::move(alias);

    CHECK(registration.valid());
    CHECK(source.request_cancel());
    CHECK(count.load(std::memory_order_relaxed) == 1);
  }

  // ---

  TEST_CASE("registration becomes invalid after source cancellation drains slot") {
    CancellationSource source;
    auto token = source.token();

    auto registration = token.register_callback([] {});
    CHECK(registration.valid());

    CHECK(source.request_cancel());
    CHECK_FALSE(registration.valid());

    CHECK_NOTHROW(registration.reset());
    CHECK_FALSE(registration.valid());
  }

  // ---

  TEST_CASE("throw_if_cancellation_requested is a no-op before cancel") {
    CancellationSource source;
    auto token = source.token();

    CHECK_NOTHROW(token.throw_if_cancellation_requested());
    CHECK_NOTHROW(source.token().throw_if_cancellation_requested());
    CHECK_FALSE(token.is_cancellation_requested());
  }

  // ---

  TEST_CASE("register_callback with empty callback yields empty registration") {
    CancellationSource source;
    auto token = source.token();

    auto registration = token.register_callback(MoveFunction<void()>{});
    CHECK_FALSE(registration.valid());

    CHECK(source.request_cancel());
  }

  // ---

  TEST_CASE("many tokens observing the same source all see cancellation") {
    CancellationSource source;

    constexpr int kTokenCount = 64;
    std::vector<CancellationToken> tokens;
    tokens.reserve(kTokenCount);
    for (int i = 0; i < kTokenCount; ++i) {
      tokens.emplace_back(source.token());
      CHECK(tokens.back().valid());
      CHECK_FALSE(tokens.back().is_cancellation_requested());
    }

    CHECK(source.request_cancel());

    for (auto& token : tokens) {
      CHECK(token.is_cancellation_requested());
      CHECK_THROWS_AS(token.throw_if_cancellation_requested(), Exception::OperationCancelled);
    }
  }

  // ---

  TEST_CASE("token outlives source via shared state") {
    CancellationToken token;
    {
      CancellationSource source;
      token = source.token();
      CHECK(token.valid());
      CHECK_FALSE(token.is_cancellation_requested());
    }

    CHECK(token.valid());
    CHECK_FALSE(token.is_cancellation_requested());
    CHECK_NOTHROW(token.throw_if_cancellation_requested());

    std::atomic<bool> fired{false};
    auto registration = token.register_callback([&fired] { fired.store(true, std::memory_order_release); });
    CHECK(registration.valid());

    registration.reset();
    CHECK_FALSE(fired.load(std::memory_order_acquire));
  }

  // -------------------------------------------------------------------------
  // Additional coverage: cascading cancellation, sibling fan-in inside a
  // callback, registration lifetime edge cases, and sync-fire thread identity.
  // -------------------------------------------------------------------------

  TEST_CASE("callback firing parent token cascades to child source") {
    CancellationSource parent;
    CancellationSource child;

    auto child_token = child.token();
    auto reg = parent.token().register_callback([&child] { child.request_cancel(); });

    CHECK(parent.request_cancel());
    CHECK(child_token.is_cancellation_requested());
  }

  TEST_CASE("three-level cascading cancellation propagates through every level") {
    CancellationSource root;
    CancellationSource mid;
    CancellationSource leaf;

    auto reg_mid = root.token().register_callback([&mid] { mid.request_cancel(); });
    auto reg_leaf = mid.token().register_callback([&leaf] { leaf.request_cancel(); });

    auto leaf_token = leaf.token();
    CHECK(root.request_cancel());
    CHECK(mid.token().is_cancellation_requested());
    CHECK(leaf_token.is_cancellation_requested());
  }

  TEST_CASE("callback may fire a sibling source without self-deadlock") {
    CancellationSource a;
    CancellationSource b;
    std::atomic<bool> b_was_observed{false};

    auto reg = a.token().register_callback([token_a = a.token(), &b, &b_was_observed] {
      // The source-A mutex is already released before this callback executes,
      // so we can freely inspect A's state and trigger B without self-deadlock.
      b_was_observed.store(token_a.is_cancellation_requested(), std::memory_order_release);
      b.request_cancel();
    });

    CHECK(a.request_cancel());
    CHECK(b_was_observed.load(std::memory_order_acquire));
    CHECK(b.token().is_cancellation_requested());
  }

  TEST_CASE("register_callback after cancel fires synchronously on the caller thread") {
    CancellationSource source;
    source.request_cancel();

    const auto caller_id = std::this_thread::get_id();
    std::atomic<std::thread::id> fire_thread{};

    auto reg = source.token().register_callback(
        [&fire_thread] { fire_thread.store(std::this_thread::get_id(), std::memory_order_release); });

    CHECK_FALSE(reg.valid());
    CHECK(fire_thread.load(std::memory_order_acquire) == caller_id);
  }

  TEST_CASE("registering many callbacks with selective reset fires only the survivors") {
    CancellationSource source;
    auto token = source.token();

    std::atomic<int> survivor_fires{0};
    std::vector<CancellationRegistration> survivors;
    std::vector<CancellationRegistration> cancelled_before_fire;

    for (int i = 0; i < 16; ++i) {
      auto reg = token.register_callback([&survivor_fires] { survivor_fires.fetch_add(1); });

      if ((i & 1) == 0) {
        survivors.push_back(std::move(reg));
      } else {
        cancelled_before_fire.push_back(std::move(reg));
      }
    }
    for (auto& r : cancelled_before_fire) {
      r.reset();
    }

    CHECK(source.request_cancel());
    CHECK(survivor_fires.load(std::memory_order_acquire) == 8);
  }

  TEST_CASE("CancellationSource is implicitly copyable and copies share underlying state") {
    CancellationSource a;
    CancellationSource b = a;  // implicit copy via shared_ptr semantics

    auto token = a.token();
    CHECK_FALSE(token.is_cancellation_requested());
    CHECK_FALSE(b.is_cancellation_requested());

    // Cancelling via either handle is visible to tokens minted from either.
    CHECK(b.request_cancel());
    CHECK(token.is_cancellation_requested());
    CHECK(a.is_cancellation_requested());
    CHECK_FALSE(a.request_cancel());  // already cancelled
  }

  TEST_CASE("callback registered inside another callback fires on next cancel cycle only if source remains") {
    CancellationSource source;
    auto token = source.token();

    std::atomic<int> inner_fired{0};
    std::vector<CancellationRegistration> registrations;

    auto reg = token.register_callback([&] {
      // Source is already cancelled at this point; new registrations on the
      // same token are invoked synchronously by register_callback().
      auto inner_reg = token.register_callback([&inner_fired] { inner_fired.fetch_add(1); });
      CHECK_FALSE(inner_reg.valid());
    });

    CHECK(source.request_cancel());
    CHECK(inner_fired.load(std::memory_order_acquire) == 1);
  }

  TEST_CASE("registration destructor on cancelled source is a clean no-op") {
    CancellationSource source;
    auto token = source.token();

    {
      auto reg = token.register_callback([] {});
      CHECK(reg.valid());
      // Cancel before reg goes out of scope; the slot is drained inside request_cancel().
      CHECK(source.request_cancel());
      CHECK_FALSE(reg.valid());
    }
    // No crash, no double-erase.
    CHECK(true);  // reaching this point is sufficient
  }

  TEST_CASE("many concurrent observers race against a single producer cancel") {
    CancellationSource source;
    const int kObservers = 16;

    std::atomic<int> total_seen{0};
    std::atomic<bool> go{false};

    std::vector<std::thread> workers;
    workers.reserve(kObservers);
    for (int i = 0; i < kObservers; ++i) {
      workers.emplace_back([token = source.token(), &total_seen, &go] {
        while (!go.load(std::memory_order_acquire)) {
          std::this_thread::yield();
        }
        while (!token.is_cancellation_requested()) {
          std::this_thread::yield();
        }
        total_seen.fetch_add(1, std::memory_order_acq_rel);
      });
    }

    go.store(true, std::memory_order_release);
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
    CHECK(source.request_cancel());
    for (auto& t : workers) t.join();

    CHECK(total_seen.load() == kObservers);
  }
}

// NOLINTEND
