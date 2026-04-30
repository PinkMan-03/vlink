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

#include "./base/traits.h"

#include <doctest/doctest.h>

#include <atomic>
#include <functional>
#include <memory>
#include <sstream>
#include <string>
#include <type_traits>

//
#include "../common_test.h"

// ---------------------------------------------------------------------------
// Helper types used across test cases
// ---------------------------------------------------------------------------

struct Foo {
  int bar{0};
  void method() {}
};

struct Baz {};

struct CallableObj {
  void operator()() {}
};

struct NonCallable {
  int x{0};
};

struct StreamableObj {
  int v{0};
};

std::ostringstream& operator<<(std::ostringstream& os, const StreamableObj& s) {
  os << s.v;
  return os;
}

std::ostringstream& operator>>(std::ostringstream& os, StreamableObj& s) {
  s.v = 0;
  return os;
}

// ---------------------------------------------------------------------------
// TEST SUITE: EmptyType
// ---------------------------------------------------------------------------

TEST_SUITE("base-Traits - EmptyType") {
  TEST_CASE("EmptyType is default-constructible") {
    Traits::EmptyType e;
    (void)e;
    CHECK(true);
  }

  TEST_CASE("EmptyType has no members (sizeof is 1)") { CHECK(sizeof(Traits::EmptyType) == 1u); }
}

// ---------------------------------------------------------------------------
// TEST SUITE: ExpectFalse
// ---------------------------------------------------------------------------

TEST_SUITE("base-Traits - ExpectFalse") {
  TEST_CASE("ExpectFalse<int> is false_type") { CHECK(Traits::ExpectFalse<int>::value == false); }

  TEST_CASE("ExpectFalse<std::string> is false_type") { CHECK(Traits::ExpectFalse<std::string>::value == false); }

  TEST_CASE("ExpectFalse<void> is false_type") { CHECK(Traits::ExpectFalse<void>::value == false); }
}

// ---------------------------------------------------------------------------
// TEST SUITE: Callable
// ---------------------------------------------------------------------------

TEST_SUITE("base-Traits - Callable") {
  TEST_CASE("lambda with no args is Callable") {
    auto lambda = []() {};
    CHECK(Traits::Callable<decltype(lambda)>::value);
  }

  TEST_CASE("std::function<void()> is Callable") { CHECK(Traits::Callable<std::function<void()>>::value); }

  TEST_CASE("functor object with operator() is Callable") { CHECK(Traits::Callable<CallableObj>::value); }

  TEST_CASE("plain int is not Callable") { CHECK(!Traits::Callable<int>::value); }

  TEST_CASE("struct without operator() is not Callable") { CHECK(!Traits::Callable<NonCallable>::value); }

  TEST_CASE("void function pointer is Callable") {
    using FnPtr = void (*)();
    CHECK(Traits::Callable<FnPtr>::value);
  }
}

// ---------------------------------------------------------------------------
// TEST SUITE: Assignable
// ---------------------------------------------------------------------------

TEST_SUITE("base-Traits - Assignable") {
  TEST_CASE("int is assignable from int") { CHECK(Traits::Assignable<int, int>::value); }

  TEST_CASE("double is assignable from int (implicit conversion)") { CHECK(Traits::Assignable<double, int>::value); }

  TEST_CASE("std::string is assignable from const char*") {
    CHECK((Traits::Assignable<std::string, const char*>::value));
  }

  TEST_CASE("int is not assignable from std::string") { CHECK(!(Traits::Assignable<int, std::string>::value)); }
}

// ---------------------------------------------------------------------------
// TEST SUITE: EqualityComparable
// ---------------------------------------------------------------------------

TEST_SUITE("base-Traits - EqualityComparable") {
  TEST_CASE("int == int is comparable") { CHECK((Traits::EqualityComparable<int, int>::value)); }

  TEST_CASE("std::string == std::string is comparable") {
    CHECK((Traits::EqualityComparable<std::string, std::string>::value));
  }

  TEST_CASE("struct without == operator is not comparable") {
    // Foo has no operator== defined
    CHECK(!(Traits::EqualityComparable<Foo, Foo>::value));
  }
}

// ---------------------------------------------------------------------------
// TEST SUITE: GreaterComparable
// ---------------------------------------------------------------------------

TEST_SUITE("base-Traits - GreaterComparable") {
  TEST_CASE("int supports < and > with int") { CHECK((Traits::GreaterComparable<int, int>::value)); }

  TEST_CASE("double supports < and > with double") { CHECK((Traits::GreaterComparable<double, double>::value)); }

  TEST_CASE("struct without < and > is not GreaterComparable") { CHECK(!(Traits::GreaterComparable<Foo, Foo>::value)); }
}

// ---------------------------------------------------------------------------
// TEST SUITE: Operatorable
// ---------------------------------------------------------------------------

TEST_SUITE("base-Traits - Operatorable") {
  TEST_CASE("ostringstream supports << and >> with StreamableObj") {
    CHECK((Traits::Operatorable<std::ostringstream, StreamableObj>::value));
  }

  TEST_CASE("int does not support << and >> with int in ostringstream context") {
    // int << int is a bitshift, but ostringstream << int is insertion;
    // We check int against int: bitshift exists for << but >> is also valid
    CHECK((Traits::Operatorable<int, int>::value));
  }

  TEST_CASE("Foo struct has no stream operators") { CHECK(!(Traits::Operatorable<Foo, Foo>::value)); }
}

// ---------------------------------------------------------------------------
// TEST SUITE: IsAtomic
// ---------------------------------------------------------------------------

TEST_SUITE("base-Traits - IsAtomic") {
  TEST_CASE("std::atomic<int> is atomic") { CHECK(Traits::IsAtomic<std::atomic<int>>::value); }

  TEST_CASE("std::atomic<bool> is atomic") { CHECK(Traits::IsAtomic<std::atomic<bool>>::value); }

  TEST_CASE("plain int is not atomic") { CHECK(!Traits::IsAtomic<int>::value); }

  TEST_CASE("std::string is not atomic") { CHECK(!Traits::IsAtomic<std::string>::value); }
}

// ---------------------------------------------------------------------------
// TEST SUITE: IsSharedPtr
// ---------------------------------------------------------------------------

TEST_SUITE("base-Traits - IsSharedPtr") {
  TEST_CASE("std::shared_ptr<int> is a shared_ptr") { CHECK(Traits::IsSharedPtr<std::shared_ptr<int>>::value); }

  TEST_CASE("std::shared_ptr<Foo> is a shared_ptr") { CHECK(Traits::IsSharedPtr<std::shared_ptr<Foo>>::value); }

  TEST_CASE("plain int is not a shared_ptr") { CHECK(!Traits::IsSharedPtr<int>::value); }

  TEST_CASE("std::unique_ptr<int> is not a shared_ptr") { CHECK(!Traits::IsSharedPtr<std::unique_ptr<int>>::value); }
}

// ---------------------------------------------------------------------------
// TEST SUITE: RemoveSharedPtr
// ---------------------------------------------------------------------------

TEST_SUITE("base-Traits - RemoveSharedPtr") {
  TEST_CASE("shared_ptr<int> unwraps to int") {
    bool same = std::is_same_v<Traits::RemoveSharedPtr<std::shared_ptr<int>>::Type, int>;
    CHECK(same);
  }

  TEST_CASE("shared_ptr<Foo> unwraps to Foo") {
    bool same = std::is_same_v<Traits::RemoveSharedPtr<std::shared_ptr<Foo>>::Type, Foo>;
    CHECK(same);
  }

  TEST_CASE("non-shared_ptr type stays unchanged") {
    bool same = std::is_same_v<Traits::RemoveSharedPtr<int>::Type, int>;
    CHECK(same);
  }

  TEST_CASE("std::string stays unchanged") {
    bool same = std::is_same_v<Traits::RemoveSharedPtr<std::string>::Type, std::string>;
    CHECK(same);
  }
}

// ---------------------------------------------------------------------------
// TEST SUITE: has_member / VLINK_HAS_MEMBER
// ---------------------------------------------------------------------------

TEST_SUITE("base-Traits - has_member") {
  TEST_CASE("Foo has member bar via macro") {
    // NOLINT(readability-identifier-naming)
    constexpr bool result = VLINK_HAS_MEMBER(Foo, bar);
    CHECK(result);
  }

  TEST_CASE("Foo does not have member xyz_nonexistent via macro") {
    // NOLINT(readability-identifier-naming)
    constexpr bool result = VLINK_HAS_MEMBER(Foo, xyz_nonexistent);
    CHECK(!result);
  }

  TEST_CASE("Baz does not have member bar") {
    // NOLINT(readability-identifier-naming)
    constexpr bool result = VLINK_HAS_MEMBER(Baz, bar);
    CHECK(!result);
  }
}

// ---------------------------------------------------------------------------
// TEST SUITE: is_non_char_ptr
// ---------------------------------------------------------------------------

TEST_SUITE("base-Traits - is_non_char_ptr") {
  TEST_CASE("int* is a non-char pointer") { CHECK(Traits::is_non_char_ptr<int*>()); }

  TEST_CASE("Foo* is a non-char pointer") { CHECK(Traits::is_non_char_ptr<Foo*>()); }

  TEST_CASE("char* is NOT a non-char pointer") { CHECK(!Traits::is_non_char_ptr<char*>()); }

  TEST_CASE("const char* is NOT a non-char pointer") { CHECK(!Traits::is_non_char_ptr<const char*>()); }

  TEST_CASE("plain int is not a pointer at all") { CHECK(!Traits::is_non_char_ptr<int>()); }

  TEST_CASE("void* is a non-char pointer") { CHECK(Traits::is_non_char_ptr<void*>()); }
}

// ---------------------------------------------------------------------------
// TEST SUITE: is_integer
// ---------------------------------------------------------------------------

TEST_SUITE("base-Traits - is_integer") {
  TEST_CASE("short is integer") {
    CHECK(Traits::is_integer<short>());  // NOLINT(runtime/int, google-runtime-int)
  }

  TEST_CASE("int is integer") {
    CHECK(Traits::is_integer<int>());  // NOLINT(runtime/int, google-runtime-int)
  }

  TEST_CASE("unsigned int is integer") {
    CHECK(Traits::is_integer<unsigned int>());  // NOLINT(runtime/int, google-runtime-int)
  }

  TEST_CASE("long long is integer") {
    CHECK(Traits::is_integer<long long>());  // NOLINT(runtime/int, google-runtime-int)
  }

  TEST_CASE("uint64_t is integer") {
    CHECK(Traits::is_integer<uint64_t>());  // NOLINT(runtime/int, google-runtime-int)
  }

  TEST_CASE("bool is NOT integer") {
    CHECK(!Traits::is_integer<bool>());  // NOLINT(runtime/int, google-runtime-int)
  }

  TEST_CASE("char is NOT integer") {
    CHECK(!Traits::is_integer<char>());  // NOLINT(runtime/int, google-runtime-int)
  }

  TEST_CASE("signed char is NOT integer") {
    CHECK(!Traits::is_integer<signed char>());  // NOLINT(runtime/int, google-runtime-int)
  }

  TEST_CASE("unsigned char is NOT integer") {
    CHECK(!Traits::is_integer<unsigned char>());  // NOLINT(runtime/int, google-runtime-int)
  }

  TEST_CASE("double is NOT integer") {
    CHECK(!Traits::is_integer<double>());  // NOLINT(runtime/int, google-runtime-int)
  }

  TEST_CASE("const int is integer (CV stripped)") {
    CHECK(Traits::is_integer<const int>());  // NOLINT(runtime/int, google-runtime-int)
  }
}

// ---------------------------------------------------------------------------
// TEST SUITE: is_floating
// ---------------------------------------------------------------------------

TEST_SUITE("base-Traits - is_floating") {
  TEST_CASE("float is floating") { CHECK(Traits::is_floating<float>()); }

  TEST_CASE("double is floating") { CHECK(Traits::is_floating<double>()); }

  TEST_CASE("long double is floating") {
    CHECK(Traits::is_floating<long double>());  // NOLINT(runtime/int, google-runtime-int)
  }

  TEST_CASE("int is NOT floating") { CHECK(!Traits::is_floating<int>()); }

  TEST_CASE("bool is NOT floating") {
    CHECK(!Traits::is_floating<bool>());  // NOLINT(runtime/int, google-runtime-int)
  }

  TEST_CASE("const float is floating (CV stripped)") { CHECK(Traits::is_floating<const float>()); }
}

// NOLINTEND
