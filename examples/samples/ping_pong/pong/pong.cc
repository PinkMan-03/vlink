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
// VLink core communication API
#include <vlink/vlink.h>

// Common configuration header providing helper functions for selecting transport URLs based on environment variables
#include "../ping_pong_common.h"

using namespace vlink;                 // NOLINT(build/namespaces, google-build-using-namespace)
using namespace std::chrono_literals;  // NOLINT(build/namespaces, google-build-using-namespace)

/// Pong endpoint program: receives data sent by the Ping endpoint and echoes it back
/// How it works:
///   1. Subscribe to ping endpoint messages
///   2. Upon receiving a message, immediately publish it back to the ping endpoint
///   3. The ping endpoint uses this to calculate round-trip time (RTT)
int main(int argc, char* argv[]) {
  (void)argc;
  (void)argv;

  // Create a message loop to keep the process running
  MessageLoop message_loop;

  // Register Ctrl+C signal handler for graceful shutdown
  Utils::register_terminate_signal([&message_loop](int) { message_loop.quit(); });

  // Create a Subscriber to receive messages from the ping endpoint
  Subscriber<Bytes> sub(Common::get_ping_url());

  // Create a Publisher to send received data back to the ping endpoint
  Publisher<Bytes> pub(Common::get_pong_url());

  // Register receive callback: echo back the received ping data immediately (echo mode)
  sub.listen([&pub](const Bytes& data) { pub.publish(data); });

  // Block and run the message loop
  message_loop.run();

  return 0;
}
