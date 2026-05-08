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
 * @file method_async.cc
 * @brief Asynchronous RPC: listen_for_reply + reply, async_invoke, callback invoke.
 *
 * Demonstrates:
 *   - Server: listen_for_reply(ReqAsyncRespCallback) -- deferred reply via reply(req_id, resp)
 *   - Client: invoke(req, callback) -- non-blocking, callback on response
 *   - Client: async_invoke(req) -> future<Resp> -- non-blocking, future-based
 *   - detect_connected(callback) -- async server connection notification
 */

#include <vlink/base/logger.h>
#include <vlink/vlink.h>

#include <atomic>
#include <future>
#include <string>
#include <thread>
#include <vector>

using namespace std::chrono_literals;  // NOLINT(build/namespaces, google-build-using-namespace)

// POD types defined in translate_types.h -- see that file for field descriptions
#include "translate_types.h"

int main() {
  VLOG_I("=== VLink Method Async Example ===");

  vlink::MessageLoop server_loop;
  server_loop.set_name("server_loop");
  server_loop.async_run();

  // ---------------------------------------------------------------
  // Section 1: Server with listen_for_reply (deferred response)
  //
  // listen_for_reply(callback) receives (uint64_t req_id, const Req&).
  // The callback saves the req_id and processes the request.
  // The response is sent later via server.reply(req_id, resp).
  // This pattern is useful when the handler needs to:
  //   - Perform async I/O before responding
  //   - Aggregate results from multiple sources
  //   - Delay the response based on external conditions
  // ---------------------------------------------------------------
  VLOG_I("--- Section 1: listen_for_reply ---");

  vlink::Server<TranslateRequest, TranslateResponse> server("zenoh://translate/service");
  server.attach(&server_loop);

  // In a real application, requests might be queued and processed
  // by a separate thread pool. Here we reply immediately but via the
  // deferred path to demonstrate the API.
  server.listen_for_reply([&server](uint64_t req_id, const TranslateRequest& req) {
    VLOG_I("[Server] Received request: word_id=", req.word_id, " lang=", req.target_lang, " req_id=", req_id);

    // Simulate async processing (in production, this could be a database lookup)
    TranslateResponse resp{};  // Value-initialization: all members zeroed
    resp.word_id = req.word_id;
    resp.target_lang = req.target_lang;
    resp.result_code = (req.word_id > 0 && req.word_id <= 100) ? 0 : 1;

    // Send the deferred reply
    bool ok = server.reply(req_id, resp);
    VLOG_I("[Server] Replied to req_id=", req_id, " ok=", ok);
  });

  VLOG_I("[Server] Listening with deferred reply on zenoh://translate/service");

  // ---------------------------------------------------------------
  // Section 2: Client with detect_connected
  //
  // detect_connected(callback) fires asynchronously when the server
  // connection state changes. If already connected, it fires immediately.
  // ---------------------------------------------------------------
  VLOG_I("--- Section 2: detect_connected ---");

  vlink::Client<TranslateRequest, TranslateResponse> client("zenoh://translate/service");

  std::atomic<bool> server_ready{false};
  client.detect_connected([&server_ready](bool connected) {
    VLOG_I("[Client] Server connection: ", connected);
    server_ready = connected;
  });

  client.wait_for_connected(2000ms);

  // ---------------------------------------------------------------
  // Section 3: Callback-based invoke
  //
  // invoke(req, callback) returns immediately. The callback is invoked
  // on the transport/loop thread when the response arrives.
  // ---------------------------------------------------------------
  VLOG_I("--- Section 3: invoke(req, callback) ---");

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
  // Section 4: Future-based async_invoke
  //
  // async_invoke(req) returns std::future<Resp>. Call future.get()
  // to block until the response is ready.  This pattern integrates
  // well with C++ async workflows.
  // ---------------------------------------------------------------
  VLOG_I("--- Section 4: async_invoke -> future ---");

  // Launch multiple async invocations
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
  // Section 5: Mixed usage -- combine sync and async
  // ---------------------------------------------------------------
  VLOG_I("--- Section 5: Mixed sync + async ---");

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

  // ---------------------------------------------------------------
  // Cleanup
  // ---------------------------------------------------------------
  VLOG_I("=== Example complete ===");
  server_loop.quit();
  server_loop.wait_for_quit();

  return 0;
}
