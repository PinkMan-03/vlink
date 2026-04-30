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

/// @file plugin_runnable.cc
/// @brief Host application that loads, runs, and shuts down a RunablePluginInterface plugin.
///
/// Lifecycle:
///   1. Plugin::load<RunablePluginInterface>("monitor_plugin", 1, 0)
///   2. async_run()      -- start the plugin's MessageLoop on a background thread
///   3. on_init()        -- plugin creates timers, subscribers, etc.
///   4. (application runs for ~3 seconds while the plugin processes events)
///   5. on_deinit()      -- plugin stops timers and releases resources
///   6. quit()           -- request the event loop to stop
///   7. wait_for_quit()  -- block until the loop thread has exited

#include <vlink/base/logger.h>
#include <vlink/base/plugin.h>
#include <vlink/extension/runnable_plugin_interface.h>

#include <chrono>
#include <string>
#include <thread>

int main() {
  VLOG_I("=== RunablePluginInterface example ===");
  VLOG_I("RunablePluginInterface plugin_id: ", std::string(vlink::RunablePluginInterface::get_plugin_id()));

  // ======== Load the plugin ========
  vlink::Plugin plugin;
  plugin.set_log_level(vlink::Logger::kInfo);

  auto monitor = plugin.load<vlink::RunablePluginInterface>("monitor_plugin", 1, 0);
  if (!monitor) {
    VLOG_E("Failed to load monitor_plugin. Ensure libmonitor_plugin.so is accessible.");
    return 1;
  }

  VLOG_I("Plugin loaded.");

  // ======== Start the plugin's event loop ========
  VLOG_I("Calling async_run() -- starting plugin event loop thread.");
  monitor->async_run();

  // ======== Initialise the plugin (creates timers, etc.) ========
  VLOG_I("Calling on_init().");
  monitor->on_init();

  // ======== Let the plugin run for 3 seconds ========
  VLOG_I("Sleeping 3 seconds while plugin runs...");
  std::this_thread::sleep_for(std::chrono::seconds(3));

  // ======== Shutdown sequence ========
  VLOG_I("Calling on_deinit() -- plugin releases resources.");
  monitor->on_deinit();

  VLOG_I("Calling quit() -- requesting event loop stop.");
  monitor->quit();

  VLOG_I("Calling wait_for_quit() -- waiting for loop thread exit.");
  monitor->wait_for_quit();

  // ======== Cleanup ========
  VLOG_I("Releasing shared_ptr and clearing plugin loader.");
  monitor.reset();
  plugin.clear();

  VLOG_I("Plugin runnable example complete.");
  return 0;
}
