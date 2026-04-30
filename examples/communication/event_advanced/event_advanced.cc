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
 * @file event_advanced.cc
 * @brief Advanced Publisher/Subscriber features: detect_subscribers, latency tracking,
 *        force publish, and multiple subscribers on the same topic.
 *
 * Demonstrates:
 *   - detect_subscribers(callback): async notification when subscribers connect/disconnect
 *   - wait_for_subscribers(timeout): block until a subscriber appears
 *   - has_subscribers(): non-blocking subscriber presence query
 *   - publish(msg, force=true): publish even when no subscribers are present
 *   - set_latency_and_lost_enabled(true): enable per-message latency tracking
 *   - get_latency(): read end-to-end latency in microseconds
 *   - get_lost(): read cumulative sample delivery statistics
 *   - Multiple subscribers on one topic (fan-out)
 */

#include <vlink/base/logger.h>
#include <vlink/vlink.h>

#include <atomic>
#include <string>
#include <thread>

using namespace std::chrono_literals;  // NOLINT(build/namespaces, google-build-using-namespace)

// POD type defined in sensor_types.h -- see that file for field descriptions
#include "sensor_types.h"

int main() {
  VLOG_I("=== VLink Event Advanced Example ===");

  vlink::MessageLoop loop;
  loop.set_name("main_loop");
  loop.async_run();

  // ---------------------------------------------------------------
  // Section 1: detect_subscribers -- async connection notification
  // ---------------------------------------------------------------
  VLOG_I("--- Section 1: detect_subscribers ---");

  vlink::Publisher<SensorReading> pub("dds://advanced/sensor");
  pub.attach(&loop);

  // Register a callback that fires when subscriber presence changes.
  // The callback receives true when at least one subscriber exists,
  // and false when the last subscriber disconnects.
  pub.detect_subscribers(
      [](bool has_subscribers) { VLOG_I("[Publisher] Subscriber presence changed: ", has_subscribers); });

  // Before any subscriber is created, has_subscribers() returns false.
  VLOG_I("[Publisher] has_subscribers (before): ", pub.has_subscribers());

  // ---------------------------------------------------------------
  // Section 2: Force publish (no subscribers yet)
  //
  // By default, publish() is a no-op when no subscribers exist.
  // Pass force=true to send anyway (useful for recording or logging).
  // ---------------------------------------------------------------
  VLOG_I("--- Section 2: Force publish ---");

  SensorReading forced_msg{0, -1.0};
  bool ok_normal = pub.publish(forced_msg);        // no subscribers => no-op
  bool ok_forced = pub.publish(forced_msg, true);  // force=true => always sends
  VLOG_I("[Publisher] Normal publish (no subs): ", ok_normal);
  VLOG_I("[Publisher] Forced publish (no subs): ", ok_forced);

  // ---------------------------------------------------------------
  // Section 3: Multiple subscribers (fan-out)
  //
  // All subscribers on the same URL receive every published message.
  // Each subscriber can have its own callback and its own loop.
  // ---------------------------------------------------------------
  VLOG_I("--- Section 3: Multiple subscribers ---");

  std::atomic<int> sub1_count{0};
  std::atomic<int> sub2_count{0};
  std::atomic<int> sub3_count{0};

  vlink::Subscriber<SensorReading> sub1("dds://advanced/sensor");
  sub1.attach(&loop);
  sub1.listen([&sub1_count](const SensorReading& msg) {
    VLOG_I("[Sub1] sensor_id=", msg.sensor_id, " value=", msg.value);
    sub1_count++;
  });

  vlink::Subscriber<SensorReading> sub2("dds://advanced/sensor");
  sub2.attach(&loop);
  sub2.listen([&sub2_count](const SensorReading& msg) {
    (void)msg;
    sub2_count++;  // Silent counter -- does not log
  });

  // Sub3 uses latency tracking (Section 4 below)
  vlink::Subscriber<SensorReading> sub3("dds://advanced/sensor");
  sub3.attach(&loop);

  // ---------------------------------------------------------------
  // Section 4: Latency and lost tracking
  //
  // set_latency_and_lost_enabled(true) must be called BEFORE listen().
  // After enabling, get_latency() returns the end-to-end latency in
  // microseconds, and get_lost() returns cumulative delivery stats.
  // ---------------------------------------------------------------
  VLOG_I("--- Section 4: Latency and lost tracking ---");

  sub3.set_latency_and_lost_enabled(true);
  sub3.listen([&sub3, &sub3_count](const SensorReading& msg) {
    sub3_count++;
    int64_t latency_us = sub3.get_latency();
    VLOG_I("[Sub3-latency] sensor_id=", msg.sensor_id, " latency=", latency_us, "us");
  });

  // ---------------------------------------------------------------
  // Section 5: wait_for_subscribers
  //
  // Block until at least one subscriber is present.
  // Default timeout is Timeout::kDefaultInterval (5000ms).
  // ---------------------------------------------------------------
  VLOG_I("--- Section 5: wait_for_subscribers ---");

  bool found = pub.wait_for_subscribers(2000ms);
  VLOG_I("[Publisher] wait_for_subscribers: ", found);
  VLOG_I("[Publisher] has_subscribers (after): ", pub.has_subscribers());

  // ---------------------------------------------------------------
  // Section 6: Publish messages and observe fan-out + latency
  // ---------------------------------------------------------------
  VLOG_I("--- Section 6: Publish and observe ---");

  for (int i = 1; i <= 5; ++i) {
    SensorReading msg{i, 10.0 + i * 0.1};
    pub.publish(msg);
    std::this_thread::sleep_for(100ms);
  }

  // Wait for all callbacks to complete
  loop.wait_for_idle(2000);

  // ---------------------------------------------------------------
  // Section 7: Check lost statistics
  // ---------------------------------------------------------------
  VLOG_I("--- Section 7: Delivery statistics ---");

  vlink::SampleLostInfo lost_info = sub3.get_lost();
  VLOG_I("[Sub3] total=", lost_info.total, " lost=", lost_info.lost);
  VLOG_I("[Sub1] received: ", sub1_count.load());
  VLOG_I("[Sub2] received: ", sub2_count.load());
  VLOG_I("[Sub3] received: ", sub3_count.load());

  // ---------------------------------------------------------------
  // Cleanup
  // ---------------------------------------------------------------
  VLOG_I("=== Example complete ===");
  loop.quit();
  loop.wait_for_quit();

  return 0;
}
