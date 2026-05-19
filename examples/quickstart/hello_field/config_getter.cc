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
 * @file config_getter.cc
 * @brief VLink Field Model -- Getter program that reads sensor configuration.
 *
 * This program creates a Getter<SensorConfig> and demonstrates three
 * ways to read a field value:
 *   1. wait_for_value()  -- block until a value is available (late join)
 *   2. get()             -- non-blocking poll returning std::optional
 *   3. listen()          -- callback notification on every value change
 *
 * For intra:// transport this must run in the same process as the Setter.
 * For cross-process usage, switch to shm:// or dds://.
 */

#include <vlink/base/logger.h>
#include <vlink/vlink.h>

#include <atomic>
#include <chrono>
#include <string>
#include <thread>

#include "./config_types.h"

using namespace std::chrono_literals;  // NOLINT(build/namespaces, google-build-using-namespace)

/// Helper: print a SensorConfig.
static void print_config(const char* prefix, const example::SensorConfig& cfg) {
  VLOG_I(prefix, " rate=", cfg.sample_rate_hz,
         "Hz"
         " filter=",
         cfg.filter_window_size, " low=", cfg.threshold_low, " high=", cfg.threshold_high, " ts=", cfg.updated_at_ms);
}

int main() {
  // ---------------------------------------------------------------
  // Step 1: Initialise logger
  // ---------------------------------------------------------------
  VLOG_I("=== VLink Config Getter ===");
  const std::string config_url = vlink::Utils::get_env("VLINK_CONFIG_URL", example::kConfigUrl);

  // ---------------------------------------------------------------
  // Step 2: Create MessageLoop for callback dispatch
  // ---------------------------------------------------------------
  vlink::MessageLoop loop;
  loop.set_name("getter_loop");
  loop.async_run();

  // ---------------------------------------------------------------
  // Step 3: Create the Getter
  //
  // Getter<SensorConfig> is the "reader" side of a field.
  // It receives the latest value from the Setter, either through
  // polling (get()), blocking (wait_for_value()), or callback
  // notification (listen()).
  // ---------------------------------------------------------------
  vlink::Getter<example::SensorConfig> getter(config_url);
  getter.attach(&loop);
  VLOG_I("[Getter] Created on ", config_url);

  // ---------------------------------------------------------------
  // Step 4: Register value-change callback (Pattern: listen)
  //
  // This fires every time the Setter calls set() with a new value.
  // The callback runs on the loop thread (because we called attach).
  // ---------------------------------------------------------------
  std::atomic<int> change_count{0};
  getter.listen([&change_count](const example::SensorConfig& cfg) {
    int n = change_count.fetch_add(1) + 1;
    VLOG_I("[Getter] Change #", n, ":");
    print_config("  ", cfg);
  });

  // ---------------------------------------------------------------
  // Step 5: Wait for the initial value (Pattern: wait_for_value)
  //
  // This demonstrates late-join: even if the Setter has already
  // written a value before we connected, we will receive it.
  // ---------------------------------------------------------------
  VLOG_I("[Getter] Waiting for initial value...");
  if (getter.wait_for_value(std::chrono::milliseconds(3000))) {
    VLOG_I("[Getter] wait_for_value() succeeded");
  } else {
    VLOG_W("[Getter] wait_for_value() timed out -- is the setter running?");
  }

  // ---------------------------------------------------------------
  // Step 6: Read the current value via polling (Pattern: get)
  //
  // get() returns std::optional<SensorConfig>.  It is non-blocking
  // and returns the most recent cached value, or std::nullopt if
  // no value has been received yet.
  // ---------------------------------------------------------------
  auto current = getter.get();
  if (current.has_value()) {
    print_config("[Getter] Current config:", current.value());
  } else {
    VLOG_W("[Getter] No value available yet (get returned nullopt)");
  }

  // ---------------------------------------------------------------
  // Step 7: Wait and let the listen callback process updates
  //
  // In a real application, the getter would run indefinitely or
  // until a shutdown signal is received.
  // ---------------------------------------------------------------
  VLOG_I("[Getter] Listening for configuration changes...");
  std::this_thread::sleep_for(3000ms);

  // ---------------------------------------------------------------
  // Step 8: Final read
  // ---------------------------------------------------------------
  auto final_val = getter.get();
  if (final_val.has_value()) {
    print_config("[Getter] Final config:", final_val.value());
  }

  VLOG_I("[Getter] Total changes received: ", change_count.load());
  VLOG_I("=== Config Getter complete ===");

  loop.quit();
  loop.wait_for_quit();

  return 0;
}
