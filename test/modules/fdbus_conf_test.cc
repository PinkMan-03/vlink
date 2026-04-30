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

#ifdef VLINK_SUPPORT_FDBUS

#include "../common_test.h"

TEST_SUITE("modules-FdbusConf - construction") {
  TEST_CASE("construct with address only uses defaults") {
    FdbusConf conf("my_service");
    CHECK(conf.address == "my_service");
    CHECK(conf.event.empty());
    CHECK(conf.transport == "svc");
  }

  TEST_CASE("construct with address and event") {
    FdbusConf conf("my_service", "my_event");
    CHECK(conf.address == "my_service");
    CHECK(conf.event == "my_event");
    CHECK(conf.transport == "svc");
  }

  TEST_CASE("construct with ipc transport") {
    FdbusConf conf("addr", "", "ipc");
    CHECK(conf.transport == "ipc");
  }

  TEST_CASE("svc is default transport") {
    FdbusConf conf("addr");
    CHECK(conf.transport == "svc");
  }
}

TEST_SUITE("modules-FdbusConf - equality (transport excluded)") {
  TEST_CASE("same address and event are equal regardless of transport") {
    FdbusConf a("service", "event", "svc");
    FdbusConf b("service", "event", "ipc");

    // NOTE: transport is intentionally excluded from equality
    CHECK(a == b);
    CHECK(!(a != b));
  }

  TEST_CASE("different address compares not equal") {
    FdbusConf a("service_a");
    FdbusConf b("service_b");
    CHECK(a != b);
  }

  TEST_CASE("different event compares not equal") {
    FdbusConf a("service", "event_a");
    FdbusConf b("service", "event_b");
    CHECK(a != b);
  }

  TEST_CASE("equal address and event compare equal") {
    FdbusConf a("service", "event");
    FdbusConf b("service", "event");
    CHECK(a == b);
  }
}

TEST_SUITE("modules-FdbusConf - transport type") {
  TEST_CASE("get_transport_type returns kFdbus") {
    FdbusConf conf("addr");
    CHECK(conf.get_transport_type() == TransportType::kFdbus);
  }
}

TEST_SUITE("modules-FdbusConf - edge cases") {
  TEST_CASE("empty address") {
    FdbusConf conf("");
    CHECK(conf.address.empty());
    CHECK(conf.event.empty());
    CHECK(conf.transport == "svc");
  }

  TEST_CASE("self equality") {
    FdbusConf a("service", "event", "svc");
    CHECK(a == a);
    CHECK(!(a != a));
  }

  TEST_CASE("both empty address and event are equal") {
    FdbusConf a("");
    FdbusConf b("");
    CHECK(a == b);
  }

  TEST_CASE("transport field stores custom value") {
    FdbusConf conf("addr", "evt", "custom_transport");
    CHECK(conf.transport == "custom_transport");
  }

  TEST_CASE("long address string") {
    std::string long_addr(256, 'a');
    FdbusConf conf(long_addr);
    CHECK(conf.address == long_addr);
  }

  TEST_CASE("construct with all parameters") {
    FdbusConf conf("my_address", "my_event", "ipc");
    CHECK(conf.address == "my_address");
    CHECK(conf.event == "my_event");
    CHECK(conf.transport == "ipc");
  }

  TEST_CASE("equality ignores transport difference") {
    FdbusConf a("addr", "evt", "svc");
    FdbusConf b("addr", "evt", "custom");
    CHECK(a == b);
  }

  TEST_CASE("inequality when address differs but event same") {
    FdbusConf a("addr_a", "evt");
    FdbusConf b("addr_b", "evt");
    CHECK(a != b);
  }

  TEST_CASE("inequality when event differs but address same") {
    FdbusConf a("addr", "evt_a");
    FdbusConf b("addr", "evt_b");
    CHECK(a != b);
  }
}

#else

TEST_SUITE("modules-FdbusConf - not supported") {
  TEST_CASE("VLINK_SUPPORT_FDBUS not defined - skip") { CHECK(true); }
}

#endif  // VLINK_SUPPORT_FDBUS

// NOLINTEND
