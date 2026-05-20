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
 * @file method_sync.cc
 * @brief Synchronous RPC: Server listen(ReqRespCallback), Client invoke sync.
 *
 * Demonstrates the VLink Method Model with synchronous invocation:
 *   - Server<Req, Resp>: listen with ReqRespCallback -- fill response in-place
 *   - Client<Req, Resp>: invoke(req, resp) -- blocking, output reference
 *   - Client<Req, Resp>: invoke(req) -> optional<Resp> -- blocking, returns optional
 *   - wait_for_connected(): block until a server is available
 *   - is_connected(): non-blocking connection check
 */

#include <vlink/base/logger.h>
#include <vlink/vlink.h>

#include <cmath>
#include <string>
#include <thread>

using namespace std::chrono_literals;  // NOLINT(build/namespaces, google-build-using-namespace)

// POD types defined in math_types.h -- see that file for field descriptions
#include "math_types.h"

int main() {
  VLOG_I("=== VLink Method Sync Example ===");

  vlink::MessageLoop server_loop;
  server_loop.set_name("server_loop");
  server_loop.async_run();

  // ---------------------------------------------------------------
  // Step 1: Create a Server with synchronous handler
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

  // ---------------------------------------------------------------
  // Step 2: Create a Client and wait for connection
  // ---------------------------------------------------------------
  vlink::Client<MathRequest, MathResponse> client("dds://math/calculator");
  VLOG_I("[Client] Created on dds://math/calculator");

  // wait_for_connected() blocks until the server is available.
  // Default timeout: Timeout::kDefaultInterval (5000ms)
  bool connected = client.wait_for_connected(2000ms);
  VLOG_I("[Client] wait_for_connected: ", connected);
  VLOG_I("[Client] is_connected: ", client.is_connected());

  // ---------------------------------------------------------------
  // Step 3: invoke(req, resp) -- output reference variant
  //
  // Blocks until the server responds.  Returns true on success.
  // The response is written into the resp output parameter.
  // ---------------------------------------------------------------
  VLOG_I("--- Mode 1: invoke(req, resp) ---");
  {
    MathRequest req{10.0, 3.0, 0};  // 10 + 3
    MathResponse resp{};
    bool ok = client.invoke(req, resp);
    VLOG_I("[Client] 10 + 3 = ", resp.result, " success=", resp.success, " ok=", ok);
  }
  {
    MathRequest req{100.0, 7.0, 1};  // 100 - 7
    MathResponse resp{};
    bool ok = client.invoke(req, resp);
    VLOG_I("[Client] 100 - 7 = ", resp.result, " success=", resp.success, " ok=", ok);
  }

  // ---------------------------------------------------------------
  // Step 4: invoke(req) -> optional<Resp>
  //
  // Returns std::optional<MathResponse>.
  // Returns std::nullopt on timeout or error.
  // ---------------------------------------------------------------
  VLOG_I("--- Mode 2: invoke(req) -> optional ---");
  {
    MathRequest req{6.0, 7.0, 2};  // 6 * 7
    auto result = client.invoke(req);

    if (result.has_value()) {
      VLOG_I("[Client] 6 * 7 = ", result->result, " success=", result->success);
    } else {
      VLOG_E("[Client] invoke returned nullopt");
    }
  }
  {
    MathRequest req{2.0, 10.0, 4};  // 2^10
    auto result = client.invoke(req);

    if (result.has_value()) {
      VLOG_I("[Client] 2^10 = ", result->result, " success=", result->success);
    }
  }

  // ---------------------------------------------------------------
  // Step 5: invoke with custom timeout
  //
  // The timeout parameter controls how long invoke() waits for the
  // server response. Default is Timeout::kDefaultInterval (5000ms).
  // ---------------------------------------------------------------
  VLOG_I("--- Mode 3: invoke with custom timeout ---");
  {
    MathRequest req{100.0, 0.0, 3};  // 100 / 0 (will fail gracefully)
    MathResponse resp{};
    bool ok = client.invoke(req, resp, 1000ms);
    VLOG_I("[Client] 100 / 0 = ", resp.result, " success=", resp.success, " ok=", ok);
  }
  {
    MathRequest req{15.0, 4.0, 3};  // 15 / 4
    auto result = client.invoke(req, 1000ms);

    if (result.has_value()) {
      VLOG_I("[Client] 15 / 4 = ", result->result, " success=", result->success);
    }
  }

  // ---------------------------------------------------------------
  // Step 6: Multiple sequential invocations
  // ---------------------------------------------------------------
  VLOG_I("--- Sequential invocations ---");
  for (int i = 1; i <= 5; ++i) {
    MathRequest req{static_cast<double>(i), static_cast<double>(i), 2};
    auto result = client.invoke(req);

    if (result.has_value()) {
      VLOG_I("[Client] ", i, " * ", i, " = ", result->result);
    }
  }

  // ---------------------------------------------------------------
  // Cleanup
  // ---------------------------------------------------------------
  VLOG_I("=== Example complete ===");
  server_loop.quit();
  server_loop.wait_for_quit();

  return 0;
}
