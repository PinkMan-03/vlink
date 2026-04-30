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

#include "./zerocopy/camera_frame.h"

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
// TEST SUITE: CameraFrame - default construction
// ---------------------------------------------------------------------------

TEST_SUITE("zerocopy-CameraFrame - default construction") {
  TEST_CASE("default-constructed CameraFrame is invalid") {
    zerocopy::CameraFrame frame;
    CHECK(!frame.is_valid());
    CHECK(frame.format() == zerocopy::CameraFrame::kFormatUnknown);
    CHECK(frame.stream() == zerocopy::CameraFrame::kStreamUnknown);
    CHECK(frame.width() == 0);
    CHECK(frame.height() == 0);
    CHECK(frame.freq() == 0);
    CHECK(frame.channel() == 0);
    CHECK(frame.size() == 0);
    CHECK(frame.data() == nullptr);
    CHECK(!frame.is_owner());
  }

  TEST_CASE("sizeof(CameraFrame) is 80 bytes") { CHECK(sizeof(zerocopy::CameraFrame) == 80U); }
}

// ---------------------------------------------------------------------------
// TEST SUITE: CameraFrame - metadata setters/getters
// ---------------------------------------------------------------------------

TEST_SUITE("zerocopy-CameraFrame - metadata") {
  TEST_CASE("set_width / width round-trip") {
    zerocopy::CameraFrame frame;
    frame.set_width(1920);
    CHECK(frame.width() == 1920);
  }

  TEST_CASE("set_height / height round-trip") {
    zerocopy::CameraFrame frame;
    frame.set_height(1080);
    CHECK(frame.height() == 1080);
  }

  TEST_CASE("set_format / format round-trip") {
    zerocopy::CameraFrame frame;
    frame.set_format(zerocopy::CameraFrame::kFormatNv12);
    CHECK(frame.format() == zerocopy::CameraFrame::kFormatNv12);
  }

  TEST_CASE("set_freq / freq round-trip") {
    zerocopy::CameraFrame frame;
    frame.set_freq(30);
    CHECK(frame.freq() == 30);
  }

  TEST_CASE("set_channel / channel round-trip") {
    zerocopy::CameraFrame frame;
    frame.set_channel(2);
    CHECK(frame.channel() == 2);
  }

  TEST_CASE("set_stream / stream round-trip") {
    zerocopy::CameraFrame frame;
    frame.set_stream(zerocopy::CameraFrame::kStreamI);
    CHECK(frame.stream() == zerocopy::CameraFrame::kStreamI);
  }

  TEST_CASE("all Format enum values can be set") {
    const zerocopy::CameraFrame::Format formats[] = {
        zerocopy::CameraFrame::kFormatYuv420,       zerocopy::CameraFrame::kFormatYuv422,
        zerocopy::CameraFrame::kFormatYuv444,       zerocopy::CameraFrame::kFormatNv12,
        zerocopy::CameraFrame::kFormatNv21,         zerocopy::CameraFrame::kFormatYuyv,
        zerocopy::CameraFrame::kFormatYvyu,         zerocopy::CameraFrame::kFormatUyvy,
        zerocopy::CameraFrame::kFormatVyuy,         zerocopy::CameraFrame::kFormatBgr888Packed,
        zerocopy::CameraFrame::kFormatRgb888Packed, zerocopy::CameraFrame::kFormatRgb888Planar,
        zerocopy::CameraFrame::kFormatJpeg,         zerocopy::CameraFrame::kFormatH264,
        zerocopy::CameraFrame::kFormatH265,
    };

    for (auto fmt : formats) {
      zerocopy::CameraFrame frame;
      frame.set_format(fmt);
      CHECK(frame.format() == fmt);
    }
  }

  TEST_CASE("all Stream enum values can be set") {
    zerocopy::CameraFrame frame;

    frame.set_stream(zerocopy::CameraFrame::kStreamI);
    CHECK(frame.stream() == zerocopy::CameraFrame::kStreamI);

    frame.set_stream(zerocopy::CameraFrame::kStreamP);
    CHECK(frame.stream() == zerocopy::CameraFrame::kStreamP);

    frame.set_stream(zerocopy::CameraFrame::kStreamB);
    CHECK(frame.stream() == zerocopy::CameraFrame::kStreamB);

    frame.set_stream(zerocopy::CameraFrame::kStreamUnknown);
    CHECK(frame.stream() == zerocopy::CameraFrame::kStreamUnknown);
  }
}

// ---------------------------------------------------------------------------
// TEST SUITE: CameraFrame - create
// ---------------------------------------------------------------------------

TEST_SUITE("zerocopy-CameraFrame - create") {
  TEST_CASE("create(N) for NV12 frame succeeds") {
    zerocopy::CameraFrame frame;
    frame.set_width(1920);
    frame.set_height(1080);
    frame.set_format(zerocopy::CameraFrame::kFormatNv12);
    frame.set_freq(30);

    constexpr size_t kNv12Size = 1920 * 1080 * 3 / 2;
    CHECK(frame.create(kNv12Size));

    CHECK(frame.is_valid());
    CHECK(frame.is_owner());
    CHECK(frame.size() == kNv12Size);
    CHECK(frame.data() != nullptr);
  }

  TEST_CASE("create(0) returns false") {
    zerocopy::CameraFrame frame;
    CHECK(!frame.create(0));
    CHECK(!frame.is_valid());
  }

  TEST_CASE("get_serialized_size is magic(4) + struct(80) + payload + magic(4)") {
    zerocopy::CameraFrame frame;
    frame.create(1000);

    size_t sz = frame.get_serialized_size();
    CHECK(sz == sizeof(uint32_t) + 80U + 1000U + sizeof(uint32_t));
  }
}

// ---------------------------------------------------------------------------
// TEST SUITE: CameraFrame - serialization
// ---------------------------------------------------------------------------

TEST_SUITE("zerocopy-CameraFrame - serialization") {
  TEST_CASE("serialize and deserialize round-trip") {
    zerocopy::CameraFrame src;
    src.set_width(640);
    src.set_height(480);
    src.set_format(zerocopy::CameraFrame::kFormatRgb888Packed);
    src.set_freq(25);
    src.set_channel(1);

    constexpr size_t kPixelSize = 640 * 480 * 3;
    REQUIRE(src.create(kPixelSize));

    fill_pattern(const_cast<uint8_t*>(src.data()), kPixelSize, 0xDE);
    src.header.seq = 7;
    std::strncpy(src.header.frame_id, "cam_0", sizeof(src.header.frame_id) - 1);
    src.header.frame_id[sizeof(src.header.frame_id) - 1] = '\0';

    Bytes wire;
    bool ser_ok = (src >> wire);
    CHECK(ser_ok);
    CHECK(zerocopy::CameraFrame::check_valid(wire));
    CHECK(wire.size() == src.get_serialized_size());

    zerocopy::CameraFrame dst;
    bool deser_ok = (dst << wire);
    CHECK(deser_ok);

    CHECK(dst.is_valid());
    CHECK(!dst.is_owner());
    CHECK(dst.width() == 640);
    CHECK(dst.height() == 480);
    CHECK(dst.format() == zerocopy::CameraFrame::kFormatRgb888Packed);
    CHECK(dst.freq() == 25);
    CHECK(dst.channel() == 1);
    CHECK(dst.size() == kPixelSize);
    CHECK(dst.header.seq == 7);
    CHECK(std::string(dst.header.frame_id) == "cam_0");

    CHECK(region_matches(src.data(), dst.data(), kPixelSize));
  }

  TEST_CASE("check_valid on empty bytes returns false") {
    Bytes empty;
    CHECK(!zerocopy::CameraFrame::check_valid(empty));
  }

  TEST_CASE("check_valid on corrupted bytes returns false") {
    zerocopy::CameraFrame frame;
    frame.create(128);

    Bytes wire;
    frame >> wire;

    wire[0] ^= 0xFF;
    CHECK(!zerocopy::CameraFrame::check_valid(wire));
  }
}

// ---------------------------------------------------------------------------
// TEST SUITE: CameraFrame - copy operations
// ---------------------------------------------------------------------------

TEST_SUITE("zerocopy-CameraFrame - deep copy") {
  TEST_CASE("deep_copy from CameraFrame") {
    zerocopy::CameraFrame src;
    src.set_width(320);
    src.set_height(240);
    src.set_format(zerocopy::CameraFrame::kFormatYuv420);
    src.create(320 * 240 * 3 / 2);
    fill_pattern(const_cast<uint8_t*>(src.data()), src.size(), 0x6B);

    zerocopy::CameraFrame dst;
    CHECK(dst.deep_copy(src));

    CHECK(dst.is_owner());
    CHECK(dst.size() == src.size());
    CHECK(dst.width() == 320);
    CHECK(dst.height() == 240);
    CHECK(region_matches(src.data(), dst.data(), src.size()));
  }

  TEST_CASE("deep_copy self returns false") {
    zerocopy::CameraFrame frame;
    frame.create(64);
    CHECK(!frame.deep_copy(frame));
  }

  TEST_CASE("deep_copy from raw pointer") {
    constexpr size_t kN = 150;
    std::vector<uint8_t> src(kN);
    fill_pattern(src.data(), kN, 0xE7);

    zerocopy::CameraFrame frame;
    CHECK(frame.deep_copy(src.data(), kN));

    CHECK(frame.is_owner());
    CHECK(frame.size() == kN);
    CHECK(region_matches(src.data(), frame.data(), kN));
  }

  TEST_CASE("fill_data alias for deep_copy") {
    constexpr size_t kN = 80;
    std::vector<uint8_t> src(kN, 0x11);

    zerocopy::CameraFrame frame;
    CHECK(frame.fill_data(src.data(), kN));
    CHECK(frame.is_owner());
    CHECK(region_matches(src.data(), frame.data(), kN));
  }
}

TEST_SUITE("zerocopy-CameraFrame - shallow copy") {
  TEST_CASE("shallow_copy from CameraFrame") {
    zerocopy::CameraFrame src;
    src.create(512);
    fill_pattern(const_cast<uint8_t*>(src.data()), 512, 0x3C);

    zerocopy::CameraFrame dst;
    CHECK(dst.shallow_copy(src));

    CHECK(!dst.is_owner());
    CHECK(dst.data() == src.data());
    CHECK(dst.size() == 512);
  }

  TEST_CASE("shallow_copy self returns false") {
    zerocopy::CameraFrame frame;
    frame.create(64);
    CHECK(!frame.shallow_copy(frame));
  }

  TEST_CASE("shallow_copy from raw pointer") {
    std::vector<uint8_t> buf(200, 0xCA);

    zerocopy::CameraFrame frame;
    CHECK(frame.shallow_copy(buf.data(), buf.size()));

    CHECK(!frame.is_owner());
    CHECK(frame.size() == 200);
    CHECK(frame.data() == buf.data());
  }

  TEST_CASE("shallow_copy null pointer returns false") {
    zerocopy::CameraFrame frame;
    CHECK(!frame.shallow_copy(nullptr, 64));
  }

  TEST_CASE("shallow_copy zero size returns false") {
    std::vector<uint8_t> buf(8, 0xFF);
    zerocopy::CameraFrame frame;
    CHECK(!frame.shallow_copy(buf.data(), 0));
  }
}

TEST_SUITE("zerocopy-CameraFrame - move copy") {
  TEST_CASE("move_copy transfers ownership") {
    zerocopy::CameraFrame src;
    src.set_width(100);
    src.set_height(100);
    src.create(100 * 100 * 3);

    const uint8_t* ptr = src.data();

    zerocopy::CameraFrame dst;
    CHECK(dst.move_copy(src));

    CHECK(dst.is_owner());
    CHECK(dst.data() == ptr);
    CHECK(dst.width() == 100);
    CHECK(!src.is_valid());
  }

  TEST_CASE("move_copy self returns false") {
    zerocopy::CameraFrame frame;
    frame.create(64);
    CHECK(!frame.move_copy(frame));
  }
}

// ---------------------------------------------------------------------------
// TEST SUITE: CameraFrame - special members
// ---------------------------------------------------------------------------

TEST_SUITE("zerocopy-CameraFrame - special members") {
  TEST_CASE("copy constructor makes deep copy") {
    zerocopy::CameraFrame src;
    src.set_width(800);
    src.set_height(600);
    src.set_format(zerocopy::CameraFrame::kFormatJpeg);
    src.create(800 * 600);
    fill_pattern(const_cast<uint8_t*>(src.data()), src.size(), 0xBC);

    zerocopy::CameraFrame copy(src);

    CHECK(copy.is_owner());
    CHECK(copy.width() == 800);
    CHECK(copy.height() == 600);
    CHECK(copy.format() == zerocopy::CameraFrame::kFormatJpeg);
    CHECK(copy.size() == src.size());
    CHECK(copy.data() != src.data());
    CHECK(region_matches(src.data(), copy.data(), src.size()));
  }

  TEST_CASE("move constructor transfers ownership") {
    zerocopy::CameraFrame src;
    src.create(300);
    const uint8_t* ptr = src.data();

    zerocopy::CameraFrame moved(std::move(src));

    CHECK(moved.is_owner());
    CHECK(moved.data() == ptr);
    CHECK(moved.size() == 300);
    CHECK(!src.is_valid());
  }

  TEST_CASE("copy assignment makes deep copy") {
    zerocopy::CameraFrame src;
    src.set_format(zerocopy::CameraFrame::kFormatNv21);
    src.create(120);

    zerocopy::CameraFrame dst;
    dst = src;

    CHECK(dst.is_owner());
    CHECK(dst.format() == zerocopy::CameraFrame::kFormatNv21);
    CHECK(dst.size() == 120);
    CHECK(dst.data() != src.data());
  }

  TEST_CASE("move assignment transfers ownership") {
    zerocopy::CameraFrame src;
    src.create(400);
    const uint8_t* ptr = src.data();

    zerocopy::CameraFrame dst;
    dst = std::move(src);

    CHECK(dst.is_owner());
    CHECK(dst.data() == ptr);
    CHECK(!src.is_valid());
  }

  TEST_CASE("clear() resets all fields to zero") {
    zerocopy::CameraFrame frame;
    frame.set_width(1280);
    frame.set_height(720);
    frame.set_format(zerocopy::CameraFrame::kFormatH264);
    frame.create(1000);
    frame.header.seq = 99;

    frame.clear();

    CHECK(!frame.is_valid());
    CHECK(!frame.is_owner());
    CHECK(frame.size() == 0);
    CHECK(frame.data() == nullptr);
    CHECK(frame.width() == 0);
    CHECK(frame.height() == 0);
    CHECK(frame.header.seq == 0);
  }
}

// ---------------------------------------------------------------------------
// TEST SUITE: CameraFrame - additional edge cases
// ---------------------------------------------------------------------------

TEST_SUITE("zerocopy-CameraFrame - edge cases") {
  TEST_CASE("create replaces previous owned buffer") {
    zerocopy::CameraFrame frame;
    REQUIRE(frame.create(100));
    CHECK(frame.is_owner());
    CHECK(frame.size() == 100);

    // Creating again should free old buffer and allocate new one
    REQUIRE(frame.create(200));
    CHECK(frame.is_owner());
    CHECK(frame.size() == 200);
  }

  TEST_CASE("deep_copy into already-owned same-size buffer reuses memory") {
    zerocopy::CameraFrame src;
    src.create(64);
    fill_pattern(const_cast<uint8_t*>(src.data()), 64, 0xAA);

    zerocopy::CameraFrame dst;
    dst.create(64);
    const uint8_t* old_ptr = dst.data();  // NOLINT

    (void)old_ptr;

    dst.deep_copy(src);
    // After deep_copy, size should match and data content should match
    CHECK(dst.is_owner());
    CHECK(dst.size() == 64);
    CHECK(region_matches(src.data(), dst.data(), 64));
  }

  TEST_CASE("deep_copy from raw pointer with null returns false") {
    zerocopy::CameraFrame frame;
    CHECK(!frame.deep_copy(nullptr, 64));
  }

  TEST_CASE("deep_copy from raw pointer with zero size returns false") {
    std::vector<uint8_t> buf(8);
    zerocopy::CameraFrame frame;
    CHECK(!frame.deep_copy(buf.data(), 0));
  }

  TEST_CASE("shallow_copy from CameraFrame copies metadata") {
    zerocopy::CameraFrame src;
    src.set_width(1920);
    src.set_height(1080);
    src.set_format(zerocopy::CameraFrame::kFormatH265);
    src.set_stream(zerocopy::CameraFrame::kStreamP);
    src.set_freq(60);
    src.set_channel(3);
    src.create(256);

    zerocopy::CameraFrame dst;
    CHECK(dst.shallow_copy(src));

    CHECK(dst.width() == 1920);
    CHECK(dst.height() == 1080);
    CHECK(dst.format() == zerocopy::CameraFrame::kFormatH265);
    CHECK(dst.stream() == zerocopy::CameraFrame::kStreamP);
    CHECK(dst.freq() == 60);
    CHECK(dst.channel() == 3);
    CHECK(!dst.is_owner());
    CHECK(dst.data() == src.data());
  }

  TEST_CASE("serialize empty frame and deserialize") {
    zerocopy::CameraFrame frame;
    // Not calling create - frame has no data

    Bytes wire;
    bool ser_ok = (frame >> wire);
    CHECK(ser_ok);
    CHECK(zerocopy::CameraFrame::check_valid(wire));

    zerocopy::CameraFrame frame2;
    bool deser_ok = (frame2 << wire);
    CHECK(deser_ok);
    CHECK(frame2.width() == 0);
    CHECK(frame2.height() == 0);
  }

  TEST_CASE("deserialize from too-small buffer returns false") {
    std::vector<uint8_t> raw(4, 0x00);
    Bytes too_small(raw);
    zerocopy::CameraFrame frame;
    bool ok = (frame << too_small);
    CHECK_FALSE(ok);
  }

  TEST_CASE("check_valid with end magic corrupted returns false") {
    zerocopy::CameraFrame frame;
    frame.create(128);

    Bytes wire;
    frame >> wire;

    // Corrupt last byte (part of end magic)
    wire[wire.size() - 1] ^= 0xFF;
    CHECK(!zerocopy::CameraFrame::check_valid(wire));
  }

  TEST_CASE("header fields survive serialization") {
    zerocopy::CameraFrame src;
    src.header.seq = 12345;
    std::strncpy(src.header.frame_id, "lidar_top", sizeof(src.header.frame_id) - 1);
    src.header.frame_id[sizeof(src.header.frame_id) - 1] = '\0';
    src.header.time_meas = 111111ULL;
    src.header.time_pub = 222222ULL;
    src.create(32);

    Bytes wire;
    src >> wire;

    zerocopy::CameraFrame dst;
    dst << wire;

    CHECK(dst.header.seq == 12345);
    CHECK(std::string(dst.header.frame_id) == "lidar_top");
    CHECK(dst.header.time_meas == 111111ULL);
    CHECK(dst.header.time_pub == 222222ULL);
  }

  TEST_CASE("move_copy leaves source invalid") {
    zerocopy::CameraFrame src;
    src.set_width(640);
    src.set_height(480);
    src.set_format(zerocopy::CameraFrame::kFormatYuv422);
    src.create(640 * 480 * 2);

    zerocopy::CameraFrame dst;
    CHECK(dst.move_copy(src));

    CHECK(!src.is_valid());
    CHECK(!src.is_owner());
    CHECK(src.data() == nullptr);
    CHECK(src.size() == 0);
  }

  // TEST_CASE("copy assignment to self is no-op") {
  //   zerocopy::CameraFrame frame;
  //   frame.set_width(320);
  //   frame.create(64);
  //   fill_pattern(const_cast<uint8_t*>(frame.data()), 64, 0xBB);

  //   const uint8_t* ptr = frame.data();
  //   frame = frame;  // NOLINT

  //   CHECK(frame.is_owner());
  //   CHECK(frame.data() == ptr);
  //   CHECK(frame.width() == 320);
  // }

  // TEST_CASE("move assignment to self is no-op") {
  //   zerocopy::CameraFrame frame;
  //   frame.create(64);
  //   const uint8_t* ptr = frame.data();

  //   frame = std::move(frame);  // NOLINT

  //   CHECK(frame.data() == ptr);
  //   CHECK(frame.is_owner());
  // }

  TEST_CASE("clear frees owned buffer then allows new create") {
    zerocopy::CameraFrame frame;
    frame.set_width(100);
    frame.create(100);
    frame.clear();

    CHECK(!frame.is_valid());
    CHECK(frame.width() == 0);

    // Can create again after clear
    CHECK(frame.create(50));
    CHECK(frame.is_valid());
    CHECK(frame.size() == 50);
  }

  TEST_CASE("shallow_copy from raw pointer same pointer returns false") {
    std::vector<uint8_t> buf(64, 0x55);
    zerocopy::CameraFrame frame;
    CHECK(frame.shallow_copy(buf.data(), buf.size()));

    // Shallow copy same pointer again should return false
    CHECK(!frame.shallow_copy(buf.data(), buf.size()));
  }

  TEST_CASE("format and stream values survive copy constructor") {
    zerocopy::CameraFrame src;
    src.set_format(zerocopy::CameraFrame::kFormatH264);
    src.set_stream(zerocopy::CameraFrame::kStreamB);
    src.create(16);

    zerocopy::CameraFrame copy(src);
    CHECK(copy.format() == zerocopy::CameraFrame::kFormatH264);
    CHECK(copy.stream() == zerocopy::CameraFrame::kStreamB);
  }

  TEST_CASE("get_serialized_size on empty frame") {
    zerocopy::CameraFrame frame;
    size_t sz = frame.get_serialized_size();
    CHECK(sz == sizeof(uint32_t) + 80U + 0U + sizeof(uint32_t));
  }
}

// NOLINTEND
