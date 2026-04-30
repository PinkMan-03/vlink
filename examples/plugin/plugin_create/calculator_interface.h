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

/// @file calculator_interface.h
/// @brief Abstract calculator plugin interface -- shared between host and plugin .so.
///
/// Step 1 of creating a VLink plugin: define the interface.
///
/// Rules:
///   - The class MUST be abstract (at least one pure virtual method).
///   - The class MUST have a virtual destructor.
///   - Use VLINK_PLUGIN_REGISTER(InterfaceType) to derive the plugin ID.

#ifndef EXAMPLES_PLUGIN_PLUGIN_CREATE_CALCULATOR_INTERFACE_H_
#define EXAMPLES_PLUGIN_PLUGIN_CREATE_CALCULATOR_INTERFACE_H_

#include <vlink/base/plugin.h>

#include <string>

/// Abstract calculator interface.
///
/// The host loads a shared library that implements this interface and calls
/// the pure virtual methods through a shared_ptr<CalculatorInterface>.
class CalculatorInterface {
  VLINK_PLUGIN_REGISTER(CalculatorInterface)

 public:
  virtual ~CalculatorInterface() = default;

  /// Add two integers and return the sum.
  virtual int add(int a, int b) = 0;

  /// Multiply two integers and return the product.
  virtual int multiply(int a, int b) = 0;

  /// Return the display name of this plugin implementation.
  virtual std::string name() const = 0;
};

#endif  // EXAMPLES_PLUGIN_PLUGIN_CREATE_CALCULATOR_INTERFACE_H_
