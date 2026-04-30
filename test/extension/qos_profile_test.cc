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

#include "./extension/qos_profile.h"

#include <doctest/doctest.h>

#include <string>
#include <unordered_map>

//
#include "../common_test.h"

// ---------------------------------------------------------------------------
// TEST SUITE: Individual profiles
// ---------------------------------------------------------------------------

TEST_SUITE("extension-QosProfile - individual profiles") {
  TEST_CASE("kEvent profile") {
    const Qos& q = QosProfile::kEvent;

    CHECK(q.valid == true);
    CHECK(q.reliability.kind == Qos::Reliability::kReliable);
    CHECK(q.history.kind == Qos::History::kKeepLast);
    CHECK(q.history.depth == 10);
    CHECK(q.durability.kind == Qos::Durability::kVolatile);
    CHECK(q.publish_mode.kind == Qos::PublishMode::kSync);
    CHECK(q.additions.priority == Qos::Additions::kPriorityRealTime);
    CHECK(q.additions.is_express == false);

    std::string name(q.name);
    CHECK(name == "event");
  }

  TEST_CASE("kMethod profile") {
    const Qos& q = QosProfile::kMethod;

    CHECK(q.valid == true);
    CHECK(q.reliability.kind == Qos::Reliability::kReliable);
    CHECK(q.history.kind == Qos::History::kKeepAll);
    CHECK(q.durability.kind == Qos::Durability::kVolatile);
    CHECK(q.publish_mode.kind == Qos::PublishMode::kSync);
    CHECK(q.additions.priority == Qos::Additions::kPriorityHigh);

    std::string name(q.name);
    CHECK(name == "method");
  }

  TEST_CASE("kField profile") {
    const Qos& q = QosProfile::kField;

    CHECK(q.valid == true);
    CHECK(q.reliability.kind == Qos::Reliability::kReliable);
    CHECK(q.history.kind == Qos::History::kKeepLast);
    CHECK(q.history.depth == 1);
    CHECK(q.durability.kind == Qos::Durability::kTransientLocal);
    CHECK(q.publish_mode.kind == Qos::PublishMode::kSync);
    CHECK(q.additions.priority == Qos::Additions::kPriorityHigh);

    std::string name(q.name);
    CHECK(name == "field");
  }

  TEST_CASE("kSensor profile") {
    const Qos& q = QosProfile::kSensor;

    CHECK(q.valid == true);
    CHECK(q.reliability.kind == Qos::Reliability::kBestEffort);
    CHECK(q.history.kind == Qos::History::kKeepLast);
    CHECK(q.history.depth == 20);
    CHECK(q.durability.kind == Qos::Durability::kVolatile);
    CHECK(q.publish_mode.kind == Qos::PublishMode::kASync);
    CHECK(q.additions.priority == Qos::Additions::kPriorityNormal);
    CHECK(q.additions.is_express == true);

    std::string name(q.name);
    CHECK(name == "sensor");
  }

  TEST_CASE("kParameter profile") {
    const Qos& q = QosProfile::kParameter;

    CHECK(q.valid == true);
    CHECK(q.reliability.kind == Qos::Reliability::kReliable);
    CHECK(q.history.kind == Qos::History::kKeepLast);
    CHECK(q.history.depth == 1000);
    CHECK(q.durability.kind == Qos::Durability::kVolatile);
    CHECK(q.publish_mode.kind == Qos::PublishMode::kSync);
    CHECK(q.additions.priority == Qos::Additions::kPriorityNormal);

    std::string name(q.name);
    CHECK(name == "parameter");
  }

  TEST_CASE("kService profile") {
    const Qos& q = QosProfile::kService;

    CHECK(q.valid == true);
    CHECK(q.reliability.kind == Qos::Reliability::kReliable);
    CHECK(q.history.kind == Qos::History::kKeepLast);
    CHECK(q.history.depth == 10);
    CHECK(q.durability.kind == Qos::Durability::kTransientLocal);
    CHECK(q.publish_mode.kind == Qos::PublishMode::kSync);

    std::string name(q.name);
    CHECK(name == "service");
  }

  TEST_CASE("kClock profile") {
    const Qos& q = QosProfile::kClock;

    CHECK(q.valid == true);
    CHECK(q.reliability.kind == Qos::Reliability::kBestEffort);
    CHECK(q.history.kind == Qos::History::kKeepLast);
    CHECK(q.history.depth == 1);
    CHECK(q.durability.kind == Qos::Durability::kVolatile);
    CHECK(q.publish_mode.kind == Qos::PublishMode::kASync);
    CHECK(q.additions.priority == Qos::Additions::kPriorityLow);

    std::string name(q.name);
    CHECK(name == "clock");
  }

  TEST_CASE("kStatic profile") {
    const Qos& q = QosProfile::kStatic;

    CHECK(q.valid == true);
    CHECK(q.reliability.kind == Qos::Reliability::kReliable);
    CHECK(q.history.kind == Qos::History::kKeepAll);
    CHECK(q.durability.kind == Qos::Durability::kTransientLocal);
    CHECK(q.publish_mode.kind == Qos::PublishMode::kSync);

    std::string name(q.name);
    CHECK(name == "static");
  }

  TEST_CASE("kLight profile") {
    const Qos& q = QosProfile::kLight;

    CHECK(q.valid == true);
    CHECK(q.reliability.kind == Qos::Reliability::kReliable);
    CHECK(q.history.kind == Qos::History::kKeepLast);
    CHECK(q.history.depth == 1);
    CHECK(q.durability.kind == Qos::Durability::kVolatile);
    CHECK(q.publish_mode.kind == Qos::PublishMode::kASync);
    CHECK(q.additions.priority == Qos::Additions::kPriorityHigh);

    std::string name(q.name);
    CHECK(name == "light");
  }

  TEST_CASE("kPoor profile") {
    const Qos& q = QosProfile::kPoor;

    CHECK(q.valid == true);
    CHECK(q.reliability.kind == Qos::Reliability::kBestEffort);
    CHECK(q.history.kind == Qos::History::kKeepLast);
    CHECK(q.history.depth == 5);
    CHECK(q.durability.kind == Qos::Durability::kVolatile);
    CHECK(q.publish_mode.kind == Qos::PublishMode::kASync);
    CHECK(q.additions.priority == Qos::Additions::kPriorityBackground);

    std::string name(q.name);
    CHECK(name == "poor");
  }

  TEST_CASE("kBetter profile") {
    const Qos& q = QosProfile::kBetter;

    CHECK(q.valid == true);
    CHECK(q.reliability.kind == Qos::Reliability::kBestEffort);
    CHECK(q.history.kind == Qos::History::kKeepLast);
    CHECK(q.history.depth == 50);
    CHECK(q.publish_mode.kind == Qos::PublishMode::kSync);
    CHECK(q.additions.priority == Qos::Additions::kPriorityRealTime);

    std::string name(q.name);
    CHECK(name == "better");
  }

  TEST_CASE("kBest profile") {
    const Qos& q = QosProfile::kBest;

    CHECK(q.valid == true);
    CHECK(q.reliability.kind == Qos::Reliability::kReliable);
    CHECK(q.history.kind == Qos::History::kKeepLast);
    CHECK(q.history.depth == 200);
    CHECK(q.publish_mode.kind == Qos::PublishMode::kSync);
    CHECK(q.additions.priority == Qos::Additions::kPriorityRealTime);

    std::string name(q.name);
    CHECK(name == "best");
  }

  TEST_CASE("kLarge profile") {
    const Qos& q = QosProfile::kLarge;

    CHECK(q.valid == true);
    CHECK(q.reliability.kind == Qos::Reliability::kReliable);
    CHECK(q.history.kind == Qos::History::kKeepLast);
    CHECK(q.history.depth == 500);
    CHECK(q.reliability.heartbeat_time == 500);
    CHECK(q.durability.kind == Qos::Durability::kVolatile);
    CHECK(q.publish_mode.kind == Qos::PublishMode::kSync);
    CHECK(q.additions.priority == Qos::Additions::kPriorityLow);

    std::string name(q.name);
    CHECK(name == "large");
  }
}

// ---------------------------------------------------------------------------
// TEST SUITE: get_available_qos_map
// ---------------------------------------------------------------------------

TEST_SUITE("extension-QosProfile - get_available_qos_map") {
  TEST_CASE("map is not empty") {
    const auto& m = QosProfile::get_available_qos_map();
    CHECK(!m.empty());
  }

  TEST_CASE("map contains all 13 expected profiles") {
    const auto& m = QosProfile::get_available_qos_map();

    CHECK(m.count("event") == 1);
    CHECK(m.count("method") == 1);
    CHECK(m.count("field") == 1);
    CHECK(m.count("sensor") == 1);
    CHECK(m.count("parameter") == 1);
    CHECK(m.count("service") == 1);
    CHECK(m.count("clock") == 1);
    CHECK(m.count("static") == 1);
    CHECK(m.count("light") == 1);
    CHECK(m.count("poor") == 1);
    CHECK(m.count("better") == 1);
    CHECK(m.count("best") == 1);
    CHECK(m.count("large") == 1);

    CHECK(m.size() >= 13);
  }

  TEST_CASE("map entries have valid=true") {
    const auto& m = QosProfile::get_available_qos_map();

    for (const auto& [name, qos] : m) {
      CHECK(qos.valid == true);
    }
  }

  TEST_CASE("sensor entry is BestEffort ASync") {
    const auto& m = QosProfile::get_available_qos_map();

    auto it = m.find("sensor");
    REQUIRE(it != m.end());
    CHECK(it->second.reliability.kind == Qos::Reliability::kBestEffort);
    CHECK(it->second.publish_mode.kind == Qos::PublishMode::kASync);
  }

  TEST_CASE("field entry is TransientLocal") {
    const auto& m = QosProfile::get_available_qos_map();

    auto it = m.find("field");
    REQUIRE(it != m.end());
    CHECK(it->second.durability.kind == Qos::Durability::kTransientLocal);
  }

  TEST_CASE("map is returned by const reference - same address on repeated calls") {
    const auto& m1 = QosProfile::get_available_qos_map();
    const auto& m2 = QosProfile::get_available_qos_map();

    CHECK(&m1 == &m2);
  }
}

// NOLINTEND
