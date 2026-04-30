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

// Proxy Runnable Plugin Development Example
// Shows how to create a RunablePluginInterface plugin for ProxyServer.

#include <vlink/base/logger.h>
#include <vlink/base/plugin.h>
#include <vlink/extension/runnable_plugin_interface.h>
#include <vlink/external/proxy_server.h>
#include <vlink/vlink.h>

#include <chrono>
#include <iostream>
#include <thread>

int main() {
  // ======== Section 1: Plugin for ProxyServer ========
  {
    std::cout << "\n[1] RunablePluginInterface for ProxyServer" << std::endl;
    VLOG_I("[Plugin] ProxyServer loads RunablePluginInterface plugins to extend");
    VLOG_I("[Plugin] its capabilities (analysis, recording, custom processing).");
    VLOG_I("[Plugin] Each plugin inherits from vlink::RunablePluginInterface,");
    VLOG_I("[Plugin] which provides its own MessageLoop event thread.");
  }

  // ======== Section 2: Plugin Implementation ========
  // Explains what a plugin .so looks like. The actual plugin is built separately
  // as a shared library; see examples/plugin/plugin_runnable/monitor_plugin.cc
  // for a complete working example.
  {
    std::cout << "\n[2] Plugin Implementation" << std::endl;
    VLOG_I("[Plugin] A plugin .so must:");
    VLOG_I("  1. Inherit from vlink::RunablePluginInterface");
    VLOG_I("  2. Use VLINK_PLUGIN_REGISTER(vlink::RunablePluginInterface) in the class body");
    VLOG_I("  3. Override on_init() to create subscribers, timers, etc.");
    VLOG_I("  4. Override on_deinit() to release all resources");
    VLOG_I("  5. Use VLINK_PLUGIN_DECLARE(ClassName, MajorVer, MinorVer) at file scope");
    VLOG_I("[Plugin] See examples/plugin/plugin_runnable/monitor_plugin.cc for a working example.");
  }

  // ======== Section 3: ProxyServer Integration ========
  // Demonstrate building a ProxyServer::Config with runnable_list and
  // loading the plugin via the server.
  {
    std::cout << "\n[3] ProxyServer Integration" << std::endl;

    // Build a config that references runnable plugins
    vlink::ProxyServer::Config cfg;
    cfg.dds_impl = "dds";
    cfg.domain_id = 0;
    cfg.async = true;

    // Plugin names correspond to shared library filenames without the lib prefix and .so suffix.
    // e.g., "monitor_plugin" loads libmonitor_plugin.so
    cfg.runnable_list = {"monitor_plugin"};
    cfg.runnable_version_major = 1;
    cfg.runnable_version_minor = 0;

    VLOG_I("[Integration] Config runnable_list has", cfg.runnable_list.size(), "plugin(s):");
    for (const auto& name : cfg.runnable_list) {
      VLOG_I("  plugin:", name, " required version:", cfg.runnable_version_major, ".", cfg.runnable_version_minor);
    }

    VLOG_I("[Integration] ProxyServer automatically manages plugin lifecycle:");
    VLOG_I("  1. Loads the plugin via vlink::Plugin::load<vlink::RunablePluginInterface>()");
    VLOG_I("  2. Calls async_run() on the plugin's MessageLoop");
    VLOG_I("  3. Calls on_init() when the server's loop starts");
    VLOG_I("  4. Calls on_deinit() + quit() + wait_for_quit() on shutdown");
  }

  // ======== Section 4: Direct Plugin Loading ========
  // Demonstrate loading a RunablePluginInterface plugin directly via vlink::Plugin
  // (without a ProxyServer). This is useful for standalone testing.
  {
    std::cout << "\n[4] Direct Plugin Loading (standalone)" << std::endl;

    vlink::Plugin plugin;
    auto instance = plugin.load<vlink::RunablePluginInterface>("monitor_plugin", 1, 0);

    if (instance) {
      VLOG_I("[Direct] Plugin loaded successfully");
      instance->async_run();
      instance->on_init();
      VLOG_I("[Direct] Plugin on_init() called, event loop running");

      // Let the plugin run briefly
      std::this_thread::sleep_for(std::chrono::milliseconds(200));

      // Shutdown the plugin
      instance->on_deinit();
      instance->quit();
      instance->wait_for_quit();
      VLOG_I("[Direct] Plugin shut down cleanly");
    } else {
      VLOG_I("[Direct] Plugin 'monitor_plugin' not found on this system (expected in CI).");
      VLOG_I("[Direct] Build the plugin with: add_library(monitor_plugin SHARED monitor_plugin.cc)");
    }
  }

  // ======== Section 5: Best Practices ========
  {
    std::cout << "\n[5] Best Practices" << std::endl;
    VLOG_I("[BestPractices] 1. Use the plugin's MessageLoop (this) for all callbacks");
    VLOG_I("[BestPractices] 2. Create resources in on_init(), release in on_deinit()");
    VLOG_I("[BestPractices] 3. Never block in on_init() or on_deinit()");
    VLOG_I("[BestPractices] 4. Use VLINK_PLUGIN_DIR env var to set the plugin search path");
    VLOG_I("[BestPractices] 5. Match version numbers between loader and plugin");
  }

  VLOG_I("Proxy runnable plugin example complete.");
  return 0;
}
