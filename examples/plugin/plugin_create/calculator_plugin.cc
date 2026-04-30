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

/// @file calculator_plugin.cc
/// @brief Step 2 -- concrete CalculatorInterface implementation.
///
/// This file is compiled into libcalculator_plugin.so.  It provides:
///   - A concrete class (CalculatorImpl) implementing every pure virtual.
///   - VLINK_PLUGIN_DECLARE to export the C entry points required by the loader.

#include "calculator_interface.h"

/// Concrete calculator implementation.
///
/// Key points:
///   - VLINK_PLUGIN_REGISTER uses the *interface* type so that plugin IDs match.
///   - The class must be default-constructible (no constructor arguments).
///   - The class must not be abstract (all pure virtuals must be overridden).
class CalculatorImpl : public CalculatorInterface {
  VLINK_PLUGIN_REGISTER(CalculatorInterface)

 public:
  int add(int a, int b) override { return a + b; }

  int multiply(int a, int b) override { return a * b; }

  std::string name() const override { return "BasicCalculator"; }
};

/// Export vlink_plugin_create / vlink_plugin_destroy entry points.
/// Arguments: ConcreteClass, VersionMajor, VersionMinor
VLINK_PLUGIN_DECLARE(CalculatorImpl, 1, 0)
