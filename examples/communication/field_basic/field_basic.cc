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

/**
 * @file field_basic.cc
 * @brief Basic Setter/Getter example: set, get()->optional, listen, wait_for_value.
 *
 * Demonstrates the VLink Field Model fundamentals:
 *   - Setter<T>: writes a cached field value via set()
 *   - Getter<T>: reads the latest value via get(), wait_for_value(), listen()
 *   - Late-joiner support: Getter created after Setter receives the current value
 *   - Difference from Event Model: field retains the latest value
 */

#include <vlink/base/logger.h>
#include <vlink/vlink.h>

#include <string>
#include <thread>

using namespace std::chrono_literals;  // NOLINT(build/namespaces, google-build-using-namespace)

// POD type defined in vehicle_types.h -- see that file for field descriptions
#include "vehicle_types.h"

int main() {
  VLOG_I("=== VLink Field Basic Example ===");

  vlink::MessageLoop loop;
  loop.set_name("getter_loop");
  loop.async_run();

  // ---------------------------------------------------------------
  // Step 1: Create a Setter and set an initial value
  //
  // The Setter caches the value internally. When a Getter connects
  // later, it receives this cached value immediately.
  // ---------------------------------------------------------------
  vlink::Setter<GearState> setter("dds://vehicle/gear");
  VLOG_I("[Setter] Created on dds://vehicle/gear");

  GearState initial_gear{0, true};  // Park, engaged
  setter.set(initial_gear);
  VLOG_I("[Setter] Initial gear set to Park (0)");

  std::this_thread::sleep_for(50ms);

  // ---------------------------------------------------------------
  // Step 2: Create a Getter (late joiner)
  //
  // The Getter is created AFTER the Setter has already written.
  // Thanks to the field model's caching, it will receive the value.
  // ---------------------------------------------------------------
  vlink::Getter<GearState> getter("dds://vehicle/gear");
  getter.attach(&loop);
  VLOG_I("[Getter] Created on dds://vehicle/gear");

  // ---------------------------------------------------------------
  // Step 3: wait_for_value -- block until a value is available
  //
  // Since the Setter has already written, this returns almost
  // immediately. Timeout default is Timeout::kDefaultInterval (5s).
  // ---------------------------------------------------------------
  VLOG_I("[Getter] Waiting for initial value...");
  if (getter.wait_for_value(2000ms)) {
    VLOG_I("[Getter] wait_for_value() succeeded");
  } else {
    VLOG_W("[Getter] wait_for_value() timed out");
  }

  // ---------------------------------------------------------------
  // Step 4: get() -- non-blocking poll returning std::optional
  //
  // Returns std::optional<GearState>. Empty if no value received yet.
  // ---------------------------------------------------------------
  auto current = getter.get();
  if (current.has_value()) {
    VLOG_I("[Getter] Current gear: ", current->gear, " engaged: ", current->engaged);
  } else {
    VLOG_W("[Getter] No value available (unexpected)");
  }

  // ---------------------------------------------------------------
  // Step 5: listen -- register a callback for value changes
  //
  // The callback fires every time the Setter writes a new value.
  // It runs on the attached loop thread.
  // ---------------------------------------------------------------
  std::atomic<int> callback_count{0};
  getter.listen([&callback_count](const GearState& gear) {
    VLOG_I("[Getter] Value changed: gear=", gear.gear, " engaged=", gear.engaged);
    callback_count++;
  });

  // ---------------------------------------------------------------
  // Step 6: Update the field value several times
  //
  // Each set() call overwrites the cached value and triggers the
  // Getter's listen callback.
  // ---------------------------------------------------------------
  VLOG_I("[Setter] Shifting gears...");

  GearState gears[] = {
      {2, true},  // Neutral
      {3, true},  // Drive 1st
      {4, true},  // Drive 2nd
      {5, true},  // Drive 3rd
      {0, true},  // Park
  };

  for (const auto& gear : gears) {
    setter.set(gear);
    VLOG_I("[Setter] Set gear to: ", gear.gear);
    std::this_thread::sleep_for(100ms);
  }

  // ---------------------------------------------------------------
  // Step 7: Final read -- confirm the latest value
  // ---------------------------------------------------------------
  loop.wait_for_idle(1000);

  auto final_val = getter.get();
  if (final_val.has_value()) {
    VLOG_I("[Getter] Final gear: ", final_val->gear, " engaged: ", final_val->engaged);
  }

  VLOG_I("[Getter] Callback invocations: ", callback_count.load());
  VLOG_I("=== Example complete ===");

  loop.quit();
  loop.wait_for_quit();

  return 0;
}
