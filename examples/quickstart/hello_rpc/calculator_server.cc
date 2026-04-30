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
 * @file calculator_server.cc
 * @brief VLink Method Model -- Server program providing a calculator RPC service.
 *
 * This server registers two services:
 *   1. Calculator service (request/response): receives CalcRequest, returns CalcResponse.
 *   2. Notification service (fire-and-forget): receives CalcRequest with no response.
 *
 * The server uses a MessageLoop to dispatch handler callbacks on a
 * dedicated thread.  It runs until SIGINT/SIGTERM is received.
 */

#include <vlink/base/logger.h>
#include <vlink/vlink.h>

#include <atomic>
#include <string>
#include <thread>

#include "./calculator_types.h"

using namespace std::chrono_literals;  // NOLINT(build/namespaces, google-build-using-namespace)

int main() {
  // ---------------------------------------------------------------
  // Step 1: Initialise logger and signal handler
  // ---------------------------------------------------------------
  VLOG_I("=== VLink Calculator Server ===");

  std::atomic<bool> running{true};
  vlink::Utils::register_terminate_signal([&running](int sig) {
    VLOG_I("Signal ", sig, " received, shutting down server...");
    running = false;
  });

  // ---------------------------------------------------------------
  // Step 2: Create a MessageLoop for the server callbacks
  //
  // Server handler callbacks are dispatched on this loop thread,
  // keeping handler execution serialised and thread-safe.
  // ---------------------------------------------------------------
  vlink::MessageLoop server_loop;
  server_loop.set_name("server_loop");
  server_loop.async_run();

  // ---------------------------------------------------------------
  // Step 3: Create the Calculator Server (request/response)
  //
  // Server<ReqT, RespT> listens for incoming requests.  The handler
  // receives (const CalcRequest&, CalcResponse&) and must fill the
  // response before returning -- the framework then serialises and
  // sends it back to the client.
  // ---------------------------------------------------------------
  vlink::Server<example::CalcRequest, example::CalcResponse> server(example::kCalculatorUrl);
  server.attach(&server_loop);

  server.listen([](const example::CalcRequest& req, example::CalcResponse& resp) {
    switch (req.op) {
      case '+':
        resp.result = req.a + req.b;
        break;
      case '-':
        resp.result = req.a - req.b;
        break;
      case '*':
        resp.result = req.a * req.b;
        break;
      case '/':
        resp.result = (req.b != 0) ? (req.a / req.b) : 0;
        break;
      default:
        resp.result = 0;
        break;
    }
    VLOG_I("[Server] ", req.a, " ", req.op, " ", req.b, " = ", resp.result);
  });

  VLOG_I("[Server] Calculator service listening on ", example::kCalculatorUrl);

  // ---------------------------------------------------------------
  // Step 4: Create the Notification Server (fire-and-forget)
  //
  // Server<ReqT> (without RespT) handles fire-and-forget requests.
  // The handler receives only the request -- there is no response
  // to fill or return.
  // ---------------------------------------------------------------
  vlink::Server<example::CalcRequest> notify_server(example::kNotifyUrl);
  notify_server.attach(&server_loop);

  notify_server.listen([](const example::CalcRequest& req) {
    VLOG_I("[NotifyServer] Fire-and-forget received: ", req.a, " ", req.op, " ", req.b);
  });

  VLOG_I("[Server] Notification service listening on ", example::kNotifyUrl);

  // ---------------------------------------------------------------
  // Step 5: Run until signal received
  // ---------------------------------------------------------------
  VLOG_I("[Server] Ready -- waiting for client requests (Ctrl+C to stop)...");

  while (running) {
    std::this_thread::sleep_for(100ms);
  }

  // ---------------------------------------------------------------
  // Step 6: Clean shutdown
  // ---------------------------------------------------------------
  VLOG_I("=== Calculator Server shutting down ===");

  server_loop.quit();
  server_loop.wait_for_quit();

  return 0;
}
