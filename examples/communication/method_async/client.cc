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
 * @brief Client side of the method_async example.
 *
 * Demonstrates:
 *   - invoke(req, callback): non-blocking, callback on response
 *   - async_invoke(req) -> future<Resp>: non-blocking, future-based
 *   - detect_connected(callback): async server connection notification
 *   - Mixed sync + async usage
 *
 * Run this alongside server.cc for cross-process async RPC.
 */

#include <vlink/base/logger.h>
#include <vlink/vlink.h>

#include <atomic>
#include <future>
#include <string>
#include <thread>
#include <vector>

#include "translate_types.h"

using namespace std::chrono_literals;  // NOLINT(build/namespaces, google-build-using-namespace)

int main() {
  VLOG_I("=== VLink Method Async Client ===");

  // ---------------------------------------------------------------
  // Section 1: detect_connected
  // ---------------------------------------------------------------
  VLOG_I("--- Section 1: detect_connected ---");

  vlink::Client<TranslateRequest, TranslateResponse> client("zenoh://translate/service");

  std::atomic<bool> server_ready{false};
  client.detect_connected([&server_ready](bool connected) {
    VLOG_I("[Client] Server connection: ", connected);
    server_ready = connected;
  });

  client.wait_for_connected(10000ms);

  if (!client.is_connected()) {
    VLOG_E("[Client] Server not found, exiting.");
    return 1;
  }

  // ---------------------------------------------------------------
  // Section 2: Callback-based invoke
  // ---------------------------------------------------------------
  VLOG_I("--- Section 2: invoke(req, callback) ---");

  std::atomic<int> callback_done{0};

  for (int i = 1; i <= 3; ++i) {
    TranslateRequest req{i, i % 3};
    client.invoke(req, [i, &callback_done](const TranslateResponse& resp) {
      VLOG_I("[Client] Callback #", i, ": word_id=", resp.word_id, " lang=", resp.target_lang,
             " code=", resp.result_code);
      callback_done++;
    });
    VLOG_I("[Client] invoke #", i, " dispatched (non-blocking)");
  }

  // Wait for all callbacks to complete
  for (int wait = 0; wait < 50 && callback_done < 3; ++wait) {
    std::this_thread::sleep_for(20ms);
  }
  VLOG_I("[Client] Callbacks completed: ", callback_done.load(), "/3");

  // ---------------------------------------------------------------
  // Section 3: Future-based async_invoke
  // ---------------------------------------------------------------
  VLOG_I("--- Section 3: async_invoke -> future ---");

  std::vector<std::future<TranslateResponse>> futures;
  for (int i = 10; i <= 14; ++i) {
    TranslateRequest req{i, 1};
    futures.push_back(client.async_invoke(req));
    VLOG_I("[Client] async_invoke word_id=", i, " dispatched");
  }

  // Collect all results
  for (size_t i = 0; i < futures.size(); ++i) {
    TranslateResponse resp = futures[i].get();
    VLOG_I("[Client] Future #", i, ": word_id=", resp.word_id, " lang=", resp.target_lang, " code=", resp.result_code);
  }

  // ---------------------------------------------------------------
  // Section 4: Mixed sync + async
  // ---------------------------------------------------------------
  VLOG_I("--- Section 4: Mixed sync + async ---");

  // Synchronous call
  TranslateRequest sync_req{50, 2};
  auto sync_result = client.invoke(sync_req);
  if (sync_result.has_value()) {
    VLOG_I("[Client] Sync result: word_id=", sync_result->word_id, " code=", sync_result->result_code);
  }

  // Immediately followed by async call
  auto future = client.async_invoke(TranslateRequest{51, 0});
  VLOG_I("[Client] Async invoke dispatched, waiting for result...");
  TranslateResponse async_resp = future.get();
  VLOG_I("[Client] Async result: word_id=", async_resp.word_id, " code=", async_resp.result_code);

  VLOG_I("=== Client complete ===");

  return 0;
}
