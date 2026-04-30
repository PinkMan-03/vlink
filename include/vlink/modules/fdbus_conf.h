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
 * @file fdbus_conf.h
 * @brief Transport configuration for the @c fdbus:// FDBus IPC backend.
 *
 * @details
 * @c FdbusConf configures the FDBus IPC transport, a same-machine inter-process
 * communication framework based on D-Bus-style service registration.  FDBus
 * supports both service-oriented (@c svc) and peer-to-peer IPC (@c ipc) modes.
 *
 * @par Supported Node Types
 * @c fdbus:// supports all six node types: @c kPublisher, @c kSubscriber, @c kServer,
 * @c kClient, @c kSetter, and @c kGetter.
 *
 * @par URL Format
 * @code
 *   fdbus://<address>[?event=<name>][#<transport>]
 * @endcode
 *
 * | Component  | Description                                                           |
 * | ---------- | --------------------------------------------------------------------- |
 * | @c address | FDBus service/topic address; formed from @c host + @c "/" + @c path   |
 * | @c event   | Optional secondary event name (@c ?event=)                            |
 * | @c transport  | Transport mode: @c "svc" (service, default) or @c "ipc" (fragment)    |
 *
 * @par Example
 * @code
 *   // Service-oriented mode (default):
 *   vlink::Publisher<MyMsg> pub("fdbus://my_service");
 *
 *   // IPC mode via URL fragment:
 *   vlink::Publisher<MyMsg> pub("fdbus://my_service#ipc");
 *
 *   // Direct construction:
 *   vlink::FdbusConf conf("my_service", "my_event", "svc");
 *   vlink::Publisher<MyMsg> pub(conf);
 * @endcode
 *
 * @note This header is compiled only when @c VLINK_SUPPORT_FDBUS is defined.
 * @note @c is_valid() returns @c false if @c address is empty or @c transport is
 *       neither @c "svc" nor @c "ipc".
 */

#pragma once

#ifdef VLINK_SUPPORT_FDBUS

#include <cstdint>
#include <string>

#include "../impl/conf.h"

namespace vlink {

/**
 * @struct FdbusConf
 * @brief Configuration for the @c fdbus:// FDBus IPC transport.
 *
 * @details
 * Can be constructed directly or parsed from a URL string via @c Url.
 * The URL fragment selects the transport mode (@c "svc" or @c "ipc").
 */
struct VLINK_EXPORT FdbusConf final : public Conf {
  std::string address;           ///< FDBus service/topic address (host + "/" + path from URL).
  std::string event;             ///< Optional secondary event name.
  std::string transport{"svc"};  ///< Transport mode: @c "svc" (service registry) or @c "ipc" (direct IPC).

  /**
   * @brief Constructs a @c FdbusConf with explicit parameters.
   *
   * @param _address  Service/topic address string.
   * @param _event    Optional event name; empty by default.
   * @param _transport   Transport mode: @c "svc" or @c "ipc"; default @c "svc".
   */
  explicit FdbusConf(const std::string& _address, const std::string& _event = "",
                     const std::string& _transport = "svc");

  /**
   * @brief Returns @c true if address and event match @p conf.
   *
   * @details
   * Note that @c transport is intentionally excluded from equality comparison.
   *
   * @param conf  Configuration to compare.
   * @return      @c true if @c address and @c event are equal.
   */
  [[nodiscard]] bool operator==(const FdbusConf& conf) const noexcept;

  /**
   * @brief Returns @c true if address or event differ from @p conf.
   *
   * @param conf  Configuration to compare.
   * @return      Logical negation of @c operator==.
   */
  [[nodiscard]] bool operator!=(const FdbusConf& conf) const noexcept;

  /**
   * @brief Returns @c TransportType::kFdbus identifying this transport.
   *
   * @return @c TransportType::kFdbus.
   */
  [[nodiscard]] TransportType get_transport_type() const override;

  /**
   * @brief Cheap probe for whether the FDBus @c name_server daemon is running.
   *
   * @details
   * Non-invasive check intended for CLI diagnostics, bench preflight and
   * self-test skip logic.  Does NOT create any FDBus endpoint, does NOT open
   * any connection, and is safe to call repeatedly from any thread.
   *
   * @return @c true if a @c name_server process is currently running on this
   *         host, otherwise @c false.
   */
  [[nodiscard]] static bool has_name_server();

#ifndef VLINK_ENABLE_C_INTERFACE
  VLINK_DECLARE_GLOBAL_PROPERTY()
#endif
  VLINK_ALLOW_IMPL_TYPE(kServer | kClient | kPublisher | kSubscriber | kSetter | kGetter)
  VLINK_CONF_IMPL(FdbusConf)
};

////////////////////////////////////////////////////////////////
/// Details
////////////////////////////////////////////////////////////////

inline FdbusConf::FdbusConf(const std::string& _address, const std::string& _event, const std::string& _transport)
    : address(_address), event(_event), transport(_transport) {}

inline bool FdbusConf::operator==(const FdbusConf& conf) const noexcept {
  return address == conf.address && event == conf.event;
}

inline bool FdbusConf::operator!=(const FdbusConf& conf) const noexcept { return !(*this == conf); }

inline TransportType FdbusConf::get_transport_type() const { return TransportType::kFdbus; }

}  // namespace vlink

#endif
