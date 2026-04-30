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

#include "./extension/qos.h"

#include <doctest/doctest.h>

//
#include "../common_test.h"

// ---------------------------------------------------------------------------
// TEST SUITE: Qos default values
// ---------------------------------------------------------------------------

TEST_SUITE("extension-Qos - default construction") {
  TEST_CASE("default-constructed Qos has expected defaults") {
    Qos qos;

    CHECK(qos.valid == false);

    // Reliability defaults
    CHECK(qos.reliability.kind == Qos::Reliability::kReliable);
    CHECK(qos.reliability.block_time == 100);
    CHECK(qos.reliability.heartbeat_time == 3000);

    // History defaults
    CHECK(qos.history.kind == Qos::History::kKeepLast);
    CHECK(qos.history.depth == 1);

    // Durability defaults
    CHECK(qos.durability.kind == Qos::Durability::kVolatile);

    // PublishMode defaults
    CHECK(qos.publish_mode.kind == Qos::PublishMode::kSync);

    // Liveliness defaults
    CHECK(qos.liveliness.kind == Qos::Liveliness::kAutomatic);
    CHECK(qos.liveliness.duration == -1);

    // DestinationOrder defaults
    CHECK(qos.destination_order.kind == Qos::DestinationOrder::kReceptionTimestamp);

    // Ownership defaults
    CHECK(qos.ownership.kind == Qos::Ownership::kShared);

    // Deadline defaults
    CHECK(qos.deadline.period == -1);

    // Lifespan defaults
    CHECK(qos.lifespan.duration == -1);

    // LatencyBudget defaults
    CHECK(qos.latency_budget.duration == 0);

    // ResourceLimits defaults
    CHECK(qos.resource_limits.max_samples == 6000);
    CHECK(qos.resource_limits.max_instances == 10);
    CHECK(qos.resource_limits.max_samples_per_instance == 500);

    // Additions defaults
    CHECK(qos.additions.priority == Qos::Additions::kPriorityNormal);
    CHECK(qos.additions.is_express == false);
  }

  TEST_CASE("name field is zero-initialized") {
    Qos qos;
    CHECK(qos.name[0] == '\0');
  }
}

// ---------------------------------------------------------------------------
// TEST SUITE: Qos::Reliability
// ---------------------------------------------------------------------------

TEST_SUITE("extension-Qos::Reliability") {
  TEST_CASE("kBestEffort kind") {
    Qos::Reliability r{Qos::Reliability::kBestEffort};
    CHECK(r.kind == Qos::Reliability::kBestEffort);
  }

  TEST_CASE("kReliable kind") {
    Qos::Reliability r{Qos::Reliability::kReliable};
    CHECK(r.kind == Qos::Reliability::kReliable);
  }

  TEST_CASE("custom block_time") {
    Qos::Reliability r{Qos::Reliability::kReliable, 500, 1000};
    CHECK(r.block_time == 500);
    CHECK(r.heartbeat_time == 1000);
  }

  TEST_CASE("enum values are distinct") {
    CHECK(static_cast<int>(Qos::Reliability::kBestEffort) != static_cast<int>(Qos::Reliability::kReliable));
  }
}

// ---------------------------------------------------------------------------
// TEST SUITE: Qos::History
// ---------------------------------------------------------------------------

TEST_SUITE("extension-Qos::History") {
  TEST_CASE("kKeepLast with depth") {
    Qos::History h{Qos::History::kKeepLast, 10};
    CHECK(h.kind == Qos::History::kKeepLast);
    CHECK(h.depth == 10);
  }

  TEST_CASE("kKeepAll") {
    Qos::History h{Qos::History::kKeepAll, 1};
    CHECK(h.kind == Qos::History::kKeepAll);
  }

  TEST_CASE("default depth is 1") {
    Qos::History h;
    CHECK(h.depth == 1);
  }
}

// ---------------------------------------------------------------------------
// TEST SUITE: Qos::Durability
// ---------------------------------------------------------------------------

TEST_SUITE("extension-Qos::Durability") {
  TEST_CASE("kVolatile") {
    Qos::Durability d{Qos::Durability::kVolatile};
    CHECK(d.kind == Qos::Durability::kVolatile);
  }

  TEST_CASE("kTransientLocal") {
    Qos::Durability d{Qos::Durability::kTransientLocal};
    CHECK(d.kind == Qos::Durability::kTransientLocal);
  }

  TEST_CASE("kTransient") {
    Qos::Durability d{Qos::Durability::kTransient};
    CHECK(d.kind == Qos::Durability::kTransient);
  }

  TEST_CASE("kPersistent") {
    Qos::Durability d{Qos::Durability::kPersistent};
    CHECK(d.kind == Qos::Durability::kPersistent);
  }

  TEST_CASE("enum values are sequential 0-3") {
    CHECK(static_cast<int>(Qos::Durability::kVolatile) == 0);
    CHECK(static_cast<int>(Qos::Durability::kTransientLocal) == 1);
    CHECK(static_cast<int>(Qos::Durability::kTransient) == 2);
    CHECK(static_cast<int>(Qos::Durability::kPersistent) == 3);
  }
}

// ---------------------------------------------------------------------------
// TEST SUITE: Qos::PublishMode
// ---------------------------------------------------------------------------

TEST_SUITE("extension-Qos::PublishMode") {
  TEST_CASE("kSync") {
    Qos::PublishMode pm{Qos::PublishMode::kSync};
    CHECK(pm.kind == Qos::PublishMode::kSync);
  }

  TEST_CASE("kASync") {
    Qos::PublishMode pm{Qos::PublishMode::kASync};
    CHECK(pm.kind == Qos::PublishMode::kASync);
  }
}

// ---------------------------------------------------------------------------
// TEST SUITE: Qos::Liveliness
// ---------------------------------------------------------------------------

TEST_SUITE("extension-Qos::Liveliness") {
  TEST_CASE("kAutomatic") {
    Qos::Liveliness lv{Qos::Liveliness::kAutomatic};
    CHECK(lv.kind == Qos::Liveliness::kAutomatic);
    CHECK(lv.duration == -1);
  }

  TEST_CASE("kManualParticipant") {
    Qos::Liveliness lv{Qos::Liveliness::kManualParticipant, 5000};
    CHECK(lv.kind == Qos::Liveliness::kManualParticipant);
    CHECK(lv.duration == 5000);
  }

  TEST_CASE("kManualTopic") {
    Qos::Liveliness lv{Qos::Liveliness::kManualTopic, 1000};
    CHECK(lv.kind == Qos::Liveliness::kManualTopic);
    CHECK(lv.duration == 1000);
  }
}

// ---------------------------------------------------------------------------
// TEST SUITE: Qos::DestinationOrder
// ---------------------------------------------------------------------------

TEST_SUITE("extension-Qos::DestinationOrder") {
  TEST_CASE("kReceptionTimestamp") {
    Qos::DestinationOrder d{Qos::DestinationOrder::kReceptionTimestamp};
    CHECK(d.kind == Qos::DestinationOrder::kReceptionTimestamp);
  }

  TEST_CASE("kSourceTimestamp") {
    Qos::DestinationOrder d{Qos::DestinationOrder::kSourceTimestamp};
    CHECK(d.kind == Qos::DestinationOrder::kSourceTimestamp);
  }
}

// ---------------------------------------------------------------------------
// TEST SUITE: Qos::Ownership
// ---------------------------------------------------------------------------

TEST_SUITE("extension-Qos::Ownership") {
  TEST_CASE("kShared") {
    Qos::Ownership o{Qos::Ownership::kShared};
    CHECK(o.kind == Qos::Ownership::kShared);
  }

  TEST_CASE("kExClusive") {
    Qos::Ownership o{Qos::Ownership::kExClusive};
    CHECK(o.kind == Qos::Ownership::kExClusive);
  }
}

// ---------------------------------------------------------------------------
// TEST SUITE: Qos::Deadline / Lifespan / LatencyBudget
// ---------------------------------------------------------------------------

TEST_SUITE("extension-Qos::Deadline-Lifespan-LatencyBudget") {
  TEST_CASE("Deadline default -1") {
    Qos::Deadline d;
    CHECK(d.period == -1);
  }

  TEST_CASE("Deadline custom value") {
    Qos::Deadline d{500};
    CHECK(d.period == 500);
  }

  TEST_CASE("Lifespan default -1") {
    Qos::Lifespan ls;
    CHECK(ls.duration == -1);
  }

  TEST_CASE("Lifespan custom value") {
    Qos::Lifespan ls{2000};
    CHECK(ls.duration == 2000);
  }

  TEST_CASE("LatencyBudget default 0") {
    Qos::LatencyBudget lb;
    CHECK(lb.duration == 0);
  }

  TEST_CASE("LatencyBudget custom value") {
    Qos::LatencyBudget lb{50};
    CHECK(lb.duration == 50);
  }
}

// ---------------------------------------------------------------------------
// TEST SUITE: Qos::ResourceLimits
// ---------------------------------------------------------------------------

TEST_SUITE("extension-Qos::ResourceLimits") {
  TEST_CASE("default values") {
    Qos::ResourceLimits rl;
    CHECK(rl.max_samples == 6000);
    CHECK(rl.max_instances == 10);
    CHECK(rl.max_samples_per_instance == 500);
  }

  TEST_CASE("custom values") {
    Qos::ResourceLimits rl{1000, 5, 200};
    CHECK(rl.max_samples == 1000);
    CHECK(rl.max_instances == 5);
    CHECK(rl.max_samples_per_instance == 200);
  }
}

// ---------------------------------------------------------------------------
// TEST SUITE: Qos::Additions
// ---------------------------------------------------------------------------

TEST_SUITE("extension-Qos::Additions") {
  TEST_CASE("default priority is kPriorityNormal") {
    Qos::Additions a;
    CHECK(a.priority == Qos::Additions::kPriorityNormal);
    CHECK(a.is_express == false);
  }

  TEST_CASE("kPriorityRealTime") {
    Qos::Additions a{Qos::Additions::kPriorityRealTime, false};
    CHECK(a.priority == Qos::Additions::kPriorityRealTime);
    CHECK(static_cast<int>(a.priority) == 1);
  }

  TEST_CASE("kPriorityHigh") {
    Qos::Additions a{Qos::Additions::kPriorityHigh, false};
    CHECK(static_cast<int>(a.priority) == 2);
  }

  TEST_CASE("kPriorityNormal") { CHECK(static_cast<int>(Qos::Additions::kPriorityNormal) == 4); }

  TEST_CASE("kPriorityLow") { CHECK(static_cast<int>(Qos::Additions::kPriorityLow) == 6); }

  TEST_CASE("kPriorityBackground") { CHECK(static_cast<int>(Qos::Additions::kPriorityBackground) == 7); }

  TEST_CASE("is_express flag") {
    Qos::Additions a{Qos::Additions::kPriorityNormal, true};
    CHECK(a.is_express == true);
  }
}

// ---------------------------------------------------------------------------
// TEST SUITE: Qos field mutation
// ---------------------------------------------------------------------------

TEST_SUITE("extension-Qos - field mutation") {
  TEST_CASE("modify individual sub-policies") {
    Qos qos;
    qos.valid = true;
    qos.reliability.kind = Qos::Reliability::kBestEffort;
    qos.history.kind = Qos::History::kKeepLast;
    qos.history.depth = 20;
    qos.durability.kind = Qos::Durability::kTransientLocal;
    qos.publish_mode.kind = Qos::PublishMode::kASync;
    qos.additions.priority = Qos::Additions::kPriorityHigh;
    qos.additions.is_express = true;

    CHECK(qos.valid == true);
    CHECK(qos.reliability.kind == Qos::Reliability::kBestEffort);
    CHECK(qos.history.depth == 20);
    CHECK(qos.durability.kind == Qos::Durability::kTransientLocal);
    CHECK(qos.publish_mode.kind == Qos::PublishMode::kASync);
    CHECK(qos.additions.priority == Qos::Additions::kPriorityHigh);
    CHECK(qos.additions.is_express == true);
  }

  TEST_CASE("copy construction") {
    Qos qos;
    qos.valid = true;
    qos.history.depth = 50;
    qos.reliability.block_time = 200;

    Qos copy = qos;
    CHECK(copy.valid == true);
    CHECK(copy.history.depth == 50);
    CHECK(copy.reliability.block_time == 200);
  }

  TEST_CASE("name field can hold 19 characters") {
    Qos qos;
    const char* name = "1234567890123456789";
    std::strncpy(qos.name, name, sizeof(qos.name) - 1);
    qos.name[sizeof(qos.name) - 1] = '\0';

    CHECK(std::string(qos.name) == name);
    CHECK(std::strlen(qos.name) == 19);
  }

  TEST_CASE("assignment copies all fields") {
    Qos a;
    a.valid = true;
    a.reliability.kind = Qos::Reliability::kBestEffort;
    a.reliability.block_time = 999;
    a.reliability.heartbeat_time = 1234;
    a.history.kind = Qos::History::kKeepAll;
    a.history.depth = 100;
    a.durability.kind = Qos::Durability::kPersistent;
    a.publish_mode.kind = Qos::PublishMode::kASync;
    a.liveliness.kind = Qos::Liveliness::kManualTopic;
    a.liveliness.duration = 3000;
    a.destination_order.kind = Qos::DestinationOrder::kSourceTimestamp;
    a.ownership.kind = Qos::Ownership::kExClusive;
    a.deadline.period = 500;
    a.lifespan.duration = 10000;
    a.latency_budget.duration = 50;
    a.resource_limits.max_samples = 1000;
    a.resource_limits.max_instances = 5;
    a.resource_limits.max_samples_per_instance = 200;
    a.additions.priority = Qos::Additions::kPriorityRealTime;
    a.additions.is_express = true;

    Qos b;
    b = a;

    CHECK(b.valid == true);
    CHECK(b.reliability.kind == Qos::Reliability::kBestEffort);
    CHECK(b.reliability.block_time == 999);
    CHECK(b.reliability.heartbeat_time == 1234);
    CHECK(b.history.kind == Qos::History::kKeepAll);
    CHECK(b.history.depth == 100);
    CHECK(b.durability.kind == Qos::Durability::kPersistent);
    CHECK(b.publish_mode.kind == Qos::PublishMode::kASync);
    CHECK(b.liveliness.kind == Qos::Liveliness::kManualTopic);
    CHECK(b.liveliness.duration == 3000);
    CHECK(b.destination_order.kind == Qos::DestinationOrder::kSourceTimestamp);
    CHECK(b.ownership.kind == Qos::Ownership::kExClusive);
    CHECK(b.deadline.period == 500);
    CHECK(b.lifespan.duration == 10000);
    CHECK(b.latency_budget.duration == 50);
    CHECK(b.resource_limits.max_samples == 1000);
    CHECK(b.resource_limits.max_instances == 5);
    CHECK(b.resource_limits.max_samples_per_instance == 200);
    CHECK(b.additions.priority == Qos::Additions::kPriorityRealTime);
    CHECK(b.additions.is_express == true);
  }

  TEST_CASE("Qos is a final struct") { CHECK(std::is_final_v<Qos>); }

  TEST_CASE("sub-policy structs are final") {
    CHECK(std::is_final_v<Qos::Reliability>);
    CHECK(std::is_final_v<Qos::History>);
    CHECK(std::is_final_v<Qos::Durability>);
    CHECK(std::is_final_v<Qos::PublishMode>);
    CHECK(std::is_final_v<Qos::Liveliness>);
    CHECK(std::is_final_v<Qos::DestinationOrder>);
    CHECK(std::is_final_v<Qos::Ownership>);
    CHECK(std::is_final_v<Qos::Deadline>);
    CHECK(std::is_final_v<Qos::Lifespan>);
    CHECK(std::is_final_v<Qos::LatencyBudget>);
    CHECK(std::is_final_v<Qos::ResourceLimits>);
    CHECK(std::is_final_v<Qos::Additions>);
  }

  TEST_CASE("negative resource limits are accepted") {
    Qos::ResourceLimits rl{-1, -1, -1};
    CHECK(rl.max_samples == -1);
    CHECK(rl.max_instances == -1);
    CHECK(rl.max_samples_per_instance == -1);
  }

  TEST_CASE("zero block_time and heartbeat_time") {
    Qos::Reliability r{Qos::Reliability::kReliable, 0, 0};
    CHECK(r.block_time == 0);
    CHECK(r.heartbeat_time == 0);
  }

  TEST_CASE("liveliness with zero duration") {
    Qos::Liveliness lv{Qos::Liveliness::kManualParticipant, 0};
    CHECK(lv.duration == 0);
  }

  TEST_CASE("deadline with zero period") {
    Qos::Deadline d{0};
    CHECK(d.period == 0);
  }

  TEST_CASE("priority enum values are ordered") {
    CHECK(Qos::Additions::kPriorityRealTime < Qos::Additions::kPriorityHigh);
    CHECK(Qos::Additions::kPriorityHigh < Qos::Additions::kPriorityNormal);
    CHECK(Qos::Additions::kPriorityNormal < Qos::Additions::kPriorityLow);
    CHECK(Qos::Additions::kPriorityLow < Qos::Additions::kPriorityBackground);
  }
}

// NOLINTEND
