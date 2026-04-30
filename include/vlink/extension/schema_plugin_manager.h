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
 * @file schema_plugin_manager.h
 * @brief Process-global singleton manager for the @c SchemaPluginInterface dynamic plugin.
 */

#pragma once

#include <memory>
#include <string>

#include "./schema_plugin_interface.h"

namespace vlink {

/**
 * @class SchemaPluginManager
 * @brief Singleton manager that owns and provides access to the @c SchemaPluginInterface.
 *
 * @details
 * @c SchemaPluginManager is a process-level singleton that loads and holds a single
 * @c SchemaPluginInterface plugin. The plugin path is resolved in the following order:
 * 1. The @p schema_plugin_path argument passed to @c get() on first call.
 * 2. The @c VLINK_SCHEMA_PLUGIN environment variable if @p schema_plugin_path is empty.
 * 3. No plugin loaded (the manager is invalid) if neither is set.
 *
 * @par Usage
 * @code
 * // Load by environment variable VLINK_SCHEMA_PLUGIN:
 * auto& mgr = vlink::SchemaPluginManager::get();
 *
 * // Or explicitly (first call wins):
 * auto& mgr = vlink::SchemaPluginManager::get("/path/to/my_schema_plugin.so");
 *
 * if (mgr.is_valid()) {
 *     auto iface = mgr.get_interface();
 *     auto schema = iface->search_schema("my_pkg.MyMessage", SchemaType::kProtobuf);
 * }
 * @endcode
 *
 * @note
 * - @c get() creates the singleton on first call; subsequent calls ignore @p schema_plugin_path.
 * - @c is_valid() returns @c false when no plugin was loaded.
 * - The plugin interface is released before the @c Plugin loader on destruction, ensuring
 *   safe unloading.
 */
class VLINK_EXPORT SchemaPluginManager final {
 public:
  /**
   * @brief Returns the process-global @c SchemaPluginManager singleton.
   *
   * @details
   * On the first call, loads the plugin from @p schema_plugin_path if non-empty,
   * or from the @c VLINK_SCHEMA_PLUGIN environment variable. Subsequent calls
   * return the same singleton regardless of @p schema_plugin_path.
   *
   * @param schema_plugin_path  Path to the plugin shared library. Empty = use env var.
   * @return Reference to the singleton.
   */
  [[nodiscard]] static SchemaPluginManager& get(const std::string& schema_plugin_path = "");

  /**
   * @brief Returns @c true if a @c SchemaPluginInterface was successfully loaded.
   *
   * @return @c true if @c get_interface() will return a non-null pointer.
   */
  [[nodiscard]] bool is_valid() const;

  /**
   * @brief Returns the loaded @c SchemaPluginInterface instance.
   *
   * @return Shared pointer to the interface, or @c nullptr if not loaded.
   */
  [[nodiscard]] std::shared_ptr<SchemaPluginInterface> get_interface() const;

 private:
  explicit SchemaPluginManager(std::string schema_plugin_path);

  ~SchemaPluginManager();

  std::unique_ptr<struct SchemaPluginManagerImpl> impl_;

  VLINK_DISALLOW_COPY_AND_ASSIGN(SchemaPluginManager)
};
}  // namespace vlink
