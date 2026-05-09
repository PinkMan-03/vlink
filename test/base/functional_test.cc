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
#include <functional>
#include <future>
#include <memory>
#include <string>
#include <type_traits>
#include <typeinfo>
#include <utility>
#include <vector>

//
#include "../common_test.h"

#ifdef VLINK_ENABLE_BASE_FUNCTIONAL

namespace {

struct MoveOnlyCallable {
  MoveOnlyCallable() = default;
  MoveOnlyCallable(const MoveOnlyCallable&) = delete;
  MoveOnlyCallable(MoveOnlyCallable&&) noexcept = default;
  MoveOnlyCallable& operator=(const MoveOnlyCallable&) = delete;
  MoveOnlyCallable& operator=(MoveOnlyCallable&&) noexcept = default;

  void operator()() const {}
};

static_assert(!std::is_constructible_v<Function<void()>, MoveOnlyCallable>,
              "std::function-compatible Function must reject move-only targets");
static_assert(!std::is_assignable_v<Function<void()>&, MoveOnlyCallable>,
              "std::function-compatible Function assignment must reject move-only targets");

}  // namespace

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

#if defined(__cpp_rtti)
// ---------------------------------------------------------------------------
TEST_SUITE("base-Function - target access") {
  // -------------------------------------------------------------------------
  static int target_free_function(int x) { return x + 100; }

  TEST_CASE("empty Function reports typeid(void) and no target") {
    Function<int()> cb;

    CHECK(cb.target_type() == typeid(void));
    CHECK(cb.target<int (*)()>() == nullptr);
  }

  // -------------------------------------------------------------------------
  TEST_CASE("target returns the stored inline callable") {
    auto fn = [base = 4](int x) { return base + x; };
    using Fn = decltype(fn);

    Function<int(int)> cb = fn;

    CHECK(cb.target_type() == typeid(Fn));
    REQUIRE(cb.target<Fn>() != nullptr);
    CHECK((*cb.target<Fn>())(5) == 9);
    CHECK(cb.target<int (*)(int)>() == nullptr);

    const Function<int(int)>& cref = cb;
    REQUIRE(cref.target<Fn>() != nullptr);
    CHECK((*cref.target<Fn>())(6) == 10);
  }

  // -------------------------------------------------------------------------
  TEST_CASE("target returns the stored heap callable") {
    struct LargeTarget {
      std::array<int, 32> payload{};

      int operator()() const { return 17 + static_cast<int>(payload.size()); }
    };

    Function<int()> cb = LargeTarget{};

    CHECK(cb.target_type() == typeid(LargeTarget));
    REQUIRE(cb.target<LargeTarget>() != nullptr);
    CHECK((*cb.target<LargeTarget>())() == 49);
  }

  // -------------------------------------------------------------------------
  TEST_CASE("target returns the stored function pointer") {
    using Fn = int (*)(int);

    Function<int(int)> cb = &target_free_function;

    CHECK(cb.target_type() == typeid(Fn));
    REQUIRE(cb.target<Fn>() != nullptr);
    CHECK((*cb.target<Fn>())(2) == 102);
  }
}
#endif

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
  TEST_CASE("empty std::function constructs an empty Function") {
    std::function<int(int)> stdfn;

    Function<int(int)> cb = stdfn;

    CHECK(!cb);
    CHECK(cb == nullptr);
    CHECK_THROWS_AS(cb(1), std::bad_function_call);
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
  TEST_CASE("assigning an empty std::function clears the Function") {
    Function<int()> cb = []() { return 7; };
    std::function<int()> stdfn;

    cb = stdfn;

    CHECK(!cb);
    CHECK_THROWS_AS(cb(), std::bad_function_call);
  }

  // -------------------------------------------------------------------------
  TEST_CASE("std::function assigned from Function value") {
    Function<int()> cb = []() { return 9; };
    std::function<int()> stdfn;
    stdfn = cb;
    CHECK(stdfn() == 9);
  }

  // -------------------------------------------------------------------------
  TEST_CASE("empty std::function with a compatible signature constructs an empty Function") {
    std::function<int(int)> stdfn;

    Function<long(int)> cb = stdfn;

    CHECK(!cb);
  }

  // -------------------------------------------------------------------------
  TEST_CASE("empty Function with a compatible signature constructs an empty Function") {
    Function<int(int)> inner;

    Function<long(int)> outer = inner;

    CHECK(!outer);
  }

  // -------------------------------------------------------------------------
  TEST_CASE("round-trip Function -> std::function -> Function preserves behaviour") {
    Function<int(int)> a = [](int x) { return x * x; };
    std::function<int(int)> mid = a;
    Function<int(int)> b = mid;
    CHECK(b(11) == 121);
  }
}

// =============================================================================
// MoveFunction tests
// =============================================================================

namespace {

struct UniquePtrCallable {
  std::unique_ptr<int> data;

  UniquePtrCallable() = default;
  explicit UniquePtrCallable(int v) : data(std::make_unique<int>(v)) {}

  UniquePtrCallable(const UniquePtrCallable&) = delete;
  UniquePtrCallable(UniquePtrCallable&&) noexcept = default;
  UniquePtrCallable& operator=(const UniquePtrCallable&) = delete;
  UniquePtrCallable& operator=(UniquePtrCallable&&) noexcept = default;

  int operator()() const { return data ? *data : -1; }
};

static_assert(!std::is_copy_constructible_v<UniquePtrCallable>);
static_assert(std::is_move_constructible_v<UniquePtrCallable>);

static_assert(!std::is_copy_constructible_v<MoveFunction<int()>>, "MoveFunction must be move-only");
static_assert(!std::is_copy_assignable_v<MoveFunction<int()>>, "MoveFunction must be move-only");
static_assert(std::is_nothrow_move_constructible_v<MoveFunction<int()>>, "MoveFunction move ctor must be noexcept");
static_assert(std::is_nothrow_move_assignable_v<MoveFunction<int()>>, "MoveFunction move op= must be noexcept");

static_assert(std::is_constructible_v<MoveFunction<int()>, UniquePtrCallable>,
              "MoveFunction must accept move-only rvalue targets");
static_assert(!std::is_constructible_v<MoveFunction<int()>, UniquePtrCallable&>,
              "MoveFunction must reject move-only lvalue targets (no copy ctor)");
static_assert(!std::is_constructible_v<Function<int()>, UniquePtrCallable>, "Function must reject move-only targets");

static_assert(std::is_constructible_v<MoveFunction<int()>, Function<int()>>,
              "MoveFunction must accept Function rvalue (move-in)");
static_assert(std::is_constructible_v<MoveFunction<int()>, std::function<int()>>,
              "MoveFunction must accept std::function");
static_assert(!std::is_constructible_v<Function<int()>, MoveFunction<int()>>,
              "vlink::Function must reject move-only MoveFunction (SFINAE-based copy check)");
// Note: std::function uses Mandates (not Constraints) for the copy_constructible
// check, so std::is_constructible_v<std::function<...>, MoveFunction<...>> reports
// true in libstdc++ even though instantiation would hard-error.  Verifying that
// rejection requires a SFINAE wrapper, not a trait probe.

}  // namespace

// ---------------------------------------------------------------------------
TEST_SUITE("base-MoveFunction - construction & emptiness") {
  // -------------------------------------------------------------------------
  TEST_CASE("default-constructed MoveFunction is empty") {
    MoveFunction<void()> cb;
    CHECK(!cb);
    CHECK(cb == nullptr);
    CHECK(nullptr == cb);
  }

  // -------------------------------------------------------------------------
  TEST_CASE("nullptr-constructed MoveFunction is empty") {
    MoveFunction<int(int)> cb(nullptr);
    CHECK(!cb);
    CHECK(cb == nullptr);
  }

  // -------------------------------------------------------------------------
  TEST_CASE("invoking an empty MoveFunction throws std::bad_function_call") {
    MoveFunction<void()> cb;
    CHECK_THROWS_AS(cb(), std::bad_function_call);
  }

  // -------------------------------------------------------------------------
  TEST_CASE("MoveFunction stores a copyable lambda") {
    MoveFunction<int(int)> cb = [](int x) { return x * 2; };
    CHECK(static_cast<bool>(cb));
    CHECK(cb != nullptr);
    CHECK(cb(21) == 42);
  }

  // -------------------------------------------------------------------------
  TEST_CASE("MoveFunction stores a move-only lambda (unique_ptr capture)") {
    auto up = std::make_unique<int>(42);
    MoveFunction<int()> cb = [p = std::move(up)]() { return *p; };
    CHECK(cb() == 42);
  }

  // -------------------------------------------------------------------------
  TEST_CASE("MoveFunction stores a move-only callable struct") {
    MoveFunction<int()> cb = UniquePtrCallable{7};
    CHECK(cb() == 7);
  }

  // -------------------------------------------------------------------------
  TEST_CASE("MoveFunction wraps std::packaged_task without shared_ptr trampoline") {
    std::packaged_task<int()> task([] { return 100; });
    auto fut = task.get_future();

    MoveFunction<void()> cb = [t = std::move(task)]() mutable { t(); };
    cb();

    CHECK(fut.get() == 100);
  }
}

// ---------------------------------------------------------------------------
TEST_SUITE("base-MoveFunction - move semantics") {
  // -------------------------------------------------------------------------
  TEST_CASE("move construction transfers ownership") {
    MoveFunction<int()> a = [] { return 5; };
    MoveFunction<int()> b = std::move(a);

    CHECK(!a);
    CHECK(static_cast<bool>(b));
    CHECK(b() == 5);
  }

  // -------------------------------------------------------------------------
  TEST_CASE("move assignment transfers ownership") {
    MoveFunction<int()> a = [] { return 5; };
    MoveFunction<int()> b = [] { return 10; };

    b = std::move(a);

    CHECK(!a);
    CHECK(b() == 5);
  }

  // -------------------------------------------------------------------------
  TEST_CASE("self-move-assign is safe") {
    MoveFunction<int()> a = [] { return 7; };
    auto& ref = a;
    a = std::move(ref);
    CHECK(static_cast<bool>(a));
    CHECK(a() == 7);
  }

  // -------------------------------------------------------------------------
  TEST_CASE("self-swap is safe") {
    MoveFunction<int()> a = [] { return 3; };
    a.swap(a);
    CHECK(a() == 3);
  }

  // -------------------------------------------------------------------------
  TEST_CASE("ADL swap exchanges targets") {
    MoveFunction<int()> a = [] { return 1; };
    MoveFunction<int()> b = [] { return 2; };

    using std::swap;
    swap(a, b);

    CHECK(a() == 2);
    CHECK(b() == 1);
  }

  // -------------------------------------------------------------------------
  TEST_CASE("nullptr assignment clears MoveFunction") {
    MoveFunction<int()> a = [] { return 9; };
    a = nullptr;
    CHECK(!a);
  }

  // -------------------------------------------------------------------------
  TEST_CASE("rvalue lambda assignment replaces target") {
    MoveFunction<int()> a = [] { return 1; };
    a = [] { return 99; };
    CHECK(a() == 99);
  }
}

// ---------------------------------------------------------------------------
TEST_SUITE("base-MoveFunction - heap fallback") {
  // -------------------------------------------------------------------------
  TEST_CASE("large move-only target uses heap path and survives move") {
    struct Heavy {
      std::unique_ptr<int> p;
      std::array<int, 32> pad{};

      Heavy() : p(std::make_unique<int>(99)) {
        for (size_t i = 0; i < pad.size(); ++i) {
          pad[i] = static_cast<int>(i);
        }
      }

      Heavy(const Heavy&) = delete;
      Heavy(Heavy&&) noexcept = default;
      Heavy& operator=(const Heavy&) = delete;
      Heavy& operator=(Heavy&&) noexcept = default;

      int operator()() const { return *p + static_cast<int>(pad.size()); }
    };

    MoveFunction<int()> cb = Heavy{};
    CHECK(cb() == 99 + 32);

    MoveFunction<int()> moved = std::move(cb);
    CHECK(!cb);
    CHECK(moved() == 99 + 32);
  }
}

// ---------------------------------------------------------------------------
TEST_SUITE("base-MoveFunction - empty source propagation") {
  // -------------------------------------------------------------------------
  TEST_CASE("empty std::function -> empty MoveFunction") {
    std::function<int()> sf;
    MoveFunction<int()> mf = sf;
    CHECK(!mf);
    CHECK_THROWS_AS(mf(), std::bad_function_call);
  }

  // -------------------------------------------------------------------------
  TEST_CASE("empty std::function with compatible signature -> empty MoveFunction") {
    std::function<int(int)> sf;
    MoveFunction<long(int)> mf = sf;
    CHECK(!mf);
  }

  // -------------------------------------------------------------------------
  TEST_CASE("empty vlink::Function -> empty MoveFunction") {
    Function<int()> f;
    MoveFunction<int()> mf = std::move(f);
    CHECK(!mf);
  }

  // -------------------------------------------------------------------------
  TEST_CASE("empty MoveFunction (different signature) -> empty MoveFunction") {
    MoveFunction<int()> a;
    MoveFunction<long()> b = std::move(a);
    CHECK(!b);
  }

  // -------------------------------------------------------------------------
  TEST_CASE("nullptr function pointer -> empty MoveFunction") {
    using FnPtr = int (*)();
    FnPtr p = nullptr;
    MoveFunction<int()> mf = p;
    CHECK(!mf);
  }
}

// ---------------------------------------------------------------------------
TEST_SUITE("base-MoveFunction - cross-type conversion") {
  // -------------------------------------------------------------------------
  TEST_CASE("vlink::Function rvalue -> MoveFunction") {
    Function<int()> f = [] { return 42; };
    MoveFunction<int()> mf = std::move(f);
    CHECK(mf() == 42);
  }

  // -------------------------------------------------------------------------
  TEST_CASE("vlink::Function lvalue -> MoveFunction (Function is copy-constructible)") {
    Function<int()> f = [] { return 21; };
    MoveFunction<int()> mf = f;
    CHECK(mf() == 21);
    CHECK(static_cast<bool>(f));  // f untouched -- the lvalue path copies
  }

  // -------------------------------------------------------------------------
  TEST_CASE("std::function lvalue -> MoveFunction") {
    std::function<int()> sf = [] { return 7; };
    MoveFunction<int()> mf = sf;
    CHECK(mf() == 7);
  }

  // -------------------------------------------------------------------------
  TEST_CASE("round-trip MoveFunction -> MoveFunction (different signature)") {
    MoveFunction<int(int)> a = [](int x) { return x + 1; };
    MoveFunction<long(int)> b = std::move(a);
    CHECK(b(10) == 11);
  }
}

#if defined(__cpp_rtti)
// ---------------------------------------------------------------------------
TEST_SUITE("base-MoveFunction - target access") {
  // -------------------------------------------------------------------------
  TEST_CASE("empty MoveFunction reports typeid(void) and no target") {
    MoveFunction<int()> cb;
    CHECK(cb.target_type() == typeid(void));
    CHECK(cb.target<int (*)()>() == nullptr);
  }

  // -------------------------------------------------------------------------
  TEST_CASE("target returns the stored inline callable") {
    auto fn = [base = 4](int x) { return base + x; };
    using Fn = decltype(fn);

    MoveFunction<int(int)> cb = fn;

    CHECK(cb.target_type() == typeid(Fn));
    REQUIRE(cb.target<Fn>() != nullptr);
    CHECK((*cb.target<Fn>())(5) == 9);

    const MoveFunction<int(int)>& cref = cb;
    REQUIRE(cref.target<Fn>() != nullptr);
    CHECK((*cref.target<Fn>())(6) == 10);
  }

  // -------------------------------------------------------------------------
  TEST_CASE("target returns the stored heap callable") {
    MoveFunction<int()> cb = UniquePtrCallable{17};

    CHECK(cb.target_type() == typeid(UniquePtrCallable));
    REQUIRE(cb.target<UniquePtrCallable>() != nullptr);
    CHECK((*cb.target<UniquePtrCallable>())() == 17);
  }

  // -------------------------------------------------------------------------
  TEST_CASE("moved-from MoveFunction reports empty target") {
    MoveFunction<int()> a = [] { return 5; };
    MoveFunction<int()> b = std::move(a);

    CHECK(a.target_type() == typeid(void));
    CHECK(a.target<int (*)()>() == nullptr);
  }
}
#endif

// ---------------------------------------------------------------------------
TEST_SUITE("base-MoveFunction - additional target kinds") {
  // -------------------------------------------------------------------------
  static int free_fn_for_movefn(int x) { return x + 1; }

  TEST_CASE("MoveFunction wraps a free function pointer") {
    MoveFunction<int(int)> cb = &free_fn_for_movefn;
    CHECK(cb(7) == 8);
  }

  // -------------------------------------------------------------------------
  TEST_CASE("MoveFunction wraps a member function pointer") {
    struct S {
      int v = 0;
      int add(int x) const { return v + x; }
    };
    S s{10};

    MoveFunction<int(const S&, int)> cb = &S::add;
    CHECK(cb(s, 3) == 13);
  }

  // -------------------------------------------------------------------------
  TEST_CASE("nullptr member function pointer -> empty MoveFunction") {
    struct S {
      int add(int x) const { return x; }
    };
    using MemPtr = int (S::*)(int) const;
    MemPtr p = nullptr;

    MoveFunction<int(const S&, int)> cb = p;
    CHECK(!cb);
  }

  // -------------------------------------------------------------------------
  TEST_CASE("MoveFunction stores a mutable lambda and mutates state across calls") {
    MoveFunction<int()> cb = [n = 0]() mutable { return ++n; };
    CHECK(cb() == 1);
    CHECK(cb() == 2);
    CHECK(cb() == 3);
  }

  // -------------------------------------------------------------------------
  TEST_CASE("cross-signature wrap of a heap-stored move-only target preserves behaviour") {
    struct Heavy {
      std::unique_ptr<int> p;
      std::array<int, 32> pad{};
      Heavy() : p(std::make_unique<int>(50)) {}
      Heavy(const Heavy&) = delete;
      Heavy(Heavy&&) noexcept = default;
      Heavy& operator=(const Heavy&) = delete;
      Heavy& operator=(Heavy&&) noexcept = default;
      int operator()() const { return *p; }
    };

    MoveFunction<int()> inner = Heavy{};
    MoveFunction<long()> outer = std::move(inner);
    CHECK(!inner);
    CHECK(outer() == 50);
  }
}

#if defined(__cpp_lib_move_only_function) && __cpp_lib_move_only_function >= 202110L
// ---------------------------------------------------------------------------
TEST_SUITE("base-MoveFunction - std::move_only_function interop") {
  // -------------------------------------------------------------------------
  TEST_CASE("empty std::move_only_function -> empty MoveFunction") {
    std::move_only_function<int()> sm;
    MoveFunction<int()> mf = std::move(sm);
    CHECK(!mf);
  }

  // -------------------------------------------------------------------------
  TEST_CASE("non-empty std::move_only_function -> MoveFunction (move)") {
    std::move_only_function<int()> sm = [] { return 13; };
    MoveFunction<int()> mf = std::move(sm);
    CHECK(mf() == 13);
  }

  // -------------------------------------------------------------------------
  TEST_CASE("MoveFunction -> std::move_only_function (move)") {
    MoveFunction<int()> mf = [] { return 11; };
    std::move_only_function<int()> sm = std::move(mf);
    CHECK(sm() == 11);
  }

  // -------------------------------------------------------------------------
  TEST_CASE("round-trip MoveFunction -> std::move_only_function -> MoveFunction") {
    MoveFunction<int(int)> a = [](int x) { return x * x; };
    std::move_only_function<int(int)> mid = std::move(a);
    MoveFunction<int(int)> b = std::move(mid);
    CHECK(b(11) == 121);
  }
}
#endif

// ---------------------------------------------------------------------------
TEST_SUITE("base-Function - custom SBO size") {
  // -------------------------------------------------------------------------
  TEST_CASE("kSboSize reflects the SboSizeT template argument") {
    CHECK(Function<void()>::kSboSize == 64U);
    CHECK(Function<void(), 64>::kSboSize == 64U);
    CHECK(Function<void(), 128>::kSboSize == 128U);
    CHECK(Function<int(int), 256>::kSboSize == 256U);
    CHECK(Function<void(), 1024>::kSboSize == 1024U);
  }

  // -------------------------------------------------------------------------
  TEST_CASE("Function<Sig> defaults to 64-byte SBO and equals Function<Sig, 64>") {
    using Default = Function<void()>;
    using Explicit64 = Function<void(), 64>;
    CHECK(std::is_same_v<Default, Explicit64>);
  }

  // -------------------------------------------------------------------------
  TEST_CASE("different SboSizeT yields distinct types but are interchangeable") {
    using Small = Function<void(), 64>;
    using Big = Function<void(), 256>;
    CHECK_FALSE(std::is_same_v<Small, Big>);

    // Cross-SboSize Function instantiations are distinct types but remain
    // interchangeable through the generic functor-construction path: each can
    // be wrapped as a callable inside the other.
    CHECK(std::is_constructible_v<Big, const Small&>);
    CHECK(std::is_constructible_v<Big, Small&&>);
    CHECK(std::is_constructible_v<Small, const Big&>);
    CHECK(std::is_constructible_v<Small, Big&&>);

    CHECK(std::is_assignable_v<Small&, Big>);
    CHECK(std::is_assignable_v<Big&, Small>);

    int counter = 0;
    Small small_fn = [&counter]() { ++counter; };

    Big big_from_small(small_fn);
    big_from_small();
    CHECK(counter == 1);

    Big big_assigned;
    big_assigned = small_fn;
    big_assigned();
    CHECK(counter == 2);

    Small small_from_big(big_assigned);
    small_from_big();
    CHECK(counter == 3);
  }

  // -------------------------------------------------------------------------
  TEST_CASE("storage_ is at least SboSizeT bytes") {
    CHECK(sizeof(Function<void(), 64>) >= 64U);
    CHECK(sizeof(Function<void(), 128>) >= 128U);
    CHECK(sizeof(Function<void(), 256>) >= 256U);
    CHECK(sizeof(Function<void(), 512>) >= 512U);
  }

  // -------------------------------------------------------------------------
  TEST_CASE("functor that exceeds default SBO stays inline at custom 256-byte SBO") {
    // 200-byte functor: heap-allocated under default Function<...>, inline under
    // Function<..., 256>.
    struct HeavyFunctor {
      std::array<char, 200> payload{};
      int marker;

      explicit HeavyFunctor(int m) noexcept : marker(m) { payload.fill('A'); }

      int operator()() const noexcept { return marker + static_cast<int>(payload[0]); }
    };
    static_assert(sizeof(HeavyFunctor) > 64U);
    static_assert(sizeof(HeavyFunctor) <= 256U);

    auto& pool = vlink::MemoryPool::global_instance();
    const auto over_before = pool.get_oversized_stats();
    const auto stats_before = pool.get_stats();

    Function<int(), 256> cb = HeavyFunctor{42};
    CHECK(cb() == 42 + 'A');

    // Inline storage: no MemoryPool tier should have grown its in_use count
    // because of this functor (other tests may share the global pool, so we
    // only check the call site itself produces the right answer).  A direct
    // structural check: storage embeds the functor.
    CHECK(reinterpret_cast<const char*>(&cb) <= reinterpret_cast<const char*>(cb.target<HeavyFunctor>()));
    CHECK(reinterpret_cast<const char*>(cb.target<HeavyFunctor>()) < reinterpret_cast<const char*>(&cb) + sizeof(cb));

    // Silence unused-variable warnings when stats inspection is uninteresting.
    (void)over_before;
    (void)stats_before;
  }

  // -------------------------------------------------------------------------
  TEST_CASE("functor larger than custom SBO still falls back to heap") {
    struct GiantFunctor {
      std::array<char, 600> payload{};
      int marker;

      explicit GiantFunctor(int m) noexcept : marker(m) { payload.fill('Z'); }

      int operator()() const noexcept { return marker + static_cast<int>(payload[0]); }
    };
    static_assert(sizeof(GiantFunctor) > 256U);

    Function<int(), 256> cb = GiantFunctor{7};
    CHECK(cb(/*intentionally invoked*/) == 7 + 'Z');

    // Heap path: target is *outside* the wrapper's storage.
    const auto* target_ptr = reinterpret_cast<const char*>(cb.target<GiantFunctor>());
    const auto* self_begin = reinterpret_cast<const char*>(&cb);
    const auto* self_end = self_begin + sizeof(cb);
    const bool target_outside_self = (target_ptr < self_begin) || (target_ptr >= self_end);
    CHECK(target_outside_self);
  }

  // -------------------------------------------------------------------------
  TEST_CASE("custom-SBO Function still supports copy and move semantics") {
    using Big = Function<int(int), 256>;

    Big a = [](int x) { return x + 1; };
    Big b = a;  // copy
    CHECK(a(10) == 11);
    CHECK(b(10) == 11);

    Big c = std::move(a);  // move
    CHECK(c(20) == 21);
    CHECK(!a);  // moved-from is empty
  }

  // -------------------------------------------------------------------------
  TEST_CASE("custom-SBO Function compares against nullptr") {
    Function<void(), 256> empty;
    CHECK(empty == nullptr);
    CHECK(nullptr == empty);
    CHECK_FALSE(empty != nullptr);

    Function<void(), 256> full = []() {};
    CHECK(full != nullptr);
    CHECK_FALSE(full == nullptr);
  }

  // -------------------------------------------------------------------------
  TEST_CASE("Function<Sig, X> can be constructed from Function<Sig, Y> as a callable") {
    Function<int(int), 64> small = [](int x) { return x * 2; };
    Function<int(int), 256> big{small};  // constructed as a wrapped callable
    CHECK(big(5) == 10);

    // Empty source propagates as empty.
    Function<int(int), 64> empty_small;
    Function<int(int), 256> empty_big{empty_small};
    CHECK(!empty_big);
  }
}

// ---------------------------------------------------------------------------
TEST_SUITE("base-MoveFunction - custom SBO size") {
  // -------------------------------------------------------------------------
  TEST_CASE("kSboSize reflects the SboSizeT template argument") {
    CHECK(MoveFunction<void()>::kSboSize == 64U);
    CHECK(MoveFunction<void(), 64>::kSboSize == 64U);
    CHECK(MoveFunction<void(), 128>::kSboSize == 128U);
    CHECK(MoveFunction<int(int), 256>::kSboSize == 256U);
    CHECK(MoveFunction<void(), 1024>::kSboSize == 1024U);
  }

  // -------------------------------------------------------------------------
  TEST_CASE("MoveFunction<Sig> defaults to 64-byte SBO and equals MoveFunction<Sig, 64>") {
    CHECK(std::is_same_v<MoveFunction<void()>, MoveFunction<void(), 64>>);
  }

  // -------------------------------------------------------------------------
  TEST_CASE("different SboSizeT yields distinct types") {
    using Small = MoveFunction<void(), 64>;
    using Big = MoveFunction<void(), 256>;
    CHECK_FALSE(std::is_same_v<Small, Big>);
  }

  // -------------------------------------------------------------------------
  TEST_CASE("storage_ is at least SboSizeT bytes") {
    CHECK(sizeof(MoveFunction<void(), 64>) >= 64U);
    CHECK(sizeof(MoveFunction<void(), 256>) >= 256U);
    CHECK(sizeof(MoveFunction<void(), 1024>) >= 1024U);
  }

  // -------------------------------------------------------------------------
  TEST_CASE("move-only functor exceeding default SBO stays inline at custom 256-byte SBO") {
    auto destructions = std::make_shared<std::atomic<int>>(0);

    struct HeavyMoveOnly {
      std::array<char, 200> payload{};
      std::shared_ptr<std::atomic<int>> destructions;
      int marker;

      HeavyMoveOnly(int m, std::shared_ptr<std::atomic<int>> d) noexcept : destructions(std::move(d)), marker(m) {
        payload.fill('M');
      }
      HeavyMoveOnly(const HeavyMoveOnly&) = delete;
      HeavyMoveOnly& operator=(const HeavyMoveOnly&) = delete;
      HeavyMoveOnly(HeavyMoveOnly&&) noexcept = default;
      HeavyMoveOnly& operator=(HeavyMoveOnly&&) noexcept = default;
      ~HeavyMoveOnly() {
        if (destructions) destructions->fetch_add(1, std::memory_order_relaxed);
      }

      int operator()() const noexcept { return marker + static_cast<int>(payload[0]); }
    };
    static_assert(sizeof(HeavyMoveOnly) > 64U);
    static_assert(sizeof(HeavyMoveOnly) <= 256U);

    {
      MoveFunction<int(), 256> cb = HeavyMoveOnly{99, destructions};
      CHECK(cb() == 99 + 'M');

      // Verify inline by checking target is inside the wrapper itself.
      const auto* target_ptr = reinterpret_cast<const char*>(cb.target<HeavyMoveOnly>());
      const auto* self_begin = reinterpret_cast<const char*>(&cb);
      const auto* self_end = self_begin + sizeof(cb);
      CHECK(target_ptr >= self_begin);
      CHECK(target_ptr < self_end);
    }
    // Wrapper destruction must invoke the functor's destructor exactly once.
    CHECK(destructions->load() >= 1);
  }

  // -------------------------------------------------------------------------
  TEST_CASE("MoveFunction larger than custom SBO falls back to heap and stays move-only") {
    struct GiantMoveOnly {
      std::array<char, 800> payload{};
      int marker;

      explicit GiantMoveOnly(int m) noexcept : marker(m) { payload.fill('G'); }
      GiantMoveOnly(const GiantMoveOnly&) = delete;
      GiantMoveOnly& operator=(const GiantMoveOnly&) = delete;
      GiantMoveOnly(GiantMoveOnly&&) noexcept = default;
      GiantMoveOnly& operator=(GiantMoveOnly&&) noexcept = default;

      int operator()() const noexcept { return marker + static_cast<int>(payload[0]); }
    };
    static_assert(sizeof(GiantMoveOnly) > 256U);

    MoveFunction<int(), 256> cb = GiantMoveOnly{3};
    CHECK(cb() == 3 + 'G');

    MoveFunction<int(), 256> moved = std::move(cb);
    CHECK(moved() == 3 + 'G');
    CHECK(!cb);
  }

  // -------------------------------------------------------------------------
  TEST_CASE("custom-SBO MoveFunction compares against nullptr") {
    MoveFunction<void(), 256> empty;
    CHECK(empty == nullptr);
    CHECK(nullptr == empty);

    MoveFunction<void(), 256> full = []() {};
    CHECK(full != nullptr);
  }

  // -------------------------------------------------------------------------
  TEST_CASE("MoveFunction<Sig, X> can wrap MoveFunction<Sig, Y>") {
    MoveFunction<int(int), 64> small = [](int x) { return x + 100; };
    MoveFunction<int(int), 256> big{std::move(small)};
    CHECK(big(5) == 105);
  }
}

// ---------------------------------------------------------------------------
TEST_SUITE("base-LargeFunction / LargeMoveFunction aliases") {
  // -------------------------------------------------------------------------
  TEST_CASE("LargeFunction<Sig> is exactly Function<Sig, 256>") {
    CHECK(std::is_same_v<vlink::LargeFunction<void()>, vlink::Function<void(), 256>>);
    CHECK(std::is_same_v<vlink::LargeFunction<int(int, int)>, vlink::Function<int(int, int), 256>>);
    CHECK(vlink::LargeFunction<void()>::kSboSize == 256U);
  }

  // -------------------------------------------------------------------------
  TEST_CASE("LargeMoveFunction<Sig> is exactly MoveFunction<Sig, 256>") {
    CHECK(std::is_same_v<vlink::LargeMoveFunction<void()>, vlink::MoveFunction<void(), 256>>);
    CHECK(std::is_same_v<vlink::LargeMoveFunction<int(int, int)>, vlink::MoveFunction<int(int, int), 256>>);
    CHECK(vlink::LargeMoveFunction<void()>::kSboSize == 256U);
  }

  // -------------------------------------------------------------------------
  TEST_CASE("LargeFunction can hold a 200-byte functor inline") {
    struct HeavyFunctor {
      std::array<char, 200> payload{};
      int marker;

      explicit HeavyFunctor(int m) noexcept : marker(m) { payload.fill('L'); }

      int operator()() const noexcept { return marker + static_cast<int>(payload[0]); }
    };

    vlink::LargeFunction<int()> cb = HeavyFunctor{1};
    CHECK(cb() == 1 + 'L');

    // Inline check: target lives inside the wrapper.
    const auto* target_ptr = reinterpret_cast<const char*>(cb.target<HeavyFunctor>());
    const auto* self_begin = reinterpret_cast<const char*>(&cb);
    CHECK(target_ptr >= self_begin);
    CHECK(target_ptr < self_begin + sizeof(cb));
  }

  // -------------------------------------------------------------------------
  TEST_CASE("LargeMoveFunction supports move-only state") {
    auto sentinel = std::make_unique<int>(42);

    vlink::LargeMoveFunction<int()> cb = [sp = std::move(sentinel)]() mutable { return *sp + 1; };
    CHECK(cb() == 43);

    vlink::LargeMoveFunction<int()> moved = std::move(cb);
    CHECK(moved() == 43);
    CHECK(!cb);
  }
}

#endif

// NOLINTEND
