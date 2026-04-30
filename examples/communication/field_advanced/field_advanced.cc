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
 * @file field_advanced.cc
 * @brief Advanced Field Model features: change_reporting, late-joiner sync, multi-Getter.
 *
 * Demonstrates:
 *   - set_change_reporting(true): suppress duplicate value callbacks
 *   - Late-joiner synchronization: Getter created after Setter auto-receives cached value
 *   - Multiple Getters on one field (fan-out)
 *   - Latency and lost tracking on Getter
 */

#include <vlink/base/logger.h>
#include <vlink/vlink.h>

#include <atomic>
#include <string>
#include <thread>

using namespace std::chrono_literals;  // NOLINT(build/namespaces, google-build-using-namespace)

// POD type defined in config_types.h -- see that file for field descriptions
#include "config_types.h"

int main() {
  VLOG_I("=== VLink Field Advanced Example ===");

  vlink::MessageLoop loop;
  loop.set_name("main_loop");
  loop.async_run();

  // ---------------------------------------------------------------
  // Section 1: Change reporting (duplicate suppression)
  //
  // When set_change_reporting(true) is enabled, the Getter's listen
  // callback is only invoked when the new value differs from the
  // previous one (compared at the raw bytes level).
  // ---------------------------------------------------------------
  VLOG_I("--- Section 1: Change reporting ---");

  vlink::Setter<BrightnessConfig> setter("ddsc://display/brightness");
  setter.set({50, false});

  std::this_thread::sleep_for(50ms);

  vlink::Getter<BrightnessConfig> getter_cr("ddsc://display/brightness");
  getter_cr.attach(&loop);
  getter_cr.set_change_reporting(true);  // Enable change reporting BEFORE listen()

  std::atomic<int> cr_callback_count{0};
  getter_cr.listen([&cr_callback_count](const BrightnessConfig& cfg) {
    VLOG_I("[CR-Getter] Changed: level=", cfg.level, " auto=", cfg.auto_mode);
    cr_callback_count++;
  });

  getter_cr.wait_for_value(1000ms);

  // Write the same value multiple times -- callback should fire only when value changes
  setter.set({50, false});  // Same as current -- suppressed
  std::this_thread::sleep_for(50ms);
  setter.set({50, false});  // Same again -- suppressed
  std::this_thread::sleep_for(50ms);
  setter.set({75, false});  // Different -- callback fires
  std::this_thread::sleep_for(50ms);
  setter.set({75, false});  // Same -- suppressed
  std::this_thread::sleep_for(50ms);
  setter.set({100, true});  // Different -- callback fires
  std::this_thread::sleep_for(50ms);

  loop.wait_for_idle(1000);
  VLOG_I("[CR-Getter] change_reporting enabled: ", getter_cr.get_change_reporting());
  VLOG_I("[CR-Getter] Callback count (expect ~2 unique changes): ", cr_callback_count.load());

  // ---------------------------------------------------------------
  // Section 2: Late-joiner synchronization
  //
  // A Getter created AFTER the Setter has written will automatically
  // receive the current cached value. This is the key difference
  // between the Field Model and the Event Model.
  // ---------------------------------------------------------------
  VLOG_I("--- Section 2: Late-joiner sync ---");

  // The setter already has value {100, true} from Section 1
  VLOG_I("[Setter] Current cached value: level=100, auto=true");

  // Create a brand-new Getter -- it should immediately receive the cached value
  vlink::Getter<BrightnessConfig> late_getter("ddsc://display/brightness");
  late_getter.attach(&loop);

  std::atomic<bool> late_got_value{false};
  late_getter.listen([&late_got_value](const BrightnessConfig& cfg) {
    VLOG_I("[LateGetter] Received via sync: level=", cfg.level, " auto=", cfg.auto_mode);
    late_got_value = true;
  });

  // wait_for_value should succeed because the Setter's cached value is synced
  if (late_getter.wait_for_value(2000ms)) {
    auto val = late_getter.get();
    if (val.has_value()) {
      VLOG_I("[LateGetter] get(): level=", val->level, " auto=", val->auto_mode);
    }
  } else {
    VLOG_W("[LateGetter] wait_for_value timed out (unexpected)");
  }

  // ---------------------------------------------------------------
  // Section 3: Multiple Getters (fan-out)
  //
  // Multiple Getters can listen to the same field URL. Each Getter
  // independently receives value updates and maintains its own cache.
  // ---------------------------------------------------------------
  VLOG_I("--- Section 3: Multiple Getters ---");

  vlink::Setter<int> multi_setter("ddsc://config/volume");

  std::atomic<int> g1_count{0};
  std::atomic<int> g2_count{0};
  std::atomic<int> g3_count{0};

  vlink::Getter<int> g1("ddsc://config/volume");
  g1.attach(&loop);
  g1.listen([&g1_count](const int& v) {
    VLOG_I("[Getter1] volume=", v);
    g1_count++;
  });

  vlink::Getter<int> g2("ddsc://config/volume");
  g2.attach(&loop);
  g2.listen([&g2_count](const int& v) {
    (void)v;
    g2_count++;  // Silent counter
  });

  vlink::Getter<int> g3("ddsc://config/volume");
  g3.attach(&loop);
  g3.set_latency_and_lost_enabled(true);
  g3.listen([&g3, &g3_count](const int& v) {
    g3_count++;
    VLOG_I("[Getter3] volume=", v, " latency=", g3.get_latency(), "us");
  });

  std::this_thread::sleep_for(50ms);

  // Publish several values
  for (int vol = 0; vol <= 100; vol += 25) {
    multi_setter.set(vol);
    std::this_thread::sleep_for(50ms);
  }

  loop.wait_for_idle(1000);

  VLOG_I("[Getter1] received: ", g1_count.load());
  VLOG_I("[Getter2] received: ", g2_count.load());
  VLOG_I("[Getter3] received: ", g3_count.load());

  // All three getters can read the latest value independently
  auto v1 = g1.get();
  auto v2 = g2.get();
  auto v3 = g3.get();
  if (v1.has_value()) VLOG_I("[Getter1] final: ", v1.value());
  if (v2.has_value()) VLOG_I("[Getter2] final: ", v2.value());
  if (v3.has_value()) VLOG_I("[Getter3] final: ", v3.value());

  // Check lost stats on Getter3
  vlink::SampleLostInfo lost = g3.get_lost();
  VLOG_I("[Getter3] total=", lost.total, " lost=", lost.lost);

  // ---------------------------------------------------------------
  // Cleanup
  // ---------------------------------------------------------------
  VLOG_I("=== Example complete ===");
  loop.quit();
  loop.wait_for_quit();

  return 0;
}
