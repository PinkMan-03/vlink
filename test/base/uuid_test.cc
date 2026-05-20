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

#include "./base/uuid.h"

#include <doctest/doctest.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <random>
#include <set>
#include <sstream>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

//
#include "../common_test.h"

// ---------------------------------------------------------------------------
// COMPILE-TIME ASSERTIONS: constexpr usability
// ---------------------------------------------------------------------------

static_assert(Uuid{}.is_nil(), "default-constructed Uuid must be nil at compile time");

static_assert(Uuid{}.variant() == Uuid::Variant::kNcs, "nil UUID must be NCS variant at compile time");

static_assert(Uuid{}.version() == Uuid::Version::kNone, "nil UUID must have no version at compile time");

static_assert(Uuid::kByteSize == 16U, "RFC 4122 UUIDs are 16 bytes");

static_assert(Uuid::kStringSize == 36U, "canonical UUID string is 36 chars");

static constexpr std::array<uint8_t, 16> kSampleData{
    0x47, 0xac, 0x10, 0xb8, 0x58, 0xcc, 0x4a, 0x3c, 0x8c, 0x5b, 0x0e, 0x77, 0x88, 0x99, 0xaa, 0xbb,
};

static constexpr Uuid kSampleConstexpr{kSampleData};

static_assert(!kSampleConstexpr.is_nil(), "non-zero data must not be nil at compile time");

static_assert(kSampleConstexpr.bytes()[0] == 0x47U, "bytes() must be constexpr-accessible");

static_assert(kSampleConstexpr.bytes()[15] == 0xbbU, "bytes()[15] must be constexpr-accessible");

// ---------------------------------------------------------------------------
// TEST SUITE: construction
// ---------------------------------------------------------------------------

TEST_SUITE("base-Uuid - construction") {
  TEST_CASE("default constructed is nil") {
    Uuid id;

    CHECK(id.is_nil());
    CHECK(id.to_string() == "00000000-0000-0000-0000-000000000000");
  }

  TEST_CASE("constructs from std::array") {
    std::array<uint8_t, 16> data{0x12, 0x34, 0x56, 0x78, 0x9a, 0xbc, 0xde, 0xf0,
                                 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88};
    Uuid id{data};

    CHECK_FALSE(id.is_nil());
    CHECK(id.bytes() == data);
  }

  TEST_CASE("constructs from raw C array") {
    uint8_t raw[16] = {0x47, 0xac, 0x10, 0xb8, 0x58, 0xcc, 0x4a, 0x3c, 0x8c, 0x5b, 0x0e, 0x77, 0x88, 0x99, 0xaa, 0xbb};
    Uuid id{raw};

    CHECK(id.bytes()[0] == 0x47U);
    CHECK(id.bytes()[15] == 0xbbU);

    for (size_t i = 0U; i < 16U; ++i) {
      CHECK(id.bytes()[i] == raw[i]);
    }
  }

  TEST_CASE("iterator range constructor copies 16 bytes") {
    std::vector<uint8_t> src(16U, 0xa5U);
    Uuid id{src.begin(), src.end()};

    for (size_t i = 0U; i < 16U; ++i) {
      CHECK(id.bytes()[i] == 0xa5U);
    }
  }

  TEST_CASE("iterator range with wrong size leaves nil state") {
    std::vector<uint8_t> too_short(8U, 0xffU);
    Uuid id{too_short.begin(), too_short.end()};

    CHECK(id.is_nil());
  }

  TEST_CASE("iterator range with too-many bytes leaves nil state") {
    std::vector<uint8_t> too_long(32U, 0xffU);
    Uuid id{too_long.begin(), too_long.end()};

    CHECK(id.is_nil());
  }

  TEST_CASE("iterator range accepts raw pointer pair") {
    const uint8_t buffer[16] = {1U, 2U, 3U, 4U, 5U, 6U, 7U, 8U, 9U, 10U, 11U, 12U, 13U, 14U, 15U, 16U};
    Uuid id{std::begin(buffer), std::end(buffer)};

    CHECK(id.bytes()[0] == 1U);
    CHECK(id.bytes()[15] == 16U);
  }

  TEST_CASE("copy and move preserve payload") {
    std::array<uint8_t, 16> data{};
    data[5] = 0xCDU;
    Uuid src{data};

    Uuid copy = src;
    Uuid moved = std::move(src);

    CHECK(copy.bytes()[5] == 0xCDU);
    CHECK(moved.bytes()[5] == 0xCDU);
  }
}

// ---------------------------------------------------------------------------
// TEST SUITE: variant and version
// ---------------------------------------------------------------------------

TEST_SUITE("base-Uuid - variant and version") {
  TEST_CASE("nil UUID reports NCS variant and no version") {
    Uuid id;

    CHECK(id.variant() == Uuid::Variant::kNcs);
    CHECK(id.version() == Uuid::Version::kNone);
  }

  TEST_CASE("NCS variant for any top-bit-0 octet 8") {
    std::array<uint8_t, 16> data{};

    for (uint8_t octet : {uint8_t{0x00U}, uint8_t{0x40U}, uint8_t{0x7FU}}) {
      data[8] = octet;
      CHECK(Uuid{data}.variant() == Uuid::Variant::kNcs);
    }
  }

  TEST_CASE("RFC variant detection on octet 8") {
    std::array<uint8_t, 16> data{};
    data[8] = 0x80U;

    Uuid id{data};

    CHECK(id.variant() == Uuid::Variant::kRfc);
  }

  TEST_CASE("RFC variant accepted for 0xBF as well") {
    std::array<uint8_t, 16> data{};
    data[8] = 0xBFU;

    CHECK(Uuid{data}.variant() == Uuid::Variant::kRfc);
  }

  TEST_CASE("Microsoft variant detection on octet 8") {
    std::array<uint8_t, 16> data{};
    data[8] = 0xC0U;

    Uuid id{data};

    CHECK(id.variant() == Uuid::Variant::kMicrosoft);
  }

  TEST_CASE("Reserved variant detection on octet 8") {
    std::array<uint8_t, 16> data{};
    data[8] = 0xE0U;

    Uuid id{data};

    CHECK(id.variant() == Uuid::Variant::kReserved);

    data[8] = 0xFFU;

    CHECK(Uuid{data}.variant() == Uuid::Variant::kReserved);
  }

  TEST_CASE("version detection covers all RFC values") {
    std::array<uint8_t, 16> data{};

    data[6] = 0x10U;
    CHECK(Uuid{data}.version() == Uuid::Version::kTimeBased);

    data[6] = 0x20U;
    CHECK(Uuid{data}.version() == Uuid::Version::kDceSecurity);

    data[6] = 0x30U;
    CHECK(Uuid{data}.version() == Uuid::Version::kNameBasedMd5);

    data[6] = 0x40U;
    CHECK(Uuid{data}.version() == Uuid::Version::kRandomBased);

    data[6] = 0x50U;
    CHECK(Uuid{data}.version() == Uuid::Version::kNameBasedSha1);

    data[6] = 0x70U;
    CHECK(Uuid{data}.version() == Uuid::Version::kNone);

    data[6] = 0xF0U;
    CHECK(Uuid{data}.version() == Uuid::Version::kNone);
  }
}

// ---------------------------------------------------------------------------
// TEST SUITE: to_string / to_compact_string
// ---------------------------------------------------------------------------

TEST_SUITE("base-Uuid - serialise") {
  TEST_CASE("to_string emits 36 lowercase hex with hyphens") {
    std::array<uint8_t, 16> data{0x47, 0xac, 0x10, 0xb8, 0x58, 0xcc, 0x4a, 0x3c,
                                 0x8c, 0x5b, 0x0e, 0x77, 0x88, 0x99, 0xaa, 0xbb};
    Uuid id{data};

    const auto str = id.to_string();

    CHECK(str.size() == 36U);
    CHECK(str == "47ac10b8-58cc-4a3c-8c5b-0e778899aabb");
  }

  TEST_CASE("to_string places hyphens at canonical positions") {
    const auto str = Uuid::generate_random().to_string();

    REQUIRE(str.size() == 36U);
    CHECK(str[8] == '-');
    CHECK(str[13] == '-');
    CHECK(str[18] == '-');
    CHECK(str[23] == '-');
  }

  TEST_CASE("to_string of nil UUID is all zeros") {
    Uuid id;

    CHECK(id.to_string() == "00000000-0000-0000-0000-000000000000");
  }

  TEST_CASE("to_compact_string drops hyphens") {
    std::array<uint8_t, 16> data{0x47, 0xac, 0x10, 0xb8, 0x58, 0xcc, 0x4a, 0x3c,
                                 0x8c, 0x5b, 0x0e, 0x77, 0x88, 0x99, 0xaa, 0xbb};
    Uuid id{data};

    const auto str = id.to_compact_string();

    CHECK(str.size() == 32U);
    CHECK(str == "47ac10b858cc4a3c8c5b0e778899aabb");
  }

  TEST_CASE("to_compact_string of nil UUID is 32 zeros") {
    Uuid id;

    CHECK(id.to_compact_string() == "00000000000000000000000000000000");
  }

  TEST_CASE("stream insertion matches to_string") {
    std::array<uint8_t, 16> data{0x47, 0xac, 0x10, 0xb8, 0x58, 0xcc, 0x4a, 0x3c,
                                 0x8c, 0x5b, 0x0e, 0x77, 0x88, 0x99, 0xaa, 0xbb};
    Uuid id{data};

    std::ostringstream oss;
    oss << id;

    CHECK(oss.str() == id.to_string());
  }

  TEST_CASE("to_string and to_compact_string advertise noexcept") {
    Uuid id;

    CHECK(noexcept(id.to_string()));
    CHECK(noexcept(id.to_compact_string()));
  }
}

// ---------------------------------------------------------------------------
// TEST SUITE: from_string / is_valid (string_view overloads)
// ---------------------------------------------------------------------------

TEST_SUITE("base-Uuid - parse string_view") {
  TEST_CASE("from_string round-trips canonical form") {
    const std::string text = "47ac10b8-58cc-4a3c-8c5b-0e778899aabb";

    auto parsed = Uuid::from_string(text);

    REQUIRE(parsed.has_value());
    CHECK(parsed->to_string() == text);
  }

  TEST_CASE("from_string parses uppercase hex") {
    auto parsed = Uuid::from_string("47AC10B8-58CC-4A3C-8C5B-0E778899AABB");

    REQUIRE(parsed.has_value());
    CHECK(parsed->to_string() == "47ac10b8-58cc-4a3c-8c5b-0e778899aabb");
  }

  TEST_CASE("from_string parses mixed case hex digits") {
    auto parsed = Uuid::from_string("47Ac10B8-58cc-4A3c-8C5b-0e778899AaBb");

    REQUIRE(parsed.has_value());
    CHECK(parsed->to_string() == "47ac10b8-58cc-4a3c-8c5b-0e778899aabb");
  }

  TEST_CASE("from_string accepts braced canonical form") {
    auto parsed = Uuid::from_string("{47ac10b8-58cc-4a3c-8c5b-0e778899aabb}");

    REQUIRE(parsed.has_value());
    CHECK(parsed->to_string() == "47ac10b8-58cc-4a3c-8c5b-0e778899aabb");
  }

  TEST_CASE("from_string accepts braced compact form") {
    auto parsed = Uuid::from_string("{47ac10b858cc4a3c8c5b0e778899aabb}");

    REQUIRE(parsed.has_value());
    CHECK(parsed->to_compact_string() == "47ac10b858cc4a3c8c5b0e778899aabb");
  }

  TEST_CASE("from_string accepts compact 32-char form") {
    auto parsed = Uuid::from_string("47ac10b858cc4a3c8c5b0e778899aabb");

    REQUIRE(parsed.has_value());
    CHECK(parsed->to_compact_string() == "47ac10b858cc4a3c8c5b0e778899aabb");
  }

  TEST_CASE("from_string is permissive about hyphen positions") {
    auto parsed = Uuid::from_string("47-ac10b858cc-4a3c-8c5b-0e7788-99aabb");

    REQUIRE(parsed.has_value());
    CHECK(parsed->to_compact_string() == "47ac10b858cc4a3c8c5b0e778899aabb");
  }

  TEST_CASE("from_string rejects empty input") { CHECK_FALSE(Uuid::from_string("").has_value()); }

  TEST_CASE("from_string rejects 1-char input") { CHECK_FALSE(Uuid::from_string("0").has_value()); }

  TEST_CASE("from_string rejects empty braces") { CHECK_FALSE(Uuid::from_string("{}").has_value()); }

  TEST_CASE("from_string rejects too-short input") { CHECK_FALSE(Uuid::from_string("47ac10b8").has_value()); }

  TEST_CASE("from_string rejects half-byte trailing nibble") {
    CHECK_FALSE(Uuid::from_string("47ac10b8-58cc-4a3c-8c5b-0e778899aab").has_value());
  }

  TEST_CASE("from_string rejects 33-hex extra trailing nibble") {
    CHECK_FALSE(Uuid::from_string("47ac10b858cc4a3c8c5b0e778899aabb0").has_value());
  }

  TEST_CASE("from_string rejects 34-hex extra trailing byte") {
    CHECK_FALSE(Uuid::from_string("47ac10b858cc4a3c8c5b0e778899aabbff").has_value());
  }

  TEST_CASE("from_string rejects non-hex character") {
    CHECK_FALSE(Uuid::from_string("47ac10b8-58cc-4a3c-8c5b-0e778899aabZ").has_value());
  }

  TEST_CASE("from_string rejects embedded whitespace") {
    CHECK_FALSE(Uuid::from_string("47ac10b8 58cc 4a3c 8c5b 0e778899aabb").has_value());
  }

  TEST_CASE("from_string rejects missing closing brace") {
    CHECK_FALSE(Uuid::from_string("{47ac10b8-58cc-4a3c-8c5b-0e778899aabb").has_value());
  }

  TEST_CASE("from_string rejects all-hyphens placeholder") {
    CHECK_FALSE(Uuid::from_string("--------").has_value());
    CHECK_FALSE(Uuid::from_string("--------------------------------").has_value());
  }

  TEST_CASE("from_string rejects too-few hex chars surrounded by hyphens") {
    CHECK_FALSE(Uuid::from_string("0000-0000-0000-0000").has_value());
  }

  TEST_CASE("is_valid agrees with from_string for canonical") {
    CHECK(Uuid::is_valid("47ac10b8-58cc-4a3c-8c5b-0e778899aabb"));
    CHECK(Uuid::is_valid("{47ac10b8-58cc-4a3c-8c5b-0e778899aabb}"));
    CHECK(Uuid::is_valid("47ac10b858cc4a3c8c5b0e778899aabb"));
  }

  TEST_CASE("is_valid rejects malformed inputs") {
    CHECK_FALSE(Uuid::is_valid(""));
    CHECK_FALSE(Uuid::is_valid("not-a-uuid"));
    CHECK_FALSE(Uuid::is_valid("47ac10b8-58cc-4a3c-8c5b-0e778899aabZ"));
    CHECK_FALSE(Uuid::is_valid("{}"));
  }

  TEST_CASE("from_string / is_valid advertise noexcept") {
    std::string_view sv;
    CHECK(noexcept(Uuid::from_string(sv)));
    CHECK(noexcept(Uuid::is_valid(sv)));
  }
}

// ---------------------------------------------------------------------------
// TEST SUITE: from_string / is_valid (C-string overloads)
// ---------------------------------------------------------------------------

TEST_SUITE("base-Uuid - parse C-string") {
  TEST_CASE("from_string(nullptr) returns nullopt") {
    const char* null_ptr = nullptr;

    CHECK_FALSE(Uuid::from_string(null_ptr).has_value());
  }

  TEST_CASE("is_valid(nullptr) returns false") {
    const char* null_ptr = nullptr;

    CHECK_FALSE(Uuid::is_valid(null_ptr));
  }

  TEST_CASE("from_string(const char*) parses canonical form") {
    auto parsed = Uuid::from_string("47ac10b8-58cc-4a3c-8c5b-0e778899aabb");

    REQUIRE(parsed.has_value());
    CHECK(parsed->to_string() == "47ac10b8-58cc-4a3c-8c5b-0e778899aabb");
  }

  TEST_CASE("is_valid(const char*) accepts canonical form") {
    CHECK(Uuid::is_valid("47ac10b8-58cc-4a3c-8c5b-0e778899aabb"));
  }

  TEST_CASE("is_valid(const char*) rejects malformed") { CHECK_FALSE(Uuid::is_valid("not-a-uuid")); }

  TEST_CASE("C-string overload stops at first NUL") {
    const char buffer[] = "47ac10b8-58cc-4a3c-8c5b-0e778899aabb\0junk-trailing";

    auto parsed = Uuid::from_string(static_cast<const char*>(buffer));

    REQUIRE(parsed.has_value());
    CHECK(parsed->to_string() == "47ac10b8-58cc-4a3c-8c5b-0e778899aabb");
  }

  TEST_CASE("C-string overloads advertise noexcept") {
    const char* sample = "47ac10b8-58cc-4a3c-8c5b-0e778899aabb";

    CHECK(noexcept(Uuid::from_string(sample)));
    CHECK(noexcept(Uuid::is_valid(sample)));
  }
}

// ---------------------------------------------------------------------------
// TEST SUITE: operators / swap / hash
// ---------------------------------------------------------------------------

TEST_SUITE("base-Uuid - operators") {
  TEST_CASE("equality and inequality") {
    Uuid a;
    Uuid b;
    std::array<uint8_t, 16> data{};
    data[0] = 1U;
    Uuid c{data};

    CHECK(a == b);
    CHECK_FALSE(a != b);
    CHECK(a != c);
    CHECK_FALSE(a == c);
  }

  TEST_CASE("lexicographic ordering") {
    std::array<uint8_t, 16> low{};
    std::array<uint8_t, 16> high{};
    high[0] = 1U;

    Uuid lo{low};
    Uuid hi{high};

    CHECK(lo < hi);
    CHECK_FALSE(hi < lo);
    CHECK_FALSE(lo < lo);
  }

  TEST_CASE("ordering compares byte-by-byte from index 0") {
    std::array<uint8_t, 16> first{};
    std::array<uint8_t, 16> second{};

    first[15] = 0xFFU;
    second[0] = 0x01U;

    CHECK(Uuid{first} < Uuid{second});
  }

  TEST_CASE("swap exchanges payloads") {
    std::array<uint8_t, 16> data{};
    data[0] = 0x11U;
    Uuid a{data};
    Uuid b;

    a.swap(b);

    CHECK(a.is_nil());
    CHECK(b.bytes()[0] == 0x11U);
  }

  TEST_CASE("swap advertises noexcept") {
    Uuid a;
    Uuid b;

    CHECK(noexcept(a.swap(b)));
  }

  TEST_CASE("std::swap fallback works on Uuid") {
    std::array<uint8_t, 16> data{};
    data[0] = 0x77U;
    Uuid a{data};
    Uuid b;

    std::swap(a, b);

    CHECK(a.is_nil());
    CHECK(b.bytes()[0] == 0x77U);
  }

  TEST_CASE("std::hash distributes 1000 random UUIDs without collisions") {
    std::unordered_set<Uuid> set;

    for (int i = 0; i < 1000; ++i) {
      set.insert(Uuid::generate_random());
    }

    CHECK(set.size() == 1000U);
  }

  TEST_CASE("std::hash gives same result for equal UUIDs") {
    std::array<uint8_t, 16> data{};
    data[3] = 0x42U;
    Uuid a{data};
    Uuid b{data};

    CHECK(std::hash<Uuid>{}(a) == std::hash<Uuid>{}(b));
  }

  TEST_CASE("UUID usable as unordered_map key") {
    std::unordered_map<Uuid, int> map;

    Uuid a = Uuid::generate_random();
    Uuid b = Uuid::generate_random();

    map.emplace(a, 1);
    map.emplace(b, 2);

    REQUIRE(map.size() == 2U);
    CHECK(map.at(a) == 1);
    CHECK(map.at(b) == 2);
  }
}

// ---------------------------------------------------------------------------
// TEST SUITE: generate_random
// ---------------------------------------------------------------------------

TEST_SUITE("base-Uuid - generate_random") {
  TEST_CASE("generated UUID carries RFC variant and v4 version") {
    Uuid id = Uuid::generate_random();

    CHECK(id.variant() == Uuid::Variant::kRfc);
    CHECK(id.version() == Uuid::Version::kRandomBased);
    CHECK_FALSE(id.is_nil());
  }

  TEST_CASE("two consecutive calls yield different UUIDs") {
    Uuid a = Uuid::generate_random();
    Uuid b = Uuid::generate_random();

    CHECK(a != b);
  }

  TEST_CASE("caller-supplied engine produces deterministic UUID for fixed seed") {
    std::mt19937 engine_one(0xdeadbeefU);
    std::mt19937 engine_two(0xdeadbeefU);

    Uuid a = Uuid::generate_random(engine_one);
    Uuid b = Uuid::generate_random(engine_two);

    CHECK(a == b);
    CHECK(a.variant() == Uuid::Variant::kRfc);
    CHECK(a.version() == Uuid::Version::kRandomBased);

    Uuid c = Uuid::generate_random(engine_one);

    CHECK(c != a);
    CHECK(c.variant() == Uuid::Variant::kRfc);
    CHECK(c.version() == Uuid::Version::kRandomBased);
  }

  TEST_CASE("variant and version bits hold across many random samples") {
    for (int i = 0; i < 256; ++i) {
      Uuid id = Uuid::generate_random();

      CHECK(id.variant() == Uuid::Variant::kRfc);
      CHECK(id.version() == Uuid::Version::kRandomBased);
    }
  }

  TEST_CASE("generate_random advertises noexcept") {
    CHECK(noexcept(Uuid::generate_random()));

    std::mt19937 engine(42U);

    CHECK(noexcept(Uuid::generate_random(engine)));
  }

  TEST_CASE("batch of 1000 random UUIDs has no collisions") {
    std::set<Uuid> seen;

    for (int i = 0; i < 1000; ++i) {
      seen.insert(Uuid::generate_random());
    }

    CHECK(seen.size() == 1000U);
  }
}

// ---------------------------------------------------------------------------
// TEST SUITE: random_bytes / random_hex
// ---------------------------------------------------------------------------

TEST_SUITE("base-Uuid - random_bytes and random_hex") {
  TEST_CASE("random_bytes returns requested length") {
    auto buf = Uuid::random_bytes(7U);

    CHECK(buf.size() == 7U);
  }

  TEST_CASE("random_bytes returns empty for zero count") {
    auto buf = Uuid::random_bytes(0U);

    CHECK(buf.empty());
  }

  TEST_CASE("random_bytes returns exactly count bytes for multi-word sizes") {
    for (size_t n : {1U, 3U, 4U, 5U, 8U, 15U, 16U, 17U, 32U, 64U, 127U, 128U}) {
      const auto buf = Uuid::random_bytes(n);

      CHECK(buf.size() == n);
    }
  }

  TEST_CASE("random_bytes produces variability") {
    auto a = Uuid::random_bytes(32U);
    auto b = Uuid::random_bytes(32U);

    CHECK(a != b);
  }

  TEST_CASE("random_hex returns lowercase hex of double length") {
    auto hex = Uuid::random_hex(16U);

    CHECK(hex.size() == 32U);

    for (char ch : hex) {
      const bool is_lower_hex = (ch >= '0' && ch <= '9') || (ch >= 'a' && ch <= 'f');
      CHECK(is_lower_hex);
    }
  }

  TEST_CASE("random_hex default emits 32 hex chars") {
    auto hex = Uuid::random_hex();

    CHECK(hex.size() == 32U);
  }

  TEST_CASE("random_hex handles odd byte counts") {
    auto hex = Uuid::random_hex(5U);

    CHECK(hex.size() == 10U);
  }

  TEST_CASE("random_hex returns empty for zero byte count") {
    auto hex = Uuid::random_hex(0U);

    CHECK(hex.empty());
  }

  TEST_CASE("random_hex output round-trips through from_string when byte_count == 16") {
    auto hex = Uuid::random_hex(16U);

    auto parsed = Uuid::from_string(hex);

    REQUIRE(parsed.has_value());
    CHECK(parsed->to_compact_string() == hex);
  }

  TEST_CASE("random_hex produces variability between calls") {
    auto a = Uuid::random_hex(16U);
    auto b = Uuid::random_hex(16U);

    CHECK(a != b);
  }

  TEST_CASE("random_hex / random_bytes are noexcept") {
    CHECK(noexcept(Uuid::random_bytes(1U)));
    CHECK(noexcept(Uuid::random_hex(1U)));
  }
}

// ---------------------------------------------------------------------------
// TEST SUITE: round-trips and cross-API consistency
// ---------------------------------------------------------------------------

TEST_SUITE("base-Uuid - round-trip") {
  TEST_CASE("to_string -> from_string round-trips many random UUIDs") {
    for (int i = 0; i < 256; ++i) {
      Uuid id = Uuid::generate_random();

      auto parsed = Uuid::from_string(id.to_string());

      REQUIRE(parsed.has_value());
      CHECK(*parsed == id);
    }
  }

  TEST_CASE("to_compact_string -> from_string round-trips many random UUIDs") {
    for (int i = 0; i < 256; ++i) {
      Uuid id = Uuid::generate_random();

      auto parsed = Uuid::from_string(id.to_compact_string());

      REQUIRE(parsed.has_value());
      CHECK(*parsed == id);
    }
  }
}

// NOLINTEND
