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

#include "./base/object_pool.h"

#include <doctest/doctest.h>

#include <atomic>
#include <memory>
#include <stdexcept>
#include <thread>
#include <vector>

//
#include "../common_test.h"

// ---------------------------------------------------------------------------
// Simple poolable type for tests
// ---------------------------------------------------------------------------

struct Widget {
  int value{0};
  bool reset_called{false};
};

// ---------------------------------------------------------------------------
// TEST SUITE: construction
// ---------------------------------------------------------------------------

TEST_SUITE("base-ObjectPool - construction") {
  TEST_CASE("default construction - empty pool") {
    auto pool = std::make_shared<ObjectPool<Widget>>();

    CHECK(pool->size() == 0u);
    CHECK(pool->borrowed() == 0u);
    CHECK(pool->total_created() == 0u);
    CHECK(pool->max_size() == 0u);
  }

  TEST_CASE("pre-fill with initial_size") {
    auto pool = std::make_shared<ObjectPool<Widget>>([] { return std::make_unique<Widget>(); }, 4u);

    CHECK(pool->size() == 4u);
    CHECK(pool->borrowed() == 0u);
    CHECK(pool->total_created() == 4u);
  }

  TEST_CASE("max_size is recorded correctly") {
    auto pool = std::make_shared<ObjectPool<Widget>>([] { return std::make_unique<Widget>(); }, 2u, 8u);

    CHECK(pool->max_size() == 8u);
  }

  TEST_CASE("initial_size > max_size throws invalid_argument") {
    CHECK_THROWS_AS(((void)std::make_shared<ObjectPool<Widget>>([] { return std::make_unique<Widget>(); }, 5u, 3u)),
                    std::invalid_argument);
  }

  TEST_CASE("factory returning nullptr throws runtime_error on pre-fill") {
    CHECK_THROWS_AS(((void)std::make_shared<ObjectPool<Widget>>([] { return std::unique_ptr<Widget>{}; }, 1u)),
                    std::runtime_error);
  }
}

// ---------------------------------------------------------------------------
// TEST SUITE: get() - RAII unique_ptr
// ---------------------------------------------------------------------------

TEST_SUITE("base-ObjectPool - get") {
  TEST_CASE("get returns a non-null pointer") {
    auto pool = std::make_shared<ObjectPool<Widget>>();
    auto obj = pool->get();
    CHECK(obj.get() != nullptr);
  }

  TEST_CASE("get increments borrowed count") {
    auto pool = std::make_shared<ObjectPool<Widget>>();
    auto obj = pool->get();
    CHECK(pool->borrowed() == 1u);
  }

  TEST_CASE("object is returned to pool after unique_ptr destruction") {
    auto pool = std::make_shared<ObjectPool<Widget>>();

    {
      auto obj = pool->get();
      CHECK(pool->borrowed() == 1u);
      CHECK(pool->size() == 0u);
    }

    CHECK(pool->borrowed() == 0u);
    CHECK(pool->size() == 1u);
  }

  TEST_CASE("multiple get calls create multiple objects") {
    auto pool = std::make_shared<ObjectPool<Widget>>();

    auto a = pool->get();
    auto b = pool->get();
    auto c = pool->get();

    CHECK(pool->borrowed() == 3u);
    CHECK(pool->total_created() == 3u);
  }

  TEST_CASE("pool grows without bound when max_size is 0") {
    auto pool = std::make_shared<ObjectPool<Widget>>();
    std::vector<decltype(pool->get())> objs;

    for (int i = 0; i < 50; ++i) {
      objs.emplace_back(pool->get());
    }

    CHECK(pool->borrowed() == 50u);
    CHECK(pool->total_created() == 50u);
  }

  TEST_CASE("exhausted pool (max_size > 0) throws runtime_error") {
    auto pool = std::make_shared<ObjectPool<Widget>>([] { return std::make_unique<Widget>(); }, 0u, 2u);

    auto a = pool->get();
    auto b = pool->get();

    CHECK_THROWS_AS((void)pool->get(), std::runtime_error);
  }

  TEST_CASE("after releasing one object exhausted pool allows one more get") {
    auto pool = std::make_shared<ObjectPool<Widget>>([] { return std::make_unique<Widget>(); }, 0u, 1u);

    {
      auto obj = pool->get();
      CHECK(pool->borrowed() == 1u);
      // exhausted here
      CHECK_THROWS_AS((void)pool->get(), std::runtime_error);
    }

    // obj returned; pool can serve one more
    auto obj2 = pool->get();
    CHECK(obj2.get() != nullptr);
  }

  TEST_CASE("reset failure discards object without exhausting max_size") {
    bool throw_on_release = true;
    auto pool = std::make_shared<ObjectPool<Widget>>([] { return std::make_unique<Widget>(); }, 0u, 1u,
                                                     [&throw_on_release](Widget&) {
                                                       if (throw_on_release) {
                                                         throw std::runtime_error("reset failed");
                                                       }
                                                     },
                                                     ObjectPool<Widget>::kPolicyRelease);

    {
      auto obj = pool->get();
      REQUIRE(obj.get() != nullptr);
    }

    CHECK(pool->borrowed() == 0u);
    CHECK(pool->size() == 0u);

    throw_on_release = false;
    auto obj2 = pool->get();
    CHECK(obj2.get() != nullptr);
  }
}

// ---------------------------------------------------------------------------
// TEST SUITE: get_shared() - shared_ptr
// ---------------------------------------------------------------------------

TEST_SUITE("base-ObjectPool - get_shared") {
  TEST_CASE("get_shared returns a non-null shared_ptr") {
    auto pool = std::make_shared<ObjectPool<Widget>>();
    auto obj = pool->get_shared();
    CHECK(obj.get() != nullptr);
  }

  TEST_CASE("get_shared increments borrowed") {
    auto pool = std::make_shared<ObjectPool<Widget>>();
    auto obj = pool->get_shared();
    CHECK(pool->borrowed() == 1u);
  }

  TEST_CASE("object returned to pool when last shared_ptr ref dropped") {
    auto pool = std::make_shared<ObjectPool<Widget>>();

    {
      auto obj = pool->get_shared();
      auto obj2 = obj;  // NOLINT(performance-unnecessary-copy-initialization)
      CHECK(pool->borrowed() == 1u);
    }

    CHECK(pool->borrowed() == 0u);
    CHECK(pool->size() == 1u);
  }
}

// ---------------------------------------------------------------------------
// TEST SUITE: borrow / give_back
// ---------------------------------------------------------------------------

TEST_SUITE("base-ObjectPool - borrow and give_back") {
  TEST_CASE("borrow returns a non-null raw pointer") {
    auto pool = std::make_shared<ObjectPool<Widget>>();
    Widget* raw = pool->borrow();
    REQUIRE(raw != nullptr);
    pool->give_back(raw);
  }

  TEST_CASE("give_back returns object to pool") {
    auto pool = std::make_shared<ObjectPool<Widget>>();
    Widget* raw = pool->borrow();
    CHECK(pool->borrowed() == 1u);
    pool->give_back(raw);
    CHECK(pool->borrowed() == 0u);
    CHECK(pool->size() == 1u);
  }

  TEST_CASE("give_back with nullptr is a no-op") {
    auto pool = std::make_shared<ObjectPool<Widget>>();
    pool->give_back(nullptr);
    CHECK(pool->size() == 0u);
    CHECK(pool->borrowed() == 0u);
  }
}

// ---------------------------------------------------------------------------
// TEST SUITE: reset policy
// ---------------------------------------------------------------------------

TEST_SUITE("base-ObjectPool - reset policy") {
  TEST_CASE("kPolicyNone - reset callback not called on acquire or release") {
    int reset_count = 0;

    auto pool = std::make_shared<ObjectPool<Widget>>([] { return std::make_unique<Widget>(); }, 0u, 0u,
                                                     [&reset_count](Widget&) { ++reset_count; },
                                                     ObjectPool<Widget>::kPolicyNone);

    {
      auto obj = pool->get();
      (void)obj;
    }

    CHECK(reset_count == 0);
  }

  TEST_CASE("kPolicyRelease - reset callback called on return") {
    int reset_count = 0;

    auto pool = std::make_shared<ObjectPool<Widget>>([] { return std::make_unique<Widget>(); }, 0u, 0u,
                                                     [&reset_count](Widget& w) {
                                                       ++reset_count;
                                                       w.reset_called = true;
                                                     },
                                                     ObjectPool<Widget>::kPolicyRelease);

    {
      auto obj = pool->get();
      (void)obj;
    }

    CHECK(reset_count == 1);
  }

  TEST_CASE("kPolicyRelease - reset called on initial pre-fill objects") {
    int reset_count = 0;

    auto pool = std::make_shared<ObjectPool<Widget>>([] { return std::make_unique<Widget>(); }, 3u, 0u,
                                                     [&reset_count](Widget&) { ++reset_count; },
                                                     ObjectPool<Widget>::kPolicyRelease);

    // 3 pre-filled objects each get reset on the release side of pre-fill
    CHECK(reset_count == 3);
  }

  TEST_CASE("kPolicyAcquire - reset callback called on acquire") {
    int reset_count = 0;

    auto pool = std::make_shared<ObjectPool<Widget>>([] { return std::make_unique<Widget>(); }, 0u, 0u,
                                                     [&reset_count](Widget&) { ++reset_count; },
                                                     ObjectPool<Widget>::kPolicyAcquire);

    {
      auto obj = pool->get();
      CHECK(reset_count == 1);
    }

    // should NOT be called on release
    CHECK(reset_count == 1);
  }

  TEST_CASE("kPolicyBoth - reset called on both acquire and release") {
    int reset_count = 0;

    auto pool = std::make_shared<ObjectPool<Widget>>([] { return std::make_unique<Widget>(); }, 0u, 0u,
                                                     [&reset_count](Widget&) { ++reset_count; },
                                                     ObjectPool<Widget>::kPolicyBoth);

    {
      auto obj = pool->get();
      CHECK(reset_count == 1);
    }

    CHECK(reset_count == 2);
  }
}

// ---------------------------------------------------------------------------
// TEST SUITE: stats
// ---------------------------------------------------------------------------

TEST_SUITE("base-ObjectPool - stats") {
  TEST_CASE("stats reflect correct counters") {
    auto pool = std::make_shared<ObjectPool<Widget>>([] { return std::make_unique<Widget>(); }, 2u, 10u);

    auto s = pool->stats();

    CHECK(s.pool_size == 2u);
    CHECK(s.borrowed == 0u);
    CHECK(s.total_created == 2u);
    CHECK(s.max_size == 10u);
  }

  TEST_CASE("stats update correctly after get and release") {
    auto pool = std::make_shared<ObjectPool<Widget>>();

    {
      auto obj = pool->get();
      auto s = pool->stats();
      CHECK(s.borrowed == 1u);
      CHECK(s.pool_size == 0u);
    }

    auto s = pool->stats();
    CHECK(s.borrowed == 0u);
    CHECK(s.pool_size == 1u);
    CHECK(s.total_created == 1u);
  }
}

// ---------------------------------------------------------------------------
// TEST SUITE: pool reuse
// ---------------------------------------------------------------------------

TEST_SUITE("base-ObjectPool - object reuse") {
  TEST_CASE("returned object is reused on next get") {
    auto pool = std::make_shared<ObjectPool<Widget>>();

    Widget* first_ptr = nullptr;

    {
      auto obj = pool->get();
      first_ptr = obj.get();
      obj->value = 99;
    }

    // Pool now has one idle object; next get should reuse it (LIFO)
    auto obj2 = pool->get();
    CHECK(obj2.get() == first_ptr);
    CHECK(obj2->value == 99);  // value preserved (no reset callback)
    CHECK(pool->total_created() == 1u);
  }

  TEST_CASE("repeated acquire-release cycles do not grow total_created") {
    auto pool = std::make_shared<ObjectPool<Widget>>();

    for (int i = 0; i < 10; ++i) {
      auto obj = pool->get();
      (void)obj;
    }

    // One object created, reused 10 times
    CHECK(pool->total_created() == 1u);
  }
}

// ---------------------------------------------------------------------------
// TEST SUITE: PoolDeleter when pool destroyed
// ---------------------------------------------------------------------------

TEST_SUITE("base-ObjectPool - pool deleter with expired pool") {
  TEST_CASE("unique_ptr deletes object safely when pool is gone") {
    std::unique_ptr<Widget, ObjectPool<Widget>::PoolDeleter> handle;

    {
      auto pool = std::make_shared<ObjectPool<Widget>>();
      handle = pool->get();
      // pool shared_ptr destroyed here; handle's weak_ptr will expire
    }

    // Resetting handle should call delete instead of returning to pool
    // This must not crash or leak (verified by AddressSanitizer / valgrind)
    handle.reset();
    CHECK(true);
  }
}

// ---------------------------------------------------------------------------
// TEST SUITE: thread safety
// ---------------------------------------------------------------------------

TEST_SUITE("base-ObjectPool - thread safety") {
  TEST_CASE("concurrent get and release does not corrupt state") {
    auto pool = std::make_shared<ObjectPool<Widget>>([] { return std::make_unique<Widget>(); }, 0u, 0u);

    constexpr int kThreads = 4;
    constexpr int kIter = 100;
    std::atomic<int> errors{0};

    std::vector<std::thread> threads;
    threads.reserve(kThreads);

    for (int t = 0; t < kThreads; ++t) {
      threads.emplace_back([&]() {
        for (int i = 0; i < kIter; ++i) {
          try {
            auto obj = pool->get();
            obj->value = i;
          } catch (std::exception&) {
            ++errors;
          }
        }
      });
    }

    for (auto& th : threads) {
      th.join();
    }

    CHECK(errors.load() == 0);
    CHECK(pool->borrowed() == 0u);
  }

  TEST_CASE("concurrent borrow and give_back does not corrupt state") {
    auto pool = std::make_shared<ObjectPool<Widget>>([] { return std::make_unique<Widget>(); }, 0u, 0u);

    constexpr int kThreads = 4;
    constexpr int kIter = 50;
    std::atomic<int> errors{0};

    std::vector<std::thread> threads;
    threads.reserve(kThreads);

    for (int t = 0; t < kThreads; ++t) {
      threads.emplace_back([&]() {
        for (int i = 0; i < kIter; ++i) {
          try {
            Widget* raw = pool->borrow();
            raw->value = i;
            pool->give_back(raw);
          } catch (std::exception&) {
            errors.fetch_add(1);
          }
        }
      });
    }

    for (auto& th : threads) {
      th.join();
    }

    CHECK(errors.load() == 0);
    CHECK(pool->borrowed() == 0u);
  }

  TEST_CASE("concurrent get_shared does not corrupt state") {
    auto pool = std::make_shared<ObjectPool<Widget>>([] { return std::make_unique<Widget>(); }, 0u, 0u);

    constexpr int kThreads = 4;
    constexpr int kIter = 50;
    std::atomic<int> errors{0};

    std::vector<std::thread> threads;
    threads.reserve(kThreads);

    for (int t = 0; t < kThreads; ++t) {
      threads.emplace_back([&]() {
        for (int i = 0; i < kIter; ++i) {
          try {
            auto obj = pool->get_shared();
            obj->value = i;
          } catch (std::exception&) {
            errors.fetch_add(1);
          }
        }
      });
    }

    for (auto& th : threads) {
      th.join();
    }

    CHECK(errors.load() == 0);
    CHECK(pool->borrowed() == 0u);
  }

  TEST_CASE("concurrent access with max_size respects limit") {
    auto pool = std::make_shared<ObjectPool<Widget>>([] { return std::make_unique<Widget>(); }, 4u, 8u);

    constexpr int kThreads = 4;
    constexpr int kIter = 20;
    std::atomic<int> exhaustion_errors{0};

    std::vector<std::thread> threads;
    threads.reserve(kThreads);

    for (int t = 0; t < kThreads; ++t) {
      threads.emplace_back([&]() {
        for (int i = 0; i < kIter; ++i) {
          try {
            auto obj = pool->get();
            obj->value = i;
            std::this_thread::yield();
          } catch (const std::runtime_error&) {
            exhaustion_errors.fetch_add(1);
          }
        }
      });
    }

    for (auto& th : threads) {
      th.join();
    }

    CHECK(pool->borrowed() == 0u);
    CHECK(pool->total_created() <= 8u);
  }
}

TEST_SUITE("base-ObjectPool - edge cases") {
  TEST_CASE("factory returning nullptr on demand throws runtime_error") {
    auto pool = std::make_shared<ObjectPool<Widget>>([] { return std::unique_ptr<Widget>{}; }, 0u, 0u);

    CHECK_THROWS_AS((void)pool->get(), std::runtime_error);
  }

  TEST_CASE("factory returning nullptr on borrow throws runtime_error") {
    auto pool = std::make_shared<ObjectPool<Widget>>([] { return std::unique_ptr<Widget>{}; }, 0u, 0u);

    CHECK_THROWS_AS((void)pool->borrow(), std::runtime_error);
  }

  TEST_CASE("factory returning nullptr on get_shared throws runtime_error") {
    auto pool = std::make_shared<ObjectPool<Widget>>([] { return std::unique_ptr<Widget>{}; }, 0u, 0u);

    CHECK_THROWS_AS((void)pool->get_shared(), std::runtime_error);
  }

  TEST_CASE("exhausted pool on borrow throws runtime_error") {
    auto pool = std::make_shared<ObjectPool<Widget>>([] { return std::make_unique<Widget>(); }, 0u, 1u);

    Widget* raw = pool->borrow();
    CHECK_THROWS_AS((void)pool->borrow(), std::runtime_error);
    pool->give_back(raw);
  }

  TEST_CASE("exhausted pool on get_shared throws runtime_error") {
    auto pool = std::make_shared<ObjectPool<Widget>>([] { return std::make_unique<Widget>(); }, 0u, 1u);

    auto obj = pool->get_shared();
    CHECK_THROWS_AS((void)pool->get_shared(), std::runtime_error);
  }

  TEST_CASE("pre-fill with kPolicyAcquire does not call reset on construction") {
    int reset_count = 0;
    auto pool = std::make_shared<ObjectPool<Widget>>([] { return std::make_unique<Widget>(); }, 3u, 0u,
                                                     [&reset_count](Widget&) { ++reset_count; },
                                                     ObjectPool<Widget>::kPolicyAcquire);
    CHECK(reset_count == 0);
    CHECK(pool->size() == 3u);
  }

  TEST_CASE("pre-fill with kPolicyBoth calls reset on pre-fill objects") {
    int reset_count = 0;
    auto pool = std::make_shared<ObjectPool<Widget>>([] { return std::make_unique<Widget>(); }, 2u, 0u,
                                                     [&reset_count](Widget&) { ++reset_count; },
                                                     ObjectPool<Widget>::kPolicyBoth);
    CHECK(reset_count == 2);
  }

  TEST_CASE("max_size 0 means unlimited") {
    auto pool = std::make_shared<ObjectPool<Widget>>([] { return std::make_unique<Widget>(); }, 0u, 0u);
    CHECK(pool->max_size() == 0u);

    std::vector<decltype(pool->get())> objs;
    for (int i = 0; i < 100; ++i) {
      objs.emplace_back(pool->get());
    }
    CHECK(pool->borrowed() == 100u);
  }

  TEST_CASE("stats after mixed get and borrow operations") {
    auto pool = std::make_shared<ObjectPool<Widget>>([] { return std::make_unique<Widget>(); }, 2u, 10u);

    auto obj1 = pool->get();
    Widget* raw = pool->borrow();
    auto obj2 = pool->get_shared();

    auto s = pool->stats();
    CHECK(s.borrowed == 3u);
    CHECK(s.total_created >= 3u);

    pool->give_back(raw);
    obj1.reset();
    obj2.reset();

    s = pool->stats();
    CHECK(s.borrowed == 0u);
    CHECK(s.pool_size == 3u);
  }

  TEST_CASE("shared_ptr copies keep object alive and return once") {
    auto pool = std::make_shared<ObjectPool<Widget>>();

    {
      auto obj1 = pool->get_shared();
      auto obj2 = obj1;
      auto obj3 = obj2;

      CHECK(pool->borrowed() == 1u);

      obj1.reset();
      CHECK(pool->borrowed() == 1u);

      obj2.reset();
      CHECK(pool->borrowed() == 1u);
    }

    CHECK(pool->borrowed() == 0u);
    CHECK(pool->size() == 1u);
  }

  TEST_CASE("non-std factory exception rolls counters back") {
    int calls = 0;
    auto pool = std::make_shared<ObjectPool<Widget>>(
        [&calls] {
          if (calls++ == 0) {
            throw 1;
          }
          return std::make_unique<Widget>();
        },
        0u, 1u);

    CHECK_THROWS((void)pool->get());
    CHECK(pool->borrowed() == 0u);

    auto obj = pool->get();
    CHECK(obj != nullptr);
    CHECK(pool->borrowed() == 1u);
  }

  TEST_CASE("non-std acquire reset exception rolls counters back") {
    bool throw_now = true;
    auto pool = std::make_shared<ObjectPool<Widget>>([] { return std::make_unique<Widget>(); }, 0u, 1u,
                                                     [&throw_now](Widget&) {
                                                       if (throw_now) {
                                                         throw 1;
                                                       }
                                                     },
                                                     ObjectPool<Widget>::kPolicyAcquire);

    CHECK_THROWS((void)pool->get());
    CHECK(pool->borrowed() == 0u);

    throw_now = false;
    auto obj = pool->get();
    CHECK(obj != nullptr);
    CHECK(pool->borrowed() == 1u);
  }

  TEST_CASE("non-std release reset exception discards object without terminating") {
    auto pool = std::make_shared<ObjectPool<Widget>>([] { return std::make_unique<Widget>(); }, 0u, 1u,
                                                     [](Widget&) { throw 1; }, ObjectPool<Widget>::kPolicyRelease);

    auto obj = pool->get();
    CHECK(pool->borrowed() == 1u);
    obj.reset();

    CHECK(pool->borrowed() == 0u);
    CHECK(pool->size() == 0u);
  }
}

// NOLINTEND
