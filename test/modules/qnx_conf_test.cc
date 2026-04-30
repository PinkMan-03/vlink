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

#ifdef VLINK_SUPPORT_QNX

#include "../common_test.h"

TEST_SUITE("modules-QnxConf - construction") {
  TEST_CASE("construct with address only uses defaults") {
    QnxConf conf("my_channel");
    CHECK(conf.address == "my_channel");
    CHECK(conf.event.empty());
  }

  TEST_CASE("construct with address and event") {
    QnxConf conf("my_channel", "my_event");
    CHECK(conf.address == "my_channel");
    CHECK(conf.event == "my_event");
  }
}

TEST_SUITE("modules-QnxConf - equality operators") {
  TEST_CASE("equal configs compare equal") {
    QnxConf a("channel", "event");
    QnxConf b("channel", "event");
    CHECK(a == b);
    CHECK(!(a != b));
  }

  TEST_CASE("different address compares not equal") {
    QnxConf a("channel_a");
    QnxConf b("channel_b");
    CHECK(a != b);
  }

  TEST_CASE("different event compares not equal") {
    QnxConf a("channel", "event_a");
    QnxConf b("channel", "event_b");
    CHECK(a != b);
  }
}

TEST_SUITE("modules-QnxConf - transport type") {
  TEST_CASE("get_transport_type returns kQnx") {
    QnxConf conf("channel");
    CHECK(conf.get_transport_type() == TransportType::kQnx);
  }
}

TEST_SUITE("modules-QnxConf - edge cases") {
  TEST_CASE("empty address") {
    QnxConf conf("");
    CHECK(conf.address.empty());
    CHECK(conf.event.empty());
  }

  TEST_CASE("self equality") {
    QnxConf a("channel", "event");
    CHECK(a == a);
    CHECK(!(a != a));
  }

  TEST_CASE("same address, empty vs non-empty event") {
    QnxConf a("channel");
    QnxConf b("channel", "event");
    CHECK(a != b);
  }

  TEST_CASE("both empty address and event are equal") {
    QnxConf a("");
    QnxConf b("");
    CHECK(a == b);
  }

  TEST_CASE("long address string") {
    std::string long_addr(256, 'x');
    QnxConf conf(long_addr);
    CHECK(conf.address == long_addr);
    CHECK(conf.address.size() == 256);
  }

  TEST_CASE("address with special characters") {
    QnxConf conf("/dev/qnx/channel_1");
    CHECK(conf.address == "/dev/qnx/channel_1");
  }
}

#else

TEST_SUITE("modules-QnxConf - not supported") {
  TEST_CASE("VLINK_SUPPORT_QNX not defined - skip") { CHECK(true); }
}

#endif  // VLINK_SUPPORT_QNX

// NOLINTEND
