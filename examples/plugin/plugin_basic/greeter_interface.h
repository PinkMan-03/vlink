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

/// @file greeter_interface.h
/// @brief Abstract plugin interface for a greeting service.
///
/// This header is shared between the host application (plugin_basic.cc) and the
/// plugin shared library (greeter_plugin.cc).  Both sides include it so that
/// the plugin ID derived by VLINK_PLUGIN_REGISTER matches at load time.

#ifndef EXAMPLES_PLUGIN_PLUGIN_BASIC_GREETER_INTERFACE_H_
#define EXAMPLES_PLUGIN_PLUGIN_BASIC_GREETER_INTERFACE_H_

#include <vlink/base/plugin.h>

#include <string>

/// Abstract greeting interface.
///
/// Requirements for a VLink plugin interface:
///   1. At least one pure virtual method (makes the class abstract).
///   2. A virtual destructor.
///   3. VLINK_PLUGIN_REGISTER(InterfaceType) to generate get_plugin_id().
class GreeterInterface {
  VLINK_PLUGIN_REGISTER(GreeterInterface)

 public:
  virtual ~GreeterInterface() = default;

  /// Return a personalised greeting for the given name.
  virtual std::string greet(const std::string& name) = 0;

  /// Return the display name of this plugin implementation.
  virtual std::string plugin_name() const = 0;
};

#endif  // EXAMPLES_PLUGIN_PLUGIN_BASIC_GREETER_INTERFACE_H_
