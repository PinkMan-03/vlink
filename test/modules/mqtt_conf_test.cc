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

#ifdef VLINK_SUPPORT_MQTT

#include "../common_test.h"

TEST_SUITE("modules-MqttConf - construction") {
  TEST_CASE("construct with address only uses defaults") {
    MqttConf conf("vehicle/speed");
    CHECK(conf.address == "vehicle/speed");
    CHECK(conf.event.empty());
    CHECK(conf.domain == 0);
    CHECK(conf.qos == 1);
    CHECK(conf.fragment.empty());
  }

  TEST_CASE("construct with all params") {
    MqttConf conf("addr", "evt", 2, 0, "tcp://broker:1883");
    CHECK(conf.address == "addr");
    CHECK(conf.event == "evt");
    CHECK(conf.domain == 2);
    CHECK(conf.qos == 0);
    CHECK(conf.fragment == "tcp://broker:1883");
  }
}

TEST_SUITE("modules-MqttConf - equality operators") {
  TEST_CASE("equal configs compare equal") {
    MqttConf a("addr", "evt", 1, 2, "frag");
    MqttConf b("addr", "evt", 1, 2, "frag");
    CHECK(a == b);
    CHECK(!(a != b));
  }

  TEST_CASE("different address compares not equal") {
    MqttConf a("addr_a");
    MqttConf b("addr_b");
    CHECK(a != b);
  }

  TEST_CASE("different domain compares not equal") {
    MqttConf a("addr", "", 0);
    MqttConf b("addr", "", 1);
    CHECK(a != b);
  }

  TEST_CASE("different qos compares not equal") {
    MqttConf a("addr", "", 0, 0);
    MqttConf b("addr", "", 0, 2);
    CHECK(a != b);
  }

  TEST_CASE("different fragment compares not equal") {
    MqttConf a("addr", "", 0, 1, "frag_a");
    MqttConf b("addr", "", 0, 1, "frag_b");
    CHECK(a != b);
  }
}

TEST_SUITE("modules-MqttConf - transport type") {
  TEST_CASE("get_transport_type returns kMqtt") {
    MqttConf conf("address");
    CHECK(conf.get_transport_type() == TransportType::kMqtt);
  }
}

TEST_SUITE("modules-MqttConf - url parse") {
  TEST_CASE("url-parse-all-impl-types") {
    Url url("mqtt://mqtt/conf/parse1?event=ev1");

    CHECK(url.parse(kPublisher));
    CHECK(url.parse(kSubscriber));
    CHECK(url.parse(kServer));
    CHECK(url.parse(kClient));
    CHECK(url.parse(kSetter));
    CHECK(url.parse(kGetter));
  }

  TEST_CASE("unknown-impl-type-throws") {
    Url url("mqtt://mqtt/conf/parse2");

    CHECK_THROWS_AS(url.parse(kUnknownImplType), std::runtime_error);
  }

  TEST_CASE("invalid-transport-throws") { CHECK_THROWS(Publisher<int>("mqtt1://bad/url")); }
}

#else

TEST_SUITE("modules-MqttConf - not supported") {
  TEST_CASE("VLINK_SUPPORT_MQTT not defined - skip") { CHECK(true); }
}

#endif  // VLINK_SUPPORT_MQTT

// NOLINTEND
