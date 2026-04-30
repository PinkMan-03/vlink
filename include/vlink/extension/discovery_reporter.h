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
 * @file discovery_reporter.h
 * @brief Process-level discovery reporter that broadcasts node metadata to DiscoveryViewer instances.
 *
 * @details
 * @c DiscoveryReporter runs as a background @c MessageLoop and periodically reports the
 * list of active @c NodeImpl endpoints (publishers, subscribers, clients, servers, etc.)
 * to any @c DiscoveryViewer instances on the same host or network.
 *
 * Internally it:
 * 1. Collects all registered @c NodeImpl objects.
 * 2. Serialises their metadata (URL, type, process info, CPU profiler data) into a discovery message.
 * 3. Sends the message via UDP multicast/broadcast (default address @c 239.255.0.100).
 * 4. Sends an offline notification on destruction.
 *
 * A process-global singleton is available via @c global_get(); it is created on first use
 * and destroyed with the process.
 *
 * @note
 * - @c add() and @c remove() are called automatically by @c NodeImpl constructors and destructors.
 * - Users generally do not need to interact with @c DiscoveryReporter directly.
 * - The reporter uses UDP multicast/broadcast for discovery, not any VLink transport backend.
 */

#pragma once

#include <memory>
#include <string>

#include "../base/macros.h"
#include "../base/message_loop.h"

namespace vlink {

class NodeImpl;

/**
 * @class DiscoveryReporter
 * @brief Background @c MessageLoop that reports active nodes to the discovery subsystem.
 *
 * @details
 * Automatically started and stopped by the VLink runtime.  Callers should not need to
 * manage this object directly unless building custom tooling.
 */
class VLINK_EXPORT DiscoveryReporter : public MessageLoop {
 public:
  /**
   * @brief Returns the process-global @c DiscoveryReporter singleton.
   *
   * @details
   * Created on first call.  The singleton is destroyed when the process exits.
   *
   * @return Raw pointer to the global @c DiscoveryReporter.
   */
  static DiscoveryReporter* global_get();

  /**
   * @brief Constructs the reporter and starts its background loop.
   */
  DiscoveryReporter();

  /**
   * @brief Destructor -- sends an offline notification and stops the loop.
   */
  ~DiscoveryReporter() override;

  /**
   * @brief Registers a @c NodeImpl endpoint for periodic reporting.
   *
   * @details
   * Called automatically by @c NodeImpl on construction.
   *
   * @param node  Node to add.
   */
  void add(NodeImpl* node);

  /**
   * @brief Unregisters a @c NodeImpl endpoint from periodic reporting.
   *
   * @details
   * Called automatically by @c NodeImpl on destruction.
   *
   * @param node  Node to remove.
   */
  void remove(NodeImpl* node);

 protected:
  size_t get_max_task_count() const override;

  uint32_t get_max_elapsed_time() const override;

  void on_begin() override;

  void on_end() override;

 private:
  void rebuild_message();

  void send_report();

  void send_offline();

  static const std::string& get_host_name();

  static const std::string& get_app_name();

  std::unique_ptr<struct DiscoveryReporterImpl> impl_;

  VLINK_DISALLOW_COPY_AND_ASSIGN(DiscoveryReporter)
};

}  // namespace vlink
