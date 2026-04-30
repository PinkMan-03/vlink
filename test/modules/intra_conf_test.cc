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

#ifdef VLINK_SUPPORT_INTRA

#include "../common_test.h"

// ---------------------------------------------------------------------------
// TEST SUITE: IntraConf - construction
// ---------------------------------------------------------------------------

TEST_SUITE("modules-IntraConf - construction") {
  TEST_CASE("construct with address only") {
    IntraConf conf("my_topic");
    CHECK(conf.address == "my_topic");
    CHECK(conf.event.empty());
    CHECK(conf.pipeline == 0);
    CHECK(conf.type == "queue");
  }

  TEST_CASE("construct with address and event") {
    IntraConf conf("my_service", "my_event");
    CHECK(conf.address == "my_service");
    CHECK(conf.event == "my_event");
    CHECK(conf.pipeline == 0);
    CHECK(conf.type == "queue");
  }

  TEST_CASE("construct with all params") {
    IntraConf conf("topic", "evt", 4, "direct");
    CHECK(conf.address == "topic");
    CHECK(conf.event == "evt");
    CHECK(conf.pipeline == 4);
    CHECK(conf.type == "direct");
  }

  TEST_CASE("queue type is default") {
    IntraConf conf("addr");
    CHECK(conf.type == "queue");
  }
}

// ---------------------------------------------------------------------------
// TEST SUITE: IntraConf - equality
// ---------------------------------------------------------------------------

TEST_SUITE("modules-IntraConf - equality operators") {
  TEST_CASE("equal configurations compare equal") {
    IntraConf a("topic", "event", 2, "queue");
    IntraConf b("topic", "event", 2, "queue");
    CHECK(a == b);
    CHECK(!(a != b));
  }

  TEST_CASE("different address compares not equal") {
    IntraConf a("topic_a");
    IntraConf b("topic_b");
    CHECK(a != b);
    CHECK(!(a == b));
  }

  TEST_CASE("different event compares not equal") {
    IntraConf a("topic", "event1");
    IntraConf b("topic", "event2");
    CHECK(a != b);
  }

  TEST_CASE("different pipeline compares not equal") {
    IntraConf a("topic", "", 0);
    IntraConf b("topic", "", 4);
    CHECK(a != b);
  }

  TEST_CASE("different type compares not equal") {
    IntraConf a("topic", "", 0, "queue");
    IntraConf b("topic", "", 0, "direct");
    CHECK(a != b);
  }
}

// ---------------------------------------------------------------------------
// TEST SUITE: IntraConf - transport type
// ---------------------------------------------------------------------------

TEST_SUITE("modules-IntraConf - transport type") {
  TEST_CASE("get_transport_type returns kIntra") {
    IntraConf conf("topic");
    CHECK(conf.get_transport_type() == TransportType::kIntra);
  }
}

#else

TEST_SUITE("modules-IntraConf - not supported") {
  TEST_CASE("VLINK_SUPPORT_INTRA not defined - skip") {
    CHECK(true);  // placeholder when module is not compiled
  }
}

#endif  // VLINK_SUPPORT_INTRA

// NOLINTEND
