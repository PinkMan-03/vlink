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

#include <doctest/doctest.h>

#include <string>

#ifdef VLINK_SUPPORT_SHM2

#include "../common_test.h"

TEST_SUITE("modules-Shm2Conf - constants") {
  TEST_CASE("kDefaultMemSize is 128") { CHECK(Shm2Conf::kDefaultMemSize == 128u); }

  TEST_CASE("kMaxMemSize is 32 MiB") { CHECK(Shm2Conf::kMaxMemSize == 1024UL * 1024UL * 32UL); }
}

TEST_SUITE("modules-Shm2Conf - construction") {
  TEST_CASE("construct with address only uses defaults") {
    Shm2Conf conf("my_topic");
    CHECK(conf.address == "my_topic");
    CHECK(conf.event.empty());
    CHECK(conf.domain == 0);
    CHECK(conf.depth == 0);
    CHECK(conf.history == 0);
    CHECK(conf.wait == 0);
    CHECK(conf.size == Shm2Conf::kDefaultMemSize);
  }

  TEST_CASE("construct with custom size") {
    Shm2Conf conf("topic", "", 0, 0, 0, 0, 1024 * 1024);
    CHECK(conf.size == 1024 * 1024u);
  }

  TEST_CASE("construct with all params") {
    Shm2Conf conf("addr", "evt", 2, 8, 3, 1, 512);
    CHECK(conf.address == "addr");
    CHECK(conf.event == "evt");
    CHECK(conf.domain == 2);
    CHECK(conf.depth == 8);
    CHECK(conf.history == 3);
    CHECK(conf.wait == 1);
    CHECK(conf.size == 512u);
  }
}

TEST_SUITE("modules-Shm2Conf - equality operators") {
  TEST_CASE("equal configs compare equal") {
    Shm2Conf a("addr", "evt", 0, 0, 0, 0, 256);
    Shm2Conf b("addr", "evt", 0, 0, 0, 0, 256);
    CHECK(a == b);
    CHECK(!(a != b));
  }

  TEST_CASE("different size compares not equal") {
    Shm2Conf a("addr", "", 0, 0, 0, 0, 128);
    Shm2Conf b("addr", "", 0, 0, 0, 0, 256);
    CHECK(a != b);
  }

  TEST_CASE("different address compares not equal") {
    Shm2Conf a("addr_a");
    Shm2Conf b("addr_b");
    CHECK(a != b);
  }
}

TEST_SUITE("modules-Shm2Conf - transport type") {
  TEST_CASE("get_transport_type returns kShm2") {
    Shm2Conf conf("topic");
    CHECK(conf.get_transport_type() == TransportType::kShm2);
  }
}

#else

TEST_SUITE("modules-Shm2Conf - not supported") {
  TEST_CASE("VLINK_SUPPORT_SHM2 not defined - skip") { CHECK(true); }
}

#endif  // VLINK_SUPPORT_SHM2

// NOLINTEND
