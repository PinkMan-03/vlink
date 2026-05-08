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

// VLink condition variable (cross-platform wrapper)
#include <vlink/base/condition_variable.h>
// VLink utility library (signal handling, environment variables, etc.)
#include <vlink/base/utils.h>
// VLink core communication API (Client, Subscriber, etc.)
#include <vlink/base/logger.h>
#include <vlink/vlink.h>

// Protobuf generated message types
#if defined(__ANDROID__) && __has_include("helloworld/proto/helloworld.pb.h")
#include "helloworld/proto/helloworld.pb.h"
#else
#include "helloworld.pb.h"
#endif

// Performance timer
#include <vlink/base/elapsed_timer.h>

// Common configuration header
#include "./helloworld_common.h"

using namespace vlink;                 // NOLINT(build/namespaces, google-build-using-namespace)
using namespace std::chrono_literals;  // NOLINT(build/namespaces, google-build-using-namespace)

/// RPC call mode: send an addition request to the server and get the result
/// Demonstrates VLink's method model (Client/Server) with synchronous invocation
int set(int left, int right) {
  // Create a Client: request type is Request, response type is Response
  Client<Helloworld::Request, Helloworld::Response> client(Common::get_method_url());

  // Block and wait for the server to come online (timeout: 1 second)
  if (!client.wait_for_connected(1s)) {
    VLOG_W("[Client] Server not ready.");
    return -1;
  }

  // Construct the request message
  Helloworld::Request req;
  req.set_left(left);
  req.set_right(right);

  // Synchronous call: send the request and wait for the response (timeout: 3 seconds)
  Helloworld::Response resp;
  bool ret = client.invoke(req, resp, 3s);
  if (!ret) {
    VLOG_W("[Client] Invoke failed.");
    return -1;
  }

  // Print the result returned by the server
  VLOG_D("[Client] Receive sum: ", resp.sum());

  return 0;
}

/// Event subscription mode: continuously receive event messages published by the server
/// Demonstrates VLink's event model (Publisher/Subscriber) subscription
int sub() {
  // Create a Subscriber to receive event messages
  Subscriber<Helloworld::Message> sub(Common::get_event_url());

  // Register receive callback: print the content of each received message
  sub.listen([](const Helloworld::Message& msg) { CLOG_D("[Client] Receive event: %s.", msg.detail().c_str()); });

  // Use a condition variable to block the main thread until a termination signal is received
  std::mutex mtx;
  std::unique_lock lock(mtx);
  vlink::condition_variable cv;

  // Register Ctrl+C signal handler to wake up the condition variable and exit
  Utils::register_terminate_signal([&cv](int) { cv.notify_one(); });
  cv.wait(lock);

  return 0;
}

/// Main function: select the run mode based on command-line arguments
///   sub       - Event subscription mode, continuously receive messages
///   set L R   - RPC call mode, compute L + R
int main(int argc, char* argv[]) {
  // Mode 1: Event subscription
  if (argc == 2 && ::strcmp(argv[1], "sub") == 0) {
    return sub();
  }

  // Mode 2: RPC call
  if (argc == 4 && ::strcmp(argv[1], "set") == 0) {
    int left = std::stoi(argv[2]);
    int right = std::stoi(argv[3]);
    return set(left, right);
  }

  // Print usage instructions
  VLOG_I("Usage:");
  VLOG_I(" sample_helloworld_client [sub]");
  VLOG_I(" sample_helloworld_client [set] [left_num] [right_num]");

  return 1;
}
