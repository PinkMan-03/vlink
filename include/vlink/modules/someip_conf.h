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
 * @file someip_conf.h
 * @brief Transport configuration for the @c someip:// SOME/IP (vsomeip) backend.
 *
 * @details
 * @c SomeipConf configures the SOME/IP (Scalable service-Oriented MiddlewarE over IP)
 * transport via the vsomeip library.  SOME/IP is the standard automotive middleware
 * protocol used in AUTOSAR environments over Ethernet.
 *
 * @par Supported Node Types
 * @c someip:// supports all six node types: @c kPublisher, @c kSubscriber, @c kServer,
 * @c kClient, @c kSetter, and @c kGetter.
 *
 * @par SOME/IP Identifier Model
 * SOME/IP uses a numeric identifier hierarchy rather than string topic names:
 *
 * | Field        | Use case        | Description                                    |
 * | ------------ | --------------- | ---------------------------------------------- |
 * | @c service   | All             | SOME/IP Service ID (16-bit hex)                |
 * | @c instance  | All             | Service Instance ID (16-bit hex)               |
 * | @c method    | RPC only        | Method ID for @c kServer / @c kClient          |
 * | @c groups    | Event/Field     | Event group set for pub/sub and field nodes    |
 * | @c event     | Event/Field     | Event ID within the group                      |
 * | @c field     | Field only      | @c true when node is a field (getter/setter)   |
 *
 * @par URL Format
 * @code
 *   // RPC (Server/Client):
 *   someip://<service>/<instance>?method=<method_id>
 *
 *   // Event (Publisher/Subscriber):
 *   someip://<service>/<instance>?groups=<g1,g2,...>&event=<event_id>
 *
 *   // Field (Setter/Getter):
 *   someip://<service>/<instance>?groups=<g1,g2,...>&event=<event_id>&field=1
 * @endcode
 *
 * Numeric values are parsed with @c Helpers::to_int(), so decimal, @c 0x-prefixed
 * hexadecimal, and leading-zero octal strings are accepted.  Service and instance
 * must be non-zero.
 *
 * @par Example
 * @code
 *   // RPC server on service 0x1234, instance 0x5678, method 0x0001:
 *   vlink::Server<MyReq, MyResp> server("someip://4660/22136?method=1");
 *
 *   // Event publisher on service 0x1234, instance 0x5678, group 0x0001, event 0x0010:
 *   vlink::Publisher<MyMsg> pub("someip://4660/22136?groups=1&event=16");
 *
 *   // Or construct directly:
 *   vlink::SomeipConf pub_conf(0x1234, 0x5678, {0x0001}, 0x0010);
 *   vlink::Publisher<MyMsg> pub(pub_conf);
 * @endcode
 *
 * @par vsomeip Configuration
 * A vsomeip JSON configuration file can be loaded at startup:
 * @code
 *   vlink::SomeipConf::load_global_config_file("/etc/vsomeip/vsomeip.json");
 * @endcode
 *
 * @note This header is compiled only when @c VLINK_SUPPORT_SOMEIP is defined.
 * @note @c service and @c instance must both be non-zero for @c is_valid() to return @c true.
 * @note For @c kPublisher / @c kSubscriber / @c kSetter / @c kGetter, both @c groups and
 *       @c event must be set; otherwise @c is_valid() returns @c false.
 * @note For @c kSetter / @c kGetter, @c field must be @c true.
 */

#pragma once

#ifdef VLINK_SUPPORT_SOMEIP

#include <cstdint>
#include <set>
#include <string>

#include "../impl/conf.h"

namespace vlink {

/**
 * @struct SomeipConf
 * @brief Configuration for the @c someip:// SOME/IP transport.
 *
 * @details
 * Can be constructed directly with numeric identifiers or parsed from a URL string
 * via @c Url.  Two constructors are provided: one for RPC (method-based) nodes and
 * one for event/field (group-based) nodes.
 */
struct VLINK_EXPORT SomeipConf final : public Conf {
  using Groups = std::set<uint16_t>;  ///< Set of SOME/IP event group IDs.

  uint16_t service{0};   ///< SOME/IP Service ID; must be non-zero for a valid configuration.
  uint16_t instance{0};  ///< SOME/IP Service Instance ID; must be non-zero.
  uint16_t method{0};    ///< SOME/IP Method ID; used only for @c kServer / @c kClient nodes.
  Groups groups;         ///< Set of event group IDs; required for event and field nodes.
  uint16_t event{0};     ///< SOME/IP Event ID within the group; required for event/field nodes.
  bool field{false};     ///< @c true if this is a field node (@c kSetter / @c kGetter).

  /**
   * @brief Constructs a @c SomeipConf for an RPC node (Server or Client).
   *
   * @param _service   SOME/IP Service ID.
   * @param _instance  Service Instance ID.
   * @param _method    Method ID for the RPC interface.
   */
  explicit SomeipConf(uint16_t _service, uint16_t _instance, uint16_t _method);

  /**
   * @brief Constructs a @c SomeipConf for an event or field node.
   *
   * @param _service   SOME/IP Service ID.
   * @param _instance  Service Instance ID.
   * @param _groups    Set of event group IDs to subscribe to.
   * @param _event     SOME/IP Event ID within the group.
   * @param _field     @c true for field (getter/setter) mode; @c false for event (pub/sub).
   */
  explicit SomeipConf(uint16_t _service, uint16_t _instance, const Groups& _groups, uint16_t _event,
                      bool _field = false);

  /**
   * @brief Returns @c true if all fields equal those of @p conf.
   *
   * @param conf  Configuration to compare.
   * @return      @c true if @c service, @c instance, @c method, @c groups, @c event, and
   *              @c field all match.
   */
  [[nodiscard]] bool operator==(const SomeipConf& conf) const noexcept;

  /**
   * @brief Returns @c true if any field differs from @p conf.
   *
   * @param conf  Configuration to compare.
   * @return      Logical negation of @c operator==.
   */
  [[nodiscard]] bool operator!=(const SomeipConf& conf) const noexcept;

  /**
   * @brief Returns @c TransportType::kSomeip identifying this transport.
   *
   * @return @c TransportType::kSomeip.
   */
  [[nodiscard]] TransportType get_transport_type() const override;

  /**
   * @brief Sets a vsomeip JSON configuration file as the global vsomeip config.
   *
   * @details
   * Delegates to @c SomeipFactory::load_global_config_file(), which sets the
   * @c VSOMEIP_CONFIGURATION environment variable.  Must be called before any
   * @c someip:// nodes are created so vsomeip can read the file during application
   * initialization.
   *
   * @param filepath  Path to the vsomeip JSON configuration file.
   * @return          @c true if the environment variable was set; @c false otherwise.
   */
  static bool load_global_config_file(const std::string& filepath);

#ifndef VLINK_ENABLE_C_INTERFACE
  VLINK_DECLARE_GLOBAL_PROPERTY()
#endif
  VLINK_ALLOW_IMPL_TYPE(kServer | kClient | kPublisher | kSubscriber | kSetter | kGetter)
  VLINK_CONF_IMPL(SomeipConf)
};

////////////////////////////////////////////////////////////////
/// Details
////////////////////////////////////////////////////////////////

inline SomeipConf::SomeipConf(uint16_t _service, uint16_t _instance, uint16_t _method)
    : service(_service), instance(_instance), method(_method) {}

inline SomeipConf::SomeipConf(uint16_t _service, uint16_t _instance, const Groups& _groups, uint16_t _event,
                              bool _field)
    : service(_service), instance(_instance), groups(_groups), event(_event), field(_field) {}

inline bool SomeipConf::operator==(const SomeipConf& conf) const noexcept {
  return service == conf.service && instance == conf.instance && method == conf.method && groups == conf.groups &&
         event == conf.event && field == conf.field;
}

inline bool SomeipConf::operator!=(const SomeipConf& conf) const noexcept { return !(*this == conf); }

inline TransportType SomeipConf::get_transport_type() const { return TransportType::kSomeip; }

}  // namespace vlink

#endif
