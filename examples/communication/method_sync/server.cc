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
 * @file server.cc
 * @brief Server side of the method_sync example.
 *
 * Registers a synchronous math calculator handler via listen(ReqRespCallback).
 * The handler fills the response in-place before returning.
 * Run this alongside client.cc for cross-process RPC.
 */

#include <vlink/base/logger.h>
#include <vlink/vlink.h>

#include <atomic>
#include <cmath>
#include <thread>

#include "math_types.h"

using namespace std::chrono_literals;  // NOLINT(build/namespaces, google-build-using-namespace)

int main() {
  VLOG_I("=== VLink Method Sync Server ===");

  std::atomic<bool> running{true};
  vlink::Utils::register_terminate_signal([&running](int sig) {
    VLOG_I("Signal ", sig, " received, shutting down...");
    running = false;
  });

  vlink::MessageLoop server_loop;
  server_loop.set_name("server_loop");
  server_loop.async_run();

  // ---------------------------------------------------------------
  // Create a Server with synchronous handler
  //
  // listen(ReqRespCallback) receives (const Req&, Resp&).
  // The handler fills the response in-place before returning.
  // The framework automatically serializes and sends the response.
  // ---------------------------------------------------------------
  vlink::Server<MathRequest, MathResponse> server("dds://math/calculator");
  server.attach(&server_loop);

  server.listen([](const MathRequest& req, MathResponse& resp) {
    resp.success = true;
    switch (req.operation) {
      case 0:
        resp.result = req.x + req.y;
        break;
      case 1:
        resp.result = req.x - req.y;
        break;
      case 2:
        resp.result = req.x * req.y;
        break;
      case 3:
        if (req.y != 0.0) {
          resp.result = req.x / req.y;
        } else {
          resp.result = 0.0;
          resp.success = false;
        }

        break;
      case 4:
        resp.result = std::pow(req.x, req.y);
        break;
      default:
        resp.result = 0.0;
        resp.success = false;
        break;
    }
    VLOG_I("[Server] op=", req.operation, " x=", req.x, " y=", req.y, " => ", resp.result);
  });

  VLOG_I("[Server] Listening on dds://math/calculator");
  VLOG_I("[Server] Press Ctrl+C to stop.");

  // Wait for signal
  while (running) {
    std::this_thread::sleep_for(100ms);
  }

  VLOG_I("=== Server shutting down ===");
  server_loop.quit();
  server_loop.wait_for_quit();

  return 0;
}
