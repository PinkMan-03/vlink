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

// QoS profile definitions
#include <vlink/extension/qos_profile.h>

// URL parser for inspecting Zenoh URLs
#include <vlink/impl/url_parser.h>

#include <string>
#include <thread>

using namespace std::chrono_literals;  // NOLINT(build/namespaces, google-build-using-namespace)

/// Zenoh (zenoh://) URL Examples
///
/// Eclipse Zenoh is a protocol designed for unified data management across robots,
/// edge nodes, and cloud infrastructure. It supports peer-to-peer and routed
/// pub/sub with key-expression-based addressing.
///
/// URL format:
///   zenoh://address[?event=name&domain=N&qos=profile_name][#fragment]
///
/// Parameters:
///   - address:   Zenoh key expression (host + "/" + path from URL)
///   - event:     optional secondary event filter
///   - domain:    Zenoh session/domain identifier (default 0)
///   - qos:       named QoS profile registered via ZenohConf::register_qos()
///   - fragment:  optional transport hint passed to the Zenoh session
///
/// Key advantages of Zenoh over DDS:
///   - Lighter resource footprint (no heavy discovery protocol)
///   - Native support for WAN/cloud bridging
///   - Key expression pattern matching (similar to MQTT topic filters)
///   - Built-in storage and query primitives
int main() {
  // ======== Example 1: Basic Zenoh key expression ========
  // The address is a Zenoh key expression formed from host + "/" + path
  {
    VLOG_I("=== Example 1: Basic key expression ===");

    vlink::UrlParser parser("zenoh://vehicle/speed");
    VLOG_I("  transport:", parser.get_transport());
    VLOG_I("  host:", parser.get_host());
    VLOG_I("  path:", parser.get_path());
    VLOG_I("  key expression = vehicle/speed");

    // Usage:
    //   Publisher<std::string> pub("zenoh://vehicle/speed");
    //   Subscriber<std::string> sub("zenoh://vehicle/speed");
  }

  // ======== Example 2: Hierarchical key expressions ========
  // Zenoh key expressions support hierarchical topic paths, similar to file paths.
  // This enables organized topic namespaces.
  {
    VLOG_I("=== Example 2: Hierarchical key expressions ===");

    VLOG_I("  zenoh://robot/arm/joint1/position   -- joint 1 position");
    VLOG_I("  zenoh://robot/arm/joint2/position   -- joint 2 position");
    VLOG_I("  zenoh://robot/arm/joint1/velocity   -- joint 1 velocity");
    VLOG_I("  zenoh://robot/base/odometry         -- base odometry");

    // In Zenoh, subscribers can use wildcards to match multiple keys:
    //   zenoh://robot/arm/*/position  -- all joint positions
    //   zenoh://robot/**              -- all robot data
    // Note: VLink uses the address as-is; Zenoh-level wildcards are handled by Zenoh.
  }

  // ======== Example 3: Event parameter ========
  // ?event= provides an optional secondary filter
  {
    VLOG_I("=== Example 3: Event parameter ===");

    std::string url = "zenoh://sensor/lidar?event=scan_complete";
    vlink::UrlParser parser(url);
    const auto& dict = parser.get_query_dictionary();
    VLOG_I("  URL:", url);
    VLOG_I("  event:", dict.at("event"));
  }

  // ======== Example 4: Domain parameter ========
  // ?domain=N specifies the Zenoh session/domain identifier.
  // Different domains create separate Zenoh sessions, providing isolation.
  {
    VLOG_I("=== Example 4: Domain parameter ===");

    std::string url = "zenoh://vehicle/speed?domain=1";
    vlink::UrlParser parser(url);
    const auto& dict = parser.get_query_dictionary();
    VLOG_I("  URL:", url);
    VLOG_I("  domain:", dict.at("domain"));

    // Domain isolation examples:
    //   zenoh://vehicle/speed?domain=0  -- production domain
    //   zenoh://vehicle/speed?domain=1  -- simulation domain
    //   zenoh://vehicle/speed?domain=2  -- test domain
  }

  // ======== Example 5: Named QoS profiles ========
  // Register a QoS profile and reference it with ?qos=name
  {
    VLOG_I("=== Example 5: Named QoS profiles ===");

    // Register a custom QoS profile
    vlink::Qos reliable_qos;
    reliable_qos.valid = true;
    reliable_qos.reliability.kind = vlink::Qos::Reliability::kReliable;
    reliable_qos.history.kind = vlink::Qos::History::kKeepLast;
    reliable_qos.history.depth = 10;

    // ZenohConf::register_qos("zenoh_reliable", reliable_qos);
    VLOG_I("  ZenohConf::register_qos(\"zenoh_reliable\", qos)");

    // Then reference in URL:
    //   Subscriber<Msg> sub("zenoh://vehicle/speed?qos=zenoh_reliable");
    VLOG_I("  zenoh://vehicle/speed?qos=zenoh_reliable");

    // You can also use built-in profiles:
    //   ZenohConf::register_qos("sensor", QosProfile::kSensor);
    //   Subscriber<Msg> sub("zenoh://sensor/imu?qos=sensor");
  }

  // ======== Example 6: Fragment (transport hint) ========
  // The #fragment provides a transport hint passed to the Zenoh session.
  // This can be used for router/peer connection configuration.
  {
    VLOG_I("=== Example 6: Fragment (transport hint) ===");

    std::string url = "zenoh://vehicle/speed#tcp/192.168.1.1:7447";
    vlink::UrlParser parser(url);
    VLOG_I("  URL:", url);
    VLOG_I("  fragment:", parser.get_fragment());

    // The fragment can contain Zenoh-specific locators:
    //   #tcp/192.168.1.1:7447         -- connect via TCP to a specific peer
    //   #udp/192.168.1.1:7447         -- connect via UDP
    //   #unixsock-stream//tmp/zenoh   -- Unix socket
  }

  // ======== Example 7: Combined parameters ========
  {
    VLOG_I("=== Example 7: Combined parameters ===");

    std::string url = "zenoh://robot/arm/joint1?event=position&domain=1&qos=sensor#tcp/10.0.0.1:7447";
    vlink::UrlParser parser(url);
    VLOG_I("  URL:", url);
    VLOG_I("  address: robot/arm/joint1");

    const auto& dict = parser.get_query_dictionary();
    for (const auto& [key, value] : dict) {
      VLOG_I("  ", key, " = ", value);
    }
    VLOG_I("  fragment:", parser.get_fragment());
  }

  // ======== Example 8: Zenoh vs DDS comparison ========
  {
    VLOG_I("=== Example 8: Zenoh vs DDS comparison ===");

    VLOG_I("  Feature            | DDS (dds://)           | Zenoh (zenoh://)");
    VLOG_I("  -------------------|------------------------|------------------------");
    VLOG_I("  Discovery          | SPDP/SEDP multicast    | Scouting/gossip");
    VLOG_I("  Addressing         | topic + domain         | key expression + domain");
    VLOG_I("  Network            | LAN only (typically)   | LAN + WAN + Cloud");
    VLOG_I("  QoS                | Full DDS QoS           | Simplified QoS");
    VLOG_I("  Footprint          | Heavier                | Lighter");
    VLOG_I("  Wildcards          | Content filter only    | Key expression patterns");
    VLOG_I("  Storage            | Via durability service  | Built-in queryable");
    VLOG_I("  Transport          | UDP multicast          | TCP/UDP/QUIC/WebSocket");

    // URL comparison:
    VLOG_I("  DDS URL:   dds://vehicle/speed?domain=1&qos=sensor");
    VLOG_I("  Zenoh URL: zenoh://vehicle/speed?domain=1&qos=sensor");
    // The API is identical -- only the transport changes!
  }

  // ======== Example 9: All six node types ========
  {
    VLOG_I("=== Example 9: All six node types ===");

    VLOG_I("  Publisher:  zenoh://topic/pub_sub");
    VLOG_I("  Subscriber: zenoh://topic/pub_sub");
    VLOG_I("  Server:     zenoh://topic/rpc");
    VLOG_I("  Client:     zenoh://topic/rpc");
    VLOG_I("  Setter:     zenoh://topic/field");
    VLOG_I("  Getter:     zenoh://topic/field");
  }

  // ======== Example 10: Direct ZenohConf construction ========
  {
    VLOG_I("=== Example 10: Direct ZenohConf ===");

    // ZenohConf conf("vehicle/speed", "event_name", /*domain=*/1, "sensor", "tcp/10.0.0.1:7447");
    // Publisher<Msg> pub(conf);
    VLOG_I("  ZenohConf(address, event, domain, qos, fragment)");
    VLOG_I("  All parameters are optional except address");
  }

  return 0;
}
