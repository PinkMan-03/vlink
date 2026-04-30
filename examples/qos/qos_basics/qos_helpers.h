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

#ifndef EXAMPLES_QOS_QOS_BASICS_QOS_HELPERS_H_
#define EXAMPLES_QOS_QOS_BASICS_QOS_HELPERS_H_

#include <vlink/extension/qos.h>

#include <iostream>

namespace qos_helpers {

// Print a human-readable summary of all QoS sub-policies.
// Useful for debugging and verifying QoS configuration.
inline void print_qos(const vlink::Qos& qos) {
  std::cout << "=== QoS: " << qos.name << " (valid=" << (qos.valid ? "true" : "false") << ") ===" << std::endl;
  std::cout << "  Reliability: "
            << (qos.reliability.kind == vlink::Qos::Reliability::kBestEffort ? "BestEffort" : "Reliable")
            << " block_time=" << qos.reliability.block_time << "ms"
            << " heartbeat=" << qos.reliability.heartbeat_time << "ms" << std::endl;
  std::cout << "  History: " << (qos.history.kind == vlink::Qos::History::kKeepLast ? "KeepLast" : "KeepAll")
            << " depth=" << qos.history.depth << std::endl;
  const char* dur[] = {"Volatile", "TransientLocal", "Transient", "Persistent"};
  std::cout << "  Durability: " << dur[qos.durability.kind] << std::endl;
  std::cout << "  PublishMode: " << (qos.publish_mode.kind == vlink::Qos::PublishMode::kSync ? "Sync" : "ASync")
            << std::endl;
  std::cout << "  ResourceLimits: samples=" << qos.resource_limits.max_samples
            << " instances=" << qos.resource_limits.max_instances
            << " per_instance=" << qos.resource_limits.max_samples_per_instance << std::endl;
  std::cout << "  Deadline: " << qos.deadline.period << "ms  Lifespan: " << qos.lifespan.duration << "ms" << std::endl;
  std::cout << std::endl;
}

// Print a one-line compact summary of a QoS profile.
inline void print_profile_summary(const char* label, const vlink::Qos& qos) {
  const char* rel = (qos.reliability.kind == vlink::Qos::Reliability::kBestEffort) ? "BestEffort" : "Reliable";
  const char* hist = (qos.history.kind == vlink::Qos::History::kKeepLast) ? "KeepLast" : "KeepAll";
  const char* dur_names[] = {"Volatile", "TransientLocal", "Transient", "Persistent"};
  const char* pub_mode = (qos.publish_mode.kind == vlink::Qos::PublishMode::kSync) ? "Sync" : "ASync";

  std::cout << "  " << label << ": " << rel << ", " << hist << "(depth=" << qos.history.depth << "), "
            << dur_names[qos.durability.kind] << ", " << pub_mode << std::endl;
}

}  // namespace qos_helpers

#endif  // EXAMPLES_QOS_QOS_BASICS_QOS_HELPERS_H_
