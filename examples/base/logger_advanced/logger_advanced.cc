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

// Example: Logger advanced - custom handler, backtrace, fatal, compile-time filtering

#include <vlink/base/logger.h>

#include <stdexcept>
#include <string>

#include "custom_handler.h"

int main() {
  vlink::Logger::init("logger_advanced_demo");
  vlink::Logger::set_console_level(vlink::Logger::kTrace);

  // ---------------------------------------------------------------
  // 1. Register a custom console handler via CustomLogHandler class.
  // ---------------------------------------------------------------
  custom_handler::CustomLogHandler handler;
  handler.install();

  VLOG_I("Custom handler installed - this message is routed through it");
  MLOG_D("Captured log count so far: {}", handler.captured_count());

  // ---------------------------------------------------------------
  // 2. Enable backtrace ring buffer.
  //    Retains the last N messages regardless of current log level.
  //    Useful for post-mortem debugging.
  // ---------------------------------------------------------------
  vlink::Logger::enable_backtrace(10);

  VLOG_T("Backtrace message 1: system initializing");
  VLOG_D("Backtrace message 2: loading configuration");
  VLOG_I("Backtrace message 3: configuration loaded successfully");
  VLOG_D("Backtrace message 4: connecting to database");
  VLOG_I("Backtrace message 5: database connection established");

  // Dump the backtrace buffer to sinks.
  VLOG_W("About to dump backtrace...");
  vlink::Logger::dump_backtrace();
  VLOG_I("Backtrace dumped successfully");

  vlink::Logger::disable_backtrace();

  // ---------------------------------------------------------------
  // 3. Demonstrate that kFatal level throws RuntimeError.
  //    In production, this is used for unrecoverable errors.
  // ---------------------------------------------------------------
  VLOG_I("--- Demonstrating Fatal log (will throw RuntimeError) ---");

  try {
    VLOG_F("Fatal error: critical subsystem failure, code=", 0xDEAD);
  } catch (const std::runtime_error& e) {
    VLOG_I("Caught RuntimeError from VLOG_F: ", e.what());
  }

  try {
    MLOG_F("Fatal format: operation {} failed with status {}", "connect", -1);
  } catch (const std::runtime_error& e) {
    VLOG_I("Caught RuntimeError from MLOG_F: ", e.what());
  }

  // ---------------------------------------------------------------
  // 4. Compile-time filtering explanation.
  // ---------------------------------------------------------------
  MLOG_I("Compile-time minimum log level: {}", static_cast<int>(vlink::Logger::kMinimumLevel));
  MLOG_I("Detail annotation threshold: {}", static_cast<int>(vlink::Logger::kDetailLevel));

  VLOG_W("This warning includes {file:line} detail automatically");
  VLOG_E("This error also includes {file:line} detail");

  // ---------------------------------------------------------------
  // 5. Check writability before expensive argument construction.
  // ---------------------------------------------------------------
  if (vlink::Logger::is_writable(vlink::Logger::kDebug)) {
    std::string expensive_data = "computed-only-when-debug-is-active";
    VLOG_D("Expensive debug data: ", expensive_data);
  }

  // ---------------------------------------------------------------
  // 6. Console formatting control.
  // ---------------------------------------------------------------
  vlink::Logger::set_console_fmt_enable(false);
  VLOG_I("ANSI color codes disabled for this message");

  vlink::Logger::set_console_fmt_enable(true);
  VLOG_I("ANSI color codes re-enabled");

  // ---------------------------------------------------------------
  // 7. Stream formatting flags.
  // ---------------------------------------------------------------
  vlink::Logger::set_stream_precision(4);
  VLOG_I("Stream precision set to 4: pi=", 3.14159265);

  vlink::Logger::set_stream_precision(2);
  VLOG_I("Stream precision set to 2: pi=", 3.14159265);

  // ---------------------------------------------------------------
  // Summary of captured logs from custom handler.
  // ---------------------------------------------------------------
  MLOG_I("Total messages captured by custom handler: {}", handler.captured_count());

  vlink::Logger::flush();
  VLOG_I("Logger advanced example finished.");

  return 0;
}
