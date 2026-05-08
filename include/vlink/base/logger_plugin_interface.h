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
 * @file logger_plugin_interface.h
 * @brief Abstract interface for pluggable logger backends loaded via the Plugin system.
 *
 * @details
 * @c LoggerPluginInterface defines the contract that every custom logger plugin must implement.
 * A plugin is a shared library (.so / .dll) that exports a factory function registered via
 * the @c VLINK_PLUGIN_REGISTER macro and compiled with @c VLINK_PLUGIN_DECLARE.
 *
 * @par Plugin registration
 * Each implementing class must embed @c VLINK_PLUGIN_REGISTER(LoggerPluginInterface) and
 * provide a corresponding @c VLINK_PLUGIN_DECLARE in its translation unit so the @c Plugin
 * loader can create and destroy instances dynamically.
 *
 * @par Loading a logger plugin
 * @code
 * vlink::Plugin plugin;
 * auto logger_backend = plugin.load<vlink::LoggerPluginInterface>("my_logger_plugin", 1, 0);
 *
 * if (logger_backend) {
 *     logger_backend->init("my_app");
 *     logger_backend->log(vlink::Logger::kInfo, "Hello from plugin!");
 * }
 * @endcode
 *
 * @par Implementing a logger plugin
 * @code
 * class MyLogger : public vlink::LoggerPluginInterface {
 * public:
 *     bool init(std::string_view app_name) override {
 *         // initialise your logging backend
 *         return true;
 *     }
 *     bool log(int level, std::string_view str) override {
 *         // forward to your backend
 *         return true;
 *     }
 * };
 * VLINK_PLUGIN_DECLARE(MyLogger, 1, 0)
 * @endcode
 *
 * @note
 * - The @p level parameter passed to @c log() corresponds to @c vlink::Logger::Level values.
 * - Both methods should be implemented to be non-throwing; exceptions escaping a plugin
 *   boundary may cause undefined behaviour.
 *
 * @see Plugin, Logger
 */

#pragma once

#include <string_view>

#include "./plugin.h"

namespace vlink {

/**
 * @class LoggerPluginInterface
 * @brief Pure-virtual interface for a custom logger backend loaded as a dynamic plugin.
 *
 * @details
 * Concrete implementations are loaded at runtime by @c Plugin::load<LoggerPluginInterface>().
 * The @c VLINK_PLUGIN_REGISTER macro inside the class body wires up the factory/destroy
 * functions that @c Plugin uses to manage the lifetime of the plugin instance.
 */
class LoggerPluginInterface {
  VLINK_PLUGIN_REGISTER(LoggerPluginInterface)

 protected:
  LoggerPluginInterface() = default;

  virtual ~LoggerPluginInterface() = default;

 public:
  /**
   * @brief Initialises the logger backend for the given application name.
   *
   * @details
   * Called once by the host application after the plugin is loaded.  The @p app_name
   * string may be used to label log entries or configure a log file path.
   *
   * @param app_name  Name of the calling application.
   * @return @c true on success; @c false if initialisation failed and the plugin should
   *         not be used.
   */
  virtual bool init(std::string_view app_name) = 0;

  /**
   * @brief Writes a single log entry to the backend.
   *
   * @details
   * Called from @c vlink::Logger internals whenever a log message passes the current
   * log level filter.  Implementations should be non-blocking to avoid stalling the
   * caller thread.
   *
   * @param level  Log severity level.  Corresponds to @c vlink::Logger::Level values
   *               (e.g., @c kDebug, @c kInfo, @c kWarn, @c kError).
   * @param str    The fully-formatted log message string.
   * @return @c true if the message was written successfully; @c false on error.
   */
  virtual bool log(int level, std::string_view str) = 0;

 private:
  VLINK_DISALLOW_COPY_AND_ASSIGN(LoggerPluginInterface)
};

}  // namespace vlink
