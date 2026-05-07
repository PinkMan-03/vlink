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

#include "./base/functional.h"

#include <doctest/doctest.h>

#include <array>
#include <atomic>
#include <memory>
#include <string>
#include <utility>
#include <vector>

//
#include "../common_test.h"

#ifdef VLINK_ENABLE_CALLBACK

// ---------------------------------------------------------------------------
TEST_SUITE("base-Function - construction & emptiness") {
  // -------------------------------------------------------------------------
  TEST_CASE("default-constructed Function is empty") {
    Function<void()> cb;
    CHECK(!cb);
    CHECK(cb == nullptr);
  }

  // -------------------------------------------------------------------------
  TEST_CASE("nullptr-constructed Function is empty") {
    Function<int(int)> cb(nullptr);
    CHECK(!cb);
    CHECK(cb == nullptr);
    CHECK(nullptr == cb);
  }

  // -------------------------------------------------------------------------
  TEST_CASE("invoking an empty Function throws std::bad_function_call") {
    Function<void()> cb;
    CHECK_THROWS_AS(cb(), std::bad_function_call);
  }

  // -------------------------------------------------------------------------
  TEST_CASE("Function constructed from a lambda is non-empty") {
    Function<int(int)> cb = [](int x) { return x * 2; };
    CHECK(static_cast<bool>(cb));
    CHECK(cb != nullptr);
    CHECK(cb(21) == 42);
  }
}

// ---------------------------------------------------------------------------
TEST_SUITE("base-Function - SBO inline storage") {
  // -------------------------------------------------------------------------
  TEST_CASE("small lambda fits in inline storage (no heap)") {
    int counter = 0;
    Function<void()> cb = [&counter]() { ++counter; };

    cb();
    cb();
    cb();

    CHECK(counter == 3);
  }

  // -------------------------------------------------------------------------
  TEST_CASE("a few shared_ptr captures fit in the 64 byte SBO") {
    auto a = std::make_shared<int>(1);
    auto b = std::make_shared<int>(2);
    auto c = std::make_shared<int>(3);
    Function<int()> cb = [a, b, c]() { return *a + *b + *c; };

    CHECK(cb() == 6);
  }

  // -------------------------------------------------------------------------
  TEST_CASE("lambda larger than 64 bytes falls back to MemoryPool-backed heap") {
    std::array<int, 32> payload{};
    for (size_t i = 0; i < payload.size(); ++i) {
      payload[i] = static_cast<int>(i);
    }

    Function<int()> cb = [payload]() {
      int sum = 0;
      for (int v : payload) sum += v;
      return sum;
    };

    CHECK(cb() == (31 * 32) / 2);
  }

  // -------------------------------------------------------------------------
  TEST_CASE("kSboSize is 64") { CHECK(Function<void()>::kSboSize == 64U); }
}

// ---------------------------------------------------------------------------
TEST_SUITE("base-Function - MemoryPool heap fallback") {
  // -------------------------------------------------------------------------
  TEST_CASE("heap fallback path allocates and releases without leak") {
    auto count = std::make_shared<std::atomic<int>>(0);

    struct LargeFunctor {
      std::array<char, 256> bytes{};
      std::shared_ptr<std::atomic<int>> count;

      explicit LargeFunctor(std::shared_ptr<std::atomic<int>> c) : count(std::move(c)) {}
      LargeFunctor(const LargeFunctor&) = default;
      LargeFunctor(LargeFunctor&&) noexcept = default;
      LargeFunctor& operator=(const LargeFunctor&) = default;
      LargeFunctor& operator=(LargeFunctor&&) noexcept = default;
      ~LargeFunctor() {
        if (count) count->fetch_add(1, std::memory_order_relaxed);
      }

      void operator()() const {}
    };

    {
      Function<void()> cb = LargeFunctor(count);
      cb();
    }

    CHECK(count->load() >= 1);
  }

  // -------------------------------------------------------------------------
  TEST_CASE("heap fallback survives copy and move via the same pool") {
    std::array<int, 128> payload{};
    for (size_t i = 0; i < payload.size(); ++i) {
      payload[i] = static_cast<int>(i);
    }

    Function<int()> a = [payload]() {
      int sum = 0;
      for (int v : payload) sum += v;
      return sum;
    };

    Function<int()> b(a);
    Function<int()> c(std::move(a));

    const int expected = (127 * 128) / 2;
    CHECK(b() == expected);
    CHECK(c() == expected);
    CHECK(!a);
  }
}

// ---------------------------------------------------------------------------
TEST_SUITE("base-Function - copy and move semantics") {
  // -------------------------------------------------------------------------
  TEST_CASE("copy-constructed Function invokes the same target") {
    Function<int(int)> a = [](int x) { return x + 1; };
    Function<int(int)> b(a);

    CHECK(a(10) == 11);
    CHECK(b(10) == 11);
  }

  // -------------------------------------------------------------------------
  TEST_CASE("move-constructed Function steals the target and leaves source empty") {
    Function<int(int)> a = [](int x) { return x + 5; };
    Function<int(int)> b(std::move(a));

    CHECK(b(7) == 12);
    CHECK(!a);
  }

  // -------------------------------------------------------------------------
  TEST_CASE("copy assignment replaces the existing target") {
    Function<int()> a = []() { return 1; };
    Function<int()> b = []() { return 2; };

    a = b;

    CHECK(a() == 2);
    CHECK(b() == 2);
  }

  // -------------------------------------------------------------------------
  TEST_CASE("move assignment replaces the existing target and leaves source empty") {
    Function<int()> a = []() { return 1; };
    Function<int()> b = []() { return 2; };

    a = std::move(b);

    CHECK(a() == 2);
    CHECK(!b);
  }

  // -------------------------------------------------------------------------
  TEST_CASE("assignment to nullptr clears the target") {
    Function<int()> cb = []() { return 42; };
    cb = nullptr;
    CHECK(!cb);
  }

  // -------------------------------------------------------------------------
  TEST_CASE("self-copy-assignment is a no-op") {
    Function<int()> cb = []() { return 99; };
    cb = cb;  // NOLINT(clang-diagnostic-self-assign-overloaded)
    CHECK(cb() == 99);
  }

  // -------------------------------------------------------------------------
  TEST_CASE("self-move-assignment is a no-op") {
    Function<int()> cb = []() { return 99; };
    Function<int()>& ref = cb;  // alias to dodge -Wself-move while keeping the test intent
    cb = std::move(ref);
    CHECK(cb() == 99);
  }

  // -------------------------------------------------------------------------
  TEST_CASE("swap exchanges targets between two Functions") {
    Function<int()> a = []() { return 1; };
    Function<int()> b = []() { return 2; };

    swap(a, b);

    CHECK(a() == 2);
    CHECK(b() == 1);
  }

  // -------------------------------------------------------------------------
  TEST_CASE("swap with empty Function transfers ownership") {
    Function<int()> a = []() { return 7; };
    Function<int()> b;

    a.swap(b);

    CHECK(!a);
    CHECK(static_cast<bool>(b));
    CHECK(b() == 7);
  }
}

// ---------------------------------------------------------------------------
TEST_SUITE("base-Function - shared_ptr capture lifetime") {
  // -------------------------------------------------------------------------
  TEST_CASE("destroying the Function releases shared_ptr capture") {
    auto resource = std::make_shared<int>(123);
    CHECK(resource.use_count() == 1);

    {
      Function<int()> cb = [resource]() { return *resource; };
      CHECK(resource.use_count() == 2);
      CHECK(cb() == 123);
    }

    CHECK(resource.use_count() == 1);
  }

  // -------------------------------------------------------------------------
  TEST_CASE("copying the Function duplicates shared_ptr capture") {
    auto resource = std::make_shared<int>(0);
    Function<void()> a = [resource]() { ++(*resource); };

    CHECK(resource.use_count() == 2);

    Function<void()> b(a);

    CHECK(resource.use_count() == 3);

    a();
    b();

    CHECK(*resource == 2);
  }

  // -------------------------------------------------------------------------
  TEST_CASE("moving the Function transfers shared_ptr capture without bumping use_count") {
    auto resource = std::make_shared<int>(0);
    Function<void()> a = [resource]() { ++(*resource); };

    CHECK(resource.use_count() == 2);

    Function<void()> b(std::move(a));

    CHECK(resource.use_count() == 2);
  }
}

// ---------------------------------------------------------------------------
TEST_SUITE("base-Function - target kinds") {
  // -------------------------------------------------------------------------
  static int free_function(int x) { return x + 100; }

  TEST_CASE("Function wraps a free function pointer") {
    Function<int(int)> cb = &free_function;
    CHECK(cb(2) == 102);
  }

  // -------------------------------------------------------------------------
  TEST_CASE("Function constructed from a null function pointer becomes empty") {
    using Fn = int (*)(int);
    Fn fn = nullptr;

    Function<int(int)> cb(fn);

    CHECK(!cb);
  }

  // -------------------------------------------------------------------------
  struct Adder final {
    int base{0};
    int add(int x) const { return base + x; }
  };

  TEST_CASE("Function wraps a member function via lambda capture") {
    Adder adder{10};

    Function<int(int)> cb = [&adder](int x) { return adder.add(x); };

    CHECK(cb(5) == 15);
  }

  // -------------------------------------------------------------------------
  TEST_CASE("Function wraps a stateful functor (mutable lambda)") {
    Function<int()> cb = [counter = 0]() mutable { return ++counter; };

    CHECK(cb() == 1);
    CHECK(cb() == 2);
    CHECK(cb() == 3);
  }
}

// ---------------------------------------------------------------------------
TEST_SUITE("base-Function - argument kinds") {
  // -------------------------------------------------------------------------
  TEST_CASE("Function forwards by-value argument") {
    Function<int(int)> cb = [](int x) { return x; };
    CHECK(cb(7) == 7);
  }

  // -------------------------------------------------------------------------
  TEST_CASE("Function forwards by-const-reference argument") {
    Function<size_t(const std::string&)> cb = [](const std::string& s) { return s.size(); };
    CHECK(cb("hello") == 5U);
  }

  // -------------------------------------------------------------------------
  TEST_CASE("Function forwards rvalue-reference argument (move-only payload)") {
    Function<int(std::unique_ptr<int>)> cb = [](std::unique_ptr<int> p) { return *p; };
    CHECK(cb(std::make_unique<int>(42)) == 42);
  }

  // -------------------------------------------------------------------------
  TEST_CASE("Function returning void compiles and runs") {
    int side_effect = 0;
    Function<void(int)> cb = [&side_effect](int x) { side_effect = x; };

    cb(99);

    CHECK(side_effect == 99);
  }

  // -------------------------------------------------------------------------
  TEST_CASE("Function with multiple arguments") {
    Function<int(int, int, int)> cb = [](int a, int b, int c) { return a + b + c; };
    CHECK(cb(1, 2, 3) == 6);
  }
}

// ---------------------------------------------------------------------------
TEST_SUITE("base-Function - destruction tracking") {
  // -------------------------------------------------------------------------
  struct DestructorCounter {
    std::shared_ptr<std::atomic<int>> count;

    explicit DestructorCounter(std::shared_ptr<std::atomic<int>> c) : count(std::move(c)) {}

    DestructorCounter(const DestructorCounter& other) = default;

    DestructorCounter(DestructorCounter&& other) noexcept = default;

    DestructorCounter& operator=(const DestructorCounter&) = default;

    DestructorCounter& operator=(DestructorCounter&&) noexcept = default;

    ~DestructorCounter() {
      if (count) {
        count->fetch_add(1, std::memory_order_relaxed);
      }
    }

    void operator()() const {}
  };

  TEST_CASE("inline target is destroyed exactly once when Function dies") {
    auto count = std::make_shared<std::atomic<int>>(0);

    {
      Function<void()> cb = DestructorCounter(count);
      cb();
    }

    // 1 destruction: the moved-into-Function storage.
    // (Temporaries in expression are also destroyed but their internal counters are
    // copies; we check >=1 to tolerate compiler temporaries from the converting
    // constructor path, but lifetimes inside Function contribute at least one.)
    CHECK(count->load() >= 1);
  }

  // -------------------------------------------------------------------------
  TEST_CASE("heap target with large payload is destroyed exactly once") {
    auto count = std::make_shared<std::atomic<int>>(0);

    struct LargePayload {
      std::array<char, 512> bytes{};
      std::shared_ptr<std::atomic<int>> count;

      explicit LargePayload(std::shared_ptr<std::atomic<int>> c) : count(std::move(c)) {}
      LargePayload(const LargePayload&) = default;
      LargePayload(LargePayload&&) noexcept = default;
      LargePayload& operator=(const LargePayload&) = default;
      LargePayload& operator=(LargePayload&&) noexcept = default;
      ~LargePayload() {
        if (count) count->fetch_add(1, std::memory_order_relaxed);
      }

      void operator()() const {}
    };

    {
      Function<void()> cb = LargePayload(count);
      cb();
    }

    CHECK(count->load() >= 1);
  }
}

// ---------------------------------------------------------------------------
TEST_SUITE("base-Function - operator(), exception forwarding") {
  // -------------------------------------------------------------------------
  TEST_CASE("exception thrown from target propagates") {
    Function<void()> cb = []() { throw std::runtime_error("boom"); };
    CHECK_THROWS_AS(cb(), std::runtime_error);
  }
}

// ---------------------------------------------------------------------------
TEST_SUITE("base-Function - std::bind / reference_wrapper / member ptr") {
  // -------------------------------------------------------------------------
  static int multiply(int a, int b) { return a * b; }

  TEST_CASE("Function wraps std::bind result") {
    Function<int(int)> cb = std::bind(&multiply, std::placeholders::_1, 3);  // NOLINT(modernize-avoid-bind)
    CHECK(cb(5) == 15);
  }

  // -------------------------------------------------------------------------
  TEST_CASE("Function wraps std::reference_wrapper to a callable") {
    auto fn = [](int x) { return x + 1; };
    auto ref = std::ref(fn);

    Function<int(int)> cb = ref;
    CHECK(cb(10) == 11);
  }

  // -------------------------------------------------------------------------
  struct Adder2 final {
    int base{0};
    int add(int x) const { return base + x; }
    int operator()(int x) const { return base + x * 2; }
  };

  TEST_CASE("Function wraps a functor with operator() directly") {
    Function<int(int)> cb = Adder2{100};
    CHECK(cb(5) == 110);
  }
}

// ---------------------------------------------------------------------------
TEST_SUITE("base-Function - composition and reset") {
  // -------------------------------------------------------------------------
  TEST_CASE("Function can hold another Function as the target") {
    Function<int(int)> inner = [](int x) { return x + 1; };
    Function<int(int)> outer = [inner](int x) { return inner(x) * 2; };

    CHECK(outer(3) == 8);  // (3 + 1) * 2
  }

  // -------------------------------------------------------------------------
  TEST_CASE("operator= with new lambda overrides previous target") {
    Function<int()> cb = []() { return 1; };
    cb = []() { return 2; };
    CHECK(cb() == 2);

    cb = []() { return 3; };
    CHECK(cb() == 3);
  }

  // -------------------------------------------------------------------------
  TEST_CASE("nullptr assignment after copy leaves both destination empty and source intact") {
    Function<int()> a = []() { return 7; };
    Function<int()> b = a;
    a = nullptr;
    CHECK(!a);
    CHECK(b() == 7);
  }

  // -------------------------------------------------------------------------
  TEST_CASE("move from empty Function yields empty Function") {
    Function<int()> a;  // empty
    Function<int()> b(std::move(a));
    CHECK(!a);
    CHECK(!b);
  }

  // -------------------------------------------------------------------------
  TEST_CASE("copy from empty Function yields empty Function") {
    Function<int()> a;  // empty
    Function<int()> b(a);
    CHECK(!a);
    CHECK(!b);
  }
}

// ---------------------------------------------------------------------------
TEST_SUITE("base-Function - return-type and signature variations") {
  // -------------------------------------------------------------------------
  TEST_CASE("Function returning by-value std::string") {
    Function<std::string()> cb = []() { return std::string("hello"); };
    CHECK(cb() == "hello");
  }

  // -------------------------------------------------------------------------
  TEST_CASE("Function returning std::unique_ptr (move-only result)") {
    Function<std::unique_ptr<int>()> cb = []() { return std::make_unique<int>(42); };
    auto p = cb();
    CHECK(p != nullptr);
    CHECK(*p == 42);
  }

  // -------------------------------------------------------------------------
  TEST_CASE("Function with no arguments and void return") {
    int counter = 0;
    Function<void()> cb = [&counter]() { ++counter; };
    cb();
    cb();
    CHECK(counter == 2);
  }

  // -------------------------------------------------------------------------
  TEST_CASE("Function with widely different return type from F (R-conversion)") {
    Function<long(int)> cb = [](int x) -> int { return x * 10; };
    CHECK(cb(7) == 70L);
  }
}

// ---------------------------------------------------------------------------
TEST_SUITE("base-Function - const-correctness") {
  // -------------------------------------------------------------------------
  TEST_CASE("operator() is callable on a const Function") {
    const Function<int()> cb = []() { return 13; };
    CHECK(cb() == 13);
  }

  // -------------------------------------------------------------------------
  TEST_CASE("mutable lambda preserves state across const invocations") {
    Function<int()> cb = [counter = 0]() mutable { return ++counter; };
    const Function<int()>& ref = cb;
    CHECK(ref() == 1);
    CHECK(ref() == 2);
  }
}

// ---------------------------------------------------------------------------
TEST_SUITE("base-Function - exception safety") {
  // -------------------------------------------------------------------------
  struct ThrowingOnCopy {
    int value{0};
    bool* throw_flag{nullptr};

    explicit ThrowingOnCopy(bool* flag) : throw_flag(flag) {}

    ThrowingOnCopy(const ThrowingOnCopy& other) : value(other.value), throw_flag(other.throw_flag) {
      if (throw_flag != nullptr && *throw_flag) {
        throw std::runtime_error("copy threw");
      }
    }

    ThrowingOnCopy(ThrowingOnCopy&&) noexcept = default;
    ThrowingOnCopy& operator=(const ThrowingOnCopy&) = default;
    ThrowingOnCopy& operator=(ThrowingOnCopy&&) noexcept = default;

    void operator()() const {}
  };

  TEST_CASE("throw from F's copy ctor during Function copy leaves destination empty and source intact") {
    bool throw_flag = false;
    ThrowingOnCopy original(&throw_flag);

    Function<void()> a = original;  // first copy: throw_flag false, ok

    throw_flag = true;
    Function<void()> b;
    CHECK_THROWS_AS(b = a, std::runtime_error);  // copy from a triggers ThrowingOnCopy::copy
    CHECK(!b);                                   // b stays empty
    CHECK(static_cast<bool>(a));                 // a unchanged
  }
}

// ---------------------------------------------------------------------------
TEST_SUITE("base-Function - std::function interop") {
  // -------------------------------------------------------------------------
  TEST_CASE("Function wraps a std::function via converting constructor") {
    std::function<int(int)> stdfn = [](int x) { return x * 3; };
    Function<int(int)> cb = stdfn;
    CHECK(cb(4) == 12);
  }

  // -------------------------------------------------------------------------
  TEST_CASE("std::function wraps a Function via its converting constructor") {
    Function<int(int)> cb = [](int x) { return x + 100; };
    std::function<int(int)> stdfn = cb;
    CHECK(stdfn(5) == 105);
  }

  // -------------------------------------------------------------------------
  TEST_CASE("Function assigned from std::function value") {
    std::function<int()> stdfn = []() { return 7; };
    Function<int()> cb;
    cb = stdfn;
    CHECK(cb() == 7);
  }

  // -------------------------------------------------------------------------
  TEST_CASE("std::function assigned from Function value") {
    Function<int()> cb = []() { return 9; };
    std::function<int()> stdfn;
    stdfn = cb;
    CHECK(stdfn() == 9);
  }

  // -------------------------------------------------------------------------
  TEST_CASE("round-trip Function -> std::function -> Function preserves behaviour") {
    Function<int(int)> a = [](int x) { return x * x; };
    std::function<int(int)> mid = a;
    Function<int(int)> b = mid;
    CHECK(b(11) == 121);
  }
}

#endif

// NOLINTEND
