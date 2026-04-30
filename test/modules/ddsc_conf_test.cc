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

#ifdef VLINK_SUPPORT_DDSC

#include "../common_test.h"

TEST_SUITE("modules-DdscConf - construction") {
  TEST_CASE("construct with topic only uses defaults") {
    DdscConf conf("vehicle/speed");
    CHECK(conf.topic == "vehicle/speed");
    CHECK(conf.domain == 0);
    CHECK(conf.depth == 0);
    CHECK(conf.qos.empty());
  }

  TEST_CASE("construct with topic, domain, depth") {
    DdscConf conf("my_topic", 3, 20);
    CHECK(conf.topic == "my_topic");
    CHECK(conf.domain == 3);
    CHECK(conf.depth == 20);
  }

  TEST_CASE("construct with named QoS") {
    DdscConf conf("my_topic", 0, 0, "my_qos");
    CHECK(conf.qos == "my_qos");
  }
}

TEST_SUITE("modules-DdscConf - equality operators") {
  TEST_CASE("equal configs compare equal") {
    DdscConf a("topic", 1, 5, "event");
    DdscConf b("topic", 1, 5, "event");
    CHECK(a == b);
    CHECK(!(a != b));
  }

  TEST_CASE("different topic compares not equal") {
    DdscConf a("topic_a");
    DdscConf b("topic_b");
    CHECK(a != b);
  }

  TEST_CASE("different domain compares not equal") {
    DdscConf a("topic", 0);
    DdscConf b("topic", 2);
    CHECK(a != b);
  }
}

TEST_SUITE("modules-DdscConf - transport type") {
  TEST_CASE("get_transport_type returns kDdsc") {
    DdscConf conf("topic");
    CHECK(conf.get_transport_type() == TransportType::kDdsc);
  }
}

#else

TEST_SUITE("modules-DdscConf - not supported") {
  TEST_CASE("VLINK_SUPPORT_DDSC not defined - skip") { CHECK(true); }
}

#endif  // VLINK_SUPPORT_DDSC

// NOLINTEND
