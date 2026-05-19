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

// VLink core communication API
#include <vlink/base/logger.h>
#include <vlink/vlink.h>

// QoS profile definitions (kSensor, kEvent, kField, etc.)
#include <vlink/extension/qos_profile.h>

// URL parser for inspecting DDS URLs
#include <vlink/impl/url_parser.h>

#include <string>
#include <thread>

using namespace std::chrono_literals;  // NOLINT(build/namespaces, google-build-using-namespace)

/// DDS URL Examples (dds:// and ddsc://)
///
/// DDS (Data Distribution Service) is the primary cross-machine transport in VLink.
/// Two DDS backends are available:
///   - dds://   Fast-DDS (eProsima)
///   - ddsc://  CycloneDDS (Eclipse)
///
/// URL format:
///   dds://topic[?domain=N&depth=N&qos=profile_name]
///   dds://topic[?domain=N&part=xml&topic=xml&pub=xml&sub=xml&writer=xml&reader=xml]
///
/// Key parameters:
///   - topic:   DDS topic name, formed from host + "/" + path
///   - domain:  DDS Domain Participant ID (default from VLINK_DDS_DOMAIN env, or 0)
///   - depth:   DDS history depth for the endpoint (0 = transport default)
///   - qos:     named QoS profile registered via DdsConf::register_qos()
///   - qos_ext: per-entity QoS XML strings (part, topic, pub, sub, writer, reader)
///
/// Environment variables:
///   - VLINK_DDS_DOMAIN:  default DDS domain ID for all dds:// URLs without ?domain=
///   - VLINK_DDS_BIND:    rewrite DDS URLs to a concrete DDS backend scheme
int main() {
  // ======== Example 1: Basic DDS topic ========
  // The simplest DDS URL: just a topic name
  // Domain defaults to VLINK_DDS_DOMAIN env var (or 0 if not set)
  {
    VLOG_I("=== Example 1: Basic DDS topic ===");

    // Parse the URL to show how topic is formed from host + "/" + path
    vlink::UrlParser parser("dds://vehicle/speed");
    VLOG_I("  host:", parser.get_host());  // "vehicle"
    VLOG_I("  path:", parser.get_path());  // "/speed"
    // DDS topic name = "vehicle" + "/speed" = "vehicle/speed"

    // In a real application, you would create nodes:
    // Subscriber<std::string> sub("dds://vehicle/speed");
    // Publisher<std::string> pub("dds://vehicle/speed");
    VLOG_I("  DDS topic name would be: vehicle/speed");
  }

  // ======== Example 2: Domain ID via URL ========
  // ?domain=N overrides the default DDS domain.
  // Different domains are completely isolated -- participants on domain 0
  // cannot see participants on domain 1.
  {
    VLOG_I("=== Example 2: Domain ID ===");

    // Domain 42 -- isolated from the default domain 0
    vlink::UrlParser parser("dds://vehicle/speed?domain=42");
    VLOG_I("  query:", parser.get_query());

    const auto& dict = parser.get_query_dictionary();
    auto it = dict.find("domain");
    if (it != dict.end()) {
      VLOG_I("  domain ID:", it->second);
    }

    // Practical usage:
    //   Publisher<int> pub("dds://vehicle/speed?domain=42");
    //   Subscriber<int> sub("dds://vehicle/speed?domain=42");  // same domain -- can communicate
    //   Subscriber<int> sub2("dds://vehicle/speed?domain=0");  // different domain -- isolated
  }

  // ======== Example 3: Domain ID via environment variable ========
  // VLINK_DDS_DOMAIN sets the default domain for all dds:// URLs that omit ?domain=
  {
    VLOG_I("=== Example 3: VLINK_DDS_DOMAIN env ===");

    // Save current value
    std::string saved = vlink::Utils::get_env("VLINK_DDS_DOMAIN");

    // Set default domain to 10
    vlink::Utils::set_env("VLINK_DDS_DOMAIN", "10");
    VLOG_I("  VLINK_DDS_DOMAIN=", vlink::Utils::get_env("VLINK_DDS_DOMAIN"));

    // Now all dds:// URLs without ?domain= will use domain 10:
    //   Publisher<int> pub("dds://vehicle/speed");  // domain=10

    // A URL with explicit ?domain= overrides the env var:
    //   Publisher<int> pub("dds://vehicle/speed?domain=99");  // domain=99, NOT 10

    // Restore
    if (saved.empty()) {
      vlink::Utils::unset_env("VLINK_DDS_DOMAIN");
    } else {
      vlink::Utils::set_env("VLINK_DDS_DOMAIN", saved);
    }
  }

  // ======== Example 4: Backend binding via VLINK_DDS_BIND ========
  // VLINK_DDS_BIND rewrites DDS URLs to a concrete DDS backend scheme.
  // Use VLINK_DDS_IP or backend-specific network configuration for NIC/IP binding.
  {
    VLOG_I("=== Example 4: VLINK_DDS_BIND ===");

    // Route dds:// URLs to CycloneDDS
    // Utils::set_env("VLINK_DDS_BIND", "ddsc");
    VLOG_I("  Set VLINK_DDS_BIND=ddsc to route dds:// URLs to CycloneDDS");

    // Show available network interfaces for reference
    auto addrs = vlink::Utils::get_all_ipv4_address(true);
    VLOG_I("  Available IPv4 addresses:");
    for (const auto& addr : addrs) {
      VLOG_I("    ", addr);
    }
  }

  // ======== Example 5: History depth ========
  // ?depth=N sets the DDS history depth for publishers and subscribers.
  // A larger depth allows more historical samples to be retained.
  {
    VLOG_I("=== Example 5: History depth ===");

    // Keep last 20 samples for late-joining subscribers
    vlink::UrlParser parser("dds://sensor/lidar?depth=20");
    const auto& dict = parser.get_query_dictionary();
    VLOG_I("  depth:", dict.at("depth"));

    // Usage:
    //   Publisher<PointCloud> pub("dds://sensor/lidar?depth=20");
    //   Subscriber<PointCloud> sub("dds://sensor/lidar?depth=20");
  }

  // ======== Example 6: Named QoS profiles ========
  // Register a named QoS profile, then reference it in the URL with ?qos=name.
  // This separates QoS policy from URL construction, enabling runtime configuration.
  {
    VLOG_I("=== Example 6: Named QoS profiles ===");

    // Register a custom QoS profile for high-rate sensor data
    vlink::Qos sensor_qos;
    sensor_qos.valid = true;
    sensor_qos.reliability.kind = vlink::Qos::Reliability::kBestEffort;
    sensor_qos.history.kind = vlink::Qos::History::kKeepLast;
    sensor_qos.history.depth = 20;
    sensor_qos.durability.kind = vlink::Qos::Durability::kVolatile;
    sensor_qos.publish_mode.kind = vlink::Qos::PublishMode::kASync;

    // DdsConf::register_qos("fast_sensor", sensor_qos);
    // After registration, use ?qos=fast_sensor in URLs:
    //   Publisher<SensorData> pub("dds://sensor/imu?qos=fast_sensor");
    //   Subscriber<SensorData> sub("dds://sensor/imu?qos=fast_sensor");

    VLOG_I("  Registered QoS profile 'fast_sensor':");
    VLOG_I("    reliability: BestEffort");
    VLOG_I("    history:     KeepLast(20)");
    VLOG_I("    durability:  Volatile");
    VLOG_I("    publish_mode: ASync");

    // Built-in QoS profiles from QosProfile namespace:
    VLOG_I("  Built-in profiles: event, method, field, sensor, parameter, service, ...");

    // The kSensor profile is pre-defined for high-rate sensor data:
    //   DdsConf::register_qos("sensor", QosProfile::kSensor);
    //   Publisher<SensorData> pub("dds://sensor/imu?qos=sensor");
  }

  // ======== Example 7: Extended QoS (per-entity XML) ========
  // For advanced use cases, per-entity QoS can be set via URL query:
  //   ?part=<xml>&topic=<xml>&pub=<xml>&sub=<xml>&writer=<xml>&reader=<xml>
  // These are mutually exclusive with ?qos= -- do not combine them.
  {
    VLOG_I("=== Example 7: Extended QoS (qos_ext) ===");

    // Example: set writer-level QoS via XML string in the URL
    vlink::UrlParser parser("dds://vehicle/speed?writer=my_writer_qos&reader=my_reader_qos");
    const auto& dict = parser.get_query_dictionary();
    for (const auto& [key, value] : dict) {
      VLOG_I("  ", key, " = ", value);
    }

    // Note: ?qos=profile and ?writer=xml cannot be used together.
    // If both are present, DdsConf::is_valid() returns false.
  }

  // ======== Example 8: DDS Global QoS file ========
  // Load a Fast-DDS XML QoS profile file to configure all participants globally.
  // Must be called BEFORE creating any dds:// nodes.
  {
    VLOG_I("=== Example 8: Global QoS file ===");

    // DdsConf::load_global_qos_file("/etc/vlink/dds_profile.xml");
    VLOG_I("  Use DdsConf::load_global_qos_file() to load XML profiles");
    VLOG_I("  Must be called before creating any dds:// nodes");
  }

  // ======== Example 9: CycloneDDS (ddsc://) ========
  // ddsc:// has the same URL format as dds:// but uses CycloneDDS internally.
  // It supports ?domain=, ?depth=, and ?qos= (but NOT qos_ext or register_topic).
  {
    VLOG_I("=== Example 9: CycloneDDS (ddsc://) ===");

    vlink::UrlParser parser("ddsc://vehicle/speed?domain=1&qos=sensor");
    VLOG_I("  transport:", parser.get_transport());
    VLOG_I("  topic:", parser.get_host(), parser.get_path());

    // Register QoS for CycloneDDS:
    //   DdscConf::register_qos("sensor", QosProfile::kSensor);
    //   Subscriber<int> sub("ddsc://vehicle/speed?qos=sensor");
  }

  // ======== Example 10: Practical multi-domain deployment ========
  // In automotive systems, different domains isolate different subsystems
  {
    VLOG_I("=== Example 10: Multi-domain deployment ===");

    VLOG_I("  Domain 0: vehicle dynamics   (dds://chassis/brake_pressure?domain=0)");
    VLOG_I("  Domain 1: infotainment       (dds://media/track_info?domain=1)");
    VLOG_I("  Domain 2: ADAS perception    (dds://perception/objects?domain=2)");
    VLOG_I("  Domain 3: V2X communication  (dds://v2x/bsm?domain=3)");
    VLOG_I("  Each domain is fully isolated -- no cross-domain message leakage");
  }

  return 0;
}
