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

// QoS Profiles Example
// Demonstrates using QosProfile presets (kSensor, kEvent, kField, etc.),
// looking up profiles by name from the map, registering custom QoS profiles
// with transport Conf, and the VLINK_QOS_CONFIG environment variable.

#include <vlink/base/logger.h>
#include <vlink/extension/qos.h>
#include <vlink/extension/qos_profile.h>
#include <vlink/vlink.h>

#include <cstring>
#include <iostream>
#include <thread>

using namespace std::chrono_literals;  // NOLINT(build/namespaces, google-build-using-namespace)

// Helper to print a brief summary of a QoS profile
void print_profile_summary(const char* label, const vlink::Qos& qos) {
  const char* rel = (qos.reliability.kind == vlink::Qos::Reliability::kBestEffort) ? "BestEffort" : "Reliable";
  const char* hist = (qos.history.kind == vlink::Qos::History::kKeepLast) ? "KeepLast" : "KeepAll";
  const char* dur_names[] = {"Volatile", "TransientLocal", "Transient", "Persistent"};
  const char* pub_mode = (qos.publish_mode.kind == vlink::Qos::PublishMode::kSync) ? "Sync" : "ASync";
  const char* prio_names[] = {"", "RealTime", "High", "", "Normal", "", "Low", "Background"};

  std::cout << "  " << label << ": " << rel << ", " << hist << "(depth=" << qos.history.depth << "), "
            << dur_names[qos.durability.kind] << ", " << pub_mode << ", Priority=" << prio_names[qos.additions.priority]
            << (qos.additions.is_express ? ", Express" : "") << std::endl;
}

int main() {
  // ======== Section 1: Built-in QosProfile Presets ========
  // VLink provides pre-defined Qos constants for common scenarios.
  // All presets have valid=true and can be used directly.
  {
    std::cout << "\n[1] Built-in QosProfile Presets" << std::endl;

    print_profile_summary("kEvent     ", vlink::QosProfile::kEvent);
    print_profile_summary("kMethod    ", vlink::QosProfile::kMethod);
    print_profile_summary("kField     ", vlink::QosProfile::kField);
    print_profile_summary("kSensor    ", vlink::QosProfile::kSensor);
    print_profile_summary("kParameter ", vlink::QosProfile::kParameter);
    print_profile_summary("kService   ", vlink::QosProfile::kService);
    print_profile_summary("kClock     ", vlink::QosProfile::kClock);
    print_profile_summary("kStatic    ", vlink::QosProfile::kStatic);
    print_profile_summary("kLight     ", vlink::QosProfile::kLight);
    print_profile_summary("kPoor      ", vlink::QosProfile::kPoor);
    print_profile_summary("kBetter    ", vlink::QosProfile::kBetter);
    print_profile_summary("kBest      ", vlink::QosProfile::kBest);
    print_profile_summary("kLarge     ", vlink::QosProfile::kLarge);
  }

  // ======== Section 2: Profile Lookup by Name ========
  // get_available_qos_map() returns a map from profile name string to Qos.
  // This enables runtime profile selection from configuration files.
  {
    std::cout << "\n[2] Profile Lookup by Name" << std::endl;

    const auto& qos_map = vlink::QosProfile::get_available_qos_map();

    std::cout << "  Available profiles (" << qos_map.size() << " total):" << std::endl;
    for (const auto& [name, qos] : qos_map) {
      std::cout << "    \"" << name << "\"" << std::endl;
    }

    // Look up a specific profile by name
    auto it = qos_map.find("sensor");
    if (it != qos_map.end()) {
      std::cout << "\n  Found 'sensor' profile:" << std::endl;
      print_profile_summary("sensor", it->second);
    }

    // Look up a non-existent profile
    auto it2 = qos_map.find("nonexistent");
    if (it2 == qos_map.end()) {
      std::cout << "  Profile 'nonexistent' not found (expected)." << std::endl;
    }
  }

  // ======== Section 3: Using Profiles with Publishers/Subscribers ========
  // Apply a preset profile to a pub/sub pair.
  {
    std::cout << "\n[3] Applying QosProfile::kSensor to Pub/Sub" << std::endl;

    // The kSensor profile: BestEffort, KeepLast(20), Volatile, ASync, Normal priority, Express
    // Perfect for high-rate sensor data streams.

    std::atomic<int> received{0};
    vlink::Subscriber<std::string> sub("dds://qos_profiles/sensor");
    sub.listen([&received](const std::string& msg) {
      (void)msg;
      received++;
    });

    vlink::Publisher<std::string> pub("dds://qos_profiles/sensor");
    pub.wait_for_subscribers();

    for (int i = 0; i < 50; ++i) {
      pub.publish("sensor_data_" + std::to_string(i));
    }

    std::this_thread::sleep_for(100ms);
    VLOG_I("kSensor profile: sent 50, received", received.load());
  }

  // ======== Section 4: Customizing a Profile Copy ========
  // Start from a preset and modify specific fields for your use case.
  {
    std::cout << "\n[4] Customizing a Profile Copy" << std::endl;

    // Start from kSensor and increase the history depth
    vlink::Qos custom_sensor = vlink::QosProfile::kSensor;  // Copy the preset
    std::strncpy(custom_sensor.name, "custom_sensor", sizeof(custom_sensor.name) - 1);

    // Override specific fields
    custom_sensor.history.depth = 50;                                     // More history than the default 20
    custom_sensor.reliability.kind = vlink::Qos::Reliability::kReliable;  // Upgrade to reliable
    custom_sensor.deadline.period = 100;                                  // Expect data every 100ms

    print_profile_summary("original kSensor ", vlink::QosProfile::kSensor);
    print_profile_summary("custom_sensor    ", custom_sensor);
    std::cout << "  custom_sensor deadline=" << custom_sensor.deadline.period << "ms" << std::endl;
  }

  // ======== Section 5: VLINK_QOS_CONFIG Environment Variable ========
  // QoS profiles can also be loaded from a JSON configuration file via
  // the VLINK_QOS_CONFIG environment variable.
  //
  // Example JSON file (qos_config.json):
  // {
  //   "profiles": {
  //     "my_sensor": {
  //       "reliability": { "kind": 0, "block_time": 50 },
  //       "history": { "kind": 0, "depth": 30 },
  //       "durability": { "kind": 0 },
  //       "publish_mode": { "kind": 1 },
  //       "additions": { "priority": 4, "is_express": true }
  //     },
  //     "my_command": {
  //       "reliability": { "kind": 1, "block_time": 200, "heartbeat_time": 1000 },
  //       "history": { "kind": 1 },
  //       "durability": { "kind": 1 },
  //       "publish_mode": { "kind": 0 },
  //       "additions": { "priority": 1, "is_express": false }
  //     }
  //   }
  // }
  //
  // Usage:
  //   export VLINK_QOS_CONFIG=/path/to/qos_config.json
  //   ./example_qos_profiles
  //
  // Profiles defined in the JSON file are merged with the built-in presets
  // and become available via get_available_qos_map() and register_qos().
  {
    std::cout << "\n[5] VLINK_QOS_CONFIG Environment Variable" << std::endl;

    const char* qos_config = std::getenv("VLINK_QOS_CONFIG");
    if (qos_config != nullptr) {
      VLOG_I("VLINK_QOS_CONFIG is set to:", qos_config);
      VLOG_I("Custom QoS profiles from this file are merged into the map.");
    } else {
      VLOG_I("VLINK_QOS_CONFIG is not set.");
      VLOG_I("Set it to a JSON file path to load custom QoS profiles at startup.");
    }

    std::cout << "  Example: export VLINK_QOS_CONFIG=/path/to/qos_config.json" << std::endl;
    std::cout << "  The JSON file defines named profiles with sub-policy values." << std::endl;
    std::cout << "  Loaded profiles are accessible via QosProfile::get_available_qos_map()." << std::endl;
  }

  // ======== Section 6: Profile Selection Guide ========
  {
    std::cout << "\n[6] Profile Selection Guide" << std::endl;
    std::cout << "  +--------------+------------------------------------------+" << std::endl;
    std::cout << "  | Scenario     | Recommended Profile                      |" << std::endl;
    std::cout << "  +--------------+------------------------------------------+" << std::endl;
    std::cout << "  | Camera/LiDAR | kSensor (BestEffort, ASync, Express)     |" << std::endl;
    std::cout << "  | Control cmd  | kEvent (Reliable, Sync, RealTime)        |" << std::endl;
    std::cout << "  | RPC calls    | kMethod (Reliable, KeepAll, High)        |" << std::endl;
    std::cout << "  | State sync   | kField (Reliable, TransientLocal, High)  |" << std::endl;
    std::cout << "  | Diagnostics  | kPoor (BestEffort, Background)           |" << std::endl;
    std::cout << "  | Time sync    | kClock (BestEffort, ASync, Low)          |" << std::endl;
    std::cout << "  | Map data     | kLarge (Reliable, Sync, depth=500, Low)  |" << std::endl;
    std::cout << "  | Config params| kParameter (Reliable, depth=1000)        |" << std::endl;
    std::cout << "  +--------------+------------------------------------------+" << std::endl;
  }

  VLOG_I("QoS profiles example complete.");
  return 0;
}
