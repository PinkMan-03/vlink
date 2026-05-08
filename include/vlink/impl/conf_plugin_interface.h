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
 * @file conf_plugin_interface.h
 * @brief Plugin ABI for dynamically loaded transport @c Conf factories.
 *
 * @details
 * @c ConfPluginInterface is the abstract C++ interface that every externally loaded
 * VLink transport plugin must implement.  Plugins are shared libraries discovered at
 * runtime via the @c VLINK_URL_PLUGINS environment variable (or the
 * @c Url::init_plugins() call).
 *
 * @par Plugin Discovery and Loading
 * When a @c Url is constructed and the built-in transport table does not recognise the
 * transport field in the URL, @c Url::load_for_plugin() iterates over all loaded plugins and calls
 * @c get_transport_type() on each to find a matching transport.  If found,
 * @c create() is called to obtain a fresh @c Conf instance for that transport.
 *
 * @par Implementing a Plugin
 * @code
 *   // In your plugin shared library:
 *   struct MyTransportPlugin final : public vlink::ConfPluginInterface {
 *     VLINK_PLUGIN_REGISTER(ConfPluginInterface)
 *     vlink::TransportType get_transport_type() const override {
 *       return vlink::TransportType::kMyCustomTransport;
 *     }
 *     std::unique_ptr<vlink::Conf> create() const override {
 *       return std::make_unique<MyTransportConf>();
 *     }
 *   };
 *   VLINK_PLUGIN_DECLARE(MyTransportPlugin, 1, 0)
 * @endcode
 *
 * @note The @c VLINK_PLUGIN_DECLARE macro (from @c base/plugin.h) exports the
 *       create/destroy entry points needed for the plugin loader to locate and
 *       instantiate this type.
 *
 * @note Implementations must be stateless; @c create() may be called multiple times
 *       to produce independent @c Conf objects for different @c Url instances.
 */

#pragma once

#include <memory>

#include "../base/plugin.h"
#include "./conf.h"

namespace vlink {

/**
 * @struct ConfPluginInterface
 * @brief Pure-virtual plugin interface for external transport @c Conf factories.
 *
 * @details
 * Each VLink transport plugin exports exactly one concrete subclass of this
 * interface.  @c VLINK_PLUGIN_REGISTER tags the base type with a stable plugin
 * id; the concrete subclass exports its create/destroy entry points via
 * @c VLINK_PLUGIN_DECLARE in its translation unit.  The VLink runtime then
 * loads the plugin shared library, instantiates the concrete subclass and
 * queries it for the supported transport backend and new @c Conf objects.
 */
struct ConfPluginInterface {
  VLINK_PLUGIN_REGISTER(ConfPluginInterface)

 protected:
  ConfPluginInterface() = default;

  virtual ~ConfPluginInterface() = default;

 public:
  /**
   * @brief Returns the transport backend handled by this plugin.
   *
   * @details
   * Called by @c Url::load_for_plugin() to determine whether this plugin supports
   * the transport of the URL being constructed.
   *
   * @return The @c TransportType enumeration value identifying the transport backend.
   */
  [[nodiscard]] virtual TransportType get_transport_type() const = 0;

  /**
   * @brief Creates and returns a new transport @c Conf instance.
   *
   * @details
   * Called once per @c Url construction that matches this plugin's transport.  The
   * returned object must be fully initialised and ready for @c parse() calls.
   * Ownership is transferred to the caller via @c unique_ptr.
   *
   * @return A heap-allocated @c Conf subclass specific to this transport.
   */
  [[nodiscard]] virtual std::unique_ptr<Conf> create() const = 0;

 private:
  VLINK_DISALLOW_COPY_AND_ASSIGN(ConfPluginInterface)
};

}  // namespace vlink
