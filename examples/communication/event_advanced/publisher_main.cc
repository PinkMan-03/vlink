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
 * @file publisher_main.cc
 * @brief Publisher side of the event_advanced example.
 *
 * Demonstrates advanced Publisher features:
 *   - detect_subscribers(callback): async notification when subscribers connect/disconnect
 *   - wait_for_subscribers(timeout): block until a subscriber appears
 *   - has_subscribers(): non-blocking subscriber presence query
 *   - publish(msg, force=true): publish even when no subscribers are present
 *
 * Run this alongside subscriber_main to observe cross-process behavior.
 */

#include <vlink/base/logger.h>
#include <vlink/vlink.h>

#include <atomic>
#include <string>
#include <thread>

#include "sensor_types.h"

using namespace std::chrono_literals;  // NOLINT(build/namespaces, google-build-using-namespace)

int main() {
  VLOG_I("=== VLink Event Advanced Publisher ===");

  vlink::MessageLoop loop;
  loop.set_name("pub_loop");
  loop.async_run();

  std::atomic<bool> running{true};
  vlink::Utils::register_terminate_signal([&running](int sig) {
    VLOG_I("Signal ", sig, " received, shutting down...");
    running = false;
  });

  // ---------------------------------------------------------------
  // Section 1: detect_subscribers -- async connection notification
  // ---------------------------------------------------------------
  VLOG_I("--- Section 1: detect_subscribers ---");

  vlink::Publisher<SensorReading> pub("dds://advanced/sensor");
  pub.attach(&loop);

  // Register a callback that fires when subscriber presence changes.
  pub.detect_subscribers(
      [](bool has_subscribers) { VLOG_I("[Publisher] Subscriber presence changed: ", has_subscribers); });

  // Before any subscriber is created, has_subscribers() returns false.
  VLOG_I("[Publisher] has_subscribers (before): ", pub.has_subscribers());

  // ---------------------------------------------------------------
  // Section 2: Force publish (no subscribers yet)
  // ---------------------------------------------------------------
  VLOG_I("--- Section 2: Force publish ---");

  SensorReading forced_msg{0, -1.0};
  bool ok_normal = pub.publish(forced_msg);        // no subscribers => no-op
  bool ok_forced = pub.publish(forced_msg, true);  // force=true => always sends
  VLOG_I("[Publisher] Normal publish (no subs): ", ok_normal);
  VLOG_I("[Publisher] Forced publish (no subs): ", ok_forced);

  // ---------------------------------------------------------------
  // Section 3: wait_for_subscribers
  // ---------------------------------------------------------------
  VLOG_I("--- Section 3: wait_for_subscribers ---");

  bool found = pub.wait_for_subscribers(10000ms);
  VLOG_I("[Publisher] wait_for_subscribers: ", found);
  VLOG_I("[Publisher] has_subscribers (after): ", pub.has_subscribers());

  if (!found) {
    VLOG_W("[Publisher] No subscriber found, exiting.");
    loop.quit();
    loop.wait_for_quit();
    return 0;
  }

  // ---------------------------------------------------------------
  // Section 4: Publish messages
  // ---------------------------------------------------------------
  VLOG_I("--- Section 4: Publishing messages ---");

  for (int i = 1; i <= 10 && running; ++i) {
    SensorReading msg{i, 10.0 + i * 0.1};
    pub.publish(msg);
    VLOG_I("[Publisher] Published sensor_id=", i, " value=", msg.value);
    std::this_thread::sleep_for(200ms);
  }

  // ---------------------------------------------------------------
  // Cleanup
  // ---------------------------------------------------------------
  VLOG_I("=== Publisher complete ===");
  loop.quit();
  loop.wait_for_quit();

  return 0;
}
