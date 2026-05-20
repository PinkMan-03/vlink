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

// URL parser for inspecting MQTT URLs
#include <vlink/impl/url_parser.h>

#include <string>
#include <thread>

using namespace std::chrono_literals;  // NOLINT(build/namespaces, google-build-using-namespace)

/// MQTT (mqtt://) URL Examples
///
/// MQTT is a lightweight publish/subscribe messaging protocol designed for
/// constrained devices and low-bandwidth networks. VLink uses the Eclipse Paho
/// MQTT C library as the backend.
///
/// URL format:
///   mqtt://address[?event=name&domain=N&qos=0|1|2][#broker_uri]
///
/// Parameters:
///   - address:   MQTT topic path (host + "/" + path from URL)
///   - event:     optional secondary event filter
///   - domain:    domain/namespace identifier (default 0)
///   - qos:       MQTT QoS level: 0 (at most once), 1 (at least once), 2 (exactly once)
///   - fragment:  optional broker URI override (e.g., tcp://192.168.1.1:1883)
///
/// Environment variables:
///   - VLINK_MQTT_BROKER:     MQTT broker URI (default: tcp://localhost:1883)
///   - VLINK_MQTT_DOMAIN:     default domain ID (default: 0)
///   - VLINK_MQTT_QOS:        default QoS level 0/1/2 (default: 1)
///   - VLINK_MQTT_KEEPALIVE:  keep-alive interval in seconds (default: 60)
///   - VLINK_MQTT_CLIENT_ID:  client ID prefix (default: vlink_mqtt)
///
/// Prerequisites:
///   - An MQTT broker (e.g., Mosquitto) must be running and reachable
int main() {
  // ======== Example 1: Basic MQTT topic ========
  // The simplest form: just a topic path
  {
    VLOG_I("=== Example 1: Basic MQTT topic ===");

    vlink::UrlParser parser("mqtt://home/temperature");
    VLOG_I("  transport:", parser.get_transport());
    VLOG_I("  host:", parser.get_host());
    VLOG_I("  path:", parser.get_path());
    VLOG_I("  MQTT topic = home/temperature");

    // Usage:
    //   Publisher<std::string> pub("mqtt://home/temperature");
    //   Subscriber<std::string> sub("mqtt://home/temperature");
  }

  // ======== Example 2: Hierarchical topic paths ========
  // MQTT topics use "/" as a hierarchy separator, matching MQTT conventions.
  {
    VLOG_I("=== Example 2: Hierarchical topics ===");

    VLOG_I("  mqtt://home/living_room/temperature");
    VLOG_I("  mqtt://home/living_room/humidity");
    VLOG_I("  mqtt://home/bedroom/temperature");
    VLOG_I("  mqtt://office/server_room/temperature");

    // MQTT brokers support wildcard subscriptions:
    //   home/+/temperature  -- all rooms' temperature
    //   home/#              -- all home data
    // VLink subscribes to the exact topic in the URL.
  }

  // ======== Example 3: QoS levels ========
  // MQTT defines three quality-of-service levels:
  //   QoS 0: At most once (fire and forget, no ACK)
  //   QoS 1: At least once (ACK required, may duplicate)
  //   QoS 2: Exactly once (four-step handshake, guaranteed no duplicates)
  {
    VLOG_I("=== Example 3: QoS levels ===");

    // QoS 0: fastest, no delivery guarantee
    vlink::UrlParser p0("mqtt://sensor/temperature?qos=0");
    VLOG_I("  QoS 0:", p0.get_query_dictionary().at("qos"), " (at most once)");

    // QoS 1: balanced (default), at-least-once delivery
    vlink::UrlParser p1("mqtt://sensor/temperature?qos=1");
    VLOG_I("  QoS 1:", p1.get_query_dictionary().at("qos"), " (at least once)");

    // QoS 2: strictest, exactly-once delivery
    vlink::UrlParser p2("mqtt://sensor/temperature?qos=2");
    VLOG_I("  QoS 2:", p2.get_query_dictionary().at("qos"), " (exactly once)");

    // Recommendations:
    VLOG_I("  Use QoS 0 for: high-rate sensor data where occasional loss is OK");
    VLOG_I("  Use QoS 1 for: control commands, status updates (default)");
    VLOG_I("  Use QoS 2 for: financial transactions, critical alarms");
  }

  // ======== Example 4: Broker URI override via fragment ========
  // The #fragment overrides the default broker URI from VLINK_MQTT_BROKER env.
  // This allows different topics to connect to different brokers.
  {
    VLOG_I("=== Example 4: Broker URI override ===");

    std::string url = "mqtt://home/temperature?qos=1#tcp://192.168.1.100:1883";
    vlink::UrlParser parser(url);
    VLOG_I("  URL:", url);
    VLOG_I("  fragment (broker):", parser.get_fragment());

    // Different broker URIs:
    VLOG_I("  tcp://host:1883          -- unencrypted TCP (default port 1883)");
    VLOG_I("  ssl://host:8883          -- TLS-encrypted TCP (default port 8883)");
    VLOG_I("  ws://host:9001           -- WebSocket (for browser clients)");
    VLOG_I("  wss://host:9001          -- Secure WebSocket");
  }

  // ======== Example 5: VLINK_MQTT_BROKER environment variable ========
  // Sets the default broker for all mqtt:// URLs that do not have a #fragment.
  {
    VLOG_I("=== Example 5: VLINK_MQTT_BROKER env ===");

    std::string saved = vlink::Utils::get_env("VLINK_MQTT_BROKER");

    vlink::Utils::set_env("VLINK_MQTT_BROKER", "tcp://mqtt.example.com:1883");
    VLOG_I("  VLINK_MQTT_BROKER=", vlink::Utils::get_env("VLINK_MQTT_BROKER"));

    // Now all mqtt:// URLs without #fragment use this broker:
    //   Publisher<std::string> pub("mqtt://home/temp");  // connects to mqtt.example.com

    // Restore

    if (saved.empty()) {
      vlink::Utils::unset_env("VLINK_MQTT_BROKER");
    } else {
      vlink::Utils::set_env("VLINK_MQTT_BROKER", saved);
    }
  }

  // ======== Example 6: VLINK_MQTT_CLIENT_ID ========
  // Sets the MQTT client ID prefix. Each VLink process appends a unique suffix.
  // MQTT brokers require unique client IDs; duplicate IDs cause disconnection.
  {
    VLOG_I("=== Example 6: VLINK_MQTT_CLIENT_ID ===");

    vlink::Utils::set_env("VLINK_MQTT_CLIENT_ID", "my_robot");
    VLOG_I("  VLINK_MQTT_CLIENT_ID=", vlink::Utils::get_env("VLINK_MQTT_CLIENT_ID"));
    VLOG_I("  Actual client ID will be: my_robot_<pid>_<counter>");

    vlink::Utils::unset_env("VLINK_MQTT_CLIENT_ID");
  }

  // ======== Example 7: VLINK_MQTT_QOS default ========
  // Sets the default QoS level for all mqtt:// URLs that do not specify ?qos=.
  {
    VLOG_I("=== Example 7: VLINK_MQTT_QOS env ===");

    vlink::Utils::set_env("VLINK_MQTT_QOS", "2");
    VLOG_I("  VLINK_MQTT_QOS=", vlink::Utils::get_env("VLINK_MQTT_QOS"));
    VLOG_I("  All mqtt:// URLs without ?qos= will use QoS 2 by default");

    // Explicit ?qos= in the URL always overrides:
    //   mqtt://topic?qos=0  -- uses QoS 0 regardless of VLINK_MQTT_QOS

    vlink::Utils::unset_env("VLINK_MQTT_QOS");
  }

  // ======== Example 8: Domain parameter ========
  // ?domain=N provides namespace isolation at the VLink level.
  {
    VLOG_I("=== Example 8: Domain parameter ===");

    std::string url = "mqtt://vehicle/speed?domain=1";
    vlink::UrlParser parser(url);
    const auto& dict = parser.get_query_dictionary();
    VLOG_I("  URL:", url);
    VLOG_I("  domain:", dict.at("domain"));
  }

  // ======== Example 9: Event parameter ========
  {
    VLOG_I("=== Example 9: Event parameter ===");

    std::string url = "mqtt://sensor/camera?event=frame_ready";
    vlink::UrlParser parser(url);
    const auto& dict = parser.get_query_dictionary();
    VLOG_I("  URL:", url);
    VLOG_I("  event:", dict.at("event"));
  }

  // ======== Example 10: Combined parameters ========
  {
    VLOG_I("=== Example 10: Combined parameters ===");

    std::string url = "mqtt://factory/line1/robot/status?event=alarm&domain=1&qos=2#tcp://10.0.0.1:1883";
    vlink::UrlParser parser(url);
    VLOG_I("  URL:", url);

    const auto& dict = parser.get_query_dictionary();
    for (const auto& [key, value] : dict) {
      VLOG_I("  ", key, " = ", value);
    }
    VLOG_I("  fragment:", parser.get_fragment());
  }

  // ======== Example 11: All six node types ========
  {
    VLOG_I("=== Example 11: All six node types ===");

    VLOG_I("  Publisher:  mqtt://topic/pub_sub");
    VLOG_I("  Subscriber: mqtt://topic/pub_sub");
    VLOG_I("  Server:     mqtt://topic/rpc");
    VLOG_I("  Client:     mqtt://topic/rpc");
    VLOG_I("  Setter:     mqtt://topic/field");
    VLOG_I("  Getter:     mqtt://topic/field");
  }

  // ======== Example 12: Direct MqttConf construction ========
  {
    VLOG_I("=== Example 12: Direct MqttConf ===");

    // MqttConf conf("home/temperature", "event_name", /*domain=*/0, /*qos=*/1, "tcp://localhost:1883");
    // Publisher<std::string> pub(conf);
    VLOG_I("  MqttConf(address, event, domain, qos, fragment)");
    VLOG_I("  All parameters except address are optional");
  }

  return 0;
}
