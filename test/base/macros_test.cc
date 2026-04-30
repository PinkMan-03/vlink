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

#include "./base/macros.h"

#include <doctest/doctest.h>

#include <atomic>
#include <stdexcept>
#include <type_traits>

//
#include "../common_test.h"

// ---------------------------------------------------------------------------
// Helpers used by multiple test cases
// ---------------------------------------------------------------------------

// A class that uses VLINK_DISALLOW_COPY_AND_ASSIGN
class NoCopyClass {
 public:
  NoCopyClass() = default;

  VLINK_DISALLOW_COPY_AND_ASSIGN(NoCopyClass)
};

// ---------------------------------------------------------------------------
// TEST SUITE: VLINK_DISALLOW_COPY_AND_ASSIGN
// ---------------------------------------------------------------------------

TEST_SUITE("base-Macros - VLINK_DISALLOW_COPY_AND_ASSIGN") {
  TEST_CASE("copy constructor is deleted") { CHECK(!std::is_copy_constructible_v<NoCopyClass>); }

  TEST_CASE("copy assignment is deleted") { CHECK(!std::is_copy_assignable_v<NoCopyClass>); }

  TEST_CASE("move constructor is suppressed (deleted copy suppresses implicit move)") {
    // When copy ctor/assignment are explicitly deleted, the compiler does NOT
    // generate implicit move operations, so the class is not move-constructible.
    CHECK(!std::is_move_constructible_v<NoCopyClass>);
  }
}

// ---------------------------------------------------------------------------
// TEST SUITE: VLINK_SINGLETON_DECLARE
// ---------------------------------------------------------------------------

// NOTE: We can only safely test the first call to get() because
// VLINK_SINGLETON_CHECK throws on a second instantiation.  Since the
// singleton is a function-static, we test the get() path once and verify
// that the returned reference is stable.

class MySingleton final {
  VLINK_SINGLETON_DECLARE(MySingleton)

 public:
  int value{0};

 private:
  MySingleton() = default;
};

TEST_SUITE("base-Macros - VLINK_SINGLETON_DECLARE") {
  TEST_CASE("get() returns the same reference each time") {
    MySingleton& a = MySingleton::get();
    MySingleton& b = MySingleton::get();
    CHECK(&a == &b);
  }

  TEST_CASE("singleton value is mutable via the reference") {
    MySingleton& s = MySingleton::get();
    s.value = 42;
    CHECK(MySingleton::get().value == 42);
    // Restore for cleanliness (static, persists across test cases)
    s.value = 0;
  }

  TEST_CASE("singleton is not copy-constructible") { CHECK(!std::is_copy_constructible_v<MySingleton>); }

  TEST_CASE("singleton is not copy-assignable") { CHECK(!std::is_copy_assignable_v<MySingleton>); }
}

// ---------------------------------------------------------------------------
// TEST SUITE: VLINK_MACRO_STRING_IFY / VLINK_MACRO_STRING_GET
// ---------------------------------------------------------------------------

#define MY_TEST_TOKEN hello
#define MY_NUM_TOKEN 12345

TEST_SUITE("base-Macros - stringify") {
  TEST_CASE("VLINK_MACRO_STRING_IFY stringifies a token literally") {
    const char* s = VLINK_MACRO_STRING_IFY(hello);
    CHECK(std::string(s) == "hello");
  }

  TEST_CASE("VLINK_MACRO_STRING_GET expands macro before stringifying") {
    // VLINK_MACRO_STRING_GET(MY_TEST_TOKEN) should produce "hello"
    const char* s = VLINK_MACRO_STRING_GET(MY_TEST_TOKEN);
    CHECK(std::string(s) == "hello");
  }

  TEST_CASE("VLINK_MACRO_STRING_GET with numeric macro") {
    const char* s = VLINK_MACRO_STRING_GET(MY_NUM_TOKEN);
    CHECK(std::string(s) == "12345");
  }
}

// ---------------------------------------------------------------------------
// TEST SUITE: VLIKELY / VUNLIKELY branch hints
// ---------------------------------------------------------------------------

TEST_SUITE("base-Macros - branch hints") {
  TEST_CASE("VLIKELY does not change truthiness of true expression") {
    bool val = true;

    if VLIKELY (val) {
      CHECK(true);
    } else {
      CHECK(false);
    }
  }

  TEST_CASE("VLIKELY does not change truthiness of false expression") {
    bool val = false;

    if VLIKELY (val) {
      CHECK(false);
    } else {
      CHECK(true);
    }
  }

  TEST_CASE("VUNLIKELY does not change truthiness of true expression") {
    bool val = true;

    if VUNLIKELY (val) {
      CHECK(true);
    } else {
      CHECK(false);
    }
  }

  TEST_CASE("VUNLIKELY does not change truthiness of false expression") {
    bool val = false;

    if VUNLIKELY (val) {
      CHECK(false);
    } else {
      CHECK(true);
    }
  }
}

// ---------------------------------------------------------------------------
// TEST SUITE: VLINK_EXPORT_AND_ALIGNED
// ---------------------------------------------------------------------------

// TEST_SUITE("base-Macros - alignment") {
//   TEST_CASE("VLINK_EXPORT_AND_ALIGNED produces correctly aligned type") {
//     struct VLINK_EXPORT_AND_ALIGNED(64) AlignedStruct {
//       char data[64];
//     };

//     CHECK(alignof(AlignedStruct) == 64u);
//   }
// }

// ---------------------------------------------------------------------------
// TEST SUITE: VLINK_ASSERT_CONSTANT
// ---------------------------------------------------------------------------

TEST_SUITE("base-Macros - constant assertion") {
  TEST_CASE("VLINK_ASSERT_CONSTANT on string literal does not fail compilation") {
    // If __builtin_constant_p is supported, this static_assert should pass.
    // We simply verify that the macro expands without error.
    VLINK_ASSERT_CONSTANT("constant string");
    CHECK(true);
  }
}

// NOLINTEND
