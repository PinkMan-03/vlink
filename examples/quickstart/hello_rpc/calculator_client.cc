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
 * @file calculator_client.cc
 * @brief VLink Method Model -- Client demonstrating all 5 invocation modes.
 *
 * The 5 invocation modes:
 *   1. invoke(req, resp)         -- Synchronous, fills resp reference.
 *   2. invoke(req) -> optional   -- Synchronous, returns optional<Resp>.
 *   3. invoke(req, callback)     -- Asynchronous, callback-based.
 *   4. async_invoke(req)         -- Asynchronous, future-based.
 *   5. send(req)                 -- Fire-and-forget, no response.
 *
 * For intra:// transport, the server must be in the same process.
 * For cross-process usage, switch to shm:// or dds://.
 */

#include <vlink/base/logger.h>
#include <vlink/vlink.h>

#include <atomic>
#include <string>
#include <thread>

#include "./calculator_types.h"

int main() {
  // ---------------------------------------------------------------
  // Step 1: Initialise logger
  // ---------------------------------------------------------------
  VLOG_I("=== VLink Calculator Client ===");

  // ---------------------------------------------------------------
  // Step 2: Create a Client for the calculator service
  // ---------------------------------------------------------------
  vlink::Client<example::CalcRequest, example::CalcResponse> client(example::kCalculatorUrl);
  VLOG_I("[Client] Connected to ", example::kCalculatorUrl);

  // Small delay to ensure server is ready.
  std::this_thread::sleep_for(std::chrono::milliseconds(100));

  // ---------------------------------------------------------------
  // Mode 1: Synchronous invoke with output reference
  //
  // invoke(req, resp) blocks until the server responds.
  // Returns true if the response was received successfully.
  // ---------------------------------------------------------------
  VLOG_I("--- Mode 1: invoke(req, resp) ---");
  {
    example::CalcRequest req{10, 3, '+'};
    example::CalcResponse resp{};
    bool ok = client.invoke(req, resp);
    if (ok) {
      VLOG_I("[Client] 10 + 3 = ", resp.result);
    } else {
      VLOG_E("[Client] invoke failed");
    }
  }

  // ---------------------------------------------------------------
  // Mode 2: Synchronous invoke returning std::optional
  //
  // invoke(req) returns std::optional<CalcResponse>.
  // Returns std::nullopt on timeout or error.
  // ---------------------------------------------------------------
  VLOG_I("--- Mode 2: invoke(req) -> optional ---");
  {
    example::CalcRequest req{20, 4, '*'};
    auto result = client.invoke(req);
    if (result.has_value()) {
      VLOG_I("[Client] 20 * 4 = ", result->result);
    } else {
      VLOG_E("[Client] invoke returned nullopt");
    }
  }

  // ---------------------------------------------------------------
  // Mode 3: Asynchronous invoke with callback
  //
  // invoke(req, callback) returns immediately.  The callback is
  // called when the response arrives.
  // ---------------------------------------------------------------
  VLOG_I("--- Mode 3: invoke(req, callback) ---");
  {
    example::CalcRequest req{15, 7, '-'};
    std::atomic<bool> callback_done{false};

    client.invoke(req, [&callback_done](const example::CalcResponse& resp) {
      VLOG_I("[Client] 15 - 7 = ", resp.result, " (via callback)");
      callback_done = true;
    });

    // Wait for the callback to fire.
    for (int i = 0; i < 50 && !callback_done; ++i) {
      std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
  }

  // ---------------------------------------------------------------
  // Mode 4: Asynchronous invoke with std::future
  //
  // async_invoke(req) returns a std::future<CalcResponse>.
  // Call future.get() to block until the response is ready.
  // ---------------------------------------------------------------
  VLOG_I("--- Mode 4: async_invoke(req) -> future ---");
  {
    example::CalcRequest req{100, 5, '/'};
    auto future = client.async_invoke(req);

    // future.get() blocks until the response arrives.
    example::CalcResponse resp = future.get();
    VLOG_I("[Client] 100 / 5 = ", resp.result, " (via future)");
  }

  // ---------------------------------------------------------------
  // Mode 5: Fire-and-forget send (no response expected)
  //
  // Uses a separate Client<ReqT> (without RespT) and the send()
  // method.  The server uses Server<ReqT> (without RespT) with
  // a listen(ReqCallback) handler.
  // ---------------------------------------------------------------
  VLOG_I("--- Mode 5: send(req) fire-and-forget ---");
  {
    vlink::Client<example::CalcRequest> notify_client(example::kNotifyUrl);

    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    example::CalcRequest req{42, 0, '!'};
    bool ok = notify_client.send(req);
    VLOG_I("[Client] Fire-and-forget send: ", ok ? "accepted" : "failed");

    std::this_thread::sleep_for(std::chrono::milliseconds(100));
  }

  // ---------------------------------------------------------------
  // Done
  // ---------------------------------------------------------------
  VLOG_I("=== Calculator Client complete ===");

  return 0;
}
