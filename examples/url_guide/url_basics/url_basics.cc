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

// URL parser for decomposing and inspecting VLink URLs
#include <vlink/impl/url_parser.h>

#include <iostream>
#include <string>
#include <thread>

using namespace std::chrono_literals;  // NOLINT(build/namespaces, google-build-using-namespace)

/// VLink URL Format Anatomy Example
///
/// This example demonstrates the complete VLink URL format:
///   transport://host/path[?key=value&key=value][#fragment]
///
/// Each component serves a specific purpose in the transport configuration:
///   - transport:   selects the transport backend (intra, shm, dds, zenoh, someip, mqtt, etc.)
///   - host:     primary topic/address identifier
///   - path:     secondary topic path (combined with host to form the full address)
///   - query:    transport-specific parameters as key=value pairs
///   - fragment: mode or hint passed to the transport (e.g., queue/direct, broker URI)
///
/// The URL is parsed by UrlParser into individual components that can be inspected.

/// Helper function to parse and print all components of a VLink URL
void demonstrate_url_parsing(const std::string& url_str) {
  VLOG_I("========================================");
  VLOG_I("URL:", url_str);

  // Parse the URL using UrlParser
  vlink::UrlParser parser(url_str);

  // Extract and display each component
  VLOG_I("  transport:   ", parser.get_transport());
  VLOG_I("  host:     ", parser.get_host());
  VLOG_I("  path:     ", parser.get_path());
  VLOG_I("  query:    ", parser.get_query());
  VLOG_I("  fragment: ", parser.get_fragment());
  VLOG_I("  port:     ", parser.get_port());

  // Display parsed query dictionary (key-value pairs)
  const auto& dict = parser.get_query_dictionary();
  if (!dict.empty()) {
    VLOG_I("  query parameters:");
    for (const auto& [key, value] : dict) {
      VLOG_I("    ", key, " = ", value);
    }
  }

  // Reconstruct the URL from parsed components
  VLOG_I("  reconstructed: ", parser.to_string());
  VLOG_I("========================================");
}

int main() {
  // ======== Section 1: URL Format for Each Transport ========
  // Demonstrate how URLs are structured for different VLink transports

  // 1. intra:// - In-process messaging (no IPC overhead)
  //    Format: intra://address[?event=name&pipeline=N][#queue|#direct]
  demonstrate_url_parsing("intra://sensor/lidar");
  demonstrate_url_parsing("intra://sensor/lidar?event=scan&pipeline=4#direct");

  // 2. shm:// - Shared memory via Iceoryx (zero-copy IPC)
  //    Format: shm://address[?event=name&domain=N&depth=N&history=N&wait=0|1]
  demonstrate_url_parsing("shm://vehicle/speed");
  demonstrate_url_parsing("shm://vehicle/speed?domain=1&depth=16&history=5&wait=1");

  // 3. dds:// - Fast-DDS RTPS (cross-machine network)
  //    Format: dds://topic[?domain=N&depth=N&qos=profile_name]
  demonstrate_url_parsing("dds://vehicle/speed");
  demonstrate_url_parsing("dds://vehicle/speed?domain=42&depth=10&qos=sensor");

  // 4. ddsc:// - CycloneDDS (alternative DDS backend)
  //    Format: ddsc://topic[?domain=N&depth=N&qos=profile_name]
  demonstrate_url_parsing("ddsc://navigation/path?domain=1&qos=reliable");

  // 5. zenoh:// - Eclipse Zenoh protocol
  //    Format: zenoh://address[?event=name&domain=N&qos=profile_name][#fragment]
  demonstrate_url_parsing("zenoh://robot/arm/joint1?domain=0&qos=sensor");

  // 6. someip:// - SOME/IP automotive middleware
  //    Format: someip://service_id/instance_id?method=X (RPC)
  //    Format: someip://service_id/instance_id?groups=G&event=E (Event)
  demonstrate_url_parsing("someip://4660/22136?method=1");
  demonstrate_url_parsing("someip://4660/22136?groups=1,2&event=16&field=1");

  // 7. mqtt:// - MQTT IoT protocol
  //    Format: mqtt://address[?event=name&domain=N&qos=0|1|2][#broker_uri]
  demonstrate_url_parsing("mqtt://home/temperature?qos=2");
  demonstrate_url_parsing("mqtt://home/temperature?qos=1#tcp://192.168.1.100:1883");

  // 8. fdbus:// - FDBus IPC
  //    Format: fdbus://address[?event=name][#svc|#ipc]
  demonstrate_url_parsing("fdbus://audio/volume?event=level_changed#svc");

  // 9. qnx:// - QNX native IPC
  //    Format: qnx://address[?event=name]
  demonstrate_url_parsing("qnx://sensor/radar?event=target_detected");

  // ======== Section 2: Construct URLs from Components ========
  // Build a URL from individual UrlParser::Component entries
  std::map<vlink::UrlParser::Component, std::string> components;
  components[vlink::UrlParser::Component::kTransport] = "dds";
  components[vlink::UrlParser::Component::kHost] = "vehicle";
  components[vlink::UrlParser::Component::kPath] = "/telemetry/gps";
  components[vlink::UrlParser::Component::kQuery] = "domain=5&qos=sensor";
  components[vlink::UrlParser::Component::kFragment] = "";

  vlink::UrlParser built(components, vlink::UrlParser::Category::kHierarchical, true);
  VLOG_I("Built URL: ", built.to_string());

  // ======== Section 3: Copy and Override URL Components ========
  // Create a modified copy of an existing URL by overriding specific components
  vlink::UrlParser original("dds://vehicle/speed?domain=0&qos=sensor");

  std::map<vlink::UrlParser::Component, std::string> overrides;
  overrides[vlink::UrlParser::Component::kQuery] = "domain=99&qos=best";

  vlink::UrlParser modified(original, overrides);
  VLOG_I("Original:  ", original.to_string());
  VLOG_I("Modified:  ", modified.to_string());

  // ======== Section 4: Practical Usage - Same API, Different Transports ========
  // Demonstrate that the same VLink API works with any transport prefix in the URL.
  // Only the URL string changes; the code remains identical.

  // Use intra:// for in-process pub/sub (always available, no external dependencies)
  vlink::Subscriber<std::string> sub("intra://demo/url_basics");
  sub.listen([](const std::string& msg) { VLOG_I("Received:", msg); });

  vlink::Publisher<std::string> pub("intra://demo/url_basics");
  pub.wait_for_subscribers();
  pub.publish("Hello from url_basics example!");

  std::this_thread::sleep_for(100ms);

  // To switch transport, only the URL changes:
  //   Publisher<std::string> pub("dds://demo/url_basics");     // DDS
  //   Publisher<std::string> pub("shm://demo/url_basics");     // shared memory
  //   Publisher<std::string> pub("zenoh://demo/url_basics");   // Zenoh
  //   Publisher<std::string> pub("mqtt://demo/url_basics");    // MQTT

  // ======== Section 5: URL Utility Functions ========
  // The Url class provides static helpers to classify URLs without constructing nodes.

  VLOG_I("is_local_type('intra://x'):", vlink::Url::is_local_type("intra://x"));
  VLOG_I("is_local_type('dds://x'):", vlink::Url::is_local_type("dds://x"));
  VLOG_I("is_intra_type('intra://x'):", vlink::Url::is_intra_type("intra://x"));
  VLOG_I("is_shm_type('shm://x'):", vlink::Url::is_shm_type("shm://x"));
  VLOG_I("get_sort_index('intra://x'):", vlink::Url::get_sort_index("intra://x"));
  VLOG_I("get_sort_index('dds://x'):", vlink::Url::get_sort_index("dds://x"));

  return 0;
}
