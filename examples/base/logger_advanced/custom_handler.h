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

#pragma once

/**
 * @file custom_handler.h
 * @brief A custom log handler class that intercepts log records for
 *        custom processing (e.g., remote telemetry, buffered storage).
 *
 * This class demonstrates how to use Logger::register_console_handler()
 * to redirect all log output through a user-defined callback.
 */

#include <vlink/base/logger.h>

#include <iostream>
#include <mutex>
#include <string>
#include <vector>

namespace custom_handler {

// CustomLogHandler captures all log records into a vector and optionally
// re-prints them with a user-defined prefix.  In production code, this
// pattern can be extended to send logs to a remote server, write to a
// ring buffer, or integrate with a third-party logging framework.
class CustomLogHandler {
 public:
  // Install this handler by registering it with the Logger singleton.
  void install() {
    vlink::Logger::register_console_handler(
        [this](vlink::Logger::Level level, std::string_view message) { handle(level, message); });
  }

  // Return the number of captured log records.
  size_t captured_count() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return captured_logs_.size();
  }

  // Return a copy of all captured log messages.
  std::vector<std::string> captured_logs() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return captured_logs_;
  }

 private:
  void handle(vlink::Logger::Level level, std::string_view message) {
    {
      std::lock_guard<std::mutex> lock(mutex_);
      captured_logs_.emplace_back(message);
    }

    const char* prefix = "UNKNOWN";
    switch (level) {
      case vlink::Logger::kTrace:
        prefix = "TRACE";
        break;
      case vlink::Logger::kDebug:
        prefix = "DEBUG";
        break;
      case vlink::Logger::kInfo:
        prefix = "INFO ";
        break;
      case vlink::Logger::kWarn:
        prefix = "WARN ";
        break;
      case vlink::Logger::kError:
        prefix = "ERROR";
        break;
      case vlink::Logger::kFatal:
        prefix = "FATAL";
        break;
      default:
        break;
    }
    std::cout << "[CustomHandler][" << prefix << "] " << message << std::endl;
  }

  mutable std::mutex mutex_;
  std::vector<std::string> captured_logs_;
};

}  // namespace custom_handler
