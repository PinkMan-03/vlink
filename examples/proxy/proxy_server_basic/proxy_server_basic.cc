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

// ProxyServer Basic Example
// Demonstrates ProxyServer creation, configuration, and lifecycle.

#include <vlink/base/logger.h>
#include <vlink/external/proxy_server.h>

#include <chrono>
#include <iostream>
#include <thread>

using namespace std::chrono_literals;  // NOLINT(build/namespaces, google-build-using-namespace)

int main() {
  // ======== Section 1: ProxyServer Configuration ========
  {
    std::cout << "\n[1] ProxyServer Configuration" << std::endl;

    vlink::ProxyServer::Config cfg;
    cfg.dds_impl = "dds";  // DDS implementation
    cfg.domain_id = 0;     // DDS domain
    cfg.reliable = false;  // BestEffort for data channel
    cfg.async = true;      // Async event processing
    cfg.use_iox = false;   // Do not start embedded RouDi

    std::cout << "  dds_impl:  " << cfg.dds_impl << std::endl;
    std::cout << "  domain_id: " << cfg.domain_id << std::endl;
    std::cout << "  reliable:  " << std::boolalpha << cfg.reliable << std::endl;
    std::cout << "  async:     " << std::boolalpha << cfg.async << std::endl;
    std::cout << "  use_iox:   " << std::boolalpha << cfg.use_iox << std::endl;
  }

  // ======== Section 2: Usage Pattern ========
  // Creates a real ProxyServer, starts its event loop briefly, then shuts down.
  {
    std::cout << "\n[2] Usage Pattern" << std::endl;

    vlink::ProxyServer::Config cfg;
    cfg.dds_impl = "dds";
    cfg.domain_id = 0;
    cfg.reliable = false;
    cfg.async = true;

    // ProxyServer is a singleton per process
    vlink::ProxyServer server(cfg);
    VLOG_I("[ProxyServer] Constructed with dds_impl=", cfg.dds_impl, " domain_id=", cfg.domain_id);

    // Start the event loop (runs DiscoveryViewer, heartbeat timer, etc.)
    server.async_run();
    VLOG_I("[ProxyServer] Event loop started -- heartbeat and discovery are active");

    // Allow the server to run briefly so heartbeat and discovery can tick
    std::this_thread::sleep_for(500ms);
    VLOG_I("[ProxyServer] Server ran for 500ms");

    // Shutdown
    server.quit(true);  // true = force immediate quit
    server.wait_for_quit();
    VLOG_I("[ProxyServer] Event loop stopped");
  }

  // ======== Section 3: Communication Architecture ========
  {
    std::cout << "[3] Communication Architecture" << std::endl;
    VLOG_I("[Architecture] ProxyAPI (kController) communicates with ProxyServer via secure DDS channels:");
    VLOG_I("  HandshakeCli <--> [DDS secure] <--> HandshakeSrv (server issues token)");
    VLOG_I("  ControlPub   --> [DDS secure] --> ControlSub    (API sends token-stamped controls)");
    VLOG_I("  TimeSub      <-- [DDS secure] <-- TimePub        (server sends 1-second heartbeat + identity/token)");
    VLOG_I("  InfoSub      <-- [DDS secure] <-- InfoPub        (server sends per-topic statistics to API)");
    VLOG_I("  DataSub   <-- [DDS/SHM] <-- DataPub   (server relays raw message data to API)");
  }

  // ======== Section 4: Singleton Constraint ========
  {
    std::cout << "[4] Singleton Constraint" << std::endl;
    VLOG_I("[Singleton] Only ONE ProxyServer may exist per process.");
    VLOG_I("[Singleton] A second construction attempt logs a fatal message and returns without initialising.");
  }

  // ======== Section 5: Runnable Plugins ========
  {
    std::cout << "\n[5] Runnable Plugins" << std::endl;
    VLOG_I("[Plugins] ProxyServer can load RunablePluginInterface plugins via Config::runnable_list.");
    VLOG_I("[Plugins] Each plugin gets its own MessageLoop thread managed by the server.");
    VLOG_I("[Plugins] Lifecycle: constructor -> async_run() -> on_init() -> ... -> on_deinit() -> quit()");

    // Demonstrate building a Config with runnable_list entries
    vlink::ProxyServer::Config plugin_cfg;
    plugin_cfg.dds_impl = "dds";
    plugin_cfg.runnable_list = {"my_analysis_plugin", "my_recorder_plugin"};
    plugin_cfg.runnable_version_major = 1;
    plugin_cfg.runnable_version_minor = 0;

    VLOG_I("[Plugins] Example config has", plugin_cfg.runnable_list.size(), "plugins:");
    for (const auto& name : plugin_cfg.runnable_list) {
      VLOG_I("  plugin:", name, " version:", plugin_cfg.runnable_version_major, ".", plugin_cfg.runnable_version_minor);
    }
  }

  // ======== Section 6: Environment Variables ========
  {
    std::cout << "[6] Environment Variables" << std::endl;
    VLOG_I("[Env] VLINK_INTRA_BIND -- when set, the server subscribes to intra:// topics");
    VLOG_I("[Env] in addition to DDS/SHM topics, enabling in-process observation.");
  }

  std::cout << "\n[Note] The ProxyServer in Section 2 was started and stopped successfully." << std::endl;
  std::cout << "Run with a DDS backend: ./example_proxy_server_basic" << std::endl;

  VLOG_I("ProxyServer basic example complete.");
  return 0;
}
