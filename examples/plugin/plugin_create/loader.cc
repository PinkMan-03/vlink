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

/// @file loader.cc
/// @brief Step 3 -- host application that loads and uses the calculator plugin.
///
/// Demonstrates the full three-step plugin workflow:
///   1. Include the shared interface header (calculator_interface.h).
///   2. Use Plugin::load<CalculatorInterface>() to open the .so at runtime.
///   3. Call methods through the returned shared_ptr<CalculatorInterface>.

#include <vlink/base/logger.h>
#include <vlink/base/plugin.h>

#include <string>

#include "calculator_interface.h"

int main() {
  // ======== Compile-time checks (useful when developing a plugin) ========
  VLOG_I("=== Compile-time checks ===");
  VLOG_I("  is_abstract<CalculatorInterface>:      ", std::is_abstract_v<CalculatorInterface> ? "true" : "false");
  VLOG_I("  has_virtual_destructor<CalculatorInterface>: ",
         std::has_virtual_destructor_v<CalculatorInterface> ? "true" : "false");
  VLOG_I("  plugin_id: ", std::string(CalculatorInterface::get_plugin_id()));

  // ======== Load the plugin ========
  VLOG_I("=== Loading calculator_plugin (version 1.0) ===");

  vlink::Plugin plugin;
  plugin.set_log_level(vlink::Logger::kInfo);

  auto calc = plugin.load<CalculatorInterface>("calculator_plugin", 1, 0);

  if (!calc) {
    VLOG_E("Failed to load calculator_plugin.");
    VLOG_I("Make sure libcalculator_plugin.so is in a search path.");
    return 1;
  }

  VLOG_I("Loaded: ", calc->name());

  // ======== Exercise every interface method ========
  VLOG_I("=== Testing all methods ===");

  int sum = calc->add(17, 25);
  VLOG_I("  add(17, 25)      = ", sum);

  int product = calc->multiply(6, 7);
  VLOG_I("  multiply(6, 7)   = ", product);

  VLOG_I("  name()           = ", calc->name());

  // Edge cases
  VLOG_I("  add(0, 0)        = ", calc->add(0, 0));
  VLOG_I("  add(-5, 5)       = ", calc->add(-5, 5));
  VLOG_I("  multiply(-3, 4)  = ", calc->multiply(-3, 4));

  // ======== Introspection ========
  VLOG_I("=== Introspection ===");
  VLOG_I("  has_loaded:  ", plugin.has_loaded<CalculatorInterface>("calculator_plugin") ? "true" : "false");
  VLOG_I("  complex_id:  ", plugin.get_plugin_complex_id<CalculatorInterface>("calculator_plugin"));

  // ======== Cleanup ========
  calc.reset();
  plugin.clear();
  VLOG_I("Plugin create example complete.");
  return 0;
}
