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

/// @file plugin_basic.cc
/// @brief Host application that loads and exercises the greeter plugin at runtime.
///
/// Demonstrates:
///   - Plugin::load<T>() with default and custom search paths
///   - Calling virtual methods through the shared_ptr<T> handle
///   - has_loaded<T>(), get_plugin_complex_id<T>()
///   - Plugin::unload<T>() and reloading with a mismatched version
///   - Error handling when the plugin is not found or version-incompatible

#include <vlink/base/logger.h>
#include <vlink/base/plugin.h>

#include <iostream>
#include <string>

#include "greeter_interface.h"

int main() {
  // ======== Section 1: Default search paths ========
  VLOG_I("=== [1] Default search paths ===");
  {
    auto paths = vlink::Plugin::default_search_path();
    for (const auto& p : paths) {
      VLOG_I("  search path: ", p);
    }
  }

  // ======== Section 2: Load the greeter plugin ========
  VLOG_I("=== [2] Loading greeter_plugin (version 1.0) ===");

  vlink::Plugin plugin;
  plugin.set_log_level(vlink::Logger::kInfo);

  auto greeter = plugin.load<GreeterInterface>("greeter_plugin", 1, 0);

  if (!greeter) {
    VLOG_E("Failed to load greeter_plugin. Make sure libgreeter_plugin.so is in a search path.");
    VLOG_I("Tip: run from the build/output/bin directory or set LD_LIBRARY_PATH.");
    return 1;
  }

  VLOG_I("Plugin loaded successfully.");

  // ======== Section 3: Call plugin methods ========
  VLOG_I("=== [3] Calling plugin methods ===");
  VLOG_I("  plugin_name(): ", greeter->plugin_name());
  VLOG_I("  greet(\"VLink\"): ", greeter->greet("VLink"));
  VLOG_I("  greet(\"World\"): ", greeter->greet("World"));
  VLOG_I("  greet(\"Alice\"): ", greeter->greet("Alice"));

  // ======== Section 4: Introspection ========
  VLOG_I("=== [4] Plugin introspection ===");

  bool loaded = plugin.has_loaded<GreeterInterface>("greeter_plugin");
  VLOG_I("  has_loaded: ", loaded ? "true" : "false");

  std::string complex_id = plugin.get_plugin_complex_id<GreeterInterface>("greeter_plugin");
  VLOG_I("  complex_id: ", complex_id);

  VLOG_I("  plugin_id:  ", std::string(GreeterInterface::get_plugin_id()));
  VLOG_I("  log_level:  ", static_cast<int>(plugin.get_log_level()));

  // ======== Section 5: Unload ========
  VLOG_I("=== [5] Unloading plugin ===");

  // Release the shared_ptr first so the destroy callback can run.
  greeter.reset();

  bool unloaded = plugin.unload<GreeterInterface>("greeter_plugin");
  VLOG_I("  unload result: ", unloaded ? "true" : "false");

  loaded = plugin.has_loaded<GreeterInterface>("greeter_plugin");
  VLOG_I("  has_loaded after unload: ", loaded ? "true" : "false");

  // ======== Section 6: Load with wrong version (should fail) ========
  VLOG_I("=== [6] Attempting load with wrong version (2.0) ===");

  auto bad = plugin.load<GreeterInterface>("greeter_plugin", 2, 0);

  if (!bad) {
    VLOG_I("  Version mismatch -- load returned nullptr (expected).");
  } else {
    VLOG_W("  Unexpected success -- version check may not be working.");
    bad.reset();
  }

  // ======== Section 7: Reload correct version ========
  VLOG_I("=== [7] Reloading with correct version (1.0) ===");

  auto reloaded = plugin.load<GreeterInterface>("greeter_plugin", 1, 0);

  if (reloaded) {
    VLOG_I("  Reload OK: ", reloaded->greet("Reload"));
    reloaded.reset();
  } else {
    VLOG_E("  Reload failed unexpectedly.");
  }

  // ======== Cleanup ========
  plugin.clear();
  VLOG_I("Plugin basic example complete.");
  return 0;
}
