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
 * @file config_setter.cc
 * @brief VLink Field Model -- Setter program that writes sensor configuration.
 *
 * This program creates a Setter<SensorConfig> and periodically updates the
 * field value.  A separate program (config_getter) can read the latest
 * configuration at any time -- including late-joining after the value has
 * already been set.
 *
 * When using intra:// transport, both setter and getter must live in the
 * same process.  For cross-process usage, switch to shm:// or dds://.
 */

#include <vlink/base/logger.h>
#include <vlink/vlink.h>

#include <chrono>
#include <cstdint>
#include <thread>

#include "./config_types.h"

/// Helper: return current steady_clock time in milliseconds.
static int64_t now_ms() {
  return static_cast<int64_t>(
      std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch())
          .count());
}

int main() {
  // ---------------------------------------------------------------
  // Step 1: Initialise logger
  // ---------------------------------------------------------------
  VLOG_I("=== VLink Config Setter ===");

  // ---------------------------------------------------------------
  // Step 2: Create the Setter
  //
  // Setter<SensorConfig> is the "writer" side of a field.  Each
  // call to set() overwrites the cached value and sends it to all
  // connected Getters.  Late-joining Getters automatically receive
  // the most recent value through the internal sync() mechanism.
  // ---------------------------------------------------------------
  vlink::Setter<example::SensorConfig> setter(example::kConfigUrl);
  VLOG_I("[Setter] Created on ", example::kConfigUrl);

  // ---------------------------------------------------------------
  // Step 3: Set an initial configuration
  //
  // This value is cached by the Setter.  Even if a Getter connects
  // later, it will receive this value immediately (late join).
  // ---------------------------------------------------------------
  example::SensorConfig initial_cfg{};
  initial_cfg.sample_rate_hz = 100;
  initial_cfg.filter_window_size = 5;
  initial_cfg.threshold_low = 15.0F;
  initial_cfg.threshold_high = 35.0F;
  initial_cfg.updated_at_ms = now_ms();

  setter.set(initial_cfg);
  VLOG_I("[Setter] Initial config: rate=", initial_cfg.sample_rate_hz,
         "Hz"
         " filter=",
         initial_cfg.filter_window_size, " low=", initial_cfg.threshold_low, " high=", initial_cfg.threshold_high);

  // ---------------------------------------------------------------
  // Step 4: Simulate configuration updates
  //
  // In a real application, these updates would come from a user
  // interface, a remote management system, or a calibration routine.
  // ---------------------------------------------------------------
  struct ConfigUpdate {
    int sample_rate_hz;
    int filter_window_size;
    float threshold_low;
    float threshold_high;
  };

  ConfigUpdate updates[] = {
      {200, 10, 10.0F, 40.0F},  // Increase rate, widen thresholds
      {100, 3, 18.0F, 30.0F},   // Tighten thresholds
      {500, 8, 12.0F, 38.0F},   // High-speed sampling
      {50, 15, 20.0F, 28.0F},   // Low-speed, tight thresholds
  };

  for (const auto& u : updates) {
    std::this_thread::sleep_for(std::chrono::milliseconds(500));

    example::SensorConfig cfg{};
    cfg.sample_rate_hz = u.sample_rate_hz;
    cfg.filter_window_size = u.filter_window_size;
    cfg.threshold_low = u.threshold_low;
    cfg.threshold_high = u.threshold_high;
    cfg.updated_at_ms = now_ms();

    setter.set(cfg);
    VLOG_I("[Setter] Updated config: rate=", cfg.sample_rate_hz,
           "Hz"
           " filter=",
           cfg.filter_window_size, " low=", cfg.threshold_low, " high=", cfg.threshold_high);
  }

  // ---------------------------------------------------------------
  // Step 5: Keep alive briefly so the Getter can read the final value
  // ---------------------------------------------------------------
  std::this_thread::sleep_for(std::chrono::milliseconds(500));

  VLOG_I("=== Config Setter complete ===");
  return 0;
}
