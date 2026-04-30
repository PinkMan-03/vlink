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

/// @file greeter_plugin.cc
/// @brief Concrete GreeterInterface implementation compiled into a shared library.
///
/// Build output: libgreeter_plugin.so
/// The host application loads this .so at runtime via Plugin::load<GreeterInterface>().

#include "greeter_interface.h"

/// Concrete implementation of the GreeterInterface.
///
/// Note: VLINK_PLUGIN_REGISTER uses the *interface* type (GreeterInterface), not the
/// implementation type.  This ensures the plugin ID matches between host and plugin.
class GreeterImpl : public GreeterInterface {
  VLINK_PLUGIN_REGISTER(GreeterInterface)

 public:
  std::string greet(const std::string& name) override { return "Hello, " + name + "!"; }

  std::string plugin_name() const override { return "GreeterImpl"; }
};

/// Export C entry points: vlink_plugin_create / vlink_plugin_destroy.
/// The first argument is the concrete class; the next two are major.minor version.
VLINK_PLUGIN_DECLARE(GreeterImpl, 1, 0)
