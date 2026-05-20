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
 * @file event_basic.cc
 * @brief Basic Publisher/Subscriber example with Timer-driven periodic publishing.
 *
 * Demonstrates the VLink Event Model fundamentals:
 *   - Publisher<T>: sends messages via publish()
 *   - Subscriber<T>: receives messages via listen() callback
 *   - Timer: drives periodic publish on a MessageLoop thread
 *   - MessageLoop: event loop for dispatching callbacks
 *   - Utils::register_terminate_signal: graceful SIGINT/SIGTERM shutdown
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
  // ---------------------------------------------------------------
  // Step 1: Initialize logger and register signal handler
  // ---------------------------------------------------------------
  VLOG_I("=== VLink Event Basic Example ===");

  std::atomic<bool> running{true};
  vlink::Utils::register_terminate_signal([&running](int sig) {
    VLOG_I("Signal ", sig, " received, shutting down...");
    running = false;
  });

  // ---------------------------------------------------------------
  // Step 2: Create a MessageLoop for the subscriber and timer
  // ---------------------------------------------------------------
  vlink::MessageLoop loop;
  loop.set_name("main_loop");
  loop.async_run();

  // ---------------------------------------------------------------
  // Step 3: Create a Subscriber and register a callback
  //
  // The callback fires on the loop thread each time a message
  // arrives on "dds://sensor/temperature".
  // ---------------------------------------------------------------
  vlink::Subscriber<SensorData> sub("dds://sensor/temperature");
  sub.attach(&loop);

  std::atomic<int> received_count{0};
  sub.listen([&received_count](const SensorData& data) {
    VLOG_I("[Subscriber] id=", data.id, " value=", data.value, " ts=", data.timestamp);
    received_count++;
  });

  VLOG_I("[Subscriber] Listening on dds://sensor/temperature");

  // ---------------------------------------------------------------
  // Step 4: Create a Publisher on the same URL
  // ---------------------------------------------------------------
  vlink::Publisher<SensorData> pub("dds://sensor/temperature");
  VLOG_I("[Publisher]  Publishing on dds://sensor/temperature");

  // Wait until the subscriber is connected before publishing
  pub.wait_for_subscribers();
  VLOG_I("[Publisher]  Subscriber detected, starting timer...");

  // ---------------------------------------------------------------
  // Step 5: Use a Timer to publish periodically
  //
  // Timer fires every 500ms on the loop thread.  The callback
  // creates a SensorData message with an incrementing sequence
  // number and publishes it.
  // ---------------------------------------------------------------
  std::atomic<int> publish_count{0};
  static constexpr int kMaxPublish = 10;

  vlink::Timer timer(&loop, 500, vlink::Timer::kInfinite, [&pub, &publish_count, &running]() {
    if (publish_count >= kMaxPublish) {
      running = false;
      return;
    }

    int seq = publish_count.fetch_add(1) + 1;
    SensorData data{};  // Value-initialization: all members zeroed
    data.id = seq;
    data.value = 20.0F + static_cast<float>(seq) * 0.5F;
    data.timestamp = static_cast<int64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch())
            .count());

    bool ok = pub.publish(data);
    VLOG_I("[Publisher]  Published #", seq, " value=", data.value, " ok=", ok);
  });
  timer.start();

  // ---------------------------------------------------------------
  // Step 6: Main loop -- wait until finished or signal received
  // ---------------------------------------------------------------
  while (running) {
    std::this_thread::sleep_for(100ms);
  }

  // ---------------------------------------------------------------
  // Step 7: Clean shutdown
  // ---------------------------------------------------------------
  timer.stop();
  loop.wait_for_idle(1000);

  VLOG_I("Published: ", publish_count.load(), " Received: ", received_count.load());
  VLOG_I("=== Example complete ===");

  loop.quit();
  loop.wait_for_quit();

  return 0;
}
