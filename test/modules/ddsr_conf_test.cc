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

#ifdef VLINK_SUPPORT_DDSR

#include "../common_test.h"

TEST_SUITE("modules-DdsrConf - construction") {
  TEST_CASE("construct with topic only uses defaults") {
    DdsrConf conf("vehicle/speed");
    CHECK(conf.topic == "vehicle/speed");
    CHECK(conf.domain == 0);
    CHECK(conf.depth == 0);
    CHECK(conf.qos.empty());
  }

  TEST_CASE("construct with topic, domain, depth") {
    DdsrConf conf("my_topic", 2, 15);
    CHECK(conf.topic == "my_topic");
    CHECK(conf.domain == 2);
    CHECK(conf.depth == 15);
  }

  TEST_CASE("construct with named QoS") {
    DdsrConf conf("my_topic", 0, 0, "rti_qos");
    CHECK(conf.qos == "rti_qos");
  }
}

TEST_SUITE("modules-DdsrConf - equality operators") {
  TEST_CASE("equal configs compare equal") {
    DdsrConf a("topic", 1, 5, "event");
    DdsrConf b("topic", 1, 5, "event");
    CHECK(a == b);
    CHECK(!(a != b));
  }

  TEST_CASE("different topic compares not equal") {
    DdsrConf a("topic_a");
    DdsrConf b("topic_b");
    CHECK(a != b);
  }

  TEST_CASE("different domain compares not equal") {
    DdsrConf a("topic", 0);
    DdsrConf b("topic", 3);
    CHECK(a != b);
  }
}

TEST_SUITE("modules-DdsrConf - transport type") {
  TEST_CASE("get_transport_type returns kDdsr") {
    DdsrConf conf("topic");
    CHECK(conf.get_transport_type() == TransportType::kDdsr);
  }
}

TEST_SUITE("modules-DdsrConf - qos_ext constructor") {
  TEST_CASE("construct with qos_ext map") {
    DdsrConf::PropertiesMap ext;
    ext["writer"] = "writer_profile";
    ext["reader"] = "reader_profile";

    DdsrConf conf("my_topic", 2, ext);
    CHECK(conf.topic == "my_topic");
    CHECK(conf.domain == 2);
    CHECK(conf.depth == 0);
    CHECK(conf.qos.empty());
    CHECK(!conf.qos_ext.empty());
    CHECK(conf.qos_ext.at("writer") == "writer_profile");
  }
}

TEST_SUITE("modules-DdsrConf - equality edge cases") {
  TEST_CASE("different depth compares not equal") {
    DdsrConf a("topic", 0, 5);
    DdsrConf b("topic", 0, 10);
    CHECK(a != b);
  }

  TEST_CASE("different qos string compares not equal") {
    DdsrConf a("topic", 0, 0, "qos_a");
    DdsrConf b("topic", 0, 0, "qos_b");
    CHECK(a != b);
  }

  TEST_CASE("self equality") {
    DdsrConf a("topic", 1, 5, "qos");
    CHECK(a == a);
    CHECK(!(a != a));
  }

  TEST_CASE("empty topic configs are equal") {
    DdsrConf a("");
    DdsrConf b("");
    CHECK(a == b);
  }
}

TEST_SUITE("modules-DdsrConf - default values") {
  TEST_CASE("default domain is 0") {
    DdsrConf conf("topic");
    CHECK(conf.domain == 0);
  }

  TEST_CASE("default depth is 0") {
    DdsrConf conf("topic");
    CHECK(conf.depth == 0);
  }

  TEST_CASE("default qos is empty") {
    DdsrConf conf("topic");
    CHECK(conf.qos.empty());
  }

  TEST_CASE("qos_ext is empty by default for basic constructor") {
    DdsrConf conf("topic", 0, 0, "");
    CHECK(conf.qos_ext.empty());
  }
}

#else

TEST_SUITE("modules-DdsrConf - not supported") {
  TEST_CASE("VLINK_SUPPORT_DDSR not defined - skip") { CHECK(true); }
}

#endif  // VLINK_SUPPORT_DDSR

// NOLINTEND
