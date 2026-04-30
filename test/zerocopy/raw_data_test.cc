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

#include "./zerocopy/raw_data.h"

#include <doctest/doctest.h>

#include <cstring>
#include <vector>

#include "./base/bytes.h"

//
#include "../common_test.h"

namespace {

void fill_pattern(uint8_t* buf, size_t n, uint8_t seed = 0xA5) {
  for (size_t i = 0; i < n; ++i) {
    buf[i] = static_cast<uint8_t>(seed + static_cast<uint8_t>(i & 0xFF));
  }
}

bool region_matches(const uint8_t* a, const uint8_t* b, size_t n) { return std::memcmp(a, b, n) == 0; }

}  // namespace

// ---------------------------------------------------------------------------
// TEST SUITE: RawData - default construction
// ---------------------------------------------------------------------------

TEST_SUITE("zerocopy-RawData - default construction") {
  TEST_CASE("default-constructed RawData is invalid") {
    zerocopy::RawData rd;
    CHECK(!rd.is_valid());
    CHECK(rd.size() == 0);
    CHECK(!rd.is_owner());
    CHECK(rd.data() == nullptr);
  }
}

// ---------------------------------------------------------------------------
// TEST SUITE: RawData - create
// ---------------------------------------------------------------------------

TEST_SUITE("zerocopy-RawData - create") {
  TEST_CASE("create(N) succeeds for N > 0") {
    zerocopy::RawData rd;
    CHECK(rd.create(1024));
    CHECK(rd.is_valid());
    CHECK(rd.size() == 1024);
    CHECK(rd.is_owner());
    CHECK(rd.data() != nullptr);
  }

  TEST_CASE("create(0) returns false") {
    zerocopy::RawData rd;
    CHECK(!rd.create(0));
    CHECK(!rd.is_valid());
  }

  TEST_CASE("header fields survive create()") {
    zerocopy::RawData rd;
    rd.header.seq = 5;
    std::strncpy(rd.header.frame_id, "raw_0", sizeof(rd.header.frame_id) - 1);
    rd.header.frame_id[sizeof(rd.header.frame_id) - 1] = '\0';
    rd.header.time_meas = 12345ULL;
    rd.header.time_pub = 67890ULL;

    rd.create(256);

    CHECK(rd.header.seq == 5);
    CHECK(std::string(rd.header.frame_id) == "raw_0");
    CHECK(rd.header.time_meas == 12345ULL);
    CHECK(rd.header.time_pub == 67890ULL);
  }
}

// ---------------------------------------------------------------------------
// TEST SUITE: RawData - serialization
// ---------------------------------------------------------------------------

TEST_SUITE("zerocopy-RawData - serialization") {
  TEST_CASE("serialize and deserialize round-trip") {
    constexpr size_t kSize = 512;

    zerocopy::RawData rd;
    rd.header.seq = 1;
    rd.header.time_pub = 999999ULL;
    rd.create(kSize);

    fill_pattern(const_cast<uint8_t*>(rd.data()), kSize, 0x55);

    Bytes wire;
    bool ser_ok = (rd >> wire);
    CHECK(ser_ok);
    CHECK(zerocopy::RawData::check_valid(wire));
    CHECK(wire.size() == rd.get_serialized_size());

    zerocopy::RawData rd2;
    bool deser_ok = (rd2 << wire);
    CHECK(deser_ok);

    CHECK(rd2.is_valid());
    CHECK(rd2.size() == kSize);
    CHECK(!rd2.is_owner());
    CHECK(rd2.header.seq == 1);
    CHECK(rd2.header.time_pub == 999999ULL);
    CHECK(region_matches(rd.data(), rd2.data(), kSize));
  }

  TEST_CASE("check_valid with empty bytes returns false") {
    Bytes empty;
    CHECK(!zerocopy::RawData::check_valid(empty));
  }

  TEST_CASE("check_valid with corrupted bytes returns false") {
    zerocopy::RawData rd;
    rd.create(64);

    Bytes wire;
    rd >> wire;

    wire[0] ^= 0xFF;
    CHECK(!zerocopy::RawData::check_valid(wire));
  }

  TEST_CASE("operator>> on uninitialized RawData returns true with empty payload") {
    // An uninitialized RawData (size=0, data=null) serializes successfully:
    // it produces a magic-framed payload with zero-length data.
    zerocopy::RawData rd;
    Bytes wire;
    bool ok = (rd >> wire);
    CHECK(ok);
    // The serialized buffer should be valid (magic numbers intact).
    CHECK(zerocopy::RawData::check_valid(wire));
  }

  TEST_CASE("get_serialized_size is magic(4) + struct + data + magic(4)") {
    zerocopy::RawData rd;
    rd.create(100);

    size_t expected = sizeof(uint32_t) + sizeof(zerocopy::RawData) + 100 + sizeof(uint32_t);
    CHECK(rd.get_serialized_size() == expected);
  }

  TEST_CASE("deserialize from too-small buffer returns false") {
    std::vector<uint8_t> raw(4, 0x00);
    Bytes too_small(raw);
    zerocopy::RawData rd;
    bool ok = (rd << too_small);
    CHECK_FALSE(ok);
  }
}

// ---------------------------------------------------------------------------
// TEST SUITE: RawData - copy operations
// ---------------------------------------------------------------------------

TEST_SUITE("zerocopy-RawData - deep copy") {
  TEST_CASE("deep_copy from RawData") {
    zerocopy::RawData src;
    src.create(128);
    fill_pattern(const_cast<uint8_t*>(src.data()), 128, 0xCC);

    zerocopy::RawData dst;
    CHECK(dst.deep_copy(src));

    CHECK(dst.is_valid());
    CHECK(dst.is_owner());
    CHECK(dst.size() == 128);
    CHECK(region_matches(src.data(), dst.data(), 128));
  }

  TEST_CASE("deep_copy self returns false") {
    zerocopy::RawData rd;
    rd.create(64);
    CHECK(!rd.deep_copy(rd));
  }

  TEST_CASE("deep_copy from raw pointer") {
    constexpr size_t kN = 128;
    std::vector<uint8_t> src(kN);
    fill_pattern(src.data(), kN, 0x33);

    zerocopy::RawData rd;
    CHECK(rd.deep_copy(src.data(), kN));

    CHECK(rd.is_valid());
    CHECK(rd.is_owner());
    CHECK(rd.size() == kN);
    CHECK(region_matches(src.data(), rd.data(), kN));
  }

  TEST_CASE("deep_copy null pointer returns false") {
    zerocopy::RawData rd;
    CHECK(!rd.deep_copy(nullptr, 64));
  }

  TEST_CASE("deep_copy zero size returns false") {
    std::vector<uint8_t> buf(8);
    zerocopy::RawData rd;
    CHECK(!rd.deep_copy(buf.data(), 0));
  }

  TEST_CASE("fill_data is alias for deep_copy") {
    constexpr size_t kN = 32;
    std::vector<uint8_t> src(kN, 0xFE);

    zerocopy::RawData rd;
    CHECK(rd.fill_data(src.data(), kN));
    CHECK(rd.is_owner());
    CHECK(region_matches(src.data(), rd.data(), kN));
  }
}

TEST_SUITE("zerocopy-RawData - shallow copy") {
  TEST_CASE("shallow_copy from RawData") {
    zerocopy::RawData src;
    src.create(256);
    fill_pattern(const_cast<uint8_t*>(src.data()), 256, 0x11);

    zerocopy::RawData dst;
    CHECK(dst.shallow_copy(src));

    CHECK(dst.is_valid());
    CHECK(!dst.is_owner());
    CHECK(dst.size() == 256);
    CHECK(dst.data() == src.data());
  }

  TEST_CASE("shallow_copy self returns false") {
    zerocopy::RawData rd;
    rd.create(32);
    CHECK(!rd.shallow_copy(rd));
  }

  TEST_CASE("shallow_copy from raw pointer") {
    constexpr size_t kN = 64;
    std::vector<uint8_t> buf(kN, 0xAB);

    zerocopy::RawData rd;
    CHECK(rd.shallow_copy(buf.data(), kN));

    CHECK(rd.is_valid());
    CHECK(!rd.is_owner());
    CHECK(rd.size() == kN);
    CHECK(rd.data() == buf.data());
  }

  TEST_CASE("shallow_copy null pointer returns false") {
    zerocopy::RawData rd;
    CHECK(!rd.shallow_copy(nullptr, 64));
  }

  TEST_CASE("shallow_copy zero size returns false") {
    std::vector<uint8_t> buf(8, 0xFF);
    zerocopy::RawData rd;
    CHECK(!rd.shallow_copy(buf.data(), 0));
  }
}

TEST_SUITE("zerocopy-RawData - move copy") {
  TEST_CASE("move_copy transfers ownership") {
    zerocopy::RawData src;
    src.create(100);
    fill_pattern(const_cast<uint8_t*>(src.data()), 100, 0x77);

    const uint8_t* original_ptr = src.data();

    zerocopy::RawData dst;
    CHECK(dst.move_copy(src));

    CHECK(dst.is_valid());
    CHECK(dst.is_owner());
    CHECK(dst.data() == original_ptr);
    CHECK(dst.size() == 100);

    CHECK(!src.is_valid());
    CHECK(!src.is_owner());
    CHECK(src.size() == 0);
  }

  TEST_CASE("move_copy self returns false") {
    zerocopy::RawData rd;
    rd.create(32);
    CHECK(!rd.move_copy(rd));
  }
}

// ---------------------------------------------------------------------------
// TEST SUITE: RawData - reserved_buf
// ---------------------------------------------------------------------------

TEST_SUITE("zerocopy-RawData - reserved_buf") {
  TEST_CASE("reserved_buf can be set and read") {
    zerocopy::RawData rd;
    rd.create(16);

    rd.reserved_buf() = 0x1234;
    CHECK(rd.reserved_buf() == 0x1234);
  }

  TEST_CASE("reserved_buf survives serialization round-trip") {
    zerocopy::RawData rd;
    rd.create(32);
    rd.reserved_buf() = 0xBEEF;

    Bytes wire;
    rd >> wire;

    zerocopy::RawData rd2;
    rd2 << wire;

    CHECK(rd2.reserved_buf() == 0xBEEF);
  }
}

// ---------------------------------------------------------------------------
// TEST SUITE: RawData - C++ special members
// ---------------------------------------------------------------------------

TEST_SUITE("zerocopy-RawData - special members") {
  TEST_CASE("copy constructor makes deep copy") {
    zerocopy::RawData src;
    src.create(64);
    fill_pattern(const_cast<uint8_t*>(src.data()), 64, 0x9A);

    zerocopy::RawData copy(src);

    CHECK(copy.is_owner());
    CHECK(copy.size() == 64);
    CHECK(copy.data() != src.data());
    CHECK(region_matches(src.data(), copy.data(), 64));
  }

  TEST_CASE("move constructor transfers ownership") {
    zerocopy::RawData src;
    src.create(48);
    fill_pattern(const_cast<uint8_t*>(src.data()), 48, 0x5C);

    const uint8_t* ptr = src.data();

    zerocopy::RawData moved(std::move(src));

    CHECK(moved.is_owner());
    CHECK(moved.size() == 48);
    CHECK(moved.data() == ptr);
    CHECK(!src.is_valid());
  }

  TEST_CASE("copy assignment makes deep copy") {
    zerocopy::RawData src;
    src.create(80);
    fill_pattern(const_cast<uint8_t*>(src.data()), 80, 0x4D);

    zerocopy::RawData dst;
    dst = src;

    CHECK(dst.is_owner());
    CHECK(dst.size() == 80);
    CHECK(region_matches(src.data(), dst.data(), 80));
  }

  TEST_CASE("move assignment transfers ownership") {
    zerocopy::RawData src;
    src.create(96);

    const uint8_t* ptr = src.data();

    zerocopy::RawData dst;
    dst = std::move(src);

    CHECK(dst.is_owner());
    CHECK(dst.data() == ptr);
    CHECK(!src.is_valid());
  }

  TEST_CASE("clear() resets RawData to invalid state") {
    zerocopy::RawData rd;
    rd.create(200);
    CHECK(rd.is_valid());

    rd.clear();

    CHECK(!rd.is_valid());
    CHECK(!rd.is_owner());
    CHECK(rd.size() == 0);
    CHECK(rd.data() == nullptr);
  }
}

// ---------------------------------------------------------------------------
// TEST SUITE: RawData - additional edge cases
// ---------------------------------------------------------------------------

TEST_SUITE("zerocopy-RawData - edge cases") {
  TEST_CASE("sizeof(RawData) is 64 bytes") { CHECK(sizeof(zerocopy::RawData) == 64U); }

  TEST_CASE("create replaces previous owned buffer") {
    zerocopy::RawData rd;
    REQUIRE(rd.create(100));
    CHECK(rd.size() == 100);

    REQUIRE(rd.create(200));
    CHECK(rd.size() == 200);
    CHECK(rd.is_owner());
  }

  TEST_CASE("deep_copy into already-owned same-size buffer reuses memory") {
    zerocopy::RawData src;
    src.create(64);
    fill_pattern(const_cast<uint8_t*>(src.data()), 64, 0xAA);

    zerocopy::RawData dst;
    dst.create(64);

    dst.deep_copy(src);
    CHECK(dst.is_owner());
    CHECK(dst.size() == 64);
    CHECK(region_matches(src.data(), dst.data(), 64));
  }

  TEST_CASE("shallow_copy from raw pointer same pointer returns false") {
    std::vector<uint8_t> buf(64, 0x55);
    zerocopy::RawData rd;
    CHECK(rd.shallow_copy(buf.data(), buf.size()));
    CHECK(!rd.shallow_copy(buf.data(), buf.size()));
  }

  TEST_CASE("check_valid with end magic corrupted returns false") {
    zerocopy::RawData rd;
    rd.create(128);

    Bytes wire;
    rd >> wire;

    wire[wire.size() - 1] ^= 0xFF;
    CHECK(!zerocopy::RawData::check_valid(wire));
  }

  TEST_CASE("header fields survive serialization round-trip") {
    zerocopy::RawData rd;
    rd.header.seq = 42;
    std::strncpy(rd.header.frame_id, "sensor_1", sizeof(rd.header.frame_id) - 1);
    rd.header.frame_id[sizeof(rd.header.frame_id) - 1] = '\0';
    rd.header.time_meas = 100000ULL;
    rd.header.time_pub = 200000ULL;
    rd.create(16);

    Bytes wire;
    rd >> wire;

    zerocopy::RawData rd2;
    rd2 << wire;

    CHECK(rd2.header.seq == 42);
    CHECK(std::string(rd2.header.frame_id) == "sensor_1");
    CHECK(rd2.header.time_meas == 100000ULL);
    CHECK(rd2.header.time_pub == 200000ULL);
  }

  TEST_CASE("reserved_buf default value is 0") {
    zerocopy::RawData rd;
    CHECK(rd.reserved_buf() == 0);
  }

  TEST_CASE("reserved_buf max value") {
    zerocopy::RawData rd;
    rd.reserved_buf() = 0xFFFF;
    CHECK(rd.reserved_buf() == 0xFFFF);
  }

  TEST_CASE("clear resets reserved_buf and header") {
    zerocopy::RawData rd;
    rd.create(16);
    rd.reserved_buf() = 0x1234;
    rd.header.seq = 99;

    rd.clear();

    CHECK(rd.reserved_buf() == 0);
    CHECK(rd.header.seq == 0);
  }

  // TEST_CASE("copy assignment to self is no-op") {
  //   zerocopy::RawData rd;
  //   rd.create(64);
  //   fill_pattern(const_cast<uint8_t*>(rd.data()), 64, 0xCC);
  //   const uint8_t* ptr = rd.data();

  //   rd = rd;  // NOLINT

  //   CHECK(rd.is_owner());
  //   CHECK(rd.data() == ptr);
  // }

  // TEST_CASE("move assignment to self is no-op") {
  //   zerocopy::RawData rd;
  //   rd.create(64);
  //   const uint8_t* ptr = rd.data();

  //   rd = std::move(rd);  // NOLINT

  //   CHECK(rd.data() == ptr);
  //   CHECK(rd.is_owner());
  // }

  TEST_CASE("move_copy leaves source fully invalid") {
    zerocopy::RawData src;
    src.create(100);
    src.reserved_buf() = 0xABCD;

    zerocopy::RawData dst;
    CHECK(dst.move_copy(src));

    CHECK(!src.is_valid());
    CHECK(!src.is_owner());
    CHECK(src.data() == nullptr);
    CHECK(src.size() == 0);
  }

  TEST_CASE("clear then create again succeeds") {
    zerocopy::RawData rd;
    rd.create(100);
    rd.clear();

    CHECK(!rd.is_valid());
    CHECK(rd.create(50));
    CHECK(rd.is_valid());
    CHECK(rd.size() == 50);
  }

  TEST_CASE("get_serialized_size on empty RawData") {
    zerocopy::RawData rd;
    size_t sz = rd.get_serialized_size();
    CHECK(sz == sizeof(uint32_t) + sizeof(zerocopy::RawData) + 0 + sizeof(uint32_t));
  }

  TEST_CASE("fill_data null pointer returns false") {
    zerocopy::RawData rd;
    CHECK(!rd.fill_data(nullptr, 64));
  }

  TEST_CASE("fill_data zero size returns false") {
    std::vector<uint8_t> buf(8);
    zerocopy::RawData rd;
    CHECK(!rd.fill_data(buf.data(), 0));
  }

  TEST_CASE("reserved_buf survives copy constructor") {
    zerocopy::RawData src;
    src.create(16);
    src.reserved_buf() = 0x5678;

    zerocopy::RawData copy(src);
    CHECK(copy.reserved_buf() == 0x5678);
  }

  TEST_CASE("reserved_buf survives move constructor") {
    zerocopy::RawData src;
    src.create(16);
    src.reserved_buf() = 0x9ABC;

    zerocopy::RawData moved(std::move(src));
    CHECK(moved.reserved_buf() == 0x9ABC);
  }
}

// NOLINTEND
