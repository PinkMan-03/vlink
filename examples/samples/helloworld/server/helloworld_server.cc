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
// VLink core communication API (Publisher, Server, etc.)
#include <vlink/base/logger.h>
#include <vlink/vlink.h>

#include <iostream>

// Protobuf generated message types
// Android platform uses a different include path
#if defined(__ANDROID__) && __has_include("helloworld/proto/helloworld.pb.h")
#include "helloworld/proto/helloworld.pb.h"
#else
#include "helloworld.pb.h"
#endif

// Common configuration header providing transport URL selection helper functions
#include "./helloworld_common.h"

using namespace vlink;  // NOLINT(build/namespaces, google-build-using-namespace)

/// HelloWorld server program
/// Features:
///   1. Provides an RPC service (method model): receives two integers and returns their sum
///   2. Periodically publishes event messages (event model): publishes a message with an incrementing counter every
///   100ms
/// Demonstrates two VLink communication models working together in a single process
int main(int argc, char* argv[]) {
  (void)argc;
  (void)argv;

  // Singleton check: ensure the same program is not running more than once

  if (!Utils::check_singleton("helloworld_server")) {
    std::cerr << "Program has started." << std::endl;
    return 1;
  }

  // Create a message loop and register Ctrl+C signal handler
  MessageLoop message_loop;
  Utils::register_terminate_signal([&message_loop](int) { message_loop.quit(); });

  // ======== Method Model (RPC) ========
  // Create a Server: request type is Helloworld::Request, response type is Helloworld::Response
  // The URL is determined by environment variables for the transport backend (defaults to dds://helloworld/method)
  Server<Helloworld::Request, Helloworld::Response> server(Common::get_method_url());

  // Register a synchronous request/response callback: process the request and fill in the response directly
  server.listen([](const Helloworld::Request& req, Helloworld::Response& resp) {
    CLOG_D("[Server] Receive left = %d, right = %d.", req.left(), req.right());
    // Compute the sum of the two numbers as the response
    resp.set_sum(req.left() + req.right());
  });

  // ======== Event Model (Pub/Sub) ========
  // Create a Publisher: publishes Helloworld::Message type event messages
  Publisher<Helloworld::Message> pub(Common::get_event_url());

  // Create a timer: fires every 100ms
  Timer timer;
  timer.attach(&message_loop);             // Bind to the message loop
  timer.set_interval(100);                 // Interval of 100 milliseconds
  timer.set_loop_count(Timer::kInfinite);  // Repeat indefinitely

  int index = 0;

  // Timer callback: construct a Protobuf message and publish it
  timer.start([&pub, &index]() {
    index++;
    Helloworld::Message msg;
    msg.set_detail("hello_world_" + std::to_string(index));
    pub.publish(msg);
  });

  // Block and run the message loop until the quit signal is received
  message_loop.run();

  return 0;
}
