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

#ifdef VLINK_SUPPORT_DDS

#include "../common_test.h"

TEST_SUITE("modules-DdsConf - construction") {
  TEST_CASE("construct with topic only uses defaults") {
    DdsConf conf("vehicle/speed");
    CHECK(conf.topic == "vehicle/speed");
    CHECK(conf.domain == 0);
    CHECK(conf.depth == 0);
    CHECK(conf.qos.empty());
    CHECK(conf.qos_ext.empty());
  }

  TEST_CASE("construct with topic, domain, depth") {
    DdsConf conf("my_topic", 5, 10);
    CHECK(conf.topic == "my_topic");
    CHECK(conf.domain == 5);
    CHECK(conf.depth == 10);
    CHECK(conf.qos.empty());
  }

  TEST_CASE("construct with named QoS") {
    DdsConf conf("my_topic", 0, 0, "my_qos_profile");
    CHECK(conf.qos == "my_qos_profile");
  }

  TEST_CASE("construct with qos_ext map") {
    DdsConf::PropertiesMap ext;
    ext["pub"] = "some_pub_profile";

    DdsConf conf("my_topic", 0, ext);
    CHECK(!conf.qos_ext.empty());
    CHECK(conf.qos_ext.at("pub") == "some_pub_profile");
  }
}

TEST_SUITE("modules-DdsConf - equality operators") {
  TEST_CASE("equal configs compare equal") {
    DdsConf a("topic", 1, 5, "event");
    DdsConf b("topic", 1, 5, "event");
    CHECK(a == b);
    CHECK(!(a != b));
  }

  TEST_CASE("different topic compares not equal") {
    DdsConf a("topic_a");
    DdsConf b("topic_b");
    CHECK(a != b);
  }

  TEST_CASE("different domain compares not equal") {
    DdsConf a("topic", 0);
    DdsConf b("topic", 1);
    CHECK(a != b);
  }

  TEST_CASE("different qos compares not equal") {
    DdsConf a("topic", 0, 0, "qos_a");
    DdsConf b("topic", 0, 0, "qos_b");
    CHECK(a != b);
  }
}

TEST_SUITE("modules-DdsConf - transport type") {
  TEST_CASE("get_transport_type returns kDds") {
    DdsConf conf("topic");
    CHECK(conf.get_transport_type() == TransportType::kDds);
  }
}

#else

TEST_SUITE("modules-DdsConf - not supported") {
  TEST_CASE("VLINK_SUPPORT_DDS not defined - skip") { CHECK(true); }
}

#endif  // VLINK_SUPPORT_DDS

// NOLINTEND
