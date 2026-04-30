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

// URL parser for inspecting SHM URLs
#include <vlink/impl/url_parser.h>

#include <string>
#include <thread>

using namespace std::chrono_literals;  // NOLINT(build/namespaces, google-build-using-namespace)

/// Shared Memory (shm://) URL Examples
///
/// The shm:// transport uses Eclipse Iceoryx for zero-copy inter-process communication
/// on the same machine. It provides the lowest latency and highest throughput for
/// same-machine IPC, but requires the Iceoryx RouDi daemon to be running.
///
/// URL format:
///   shm://address[?event=name&domain=N&depth=N&history=N&wait=0|1]
///
/// Parameters:
///   - address:  topic name (host + "/" + path), max 80 characters
///   - event:    secondary event name, max 80 characters
///   - domain:   Iceoryx domain ID (default 0), isolates shared memory segments
///   - depth:    history buffer depth (0 = no buffering)
///   - history:  history count (default 0 for pub/sub, 1 for getter/setter)
///   - wait:     blocking-wait mode for pub/sub (1 = block, 0 = non-blocking)
///
/// Prerequisites:
///   - Iceoryx RouDi daemon must be running:
///       $ iox-roudi &
///   - Or initialize RouDi in-process via ShmConf::init_roudi()
int main() {
  // ======== Example 1: Basic shm:// address ========
  // The simplest form: just a topic address
  // Note: address + event strings must each be <= 80 characters (Iceoryx limit)
  {
    VLOG_I("=== Example 1: Basic shm:// address ===");

    vlink::UrlParser parser("shm://vehicle/speed");
    VLOG_I("  transport:", parser.get_transport());
    VLOG_I("  host:", parser.get_host());
    VLOG_I("  path:", parser.get_path());
    VLOG_I("  address = host + path = vehicle/speed");

    // Usage (requires running RouDi):
    // Publisher<int> pub("shm://vehicle/speed");
    // Subscriber<int> sub("shm://vehicle/speed");
  }

  // ======== Example 2: Event parameter ========
  // ?event= provides a secondary discriminator within the same address.
  // Both address and event must be <= 80 characters due to Iceoryx naming limits.
  {
    VLOG_I("=== Example 2: Event parameter ===");

    vlink::UrlParser parser("shm://sensor/camera?event=front_rgb");
    const auto& dict = parser.get_query_dictionary();
    VLOG_I("  event:", dict.at("event"));

    // Different events on the same address are separate topics:
    //   shm://sensor/camera?event=front_rgb   -- front camera RGB
    //   shm://sensor/camera?event=rear_depth  -- rear camera depth
  }

  // ======== Example 3: Depth parameter ========
  // ?depth=N sets the history buffer depth for the shared memory segment.
  // A larger depth allows more messages to be buffered before the oldest is overwritten.
  // Tuning guide:
  //   - depth=0:   no buffering (default), publisher overwrites the single slot
  //   - depth=1-5: suitable for low-rate control signals
  //   - depth=10+: suitable for high-rate sensor data (prevents data loss under burst)
  {
    VLOG_I("=== Example 3: Depth parameter ===");

    vlink::UrlParser parser("shm://sensor/lidar?depth=16");
    const auto& dict = parser.get_query_dictionary();
    VLOG_I("  depth:", dict.at("depth"));

    // Higher depth uses more shared memory but prevents message loss:
    //   Publisher<PointCloud> pub("shm://sensor/lidar?depth=16");
  }

  // ======== Example 4: History parameter ========
  // ?history=N controls how many past samples are available for late-joining subscribers.
  // Default: 0 for pub/sub, 1 for getter/setter (field model).
  {
    VLOG_I("=== Example 4: History parameter ===");

    vlink::UrlParser parser("shm://vehicle/position?history=5");
    const auto& dict = parser.get_query_dictionary();
    VLOG_I("  history:", dict.at("history"));

    // With history=5, a late-joining subscriber can receive up to 5 past messages
  }

  // ======== Example 5: Wait (blocking) mode ========
  // ?wait=1 enables blocking-wait mode for pub/sub nodes.
  // In blocking mode, the subscriber blocks until a new message arrives.
  // Note: wait mode is ONLY valid for Publisher/Subscriber, NOT for Server/Client/Getter/Setter.
  {
    VLOG_I("=== Example 5: Wait (blocking) mode ===");

    vlink::UrlParser parser("shm://vehicle/speed?wait=1");
    const auto& dict = parser.get_query_dictionary();
    VLOG_I("  wait:", dict.at("wait"));

    // Blocking subscriber will wait for data:
    //   Subscriber<int> sub("shm://vehicle/speed?wait=1");
    // Non-blocking (default):
    //   Subscriber<int> sub("shm://vehicle/speed?wait=0");
    //   Subscriber<int> sub("shm://vehicle/speed");  // same as wait=0
  }

  // ======== Example 6: Domain isolation ========
  // ?domain=N isolates shared memory segments by domain ID.
  // Different domains use separate Iceoryx memory pools.
  {
    VLOG_I("=== Example 6: Domain isolation ===");

    vlink::UrlParser parser("shm://vehicle/speed?domain=1");
    const auto& dict = parser.get_query_dictionary();
    VLOG_I("  domain:", dict.at("domain"));

    // Domain 0 and Domain 1 are completely isolated:
    //   Publisher<int> pub0("shm://vehicle/speed?domain=0");  // domain 0
    //   Publisher<int> pub1("shm://vehicle/speed?domain=1");  // domain 1
    //   Subscriber<int> sub0("shm://vehicle/speed?domain=0"); // only receives from pub0
  }

  // ======== Example 7: Combined parameters ========
  // All parameters can be combined in a single URL
  {
    VLOG_I("=== Example 7: Combined parameters ===");

    std::string url = "shm://sensor/camera?event=front&domain=1&depth=8&history=3&wait=1";
    vlink::UrlParser parser(url);
    VLOG_I("  Full URL:", url);

    const auto& dict = parser.get_query_dictionary();
    for (const auto& [key, value] : dict) {
      VLOG_I("  ", key, " = ", value);
    }
  }

  // ======== Example 8: RouDi initialization ========
  // Before using shm:// nodes, the Iceoryx runtime must be initialized.
  // Two options:
  //   Option A: In-process RouDi (for single-process testing)
  //   Option B: External RouDi (recommended for production)
  {
    VLOG_I("=== Example 8: RouDi initialization ===");

    // Option A: Start RouDi in the same process (for testing/single-process scenarios)
    // ShmConf::init_roudi();
    // ShmConf::init_runtime("my_app", true);  // same_process_from_roudi = true
    VLOG_I("  Option A: ShmConf::init_roudi() + ShmConf::init_runtime(name, true)");

    // Option B: Connect to an external RouDi daemon
    // ShmConf::init_runtime("my_app");
    VLOG_I("  Option B: ShmConf::init_runtime(\"my_app\")");

    // Check initialization status (commented out - requires linking vlink::shm directly)
    // VLOG_I("  has_roudi_inited:", ShmConf::has_roudi_inited());
    // VLOG_I("  has_runtime_inited:", ShmConf::has_runtime_inited());
    VLOG_I("  Call ShmConf::has_roudi_inited() / has_runtime_inited() to check status");

    // Cleanup (call before process exit when using init_runtime)
    // ShmConf::deinit_runtime();
  }

  // ======== Example 9: Address length constraints ========
  // Iceoryx imposes a maximum of 80 characters for address and event strings.
  // Exceeding this limit will cause an error at node creation time.
  {
    VLOG_I("=== Example 9: Address length constraints ===");

    // Good: short address (well under 80 chars)
    std::string short_addr = "shm://sensor/lidar";
    VLOG_I("  Short address (OK):", short_addr, " (", short_addr.size() - 6, " chars)");

    // Bad: excessively long address (would exceed 80 chars)
    // std::string long_addr = "shm://very/long/deeply/nested/path/.../that/exceeds/eighty/characters";
    VLOG_I("  Address and event must each be <= 80 characters");
    VLOG_I("  If you need longer identifiers, consider using a shorter alias + url_remap");
  }

  // ======== Example 10: All six node types ========
  // shm:// supports all VLink communication primitives
  {
    VLOG_I("=== Example 10: All six node types ===");

    VLOG_I("  Publisher:  shm://topic/pub_sub");
    VLOG_I("  Subscriber: shm://topic/pub_sub");
    VLOG_I("  Server:     shm://topic/rpc");
    VLOG_I("  Client:     shm://topic/rpc");
    VLOG_I("  Setter:     shm://topic/field");
    VLOG_I("  Getter:     shm://topic/field");
  }

  return 0;
}
