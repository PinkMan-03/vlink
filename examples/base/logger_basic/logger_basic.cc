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

// Example: Logger basic usage - initialization, 4 styles, all levels, level control

#include <vlink/base/logger.h>

int main() {
  // 1. Initialize the logger with an application name and optional log file path.
  vlink::Logger::init("logger_basic_demo", "/tmp/vlink_logger_basic.log");

  // 2. Set console and file output levels.
  //    Messages below this level will be filtered out for the respective sink.
  vlink::Logger::set_console_level(vlink::Logger::kTrace);
  vlink::Logger::set_file_level(vlink::Logger::kInfo);

  // ---------------------------------------------------------------
  // 3. Stream style (VLOG_*) - uses operator<< composition via FastStream.
  //    Zero heap allocation; ideal for simple concatenation.
  // ---------------------------------------------------------------
  VLOG_T("Stream style [Trace]: counter=", 0, " name=", "alpha");
  VLOG_D("Stream style [Debug]: temperature=", 23.5, " unit=C");
  VLOG_I("Stream style [Info]: application started successfully");
  VLOG_W("Stream style [Warn]: disk usage is high, used=", 91, "%");
  VLOG_E("Stream style [Error]: failed to open config file");

  // ---------------------------------------------------------------
  // 4. Format style (MLOG_*) - uses Python-style {} placeholders.
  //    Powered by vlink::format (similar to fmt/std::format).
  // ---------------------------------------------------------------
  MLOG_T("Format style [Trace]: value={}, label={}", 42, "beta");
  MLOG_D("Format style [Debug]: elapsed={}ms", 150);
  MLOG_I("Format style [Info]: connected to host={}, port={}", "192.168.1.1", 8080);
  MLOG_W("Format style [Warn]: retry attempt {}/{}", 3, 5);
  MLOG_E("Format style [Error]: timeout after {}ms", 5000);

  // ---------------------------------------------------------------
  // 5. C style (CLOG_*) - uses printf-style %d/%s format specifiers.
  //    Calls std::snprintf internally.
  // ---------------------------------------------------------------
  CLOG_T("C style [Trace]: index=%d, tag=%s", 7, "gamma");
  CLOG_D("C style [Debug]: ratio=%.2f", 3.14);
  CLOG_I("C style [Info]: PID=%d started", 12345);
  CLOG_W("C style [Warn]: memory usage=%d%%", 85);
  CLOG_E("C style [Error]: errno=%d (%s)", 2, "No such file or directory");

  // ---------------------------------------------------------------
  // 6. RAII stream style (SLOG_*) - uses WrapperStream with operator<<.
  //    The message is flushed when the temporary object is destroyed.
  // ---------------------------------------------------------------
  SLOG_T << "RAII stream [Trace]: id=" << 100 << " status=ok";
  SLOG_D << "RAII stream [Debug]: x=" << 1.5 << " y=" << 2.5;
  SLOG_I << "RAII stream [Info]: initialization complete";
  SLOG_W << "RAII stream [Warn]: connection unstable";
  SLOG_E << "RAII stream [Error]: data corruption detected";

  // ---------------------------------------------------------------
  // 7. Demonstrate dynamic level switching at runtime.
  //    Raise console level to kWarn so Trace/Debug/Info are suppressed.
  // ---------------------------------------------------------------
  VLOG_I("--- Raising console level to kWarn ---");
  vlink::Logger::set_console_level(vlink::Logger::kWarn);

  VLOG_D("This Debug message will NOT appear on console");
  VLOG_I("This Info message will NOT appear on console");
  VLOG_W("This Warn message WILL appear on console");
  VLOG_E("This Error message WILL appear on console");

  // Restore console level to kTrace for subsequent messages.
  vlink::Logger::set_console_level(vlink::Logger::kTrace);
  VLOG_I("Console level restored to kTrace");

  // ---------------------------------------------------------------
  // 8. Demonstrate set_file_level to control file output granularity.
  // ---------------------------------------------------------------
  vlink::Logger::set_file_level(vlink::Logger::kError);
  VLOG_I("This Info message goes to console but NOT to file");
  VLOG_E("This Error message goes to BOTH console and file");

  // ---------------------------------------------------------------
  // 9. Flush the logger to ensure all buffered messages are written.
  // ---------------------------------------------------------------
  vlink::Logger::flush();

  VLOG_I("Logger basic example finished.");

  return 0;
}
