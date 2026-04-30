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

// URL parser for inspecting SOME/IP URLs
#include <vlink/impl/url_parser.h>

#include <string>
#include <thread>

using namespace std::chrono_literals;  // NOLINT(build/namespaces, google-build-using-namespace)

/// SOME/IP (someip://) URL Examples
///
/// SOME/IP (Scalable service-Oriented MiddlewarE over IP) is the standard
/// automotive middleware protocol used in AUTOSAR Ethernet environments.
///
/// Unlike string-based topic names used by DDS/Zenoh/MQTT, SOME/IP uses a
/// numeric identifier hierarchy:
///   - Service ID:   16-bit, identifies the service interface
///   - Instance ID:  16-bit, identifies a specific instance of the service
///   - Method ID:    16-bit, identifies an RPC method (for Server/Client)
///   - Event Group:  16-bit, identifies a subscription group (for pub/sub and field)
///   - Event ID:     16-bit, identifies an event within a group
///
/// URL format:
///   RPC:    someip://service_id/instance_id?method=method_id
///   Event:  someip://service_id/instance_id?groups=g1,g2,...&event=event_id
///   Field:  someip://service_id/instance_id?groups=g1,g2,...&event=event_id&field=1
///
/// All numeric values in the URL are decimal. Use decimal equivalents of hex:
///   0x1234 = 4660, 0x5678 = 22136, 0xFFFF = 65535
///
/// Or use hex notation directly in the URL (0x prefix):
///   someip://0x1234/0x5678?method=0x1
///
/// Prerequisites:
///   - vsomeip daemon must be running
///   - vsomeip JSON configuration file recommended
int main() {
  // ======== Example 1: RPC (Method model) ========
  // For Server/Client, use ?method=method_id
  // The service_id and instance_id are part of the URL path.
  {
    VLOG_I("=== Example 1: RPC (Method model) ===");

    // Service 0x1234 (4660), Instance 0x5678 (22136), Method 0x0001 (1)
    std::string url_decimal = "someip://4660/22136?method=1";
    std::string url_hex = "someip://0x1234/0x5678?method=0x1";

    vlink::UrlParser parser(url_decimal);
    VLOG_I("  Decimal URL:", url_decimal);
    VLOG_I("  Hex URL:    ", url_hex);
    VLOG_I("  host (service_id):  ", parser.get_host());
    VLOG_I("  path (instance_id): ", parser.get_path());

    const auto& dict = parser.get_query_dictionary();
    VLOG_I("  method:", dict.at("method"));

    // Usage:
    //   Server<Bytes, Bytes> server("someip://0x1234/0x5678?method=0x1");
    //   server.listen([](const Bytes& req, Bytes& resp) { ... });
    //
    //   Client<Bytes, Bytes> client("someip://0x1234/0x5678?method=0x1");
    //   auto resp = client.invoke(Bytes{0x1, 0x2});
  }

  // ======== Example 2: Event model (Pub/Sub) ========
  // For Publisher/Subscriber, use ?groups=group_ids&event=event_id
  // Multiple event groups can be specified as a comma-separated list.
  {
    VLOG_I("=== Example 2: Event model (Pub/Sub) ===");

    // Service 0x1234, Instance 0x5678, Group 0x0001, Event 0x0010 (16)
    std::string url = "someip://0x1234/0x5678?groups=1&event=16";

    vlink::UrlParser parser(url);
    const auto& dict = parser.get_query_dictionary();
    VLOG_I("  URL:", url);
    VLOG_I("  groups:", dict.at("groups"));
    VLOG_I("  event:", dict.at("event"));

    // Usage:
    //   Publisher<MyMsg> pub("someip://0x1234/0x5678?groups=1&event=16");
    //   Subscriber<MyMsg> sub("someip://0x1234/0x5678?groups=1&event=16");
  }

  // ======== Example 3: Multiple event groups ========
  // A subscriber can join multiple event groups in a single URL.
  // Groups are comma-separated: ?groups=1,2,3
  {
    VLOG_I("=== Example 3: Multiple event groups ===");

    std::string url = "someip://0x1234/0x5678?groups=1,2,3&event=16";

    vlink::UrlParser parser(url);
    const auto& dict = parser.get_query_dictionary();
    VLOG_I("  URL:", url);
    VLOG_I("  groups:", dict.at("groups"));  // "1,2,3"

    // The subscriber joins event groups 1, 2, and 3 simultaneously.
    // Events published to any of these groups will be received.
  }

  // ======== Example 4: Field model (Getter/Setter) ========
  // For Getter/Setter, add ?field=1 to the event URL.
  // The ?field=1 flag tells VLink to use field semantics:
  //   - Setter: notifies all getters when the value changes
  //   - Getter: retrieves the latest value on demand
  {
    VLOG_I("=== Example 4: Field model ===");

    std::string url = "someip://0x1234/0x5678?groups=1&event=20&field=1";

    vlink::UrlParser parser(url);
    const auto& dict = parser.get_query_dictionary();
    VLOG_I("  URL:", url);
    VLOG_I("  groups:", dict.at("groups"));
    VLOG_I("  event:", dict.at("event"));
    VLOG_I("  field:", dict.at("field"));

    // Usage:
    //   Setter<int> setter("someip://0x1234/0x5678?groups=1&event=20&field=1");
    //   setter.set(42);
    //
    //   Getter<int> getter("someip://0x1234/0x5678?groups=1&event=20&field=1");
    //   auto val = getter.get();
  }

  // ======== Example 5: Hex notation in URLs ========
  // SOME/IP IDs are traditionally written in hexadecimal.
  // VLink supports the 0x prefix for convenience.
  {
    VLOG_I("=== Example 5: Hex notation ===");

    // These are equivalent:
    std::string url_dec = "someip://4660/22136?method=1";
    std::string url_hex = "someip://0x1234/0x5678?method=0x1";

    VLOG_I("  Decimal: ", url_dec);
    VLOG_I("  Hex:     ", url_hex);

    // Common automotive service ID ranges:
    VLOG_I("  Vehicle dynamics:   0x0100 - 0x01FF (256 - 511)");
    VLOG_I("  Infotainment:       0x0200 - 0x02FF (512 - 767)");
    VLOG_I("  ADAS:               0x0300 - 0x03FF (768 - 1023)");
    VLOG_I("  Diagnostics:        0xFFF0 - 0xFFFE (65520 - 65534)");
  }

  // ======== Example 6: Construct SomeipConf directly ========
  // Instead of URL strings, you can construct SomeipConf objects directly.
  // This is useful when service/instance IDs come from configuration files.
  {
    VLOG_I("=== Example 6: Direct SomeipConf construction ===");

    // RPC config: service=0x1234, instance=0x5678, method=0x0001
    // SomeipConf rpc_conf(0x1234, 0x5678, 0x0001);
    // Server<Bytes, Bytes> server(rpc_conf);
    VLOG_I("  SomeipConf(service, instance, method) for RPC");

    // Event config: service=0x1234, instance=0x5678, groups={0x0001}, event=0x0010
    // SomeipConf event_conf(0x1234, 0x5678, {0x0001}, 0x0010);
    // Publisher<MyMsg> pub(event_conf);
    VLOG_I("  SomeipConf(service, instance, groups, event) for pub/sub");

    // Field config: add field=true
    // SomeipConf field_conf(0x1234, 0x5678, {0x0001}, 0x0020, true);
    // Setter<int> setter(field_conf);
    VLOG_I("  SomeipConf(service, instance, groups, event, true) for field");
  }

  // ======== Example 7: vsomeip configuration ========
  // Load a vsomeip JSON configuration file for routing and network setup.
  // Must be called before creating any someip:// nodes.
  {
    VLOG_I("=== Example 7: vsomeip configuration ===");

    // SomeipConf::load_global_config_file("/etc/vsomeip/vsomeip.json");
    VLOG_I("  SomeipConf::load_global_config_file(\"/etc/vsomeip/vsomeip.json\")");
    VLOG_I("  The JSON file configures:");
    VLOG_I("    - Application name and routing manager");
    VLOG_I("    - Unicast address and port ranges");
    VLOG_I("    - Service discovery (SD) settings");
    VLOG_I("    - Logging level");
  }

  // ======== Example 8: Fire-and-forget RPC ========
  // A Client can be used with only a request type (no response) for
  // fire-and-forget messages. The server receives but does not reply.
  {
    VLOG_I("=== Example 8: Fire-and-forget ===");

    // Fire-and-forget: Client<ReqT> with no RespT -> send mode
    //   Server<Bytes> server("someip://0x100/0x1?method=0x5");
    //   server.listen([](const Bytes& req) { /* process */ });
    //
    //   Client<Bytes> client("someip://0x100/0x1?method=0x5");
    //   client.send(Bytes{0x01, 0x02});
    VLOG_I("  Client<ReqT> (no RespT) = fire-and-forget send mode");
  }

  // ======== Example 9: Typical automotive service layout ========
  {
    VLOG_I("=== Example 9: Automotive service layout ===");

    VLOG_I("  Brake service (RPC):");
    VLOG_I("    someip://0x0100/0x0001?method=0x01  -- getBrakePressure");
    VLOG_I("    someip://0x0100/0x0001?method=0x02  -- setBrakeForce");
    VLOG_I("");
    VLOG_I("  Brake service (Events):");
    VLOG_I("    someip://0x0100/0x0001?groups=0x01&event=0x8001  -- brakePressureChanged");
    VLOG_I("    someip://0x0100/0x0001?groups=0x01&event=0x8002  -- absActivated");
    VLOG_I("");
    VLOG_I("  Brake service (Fields):");
    VLOG_I("    someip://0x0100/0x0001?groups=0x02&event=0x8010&field=1  -- brakeWearLevel");
  }

  return 0;
}
