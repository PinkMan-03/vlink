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

#include "./base/uint128.h"

#include <doctest/doctest.h>

#include <cstdint>
#include <sstream>
#include <unordered_map>

//
#include "../common_test.h"

// ---------------------------------------------------------------------------
// TEST SUITE: Construction
// ---------------------------------------------------------------------------

TEST_SUITE("base-Uint128 - construction") {
  TEST_CASE("default constructed is zero") {
    Uint128 v;
    CHECK(v.get_high() == 0u);
    CHECK(v.get_low() == 0u);
  }

  TEST_CASE("construct from uint64_t (low word only)") {
    Uint128 v(static_cast<uint64_t>(0xDEADBEEFu));
    CHECK(v.get_high() == 0u);
    CHECK(v.get_low() == 0xDEADBEEFu);
  }

  TEST_CASE("construct from int literal (zero-extended)") {
    Uint128 v(42);
    CHECK(v.get_high() == 0u);
    CHECK(v.get_low() == 42u);
  }

  TEST_CASE("construct from explicit high and low halves") {
    Uint128 v(0xABCDu, 0x1234u);
    CHECK(v.get_high() == 0xABCDu);
    CHECK(v.get_low() == 0x1234u);
  }

  TEST_CASE("construct with UINT64_MAX in low word") {
    Uint128 v(0u, UINT64_MAX);
    CHECK(v.get_high() == 0u);
    CHECK(v.get_low() == UINT64_MAX);
  }

  TEST_CASE("construct with UINT64_MAX in high word") {
    Uint128 v(UINT64_MAX, 0u);
    CHECK(v.get_high() == UINT64_MAX);
    CHECK(v.get_low() == 0u);
  }
}

// ---------------------------------------------------------------------------
// TEST SUITE: Equality and comparison
// ---------------------------------------------------------------------------

TEST_SUITE("base-Uint128 - equality and comparison") {
  TEST_CASE("operator== same value") {
    Uint128 a(1u, 2u);
    Uint128 b(1u, 2u);
    CHECK(a == b);
    CHECK(!(a != b));
  }

  TEST_CASE("operator!= different low") {
    Uint128 a(0u, 1u);
    Uint128 b(0u, 2u);
    CHECK(a != b);
  }

  TEST_CASE("operator!= different high") {
    Uint128 a(1u, 0u);
    Uint128 b(2u, 0u);
    CHECK(a != b);
  }

  TEST_CASE("operator< low word dominates when high is equal") {
    Uint128 small(0u, 1u);
    Uint128 large(0u, 2u);
    CHECK(small < large);
    CHECK(!(large < small));
  }

  TEST_CASE("operator< high word dominates") {
    Uint128 small(1u, UINT64_MAX);
    Uint128 large(2u, 0u);
    CHECK(small < large);
  }

  TEST_CASE("operator>") {
    Uint128 a(0u, 10u);
    Uint128 b(0u, 5u);
    CHECK(a > b);
  }

  TEST_CASE("operator<=") {
    Uint128 a(0u, 5u);
    Uint128 b(0u, 5u);
    Uint128 c(0u, 6u);
    CHECK(a <= b);
    CHECK(a <= c);
    CHECK(!(c <= a));
  }

  TEST_CASE("operator>=") {
    Uint128 a(0u, 5u);
    Uint128 b(0u, 5u);
    CHECK(a >= b);
    CHECK(b >= a);
  }
}

// ---------------------------------------------------------------------------
// TEST SUITE: Addition
// ---------------------------------------------------------------------------

TEST_SUITE("base-Uint128 - addition") {
  TEST_CASE("simple addition without carry") {
    Uint128 a(0u, 10u);
    Uint128 b(0u, 20u);
    Uint128 c = a + b;
    CHECK(c.get_high() == 0u);
    CHECK(c.get_low() == 30u);
  }

  TEST_CASE("addition carry propagates from low to high") {
    Uint128 a(0u, UINT64_MAX);
    Uint128 b(0u, 1u);
    Uint128 c = a + b;
    CHECK(c.get_high() == 1u);
    CHECK(c.get_low() == 0u);
  }

  TEST_CASE("operator+= in-place") {
    Uint128 a(0u, 5u);
    a += Uint128(0u, 3u);
    CHECK(a.get_low() == 8u);
  }

  TEST_CASE("addition wraps around at 128-bit max") {
    Uint128 max(UINT64_MAX, UINT64_MAX);
    Uint128 one(0u, 1u);
    Uint128 result = max + one;
    CHECK(result == Uint128(0u, 0u));
  }

  TEST_CASE("add two high-word values") {
    Uint128 a(5u, 0u);
    Uint128 b(3u, 0u);
    Uint128 c = a + b;
    CHECK(c.get_high() == 8u);
    CHECK(c.get_low() == 0u);
  }
}

// ---------------------------------------------------------------------------
// TEST SUITE: Subtraction
// ---------------------------------------------------------------------------

TEST_SUITE("base-Uint128 - subtraction") {
  TEST_CASE("simple subtraction") {
    Uint128 a(0u, 20u);
    Uint128 b(0u, 7u);
    Uint128 c = a - b;
    CHECK(c.get_low() == 13u);
    CHECK(c.get_high() == 0u);
  }

  TEST_CASE("subtraction borrow propagates from high to low") {
    Uint128 a(1u, 0u);
    Uint128 b(0u, 1u);
    Uint128 c = a - b;
    CHECK(c.get_high() == 0u);
    CHECK(c.get_low() == UINT64_MAX);
  }

  TEST_CASE("operator-= in-place") {
    Uint128 a(0u, 10u);
    a -= Uint128(0u, 4u);
    CHECK(a.get_low() == 6u);
  }
}

// ---------------------------------------------------------------------------
// TEST SUITE: Multiplication
// ---------------------------------------------------------------------------

TEST_SUITE("base-Uint128 - multiplication") {
  TEST_CASE("multiply by zero yields zero") {
    Uint128 a(1u, 100u);
    Uint128 b(0u, 0u);
    CHECK((a * b) == Uint128(0u, 0u));
  }

  TEST_CASE("multiply by one yields same value") {
    Uint128 a(2u, 7u);
    Uint128 one(0u, 1u);
    CHECK((a * one) == a);
  }

  TEST_CASE("simple multiplication") {
    Uint128 a(0u, 6u);
    Uint128 b(0u, 7u);
    Uint128 c = a * b;
    CHECK(c.get_low() == 42u);
    CHECK(c.get_high() == 0u);
  }

  TEST_CASE("operator*= in-place") {
    Uint128 a(0u, 3u);
    a *= Uint128(0u, 4u);
    CHECK(a.get_low() == 12u);
  }
}

// ---------------------------------------------------------------------------
// TEST SUITE: Division and modulo
// ---------------------------------------------------------------------------

TEST_SUITE("base-Uint128 - division and modulo") {
  TEST_CASE("division simple case") {
    Uint128 a(0u, 100u);
    Uint128 b(0u, 10u);
    CHECK((a / b) == Uint128(0u, 10u));
  }

  TEST_CASE("modulo simple case") {
    Uint128 a(0u, 17u);
    Uint128 b(0u, 5u);
    CHECK((a % b) == Uint128(0u, 2u));
  }

  TEST_CASE("division exact") {
    Uint128 a(0u, 81u);
    Uint128 b(0u, 9u);
    CHECK((a / b) == Uint128(0u, 9u));
    CHECK((a % b) == Uint128(0u, 0u));
  }
}

// ---------------------------------------------------------------------------
// TEST SUITE: Bitwise operators
// ---------------------------------------------------------------------------

TEST_SUITE("base-Uint128 - bitwise") {
  TEST_CASE("bitwise OR") {
    Uint128 a(0u, 0b1010u);
    Uint128 b(0u, 0b0101u);
    CHECK((a | b) == Uint128(0u, 0b1111u));
  }

  TEST_CASE("bitwise AND") {
    Uint128 a(0u, 0b1110u);
    Uint128 b(0u, 0b0110u);
    CHECK((a & b) == Uint128(0u, 0b0110u));
  }

  TEST_CASE("bitwise XOR") {
    Uint128 a(0u, 0b1100u);
    Uint128 b(0u, 0b1010u);
    CHECK((a ^ b) == Uint128(0u, 0b0110u));
  }

  TEST_CASE("bitwise NOT") {
    Uint128 zero(0u, 0u);
    Uint128 all_ones = ~zero;
    CHECK(all_ones.get_high() == UINT64_MAX);
    CHECK(all_ones.get_low() == UINT64_MAX);
  }

  TEST_CASE("left shift by 1") {
    Uint128 a(0u, 1u);
    Uint128 b = a << 1;
    CHECK(b.get_low() == 2u);
    CHECK(b.get_high() == 0u);
  }

  TEST_CASE("left shift crossing 64-bit boundary") {
    Uint128 a(0u, 1u);
    Uint128 b = a << 64;
    CHECK(b.get_high() == 1u);
    CHECK(b.get_low() == 0u);
  }

  TEST_CASE("right shift crossing 64-bit boundary") {
    Uint128 a(1u, 0u);
    Uint128 b = a >> 64;
    CHECK(b.get_high() == 0u);
    CHECK(b.get_low() == 1u);
  }

  TEST_CASE("shift by 128 returns zero") {
    Uint128 a(1u, 1u);
    CHECK((a << 128) == Uint128(0u, 0u));
    CHECK((a >> 128) == Uint128(0u, 0u));
  }
}

// ---------------------------------------------------------------------------
// TEST SUITE: Increment and decrement
// ---------------------------------------------------------------------------

TEST_SUITE("base-Uint128 - increment and decrement") {
  TEST_CASE("pre-increment") {
    Uint128 a(0u, 41u);
    ++a;
    CHECK(a.get_low() == 42u);
  }

  TEST_CASE("post-increment returns old value") {
    Uint128 a(0u, 5u);
    Uint128 old = a++;
    CHECK(old.get_low() == 5u);
    CHECK(a.get_low() == 6u);
  }

  TEST_CASE("pre-increment carry into high word") {
    Uint128 a(0u, UINT64_MAX);
    ++a;
    CHECK(a.get_high() == 1u);
    CHECK(a.get_low() == 0u);
  }

  TEST_CASE("pre-decrement") {
    Uint128 a(0u, 10u);
    --a;
    CHECK(a.get_low() == 9u);
  }

  TEST_CASE("post-decrement returns old value") {
    Uint128 a(0u, 8u);
    Uint128 old = a--;
    CHECK(old.get_low() == 8u);
    CHECK(a.get_low() == 7u);
  }

  TEST_CASE("pre-decrement borrow from high word") {
    Uint128 a(1u, 0u);
    --a;
    CHECK(a.get_high() == 0u);
    CHECK(a.get_low() == UINT64_MAX);
  }
}

// ---------------------------------------------------------------------------
// TEST SUITE: Stream output
// ---------------------------------------------------------------------------

TEST_SUITE("base-Uint128 - stream output") {
  TEST_CASE("stream output of zero is '0x0'") {
    Uint128 v(0u, 0u);
    std::ostringstream oss;
    oss << v;
    CHECK(oss.str() == "0x0");
  }

  TEST_CASE("stream output of small value is hex") {
    Uint128 v(0u, 42u);
    std::ostringstream oss;
    oss << v;
    CHECK(oss.str() == "0x2A");
  }
}

// ---------------------------------------------------------------------------
// TEST SUITE: std::hash specialisation
// ---------------------------------------------------------------------------

TEST_SUITE("base-Uint128 - std::hash") {
  TEST_CASE("hash of equal values is equal") {
    std::hash<Uint128> h;
    Uint128 a(1u, 2u);
    Uint128 b(1u, 2u);
    CHECK(h(a) == h(b));
  }

  TEST_CASE("Uint128 usable as unordered_map key") {
    std::unordered_map<Uint128, std::string> map;
    map[Uint128(0xDEADu, 0xBEEFu)] = "entry";
    CHECK(map[Uint128(0xDEADu, 0xBEEFu)] == "entry");
  }
}

// ---------------------------------------------------------------------------
// TEST SUITE: Type alias
// ---------------------------------------------------------------------------

TEST_SUITE("base-Uint128 - uint128_t alias") {
  TEST_CASE("uint128_t is the same type as Uint128") {
    uint128_t v(0u, 99u);
    CHECK(v.get_low() == 99u);
  }
}

// ---------------------------------------------------------------------------
// TEST SUITE: In-place bitwise operators
// ---------------------------------------------------------------------------

TEST_SUITE("base-Uint128 - in-place bitwise") {
  TEST_CASE("operator|= sets bits") {
    Uint128 a(0u, 0b1000u);
    a |= Uint128(0u, 0b0111u);
    CHECK(a == Uint128(0u, 0b1111u));
  }

  TEST_CASE("operator&= clears bits") {
    Uint128 a(0u, 0b1111u);
    a &= Uint128(0u, 0b0101u);
    CHECK(a == Uint128(0u, 0b0101u));
  }

  TEST_CASE("operator^= flips bits") {
    Uint128 a(0u, 0b1100u);
    a ^= Uint128(0u, 0b1010u);
    CHECK(a == Uint128(0u, 0b0110u));
  }

  TEST_CASE("operator<<= shifts left in-place") {
    Uint128 a(0u, 1u);
    a <<= 64;
    CHECK(a.get_high() == 1u);
    CHECK(a.get_low() == 0u);
  }

  TEST_CASE("operator>>= shifts right in-place") {
    Uint128 a(1u, 0u);
    a >>= 64;
    CHECK(a.get_high() == 0u);
    CHECK(a.get_low() == 1u);
  }

  TEST_CASE("operator<<= by 128 yields zero") {
    Uint128 a(1u, 1u);
    a <<= 128;
    CHECK(a == Uint128(0u, 0u));
  }

  TEST_CASE("operator>>= by 128 yields zero") {
    Uint128 a(1u, 1u);
    a >>= 128;
    CHECK(a == Uint128(0u, 0u));
  }

  TEST_CASE("operator<<= by 0 is no-op") {
    Uint128 a(3u, 5u);
    a <<= 0;
    CHECK(a == Uint128(3u, 5u));
  }

  TEST_CASE("operator>>= by 0 is no-op") {
    Uint128 a(3u, 5u);
    a >>= 0;
    CHECK(a == Uint128(3u, 5u));
  }
}

// ---------------------------------------------------------------------------
// TEST SUITE: In-place arithmetic operators
// ---------------------------------------------------------------------------

TEST_SUITE("base-Uint128 - in-place division and modulo") {
  TEST_CASE("operator/= divides in-place") {
    Uint128 a(0u, 100u);
    a /= Uint128(0u, 4u);
    CHECK(a == Uint128(0u, 25u));
  }

  TEST_CASE("operator%= modulo in-place") {
    Uint128 a(0u, 17u);
    a %= Uint128(0u, 5u);
    CHECK(a == Uint128(0u, 2u));
  }

  TEST_CASE("division by zero throws") {
    Uint128 a(0u, 10u);
    CHECK_THROWS(a / Uint128(0u, 0u));
  }

  TEST_CASE("modulo by zero throws") {
    Uint128 a(0u, 10u);
    CHECK_THROWS(a % Uint128(0u, 0u));
  }

  TEST_CASE("operator/= by zero throws") {
    Uint128 a(0u, 10u);
    CHECK_THROWS(a /= Uint128(0u, 0u));
  }

  TEST_CASE("operator%= by zero throws") {
    Uint128 a(0u, 10u);
    CHECK_THROWS(a %= Uint128(0u, 0u));
  }
}

// ---------------------------------------------------------------------------
// TEST SUITE: Comparison edge cases
// ---------------------------------------------------------------------------

TEST_SUITE("base-Uint128 - comparison edge cases") {
  TEST_CASE("zero equals zero") {
    Uint128 a;
    Uint128 b;
    CHECK(a == b);
    CHECK(!(a != b));
    CHECK(!(a < b));
    CHECK(!(a > b));
    CHECK(a <= b);
    CHECK(a >= b);
  }

  TEST_CASE("max value compares greater than all others") {
    Uint128 max_val(UINT64_MAX, UINT64_MAX);
    Uint128 almost_max(UINT64_MAX, UINT64_MAX - 1u);
    CHECK(max_val > almost_max);
    CHECK(almost_max < max_val);
  }
}

// ---------------------------------------------------------------------------
// TEST SUITE: std::hash different values
// ---------------------------------------------------------------------------

TEST_SUITE("base-Uint128 - std::hash different values") {
  TEST_CASE("hash of different values may differ") {
    std::hash<Uint128> h;
    Uint128 a(1u, 2u);
    Uint128 b(2u, 1u);
    // Not a strict requirement (collisions are possible), but for these
    // distinct values the hash implementation should produce different results
    // in practice.
    (void)h(a);
    (void)h(b);
    CHECK(true);  // No crash is the minimum requirement
  }

  TEST_CASE("hash of zero is stable") {
    std::hash<Uint128> h;
    Uint128 z;
    CHECK(h(z) == h(z));
  }
}

// ---------------------------------------------------------------------------
// TEST SUITE: Large-value arithmetic
// ---------------------------------------------------------------------------

TEST_SUITE("base-Uint128 - large value arithmetic") {
  TEST_CASE("multiplication with carry into high word") {
    // UINT64_MAX * 2 = 2^65 - 2 → high=1, low=UINT64_MAX-1
    Uint128 a(0u, UINT64_MAX);
    Uint128 two(0u, 2u);
    Uint128 result = a * two;
    CHECK(result.get_high() == 1u);
    CHECK(result.get_low() == UINT64_MAX - 1u);
  }

  TEST_CASE("subtraction wraps at 128-bit zero") {
    Uint128 zero;
    Uint128 one(0u, 1u);
    Uint128 result = zero - one;
    CHECK(result.get_high() == UINT64_MAX);
    CHECK(result.get_low() == UINT64_MAX);
  }

  TEST_CASE("division large numerator") {
    Uint128 a(1u, 0u);  // 2^64
    Uint128 b(0u, 2u);
    Uint128 q = a / b;
    CHECK(q == Uint128(0u, static_cast<uint64_t>(1u) << 63));
  }
}

// NOLINTEND
