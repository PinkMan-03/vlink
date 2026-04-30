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

#include "./zerocopy/header.h"

#include <doctest/doctest.h>

//
#include "../common_test.h"

// ---------------------------------------------------------------------------
// TEST SUITE: zerocopy::Header
// ---------------------------------------------------------------------------

TEST_SUITE("zerocopy-Header - default construction") {
  TEST_CASE("default-constructed Header has expected default fields") {
    zerocopy::Header h;
    CHECK(h.seq == 0);
    CHECK(std::string(h.frame_id) == "unknown");
    CHECK(h.time_meas == 0);
    CHECK(h.time_pub == 0);
    CHECK(h.reserved == 0);
  }

  TEST_CASE("sizeof(Header) is 40 bytes") { CHECK(sizeof(zerocopy::Header) == 40U); }
}

TEST_SUITE("zerocopy-Header - field assignment") {
  TEST_CASE("fields can be assigned and read back") {
    zerocopy::Header h;
    h.seq = 42;
    std::strncpy(h.frame_id, "cam_front", sizeof(h.frame_id) - 1);
    h.frame_id[sizeof(h.frame_id) - 1] = '\0';
    h.time_meas = 1000000ULL;
    h.time_pub = 2000000ULL;
    h.reserved = 0;

    CHECK(h.seq == 42);
    CHECK(std::string(h.frame_id) == "cam_front");
    CHECK(h.time_meas == 1000000ULL);
    CHECK(h.time_pub == 2000000ULL);
    CHECK(h.reserved == 0);
  }

  TEST_CASE("seq max value") {
    zerocopy::Header h;
    h.seq = 0xFFFFFFFFU;
    CHECK(h.seq == 0xFFFFFFFFU);
  }

  TEST_CASE("frame_id stores short string") {
    zerocopy::Header h;
    std::strncpy(h.frame_id, "lidar_top", sizeof(h.frame_id) - 1);
    h.frame_id[sizeof(h.frame_id) - 1] = '\0';
    CHECK(std::string(h.frame_id) == "lidar_top");
  }

  TEST_CASE("frame_id stores max-length string") {
    zerocopy::Header h;
    // 15 chars + null terminator fills all 16 bytes
    std::strncpy(h.frame_id, "123456789012345", sizeof(h.frame_id) - 1);
    h.frame_id[sizeof(h.frame_id) - 1] = '\0';
    CHECK(std::string(h.frame_id) == "123456789012345");
  }

  TEST_CASE("time_meas max value") {
    zerocopy::Header h;
    h.time_meas = 0xFFFFFFFFFFFFFFFFULL;
    CHECK(h.time_meas == 0xFFFFFFFFFFFFFFFFULL);
  }

  TEST_CASE("time_pub max value") {
    zerocopy::Header h;
    h.time_pub = 0xFFFFFFFFFFFFFFFFULL;
    CHECK(h.time_pub == 0xFFFFFFFFFFFFFFFFULL);
  }

  TEST_CASE("reserved can be used as a scratch field") {
    zerocopy::Header h;
    h.reserved = 0xDEADBEEF;
    CHECK(h.reserved == 0xDEADBEEF);
  }
}

TEST_SUITE("zerocopy-Header - copy semantics") {
  TEST_CASE("copy constructor creates independent copy") {
    zerocopy::Header a;
    a.seq = 10;
    std::strncpy(a.frame_id, "cam_rear", sizeof(a.frame_id) - 1);
    a.frame_id[sizeof(a.frame_id) - 1] = '\0';
    a.time_meas = 300;
    a.time_pub = 400;

    zerocopy::Header b = a;

    CHECK(b.seq == 10);
    CHECK(std::string(b.frame_id) == "cam_rear");
    CHECK(b.time_meas == 300);
    CHECK(b.time_pub == 400);

    // Modifying copy must not affect original
    b.seq = 99;
    CHECK(a.seq == 10);
  }

  TEST_CASE("copy assignment works correctly") {
    zerocopy::Header a;
    a.seq = 5;
    a.time_pub = 12345ULL;

    zerocopy::Header b;
    b = a;

    CHECK(b.seq == 5);
    CHECK(b.time_pub == 12345ULL);
  }
}

TEST_SUITE("zerocopy-Header - zero-init on construction") {
  TEST_CASE("multiple default-constructed Headers have consistent fields") {
    zerocopy::Header h1;
    zerocopy::Header h2;

    CHECK(h1.seq == h2.seq);
    CHECK(std::string(h1.frame_id) == std::string(h2.frame_id));
    CHECK(h1.time_meas == h2.time_meas);
    CHECK(h1.time_pub == h2.time_pub);
    CHECK(h1.reserved == h2.reserved);
  }
}

// ---------------------------------------------------------------------------
// TEST SUITE: zerocopy::Header - additional edge cases
// ---------------------------------------------------------------------------

TEST_SUITE("zerocopy-Header - edge cases") {
  TEST_CASE("Header is standard layout") { CHECK(std::is_standard_layout_v<zerocopy::Header>); }

  TEST_CASE("Header is trivially copyable") { CHECK(std::is_trivially_copyable_v<zerocopy::Header>); }

  TEST_CASE("move constructor works correctly") {
    zerocopy::Header a;
    a.seq = 100;
    std::strncpy(a.frame_id, "radar_0", sizeof(a.frame_id) - 1);
    a.frame_id[sizeof(a.frame_id) - 1] = '\0';
    a.time_meas = 300;
    a.time_pub = 400;
    a.reserved = 500;

    zerocopy::Header b = std::move(a);
    CHECK(b.seq == 100);
    CHECK(std::string(b.frame_id) == "radar_0");
    CHECK(b.time_meas == 300);
    CHECK(b.time_pub == 400);
    CHECK(b.reserved == 500);
  }

  TEST_CASE("move assignment works correctly") {
    zerocopy::Header a;
    a.seq = 11;
    a.time_meas = 22;

    zerocopy::Header b;
    b = std::move(a);
    CHECK(b.seq == 11);
    CHECK(b.time_meas == 22);
  }

  TEST_CASE("Header binary layout is 40 bytes with memcpy round-trip") {
    zerocopy::Header h;
    h.seq = 0xAABBCCDD;
    std::strncpy(h.frame_id, "test_frame", sizeof(h.frame_id) - 1);
    h.frame_id[sizeof(h.frame_id) - 1] = '\0';
    h.time_meas = 0x5566778899AABBCCULL;
    h.time_pub = 0xDDEEFF0011223344ULL;
    h.reserved = 0;

    uint8_t buf[40] = {};
    std::memcpy(buf, &h, sizeof(h));

    zerocopy::Header h2;
    std::memcpy(&h2, buf, sizeof(h2));

    CHECK(h2.seq == 0xAABBCCDD);
    CHECK(std::string(h2.frame_id) == "test_frame");
    CHECK(h2.time_meas == 0x5566778899AABBCCULL);
    CHECK(h2.time_pub == 0xDDEEFF0011223344ULL);
    CHECK(h2.reserved == 0);
  }

  TEST_CASE("all fields can hold boundary values simultaneously") {
    zerocopy::Header h;
    h.seq = 0xFFFFFFFFU;
    std::memset(h.frame_id, 0xFF, sizeof(h.frame_id));
    h.time_meas = 0xFFFFFFFFFFFFFFFFULL;
    h.time_pub = 0xFFFFFFFFFFFFFFFFULL;
    h.reserved = 0xFFFFFFFFU;

    CHECK(h.seq == 0xFFFFFFFFU);
    CHECK(static_cast<uint8_t>(h.frame_id[0]) == 0xFF);
    CHECK(h.time_meas == 0xFFFFFFFFFFFFFFFFULL);
    CHECK(h.time_pub == 0xFFFFFFFFFFFFFFFFULL);
    CHECK(h.reserved == 0xFFFFFFFFU);
  }

  // TEST_CASE("Header can be zero-initialized via memset") {
  //   zerocopy::Header h;
  //   std::memset(&h, 0, sizeof(h));
  //   CHECK(h.seq == 0);
  //   CHECK(h.frame_id[0] == '\0');
  //   CHECK(h.time_meas == 0);
  //   CHECK(h.time_pub == 0);
  //   CHECK(h.reserved == 0);
  // }

  // TEST_CASE("Header self-assignment is safe") {
  //   zerocopy::Header h;
  //   h.seq = 42;

  //   h = h;  // NOLINT

  //   CHECK(h.seq == 42);
  // }
}

// NOLINTEND
