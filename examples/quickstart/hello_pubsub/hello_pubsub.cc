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
 * @file hello_pubsub.cc
 * @brief VLink Event Model quickstart -- Timer-driven sensor Publisher + Subscriber.
 *
 * This example composes three VLink building blocks:
 *   1. SensorPublisher  -- a reusable component (sensor_publisher.h) that
 *      wraps a Publisher<SensorReading> and a Timer to stream temperature
 *      data at a fixed interval.
 *   2. Subscriber<SensorReading> -- receives and prints each reading.
 *   3. MessageLoop -- drives both the Timer and the Subscriber callback
 *      on a single background thread, ensuring serialised execution.
 *
 * Signal handling (SIGINT / SIGTERM) is used for graceful shutdown.
 *
 * Transport URL:  "intra://sensor/temperature"
 *   Change to "dds://sensor/temperature" or "shm://sensor/temperature"
 *   to switch transport without modifying any other code.
 */

#include <vlink/base/logger.h>
#include <vlink/vlink.h>

#include <atomic>
#include <string>
#include <thread>

#include "./sensor_publisher.h"

using namespace std::chrono_literals;  // NOLINT(build/namespaces, google-build-using-namespace)

/// Topic URL -- change the transport to switch transport layer.
/// Examples: "intra://...", "dds://...", "shm://...", "zenoh://..."
static const char kTopicUrl[] = "intra://sensor/temperature";  // NOLINT(runtime/string)

/// Publish interval in milliseconds.
static constexpr int kPublishIntervalMs = 500;

/// Maximum number of messages before auto-shutdown (0 = unlimited).
static constexpr int kMaxMessages = 10;

int main() {
  // ---------------------------------------------------------------
  // Step 1: Initialise logger and register signal handler
  // ---------------------------------------------------------------
  VLOG_I("=== VLink Hello PubSub (Sensor Streaming) ===");

  std::atomic<bool> running{true};
  vlink::Utils::register_terminate_signal([&running](int sig) {
    VLOG_I("Signal ", sig, " received, shutting down...");
    running = false;
  });

  // ---------------------------------------------------------------
  // Step 2: Create a MessageLoop
  //
  // Both the Timer (inside SensorPublisher) and the Subscriber
  // callback will run on this loop thread, keeping everything
  // serialised and lock-free.
  // ---------------------------------------------------------------
  vlink::MessageLoop loop;
  loop.set_name("main_loop");
  loop.async_run();

  // ---------------------------------------------------------------
  // Step 3: Create a Subscriber and register a receive callback
  //
  // The Subscriber listens on the same URL as the SensorPublisher.
  // attach(&loop) dispatches the callback on the loop thread.
  // ---------------------------------------------------------------
  vlink::Subscriber<example::SensorReading> sub(kTopicUrl);
  sub.attach(&loop);

  std::atomic<int> received_count{0};
  sub.listen([&received_count, &running](const example::SensorReading& reading) {
    VLOG_I("[Subscriber] sensor_id=", reading.sensor_id, " seq=#", reading.sequence, " temp=", reading.temperature,
           " ts=", reading.timestamp_ms);
    int count = received_count.fetch_add(1) + 1;

    if (kMaxMessages > 0 && count >= kMaxMessages) {
      running = false;
    }
  });

  VLOG_I("[Subscriber] Listening on ", kTopicUrl);

  // ---------------------------------------------------------------
  // Step 4: Create a SensorPublisher (reusable component)
  //
  // SensorPublisher encapsulates Publisher<SensorReading> + Timer.
  // It publishes a simulated temperature reading every
  // kPublishIntervalMs milliseconds on the loop thread.
  // ---------------------------------------------------------------
  example::SensorPublisher sensor(/*sensor_id=*/1, kTopicUrl, &loop, kPublishIntervalMs);
  sensor.start();

  VLOG_I("[Publisher]  Publishing on ", kTopicUrl, " every ", kPublishIntervalMs, "ms");

  // ---------------------------------------------------------------
  // Step 5: Main loop -- wait until kMaxMessages received or signal
  // ---------------------------------------------------------------
  while (running) {
    std::this_thread::sleep_for(100ms);
  }

  // ---------------------------------------------------------------
  // Step 6: Clean shutdown
  // ---------------------------------------------------------------
  sensor.stop();
  loop.wait_for_idle(1000);

  VLOG_I("Published: ", sensor.published_count(), "  Received: ", received_count.load());
  VLOG_I("=== Example complete ===");

  loop.quit();
  loop.wait_for_quit();

  return 0;
}
