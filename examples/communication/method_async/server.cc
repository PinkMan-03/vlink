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
 * @brief Server side of the method_async example.
 *
 * Demonstrates:
 *   - listen_for_reply(ReqAsyncRespCallback): deferred reply via reply(req_id, resp)
 *
 * Run this alongside client.cc for cross-process async RPC.
 */

#include <vlink/base/logger.h>
#include <vlink/vlink.h>

#include <atomic>
#include <thread>

#include "translate_types.h"

using namespace std::chrono_literals;  // NOLINT(build/namespaces, google-build-using-namespace)

int main() {
  VLOG_I("=== VLink Method Async Server ===");

  std::atomic<bool> running{true};
  vlink::Utils::register_terminate_signal([&running](int sig) {
    VLOG_I("Signal ", sig, " received, shutting down...");
    running = false;
  });

  vlink::MessageLoop server_loop;
  server_loop.set_name("server_loop");
  server_loop.async_run();

  // ---------------------------------------------------------------
  // Server with listen_for_reply (deferred response)
  //
  // listen_for_reply(callback) receives (uint64_t req_id, const Req&).
  // The response is sent later via server.reply(req_id, resp).
  // ---------------------------------------------------------------
  vlink::Server<TranslateRequest, TranslateResponse> server("zenoh://translate/service");
  server.attach(&server_loop);

  server.listen_for_reply([&server](uint64_t req_id, const TranslateRequest& req) {
    VLOG_I("[Server] Received request: word_id=", req.word_id, " lang=", req.target_lang, " req_id=", req_id);

    // Simulate async processing
    TranslateResponse resp{};  // Value-initialization: all members zeroed
    resp.word_id = req.word_id;
    resp.target_lang = req.target_lang;
    resp.result_code = (req.word_id > 0 && req.word_id <= 100) ? 0 : 1;

    // Send the deferred reply
    bool ok = server.reply(req_id, resp);
    VLOG_I("[Server] Replied to req_id=", req_id, " ok=", ok);
  });

  VLOG_I("[Server] Listening with deferred reply on zenoh://translate/service");
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
