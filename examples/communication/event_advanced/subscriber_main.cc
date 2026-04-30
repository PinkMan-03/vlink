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
 * @file subscriber_main.cc
 * @brief Subscriber side of the event_advanced example.
 *
 * Demonstrates advanced Subscriber features:
 *   - Multiple subscribers on one topic (fan-out)
 *   - set_latency_and_lost_enabled(true): enable per-message latency tracking
 *   - get_latency(): read end-to-end latency in microseconds
 *   - get_lost(): read cumulative sample delivery statistics
 *
 * Run this alongside publisher_main to observe cross-process behavior.
 */

#include <vlink/base/logger.h>
#include <vlink/vlink.h>

#include <atomic>
#include <string>
#include <thread>

#include "sensor_types.h"

using namespace std::chrono_literals;  // NOLINT(build/namespaces, google-build-using-namespace)

int main() {
  VLOG_I("=== VLink Event Advanced Subscriber ===");

  vlink::MessageLoop loop;
  loop.set_name("sub_loop");
  loop.async_run();

  std::atomic<bool> running{true};
  vlink::Utils::register_terminate_signal([&running](int sig) {
    VLOG_I("Signal ", sig, " received, shutting down...");
    running = false;
  });

  // ---------------------------------------------------------------
  // Section 1: Multiple subscribers (fan-out)
  // ---------------------------------------------------------------
  VLOG_I("--- Section 1: Multiple subscribers ---");

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

  // ---------------------------------------------------------------
  // Section 2: Latency and lost tracking
  // ---------------------------------------------------------------
  VLOG_I("--- Section 2: Latency and lost tracking ---");

  vlink::Subscriber<SensorReading> sub3("dds://advanced/sensor");
  sub3.attach(&loop);
  sub3.set_latency_and_lost_enabled(true);
  sub3.listen([&sub3, &sub3_count](const SensorReading& msg) {
    sub3_count++;
    int64_t latency_us = sub3.get_latency();
    VLOG_I("[Sub3-latency] sensor_id=", msg.sensor_id, " latency=", latency_us, "us");
  });

  VLOG_I("[Subscribers] Listening on dds://advanced/sensor (waiting for publisher)...");

  // ---------------------------------------------------------------
  // Main loop -- wait for signal
  // ---------------------------------------------------------------
  while (running) {
    std::this_thread::sleep_for(100ms);
  }

  // ---------------------------------------------------------------
  // Delivery statistics
  // ---------------------------------------------------------------
  VLOG_I("--- Delivery statistics ---");

  vlink::SampleLostInfo lost_info = sub3.get_lost();
  VLOG_I("[Sub3] total=", lost_info.total, " lost=", lost_info.lost);
  VLOG_I("[Sub1] received: ", sub1_count.load());
  VLOG_I("[Sub2] received: ", sub2_count.load());
  VLOG_I("[Sub3] received: ", sub3_count.load());

  // ---------------------------------------------------------------
  // Cleanup
  // ---------------------------------------------------------------
  VLOG_I("=== Subscriber complete ===");
  loop.quit();
  loop.wait_for_quit();

  return 0;
}
