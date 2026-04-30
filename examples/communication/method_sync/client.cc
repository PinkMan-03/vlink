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
 * @file client.cc
 * @brief Client side of the method_sync example.
 *
 * Demonstrates synchronous RPC invocation:
 *   - invoke(req, resp): blocking, output reference
 *   - invoke(req) -> optional<Resp>: blocking, returns optional
 *   - invoke with custom timeout
 *
 * Run this alongside server.cc for cross-process RPC.
 */

#include <vlink/base/logger.h>
#include <vlink/vlink.h>

#include <string>
#include <thread>

#include "math_types.h"

using namespace std::chrono_literals;  // NOLINT(build/namespaces, google-build-using-namespace)

int main() {
  VLOG_I("=== VLink Method Sync Client ===");

  // ---------------------------------------------------------------
  // Create a Client and wait for connection
  // ---------------------------------------------------------------
  vlink::Client<MathRequest, MathResponse> client("dds://math/calculator");
  VLOG_I("[Client] Created on dds://math/calculator");

  bool connected = client.wait_for_connected(10000ms);
  VLOG_I("[Client] wait_for_connected: ", connected);
  VLOG_I("[Client] is_connected: ", client.is_connected());

  if (!connected) {
    VLOG_E("[Client] Server not found, exiting.");
    return 1;
  }

  // ---------------------------------------------------------------
  // Mode 1: invoke(req, resp) -- output reference variant
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
  // Mode 2: invoke(req) -> optional<Resp>
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
  // Mode 3: invoke with custom timeout
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
  // Sequential invocations
  // ---------------------------------------------------------------
  VLOG_I("--- Sequential invocations ---");
  for (int i = 1; i <= 5; ++i) {
    MathRequest req{static_cast<double>(i), static_cast<double>(i), 2};
    auto result = client.invoke(req);
    if (result.has_value()) {
      VLOG_I("[Client] ", i, " * ", i, " = ", result->result);
    }
  }

  VLOG_I("=== Client complete ===");

  return 0;
}
