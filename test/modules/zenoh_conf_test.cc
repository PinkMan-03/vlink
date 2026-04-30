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

#ifdef VLINK_SUPPORT_ZENOH

#include "../common_test.h"

TEST_SUITE("modules-ZenohConf - construction") {
  TEST_CASE("construct with address only uses defaults") {
    ZenohConf conf("vehicle/speed");
    CHECK(conf.address == "vehicle/speed");
    CHECK(conf.event.empty());
    CHECK(conf.domain == 0);
    CHECK(conf.qos.empty());
    CHECK(conf.fragment.empty());
  }

  TEST_CASE("construct with all params") {
    ZenohConf conf("addr", "evt", 2, "my_qos", "frag");
    CHECK(conf.address == "addr");
    CHECK(conf.event == "evt");
    CHECK(conf.domain == 2);
    CHECK(conf.qos == "my_qos");
    CHECK(conf.fragment == "frag");
  }
}

TEST_SUITE("modules-ZenohConf - equality operators") {
  TEST_CASE("equal configs compare equal") {
    ZenohConf a("addr", "evt", 1, "qos", "frag");
    ZenohConf b("addr", "evt", 1, "qos", "frag");
    CHECK(a == b);
    CHECK(!(a != b));
  }

  TEST_CASE("different address compares not equal") {
    ZenohConf a("addr_a");
    ZenohConf b("addr_b");
    CHECK(a != b);
  }

  TEST_CASE("different domain compares not equal") {
    ZenohConf a("addr", "", 0);
    ZenohConf b("addr", "", 1);
    CHECK(a != b);
  }

  TEST_CASE("different qos compares not equal") {
    ZenohConf a("addr", "", 0, "qos_a");
    ZenohConf b("addr", "", 0, "qos_b");
    CHECK(a != b);
  }

  TEST_CASE("different fragment compares not equal") {
    ZenohConf a("addr", "", 0, "", "frag_a");
    ZenohConf b("addr", "", 0, "", "frag_b");
    CHECK(a != b);
  }
}

TEST_SUITE("modules-ZenohConf - transport type") {
  TEST_CASE("get_transport_type returns kZenoh") {
    ZenohConf conf("address");
    CHECK(conf.get_transport_type() == TransportType::kZenoh);
  }
}

#else

TEST_SUITE("modules-ZenohConf - not supported") {
  TEST_CASE("VLINK_SUPPORT_ZENOH not defined - skip") { CHECK(true); }
}

#endif  // VLINK_SUPPORT_ZENOH

// NOLINTEND
