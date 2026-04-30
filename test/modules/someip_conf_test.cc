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

#include <set>
#include <string>

#ifdef VLINK_SUPPORT_SOMEIP

#include "../common_test.h"

TEST_SUITE("modules-SomeipConf - RPC construction") {
  TEST_CASE("construct RPC config with service/instance/method") {
    SomeipConf conf(0x1234, 0x5678, 0x0001);
    CHECK(conf.service == 0x1234);
    CHECK(conf.instance == 0x5678);
    CHECK(conf.method == 0x0001);
    CHECK(conf.groups.empty());
    CHECK(conf.event == 0);
    CHECK(conf.field == false);
  }

  TEST_CASE("RPC config method is set") {
    SomeipConf conf(0x0100, 0x0200, 0x0010);
    CHECK(conf.method == 0x0010);
  }
}

TEST_SUITE("modules-SomeipConf - event/field construction") {
  TEST_CASE("construct event config with groups and event") {
    SomeipConf::Groups groups = {0x0001, 0x0002};
    SomeipConf conf(0x1234, 0x5678, groups, 0x0010, false);

    CHECK(conf.service == 0x1234);
    CHECK(conf.instance == 0x5678);
    CHECK(conf.groups == groups);
    CHECK(conf.event == 0x0010);
    CHECK(conf.field == false);
    CHECK(conf.method == 0);
  }

  TEST_CASE("construct field config with field=true") {
    SomeipConf::Groups groups = {0x0001};
    SomeipConf conf(0x1234, 0x5678, groups, 0x0020, true);

    CHECK(conf.field == true);
    CHECK(conf.event == 0x0020);
  }

  TEST_CASE("groups can contain multiple IDs") {
    SomeipConf::Groups groups = {0x0001, 0x0002, 0x0003};
    SomeipConf conf(0x1, 0x1, groups, 0x01);
    CHECK(conf.groups.size() == 3);
    CHECK(conf.groups.count(0x0001) == 1);
    CHECK(conf.groups.count(0x0002) == 1);
    CHECK(conf.groups.count(0x0003) == 1);
  }
}

TEST_SUITE("modules-SomeipConf - equality operators") {
  TEST_CASE("equal RPC configs compare equal") {
    SomeipConf a(0x1234, 0x5678, 0x0001);
    SomeipConf b(0x1234, 0x5678, 0x0001);
    CHECK(a == b);
    CHECK(!(a != b));
  }

  TEST_CASE("different service compares not equal") {
    SomeipConf a(0x0001, 0x0002, 0x0003);
    SomeipConf b(0x0002, 0x0002, 0x0003);
    CHECK(a != b);
  }

  TEST_CASE("different instance compares not equal") {
    SomeipConf a(0x0001, 0x0001, 0x0003);
    SomeipConf b(0x0001, 0x0002, 0x0003);
    CHECK(a != b);
  }

  TEST_CASE("different method compares not equal") {
    SomeipConf a(0x0001, 0x0002, 0x0003);
    SomeipConf b(0x0001, 0x0002, 0x0004);
    CHECK(a != b);
  }

  TEST_CASE("equal event configs compare equal") {
    SomeipConf::Groups g = {0x0001};
    SomeipConf a(0x1234, 0x5678, g, 0x0010, false);
    SomeipConf b(0x1234, 0x5678, g, 0x0010, false);
    CHECK(a == b);
  }

  TEST_CASE("different field flag compares not equal") {
    SomeipConf::Groups g = {0x0001};
    SomeipConf a(0x1234, 0x5678, g, 0x0010, false);
    SomeipConf b(0x1234, 0x5678, g, 0x0010, true);
    CHECK(a != b);
  }
}

TEST_SUITE("modules-SomeipConf - transport type") {
  TEST_CASE("get_transport_type returns kSomeip") {
    SomeipConf conf(0x0001, 0x0001, 0x0001);
    CHECK(conf.get_transport_type() == TransportType::kSomeip);
  }
}

#else

TEST_SUITE("modules-SomeipConf - not supported") {
  TEST_CASE("VLINK_SUPPORT_SOMEIP not defined - skip") { CHECK(true); }
}

#endif  // VLINK_SUPPORT_SOMEIP

// NOLINTEND
