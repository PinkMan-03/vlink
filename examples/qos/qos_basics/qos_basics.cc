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
 * @file qos_basics.cc
 * @brief QoS Basics -- Creating, configuring, and applying QoS profiles to DDS nodes.
 *
 * QoS (Quality of Service) is primarily used with DDS-based transports
 * (dds://, ddsc://, ddsr://, ddst://) and Zenoh (zenoh://).
 *
 * The correct workflow is:
 *   1. Create a Qos struct and configure sub-policies
 *   2. Register the profile via DdsConf::register_qos("name", qos)
 *   3. Reference the profile name in the URL: "dds://topic?qos=name"
 *
 * This example covers:
 *   - Default Qos values
 *   - Creating a custom Qos from scratch
 *   - Registering QoS profiles for DDS transport
 *   - Using QoS profiles in DDS URLs
 *   - All Qos sub-policies explained
 */

#include <vlink/base/logger.h>
#include <vlink/extension/qos.h>
#include <vlink/extension/qos_profile.h>
#include <vlink/vlink.h>

#include "qos_helpers.h"

// DDS transport conf header -- required for register_qos()
#ifdef VLINK_SUPPORT_DDS
#include <vlink/modules/dds_conf.h>
#endif

#ifdef VLINK_SUPPORT_DDSC
#include <vlink/modules/ddsc_conf.h>
#endif

#include <cstring>
#include <iostream>
#include <thread>

using namespace std::chrono_literals;  // NOLINT(build/namespaces, google-build-using-namespace)

int main() {
  // ================================================================
  // Section 1: Default Qos values
  // ================================================================
  // A default-constructed Qos has valid=false.
  // It will NOT be applied to any transport until valid is set to true.
  VLOG_I("--- Section 1: Default Qos ---");
  {
    vlink::Qos qos;
    // Default values:
    //   reliability: kReliable, block_time=100ms, heartbeat_time=3000ms
    //   history:     kKeepLast, depth=1
    //   durability:  kVolatile
    //   valid:       false (IMPORTANT: must set to true before use)
    qos_helpers::print_qos(qos);
  }

  // ================================================================
  // Section 2: Build a custom QoS for sensor data
  // ================================================================
  VLOG_I("--- Section 2: Custom sensor QoS ---");
  {
    vlink::Qos sensor_qos;
    std::strncpy(sensor_qos.name, "sensor", sizeof(sensor_qos.name) - 1);
    sensor_qos.valid = true;  // MUST set to true

    // BestEffort: tolerate message loss for highest throughput.
    // Suitable for high-rate sensor data (lidar, camera, radar).
    sensor_qos.reliability.kind = vlink::Qos::Reliability::kBestEffort;

    // KeepLast with depth=5: only keep the 5 most recent samples.
    // Late-joining subscribers receive at most 5 historical messages.
    sensor_qos.history.kind = vlink::Qos::History::kKeepLast;
    sensor_qos.history.depth = 5;

    // Volatile: no persistence. Data is not stored after publishing.
    sensor_qos.durability.kind = vlink::Qos::Durability::kVolatile;

    // Async publish: non-blocking write, uses a background send thread.
    sensor_qos.publish_mode.kind = vlink::Qos::PublishMode::kASync;

    // Deadline: expect data every 100ms. If the publisher misses this,
    // a deadline-missed status event is triggered.
    sensor_qos.deadline.period = 100;

    // Lifespan: samples older than 500ms are discarded.
    sensor_qos.lifespan.duration = 500;

    qos_helpers::print_qos(sensor_qos);

    // Register the QoS profile with DDS transport.
    // After registration, use "dds://topic?qos=sensor" in URLs.
#ifdef VLINK_SUPPORT_DDS
    vlink::DdsConf::register_qos("sensor", sensor_qos);
    VLOG_I("Registered 'sensor' QoS profile with DDS");

    // Now create a publisher using this QoS via URL parameter.
    // The ?qos=sensor tells DDS to look up the registered "sensor" profile.
    vlink::Publisher<std::string> pub("dds://sensor/lidar_data?qos=sensor");
    vlink::Subscriber<std::string> sub("dds://sensor/lidar_data?qos=sensor");

    sub.listen([](const std::string& msg) { VLOG_I("Received with sensor QoS: ", msg); });

    pub.wait_for_subscribers(2s);
    pub.publish("lidar_frame_001");
    std::this_thread::sleep_for(100ms);
#else
    VLOG_W("DDS module not available. QoS registration requires DDS/DDSC/DDSR/Zenoh transport.");
    VLOG_I("Skipping DDS-specific QoS demo.");
#endif
  }

  // ================================================================
  // Section 3: Build a reliable command QoS
  // ================================================================
  VLOG_I("--- Section 3: Reliable command QoS ---");
  {
    vlink::Qos cmd_qos;
    std::strncpy(cmd_qos.name, "command", sizeof(cmd_qos.name) - 1);
    cmd_qos.valid = true;

    // Reliable: guaranteed delivery with retransmission.
    // Suitable for control commands where every message matters.
    cmd_qos.reliability.kind = vlink::Qos::Reliability::kReliable;
    cmd_qos.reliability.block_time = 500;       // wait up to 500ms for write space
    cmd_qos.reliability.heartbeat_time = 1000;  // heartbeat every 1s

    // KeepAll: buffer all unread samples. Combined with Reliable,
    // this guarantees no message loss (until ResourceLimits are hit).
    cmd_qos.history.kind = vlink::Qos::History::kKeepAll;

    // TransientLocal: DataWriter caches samples for late-joining subscribers.
    // A new subscriber that connects late will receive cached commands.
    cmd_qos.durability.kind = vlink::Qos::Durability::kTransientLocal;

    // Sync publish: write blocks until the message is committed.
    cmd_qos.publish_mode.kind = vlink::Qos::PublishMode::kSync;

    qos_helpers::print_qos(cmd_qos);

#ifdef VLINK_SUPPORT_DDS
    vlink::DdsConf::register_qos("command", cmd_qos);
    VLOG_I("Registered 'command' QoS profile with DDS");

    // Use the command QoS for an RPC service.
    // Both client and server should use the same QoS profile.
    vlink::Server<std::string, std::string> server("dds://control/brake?qos=command");
    server.listen([](const std::string& req, std::string& resp) {
      VLOG_I("Command received: ", req);
      resp = "ACK:" + req;
    });

    vlink::Client<std::string, std::string> client("dds://control/brake?qos=command");

    if (client.wait_for_connected(2s)) {
      auto resp = client.invoke("emergency_stop");
      if (resp.has_value()) {
        VLOG_I("Command response: ", resp.value());
      }
    }
#else
    VLOG_I("DDS not available, skipping command QoS demo.");
#endif
  }

  // ================================================================
  // Section 4: DDS URL with domain and depth parameters
  // ================================================================
  VLOG_I("--- Section 4: DDS URL parameters ---");
  {
    // DDS URL format: dds://topic_name[?domain=N&depth=N&qos=profile_name]
    //
    // Parameters:
    //   domain: DDS Domain Participant ID (isolates DDS traffic)
    //           Default: 0 (or VLINK_DDS_DOMAIN env var)
    //   depth:  History depth override for this specific endpoint
    //           Default: 0 (use QoS profile value or transport default)
    //   qos:    Name of a registered QoS profile
    //
    // Examples:
    //   "dds://vehicle/speed"                    -- default domain=0, default QoS
    //   "dds://vehicle/speed?domain=10"          -- domain 10
    //   "dds://vehicle/speed?depth=50"           -- history depth 50
    //   "dds://vehicle/speed?domain=10&qos=sensor"  -- domain 10 + sensor QoS
    //   "dds://vehicle/speed?qos=sensor&depth=20"   -- sensor QoS + depth override

    VLOG_I("DDS URL format: dds://topic[?domain=N&depth=N&qos=name]");
    VLOG_I("Example URLs:");
    VLOG_I("  dds://vehicle/speed");
    VLOG_I("  dds://vehicle/speed?domain=10");
    VLOG_I("  dds://vehicle/speed?depth=50");
    VLOG_I("  dds://vehicle/speed?domain=10&qos=sensor");
  }

  // ================================================================
  // Section 5: DDSC and Zenoh also support QoS registration
  // ================================================================
  VLOG_I("--- Section 5: Other transports with QoS ---");
  {
    // CycloneDDS (ddsc://) uses DdscConf::register_qos()
    //   ddsc://my_topic?qos=my_profile
    //   ddsc://my_topic?domain=5&depth=10&qos=realtime

    // Zenoh (zenoh://) uses ZenohConf::register_qos()
    //   zenoh://my_topic?qos=my_profile
    //   zenoh://my_topic?domain=0&qos=sensor

    // RTI DDS (ddsr://) uses DdsrConf::register_qos()
    //   ddsr://my_topic?qos=my_profile

    VLOG_I("QoS-capable transports and their register_qos() functions:");
    VLOG_I("  dds://    -> DdsConf::register_qos(name, qos)");
    VLOG_I("  ddsc://   -> DdscConf::register_qos(name, qos)");
    VLOG_I("  ddsr://   -> DdsrConf::register_qos(name, qos)");
    VLOG_I("  ddst://   -> DdstConf::register_qos(name, qos)");
    VLOG_I("  zenoh://  -> ZenohConf::register_qos(name, qos)");
    VLOG_I("");
    VLOG_I("Non-QoS transports (QoS is ignored):");
    VLOG_I("  intra://  -- in-process, no transport-level QoS");
    VLOG_I("  shm://    -- shared memory, uses depth/history params directly");
    VLOG_I("  someip:// -- SOME/IP has its own service-level QoS");
    VLOG_I("  mqtt://   -- MQTT uses QoS levels 0/1/2 (set via ?qos=0|1|2)");
    VLOG_I("  fdbus://  -- FDBus has no QoS concept");
  }

  VLOG_I("=== QoS Basics Complete ===");
  return 0;
}
