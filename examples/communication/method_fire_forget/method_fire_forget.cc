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
 * @file method_fire_forget.cc
 * @brief Fire-and-forget RPC: Server<Req> with no response, Client send().
 *
 * Demonstrates:
 *   - Server<Req> (no Resp type): listen(ReqCallback) for fire-and-forget
 *   - Client<Req> (no Resp type): send(req) -- non-blocking, no response
 *   - Difference from full RPC: send() returns immediately, server does not reply
 *   - Use case: logging, notifications, one-way commands
 */

#include <vlink/base/logger.h>
#include <vlink/vlink.h>

#include <atomic>
#include <string>
#include <thread>

using namespace std::chrono_literals;  // NOLINT(build/namespaces, google-build-using-namespace)

// POD types defined in log_types.h -- see that file for field descriptions
#include "log_types.h"

int main() {
  VLOG_I("=== VLink Method Fire-and-Forget Example ===");

  vlink::MessageLoop server_loop;
  server_loop.set_name("server_loop");
  server_loop.async_run();

  // ---------------------------------------------------------------
  // Section 1: Basic fire-and-forget with Server<Req>
  //
  // When RespT is omitted (defaults to Traits::EmptyType), the Server
  // only receives requests -- it never sends a response.
  // The listen callback receives (const Req&) with no Resp& parameter.
  // ---------------------------------------------------------------
  VLOG_I("--- Section 1: Basic fire-and-forget ---");

  vlink::Server<LogEntry> log_server("dds://logging/collector");
  log_server.attach(&server_loop);

  std::atomic<int> log_count{0};
  log_server.listen([&log_count](const LogEntry& entry) {
    const char* level_str[] = {"DEBUG", "INFO", "WARN", "ERROR"};
    const char* lvl = (entry.level >= 0 && entry.level <= 3) ? level_str[entry.level] : "?";
    VLOG_I("[LogServer] [", lvl, "] source=", entry.source_id, " ts=", entry.timestamp);
    log_count++;
  });

  VLOG_I("[LogServer] Listening on dds://logging/collector");

  // ---------------------------------------------------------------
  // Section 2: Client<Req> with send()
  //
  // When RespT is omitted, the Client uses send() instead of invoke().
  // send() dispatches the request and returns immediately.
  // Returns true if the transport accepted the message.
  // ---------------------------------------------------------------
  VLOG_I("--- Section 2: Client send() ---");

  vlink::Client<LogEntry> log_client("dds://logging/collector");
  log_client.wait_for_connected(2000ms);

  // Send multiple log entries -- fire and forget
  for (int i = 0; i < 5; ++i) {
    LogEntry entry{};  // Value-initialization: all members zeroed
    entry.level = i % 4;
    entry.source_id = 100 + i;
    entry.timestamp = static_cast<int64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch())
            .count());

    bool ok = log_client.send(entry);
    VLOG_I("[LogClient] Sent log entry #", i, " ok=", ok);
  }

  std::this_thread::sleep_for(200ms);
  VLOG_I("[LogServer] Total logs received: ", log_count.load());

  // ---------------------------------------------------------------
  // Section 3: Notification command pattern
  //
  // Fire-and-forget is ideal for one-way commands where the sender
  // does not need confirmation. Examples: trigger a state machine,
  // broadcast an alarm, issue a non-critical control command.
  // ---------------------------------------------------------------
  VLOG_I("--- Section 3: Notification commands ---");

  vlink::Server<NotifyCommand> notify_server("dds://control/notifications");
  notify_server.attach(&server_loop);

  std::atomic<int> cmd_count{0};
  notify_server.listen([&cmd_count](const NotifyCommand& cmd) {
    VLOG_I("[NotifyServer] cmd=", cmd.command_id, " target=", cmd.target_id, " payload=", cmd.payload);
    cmd_count++;
  });

  vlink::Client<NotifyCommand> notify_client("dds://control/notifications");
  notify_client.wait_for_connected(2000ms);

  // Send a burst of notification commands
  NotifyCommand commands[] = {
      {1, 10, 100},  // Start motor
      {2, 10, 50},   // Set speed
      {3, 20, 0},    // Reset sensor
      {4, 30, 1},    // Enable logging
      {5, 10, 0},    // Stop motor
  };

  for (const auto& cmd : commands) {
    bool ok = notify_client.send(cmd);
    VLOG_I("[NotifyClient] Sent cmd=", cmd.command_id, " ok=", ok);
  }

  std::this_thread::sleep_for(200ms);
  VLOG_I("[NotifyServer] Total commands received: ", cmd_count.load());

  // ---------------------------------------------------------------
  // Section 4: High-throughput fire-and-forget
  //
  // send() is very fast because it does not wait for a response.
  // This makes it suitable for high-frequency telemetry or logging.
  // ---------------------------------------------------------------
  VLOG_I("--- Section 4: High-throughput send ---");

  std::atomic<int> burst_count{0};
  vlink::Server<LogEntry> burst_server("dds://telemetry/burst");
  burst_server.attach(&server_loop);
  burst_server.listen([&burst_count](const LogEntry&) { burst_count++; });

  vlink::Client<LogEntry> burst_client("dds://telemetry/burst");
  burst_client.wait_for_connected(2000ms);

  constexpr int kBurstSize = 100;
  int send_ok = 0;
  for (int i = 0; i < kBurstSize; ++i) {
    LogEntry entry{1, i, static_cast<int64_t>(i)};

    if (burst_client.send(entry)) {
      send_ok++;
    }
  }

  std::this_thread::sleep_for(500ms);
  VLOG_I("[Burst] Sent: ", send_ok, "/", kBurstSize);
  VLOG_I("[Burst] Received: ", burst_count.load(), "/", kBurstSize);

  // ---------------------------------------------------------------
  // Cleanup
  // ---------------------------------------------------------------
  VLOG_I("=== Example complete ===");
  server_loop.quit();
  server_loop.wait_for_quit();

  return 0;
}
