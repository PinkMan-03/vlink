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

#ifdef VLINK_SUPPORT_DDST

#include "../common_test.h"

TEST_SUITE("modules-DdstConf - construction") {
  TEST_CASE("construct with topic only uses defaults") {
    DdstConf conf("vehicle/speed");
    CHECK(conf.topic == "vehicle/speed");
    CHECK(conf.domain == 0);
    CHECK(conf.depth == 0);
    CHECK(conf.qos.empty());
  }

  TEST_CASE("construct with topic, domain, depth") {
    DdstConf conf("my_topic", 4, 8);
    CHECK(conf.topic == "my_topic");
    CHECK(conf.domain == 4);
    CHECK(conf.depth == 8);
  }

  TEST_CASE("construct with named QoS") {
    DdstConf conf("my_topic", 0, 0, "travo_qos");
    CHECK(conf.qos == "travo_qos");
  }
}

TEST_SUITE("modules-DdstConf - equality operators") {
  TEST_CASE("equal configs compare equal") {
    DdstConf a("topic", 2, 10, "qos");
    DdstConf b("topic", 2, 10, "qos");
    CHECK(a == b);
    CHECK(!(a != b));
  }

  TEST_CASE("different topic compares not equal") {
    DdstConf a("topic_a");
    DdstConf b("topic_b");
    CHECK(a != b);
  }

  TEST_CASE("different domain compares not equal") {
    DdstConf a("topic", 0);
    DdstConf b("topic", 1);
    CHECK(a != b);
  }
}

TEST_SUITE("modules-DdstConf - transport type") {
  TEST_CASE("get_transport_type returns kDdst") {
    DdstConf conf("topic");
    CHECK(conf.get_transport_type() == TransportType::kDdst);
  }
}

TEST_SUITE("modules-DdstConf - qos_ext constructor") {
  TEST_CASE("construct with qos_ext map") {
    DdstConf::PropertiesMap ext;
    ext["pub"] = "pub_profile";
    ext["sub"] = "sub_profile";

    DdstConf conf("my_topic", 1, ext);
    CHECK(conf.topic == "my_topic");
    CHECK(conf.domain == 1);
    CHECK(conf.depth == 0);
    CHECK(conf.qos.empty());
    CHECK(!conf.qos_ext.empty());
    CHECK(conf.qos_ext.at("pub") == "pub_profile");
    CHECK(conf.qos_ext.at("sub") == "sub_profile");
  }
}

TEST_SUITE("modules-DdstConf - equality with qos_ext") {
  TEST_CASE("different depth compares not equal") {
    DdstConf a("topic", 0, 5);
    DdstConf b("topic", 0, 10);
    CHECK(a != b);
  }

  TEST_CASE("different qos string compares not equal") {
    DdstConf a("topic", 0, 0, "qos_a");
    DdstConf b("topic", 0, 0, "qos_b");
    CHECK(a != b);
  }

  TEST_CASE("equal with qos_ext compares equal") {
    DdstConf::PropertiesMap ext;
    ext["pub"] = "profile";

    DdstConf a("topic", 0, ext);
    DdstConf b("topic", 0, ext);
    CHECK(a == b);
  }

  TEST_CASE("different qos_ext compares not equal") {
    DdstConf::PropertiesMap ext1;
    ext1["pub"] = "profile1";
    DdstConf::PropertiesMap ext2;
    ext2["pub"] = "profile2";

    DdstConf a("topic", 0, ext1);
    DdstConf b("topic", 0, ext2);
    CHECK(a != b);
  }

  TEST_CASE("self equality") {
    DdstConf a("topic", 1, 5, "qos");
    CHECK(a == a);
    CHECK(!(a != a));
  }
}

TEST_SUITE("modules-DdstConf - default values") {
  TEST_CASE("default domain is 0") {
    DdstConf conf("topic");
    CHECK(conf.domain == 0);
  }

  TEST_CASE("default depth is 0") {
    DdstConf conf("topic");
    CHECK(conf.depth == 0);
  }

  TEST_CASE("empty topic") {
    DdstConf conf("");
    CHECK(conf.topic.empty());
  }
}

#else

TEST_SUITE("modules-DdstConf - not supported") {
  TEST_CASE("VLINK_SUPPORT_DDST not defined - skip") { CHECK(true); }
}

#endif  // VLINK_SUPPORT_DDST

// NOLINTEND
