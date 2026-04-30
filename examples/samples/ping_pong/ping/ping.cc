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

// VLink message loop and utility library
#include <vlink/base/message_loop.h>
#include <vlink/base/utils.h>
// VLink core communication API (Publisher, Subscriber, etc.)
#include <vlink/base/logger.h>
#include <vlink/vlink.h>

#include <charconv>
#include <string>

// Common configuration header providing helper functions for selecting transport URLs based on environment variables
#include "../ping_pong_common.h"

using namespace vlink;                 // NOLINT(build/namespaces, google-build-using-namespace)
using namespace std::chrono_literals;  // NOLINT(build/namespaces, google-build-using-namespace)

// Periodic send interval (milliseconds): send a ping every 1 second
static constexpr uint32_t kTestInterval = 1000U;

int main(int argc, char* argv[]) {
  // Default payload size is 1024 bytes; can be overridden via command-line argument
  uint64_t test_size = 1024;

  // Parse command-line arguments: optionally specify the payload size
  if (argc == 2) {
    std::string str(argv[1]);
    auto [p, error] = std::from_chars(str.data(), str.data() + str.size(), test_size);

    if (error != std::errc()) {
      VLOG_W("Invalid args.");
      return 1;
    }
  } else if (argc != 1) {
    VLOG_I("Usage:");
    VLOG_I(" example_ping [payload_size]");
    return 1;
  }

  if (test_size == 0) {
    VLOG_W("Invalid args.");
    return 1;
  }

  // Create a message loop to drive timers and process callbacks
  MessageLoop message_loop;

  // Register system signal handler (Ctrl+C) to quit the message loop on signal reception
  Utils::register_terminate_signal([&message_loop](int) { message_loop.quit(); });

  // Create a Publisher that sends raw byte data using the Bytes type
  // The URL is determined by the PING_TRANSPORT environment variable (defaults to dds://ping)
  Publisher<Bytes> pub(Common::get_ping_url());

  // Create a Subscriber to receive reply messages from the pong endpoint
  Subscriber<Bytes> sub(Common::get_pong_url());

  // Record the send timestamp for computing round-trip latency
  std::atomic<std::chrono::steady_clock::time_point> start_stamp = std::chrono::steady_clock::now();

  // Register a receive callback: compute round-trip latency when a pong reply arrives
  sub.listen([&start_stamp](const Bytes&) {
    auto duration =
        std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - start_stamp.load());
    // Divide round-trip latency by 2 to get one-way latency
    double delay = duration.count() / 2000.0;
    CLOG_D("Delay(ms) = %.3lf.", delay);
  });

  // Create a Bytes payload of the specified size (contents uninitialized)
  Bytes data = Bytes::create(test_size);

  // Send function: record timestamp and publish data
  auto send_func = [&pub, &data, &start_stamp]() {
    start_stamp = std::chrono::steady_clock::now();

    pub.publish(data);
  };

  // Send the first ping immediately
  send_func();

  // Create a timer to periodically send ping messages
  Timer timer;
  timer.attach(&message_loop);             // Bind to the message loop
  timer.set_interval(kTestInterval);       // Set interval to 1 second
  timer.set_loop_count(Timer::kInfinite);  // Repeat indefinitely
  timer.start(send_func);                  // Start the timer

  // Block and run the message loop until quit() is called
  message_loop.run();

  return 0;
}
