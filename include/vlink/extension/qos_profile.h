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

/**
 * @file qos_profile.h
 * @brief Pre-defined QoS profiles for common VLink communication patterns.
 *
 * @details
 * The @c QosProfile namespace provides a set of ready-to-use @c Qos constant instances
 * covering the most common autonomy and embedded system use cases. Each profile has
 * @c valid = @c true and can be passed directly to any VLink endpoint.
 *
 * Available profiles:
 *
 * | Profile    | Reliability | History        | Durability     | PubMode | Priority   | Use case                    |
 * | ---------- | ----------  | -------------- | -------------- | ------- | ---------- | --------------------------- |
 * | kEvent     | Reliable    | KeepLast(10)   | Volatile       | Sync    | RealTime   | Discrete control events     |
 * | kMethod    | Reliable    | KeepAll(1)     | Volatile       | Sync    | High       | RPC request/response        |
 * | kField     | Reliable    | KeepLast(1)    | TransientLocal | Sync    | High       | Latest-value state sync     |
 * | kSensor    | BestEffort  | KeepLast(20)   | Volatile       | ASync   | Normal     | High-rate sensor data       |
 * | kParameter | Reliable    | KeepLast(1000) | Volatile       | Sync    | Normal     | Configuration parameters    |
 * | kService   | Reliable    | KeepLast(10)   | TransientLocal | Sync    | Normal     | Service discovery           |
 * | kClock     | BestEffort  | KeepLast(1)    | Volatile       | ASync   | Low        | Time synchronisation        |
 * | kStatic    | Reliable    | KeepAll(1)     | TransientLocal | Sync    | Normal     | Static/slow-changing data   |
 * | kLight     | Reliable    | KeepLast(1)    | Volatile       | ASync   | High       | Lightweight fast messaging  |
 * | kPoor      | BestEffort  | KeepLast(5)    | Volatile       | ASync   | Background | Low-priority best-effort    |
 * | kBetter    | BestEffort  | KeepLast(50)   | Volatile       | Sync    | RealTime   | High-throughput best-effort |
 * | kBest      | Reliable    | KeepLast(200)  | Volatile       | Sync    | RealTime   | High-throughput reliable    |
 * | kLarge     | Reliable    | KeepLast(500)  | Volatile       | Sync    | Low        | Large payload transfers     |
 *
 * @par Lookup by name
 * @c get_available_qos_map() returns an @c unordered_map from profile name string to @c Qos,
 * enabling runtime profile selection:
 * @code
 * auto& qos_map = vlink::QosProfile::get_available_qos_map();
 * auto it = qos_map.find("sensor");
 * if (it != qos_map.end()) {
 *     pub->set_qos(it->second);
 * }
 * @endcode
 *
 * @par Direct usage
 * @code
 * auto pub = vlink::Publisher<MyMsg>::create("dds://sensor_data");
 * pub->set_qos(vlink::QosProfile::kSensor);
 * @endcode
 */

#pragma once

#include <string>
#include <unordered_map>

#include "../base/macros.h"
#include "./qos.h"

namespace vlink {

/**
 * @namespace vlink::QosProfile
 * @brief Pre-built @c Qos constant instances covering common communication patterns.
 *
 * @details
 * All profiles have @c valid = @c true.  Choose the profile that best matches your
 * use case, or customise a copy for specific requirements.
 *
 * @see Qos, get_available_qos_map()
 */
namespace QosProfile {  // NOLINT(readability-identifier-naming)

/**
 * @brief Reliable, KeepLast(10), Volatile, Sync, RealTime priority.
 *
 * @details Designed for discrete control events where delivery must be guaranteed
 * and late arrivals are acceptable up to a depth of 10.
 */
[[maybe_unused]] static inline constexpr Qos kEvent{
    "event",
    true,
    Qos::Reliability{Qos::Reliability::kReliable},
    Qos::History{Qos::History::kKeepLast, 10},
    Qos::Durability{Qos::Durability::kVolatile},
    Qos::PublishMode{Qos::PublishMode::kSync},
    Qos::Liveliness{},
    Qos::DestinationOrder{},
    Qos::Ownership{},
    Qos::Deadline{},
    Qos::Lifespan{},
    Qos::LatencyBudget{},
    Qos::ResourceLimits{},
    Qos::Additions{Qos::Additions::kPriorityRealTime, false},
};

/**
 * @brief Reliable, KeepAll, Volatile, Sync, High priority.
 *
 * @details Designed for RPC-style request/response (Method model).
 * KeepAll ensures no request is dropped even under load.
 */
[[maybe_unused]] static inline constexpr Qos kMethod{
    "method",
    true,
    Qos::Reliability{Qos::Reliability::kReliable},
    Qos::History{Qos::History::kKeepAll, 1},
    Qos::Durability{Qos::Durability::kVolatile},
    Qos::PublishMode{Qos::PublishMode::kSync},
    Qos::Liveliness{},
    Qos::DestinationOrder{},
    Qos::Ownership{},
    Qos::Deadline{},
    Qos::Lifespan{},
    Qos::LatencyBudget{},
    Qos::ResourceLimits{},
    Qos::Additions{Qos::Additions::kPriorityHigh, false},
};

/**
 * @brief Reliable, KeepLast(1), TransientLocal, Sync, High priority.
 *
 * @details Designed for Field model (Getter/Setter) where only the latest value matters
 * and late-joining subscribers must receive the last published value.
 */
[[maybe_unused]] static inline constexpr Qos kField{
    "field",
    true,
    Qos::Reliability{Qos::Reliability::kReliable},
    Qos::History{Qos::History::kKeepLast, 1},
    Qos::Durability{Qos::Durability::kTransientLocal},
    Qos::PublishMode{Qos::PublishMode::kSync},
    Qos::Liveliness{},
    Qos::DestinationOrder{},
    Qos::Ownership{},
    Qos::Deadline{},
    Qos::Lifespan{},
    Qos::LatencyBudget{},
    Qos::ResourceLimits{},
    Qos::Additions{Qos::Additions::kPriorityHigh, false},
};

/**
 * @brief BestEffort, KeepLast(20), Volatile, ASync, Normal priority, express.
 *
 * @details Designed for high-rate sensor streams (LiDAR, camera, IMU) where throughput
 * is more important than delivery guarantees.  Express mode hints for minimal queuing delay.
 */
[[maybe_unused]] static inline constexpr Qos kSensor{
    "sensor",
    true,
    Qos::Reliability{Qos::Reliability::kBestEffort},
    Qos::History{Qos::History::kKeepLast, 20},
    Qos::Durability{Qos::Durability::kVolatile},
    Qos::PublishMode{Qos::PublishMode::kASync},
    Qos::Liveliness{},
    Qos::DestinationOrder{},
    Qos::Ownership{},
    Qos::Deadline{},
    Qos::Lifespan{},
    Qos::LatencyBudget{},
    Qos::ResourceLimits{},
    Qos::Additions{Qos::Additions::kPriorityNormal, true},
};

/**
 * @brief Reliable, KeepLast(1000), Volatile, Sync, Normal priority.
 *
 * @details Designed for configuration parameters that change infrequently but must
 * be delivered reliably with a large history window.
 */
[[maybe_unused]] static inline constexpr Qos kParameter{
    "parameter",
    true,
    Qos::Reliability{Qos::Reliability::kReliable},
    Qos::History{Qos::History::kKeepLast, 1000},
    Qos::Durability{Qos::Durability::kVolatile},
    Qos::PublishMode{Qos::PublishMode::kSync},
    Qos::Liveliness{},
    Qos::DestinationOrder{},
    Qos::Ownership{},
    Qos::Deadline{},
    Qos::Lifespan{},
    Qos::LatencyBudget{},
    Qos::ResourceLimits{},
    Qos::Additions{Qos::Additions::kPriorityNormal, false},
};

/**
 * @brief Reliable, KeepLast(10), TransientLocal, Sync, Normal priority.
 *
 * @details Designed for service discovery and advertisement where late joiners
 * must see the current service registry.
 */
[[maybe_unused]] static inline constexpr Qos kService{
    "service",
    true,
    Qos::Reliability{Qos::Reliability::kReliable},
    Qos::History{Qos::History::kKeepLast, 10},
    Qos::Durability{Qos::Durability::kTransientLocal},
    Qos::PublishMode{Qos::PublishMode::kSync},
    Qos::Liveliness{},
    Qos::DestinationOrder{},
    Qos::Ownership{},
    Qos::Deadline{},
    Qos::Lifespan{},
    Qos::LatencyBudget{},
    Qos::ResourceLimits{},
    Qos::Additions{Qos::Additions::kPriorityNormal, false},
};

/**
 * @brief BestEffort, KeepLast(1), Volatile, ASync, Low priority.
 *
 * @details Designed for time synchronisation broadcasts where the newest value
 * is always more useful than older ones and missing a tick is acceptable.
 */
[[maybe_unused]] static inline constexpr Qos kClock{
    "clock",
    true,
    Qos::Reliability{Qos::Reliability::kBestEffort},
    Qos::History{Qos::History::kKeepLast, 1},
    Qos::Durability{Qos::Durability::kVolatile},
    Qos::PublishMode{Qos::PublishMode::kASync},
    Qos::Liveliness{},
    Qos::DestinationOrder{},
    Qos::Ownership{},
    Qos::Deadline{},
    Qos::Lifespan{},
    Qos::LatencyBudget{},
    Qos::ResourceLimits{},
    Qos::Additions{Qos::Additions::kPriorityLow, false},
};

/**
 * @brief Reliable, KeepAll, TransientLocal, Sync, Normal priority.
 *
 * @details Designed for slowly-changing or static data (maps, calibration files) that
 * must be fully received by any late-joining subscriber.
 */
[[maybe_unused]] static inline constexpr Qos kStatic{
    "static",
    true,
    Qos::Reliability{Qos::Reliability::kReliable},
    Qos::History{Qos::History::kKeepAll, 1},
    Qos::Durability{Qos::Durability::kTransientLocal},
    Qos::PublishMode{Qos::PublishMode::kSync},
    Qos::Liveliness{},
    Qos::DestinationOrder{},
    Qos::Ownership{},
    Qos::Deadline{},
    Qos::Lifespan{},
    Qos::LatencyBudget{},
    Qos::ResourceLimits{},
    Qos::Additions{Qos::Additions::kPriorityNormal, false},
};

/**
 * @brief Reliable, KeepLast(1), Volatile, ASync, High priority.
 *
 * @details Designed for lightweight, frequent messages where only the latest value
 * is needed and asynchronous delivery provides low overhead.
 */
[[maybe_unused]] static inline constexpr Qos kLight{
    "light",
    true,
    Qos::Reliability{Qos::Reliability::kReliable},
    Qos::History{Qos::History::kKeepLast, 1},
    Qos::Durability{Qos::Durability::kVolatile},
    Qos::PublishMode{Qos::PublishMode::kASync},
    Qos::Liveliness{},
    Qos::DestinationOrder{},
    Qos::Ownership{},
    Qos::Deadline{},
    Qos::Lifespan{},
    Qos::LatencyBudget{},
    Qos::ResourceLimits{},
    Qos::Additions{Qos::Additions::kPriorityHigh, false},
};

/**
 * @brief BestEffort, KeepLast(5), Volatile, ASync, Background priority.
 *
 * @details Designed for low-priority non-critical data streams where some loss
 * is acceptable and the lowest possible CPU overhead is desired.
 */
[[maybe_unused]] static inline constexpr Qos kPoor{
    "poor",
    true,
    Qos::Reliability{Qos::Reliability::kBestEffort},
    Qos::History{Qos::History::kKeepLast, 5},
    Qos::Durability{Qos::Durability::kVolatile},
    Qos::PublishMode{Qos::PublishMode::kASync},
    Qos::Liveliness{},
    Qos::DestinationOrder{},
    Qos::Ownership{},
    Qos::Deadline{},
    Qos::Lifespan{},
    Qos::LatencyBudget{},
    Qos::ResourceLimits{},
    Qos::Additions{Qos::Additions::kPriorityBackground, false},
};

/**
 * @brief BestEffort, KeepLast(50), Volatile, Sync, RealTime priority.
 *
 * @details Designed for high-throughput best-effort streams that benefit from
 * a larger history buffer and real-time dispatch priority.
 */
[[maybe_unused]] static inline constexpr Qos kBetter{
    "better",
    true,
    Qos::Reliability{Qos::Reliability::kBestEffort},
    Qos::History{Qos::History::kKeepLast, 50},
    Qos::Durability{Qos::Durability::kVolatile},
    Qos::PublishMode{Qos::PublishMode::kSync},
    Qos::Liveliness{},
    Qos::DestinationOrder{},
    Qos::Ownership{},
    Qos::Deadline{},
    Qos::Lifespan{},
    Qos::LatencyBudget{},
    Qos::ResourceLimits{},
    Qos::Additions{Qos::Additions::kPriorityRealTime, false},
};

/**
 * @brief Reliable, KeepLast(200), Volatile, Sync, RealTime priority.
 *
 * @details Designed for high-throughput reliable streams with a large history buffer.
 * Combines reliability guarantees with synchronous sending for predictable latency.
 */
[[maybe_unused]] static inline constexpr Qos kBest{
    "best",
    true,
    Qos::Reliability{Qos::Reliability::kReliable},
    Qos::History{Qos::History::kKeepLast, 200},
    Qos::Durability{Qos::Durability::kVolatile},
    Qos::PublishMode{Qos::PublishMode::kSync},
    Qos::Liveliness{},
    Qos::DestinationOrder{},
    Qos::Ownership{},
    Qos::Deadline{},
    Qos::Lifespan{},
    Qos::LatencyBudget{},
    Qos::ResourceLimits{},
    Qos::Additions{Qos::Additions::kPriorityRealTime, false},
};

/**
 * @brief Reliable, KeepLast(500), Volatile, Sync, Low priority, extended heartbeat.
 *
 * @details Designed for large payload transfers (maps, point clouds, images) where
 * a large history window and extended heartbeat interval accommodate slower transport.
 */
[[maybe_unused]] static inline constexpr Qos kLarge{
    "large",
    true,
    Qos::Reliability{Qos::Reliability::kReliable, 100, 500},
    Qos::History{Qos::History::kKeepLast, 500},
    Qos::Durability{Qos::Durability::kVolatile},
    Qos::PublishMode{Qos::PublishMode::kSync},
    Qos::Liveliness{},
    Qos::DestinationOrder{},
    Qos::Ownership{},
    Qos::Deadline{},
    Qos::Lifespan{},
    Qos::LatencyBudget{},
    Qos::ResourceLimits{},
    Qos::Additions{Qos::Additions::kPriorityLow, false},
};

/**
 * @brief Returns a reference to the global map of all named QoS profiles.
 *
 * @details
 * The map is keyed by the profile name string (e.g., @c "sensor", @c "event") and maps
 * to the corresponding @c Qos constant.  The map is populated with all profiles defined
 * in this namespace and is safe to query from any thread after construction.
 *
 * @return Const reference to an @c unordered_map<string, Qos> of all available profiles.
 */
[[nodiscard]] VLINK_EXPORT const std::unordered_map<std::string, Qos>& get_available_qos_map() noexcept;

}  // namespace QosProfile

}  // namespace vlink
