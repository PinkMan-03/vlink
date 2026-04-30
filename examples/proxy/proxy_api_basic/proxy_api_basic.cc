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

// ProxyAPI Basic Example
// Demonstrates ProxyAPI Config, roles, callbacks, and send_control.

#include <vlink/base/logger.h>
#include <vlink/external/proxy_api.h>

#include <iostream>
#include <thread>

using namespace std::chrono_literals;  // NOLINT(build/namespaces, google-build-using-namespace)

int main() {
  // ======== Section 1: ProxyAPI Configuration ========
  {
    std::cout << "\n[1] ProxyAPI Configuration" << std::endl;

    vlink::ProxyAPI::Config cfg;
    cfg.role = vlink::ProxyAPI::kController;  // kController or kListener
    cfg.dds_impl = "dds";                     // DDS implementation to use
    cfg.domain_id = 0;                        // DDS domain ID
    cfg.reliable = false;                     // BestEffort for data channel
    cfg.match_version = true;                 // Require matching VLink version

    std::cout << "  role:          " << (cfg.role == vlink::ProxyAPI::kController ? "Controller" : "Listener")
              << std::endl;
    std::cout << "  dds_impl:      " << cfg.dds_impl << std::endl;
    std::cout << "  domain_id:     " << cfg.domain_id << std::endl;
    std::cout << "  reliable:      " << std::boolalpha << cfg.reliable << std::endl;
    std::cout << "  match_version: " << std::boolalpha << cfg.match_version << std::endl;
  }

  // ======== Section 2: Roles ========
  {
    std::cout << "\n[2] Roles" << std::endl;
    std::cout << "  kController: Can send Control messages (observe, record, play)" << std::endl;
    std::cout << "  kListener:   Passive observer only, send_control is rejected" << std::endl;
  }

  // ======== Section 3: Operation Modes ========
  {
    std::cout << "\n[3] Operation Modes" << std::endl;
    std::cout << "  +---------------------+-------+-------------------------------------------+" << std::endl;
    std::cout << "  | Mode                | Value | Description                               |" << std::endl;
    std::cout << "  +---------------------+-------+-------------------------------------------+" << std::endl;
    std::cout << "  | kOffline            |   0   | Disconnected                              |" << std::endl;
    std::cout << "  | kObserveOne         |   1   | Observe a single topic                    |" << std::endl;
    std::cout << "  | kObserveAll         |   2   | Observe all discovered topics              |" << std::endl;
    std::cout << "  | kRecord             |   3   | Record matching topics                    |" << std::endl;
    std::cout << "  | kPlay               |   4   | Replay recorded data                      |" << std::endl;
    std::cout << "  | kEdit               |   5   | Inject data through server                |" << std::endl;
    std::cout << "  | kAuto               |   6   | Auto-observe specified topics              |" << std::endl;
    std::cout << "  | kAutoAndObserveAll  |   7   | Auto + observe all                        |" << std::endl;
    std::cout << "  +---------------------+-------+-------------------------------------------+" << std::endl;
  }

  // ======== Section 4: Usage Pattern ========
  // Demonstrates the real ProxyAPI creation and callback registration.
  // Note: Without a running ProxyServer, connect_callback will report disconnected.
  {
    std::cout << "\n[4] Usage Pattern (requires running ProxyServer)" << std::endl;

    vlink::ProxyAPI::Config cfg;
    cfg.role = vlink::ProxyAPI::kController;
    cfg.dds_impl = "dds";
    cfg.domain_id = 0;
    cfg.reliable = false;
    cfg.match_version = true;

    vlink::ProxyAPI api(cfg);

    // Register callbacks
    api.register_connect_callback(
        [](bool connected) { VLOG_I("[ProxyAPI] Connection state:", connected ? "connected" : "disconnected"); });

    api.register_error_callback(
        [](vlink::ProxyAPI::Error error) { VLOG_I("[ProxyAPI] Error code:", static_cast<int>(error)); });

    api.register_info_callback([](const std::vector<vlink::ProxyAPI::Info>& info_list) {
      VLOG_I("[ProxyAPI] Received info for", info_list.size(), "topics");
      for (const auto& info : info_list) {
        VLOG_I("  Topic:", info.url, " ser:", info.ser, " freq:", info.freq, " Hz");
      }
    });

    api.register_data_callback([](const vlink::ProxyAPI::Data& data) {
      VLOG_I("[ProxyAPI] Data from:", data.url, " size:", data.raw.size(), " bytes");
    });

    // Start the API event loop
    api.async_run();

    // Build and send a control message to observe all topics
    vlink::ProxyAPI::Control ctrl;
    ctrl.mode = vlink::ProxyAPI::kObserveAll;
    bool sent = api.send_control(ctrl);
    VLOG_I("[ProxyAPI] send_control(kObserveAll) returned:", sent);

    // Query current state
    VLOG_I("[ProxyAPI] Current mode:", static_cast<int>(api.get_current_mode()));
    VLOG_I("[ProxyAPI] is_connected:", api.is_connected());
    VLOG_I("[ProxyAPI] SHM support:", api.is_support_shm());
    VLOG_I("[ProxyAPI] Filter support:", api.is_enable_filter());

    // Wait briefly to allow any server response
    std::this_thread::sleep_for(500ms);

    // Shutdown
    api.quit();
    api.wait_for_quit();
    VLOG_I("[ProxyAPI] Event loop stopped");
  }

  // ======== Section 5: Error Codes ========
  {
    std::cout << "[5] Error Codes" << std::endl;
    std::cout << "  kNoError(0)            No error" << std::endl;
    std::cout << "  kModeError(1)          Unsupported mode" << std::endl;
    std::cout << "  kControlError(2)       Control ID mismatch" << std::endl;
    std::cout << "  kReliableCompError(3)  Reliable setting mismatch" << std::endl;
    std::cout << "  kTcpCompError(4)       TCP setting mismatch" << std::endl;
    std::cout << "  kDirectCompError(5)    Direct setting mismatch" << std::endl;
    std::cout << "  kMultiProxyError(7)    Multiple proxy servers" << std::endl;
    std::cout << "  kVersionCompError(8)   Version mismatch" << std::endl;
    std::cout << "  kUnknownError(9)       Unknown error" << std::endl;
  }

  // ======== Section 6: Note ========
  {
    std::cout << "\n[6] Note" << std::endl;
    std::cout << "  ProxyAPI requires a running ProxyServer daemon." << std::endl;
    std::cout << "  Without a server, connect_callback reports disconnected." << std::endl;
    std::cout << "  See proxy_server_basic for how to create the server." << std::endl;
  }

  VLOG_I("ProxyAPI basic example complete.");
  return 0;
}
