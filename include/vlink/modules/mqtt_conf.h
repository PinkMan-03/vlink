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
 * @file mqtt_conf.h
 * @brief Transport configuration for the @c mqtt:// MQTT protocol backend.
 *
 * @details
 * @c MqttConf configures the Eclipse Paho MQTT C transport, a lightweight
 * publish/subscribe messaging protocol designed for constrained devices and
 * low-bandwidth, high-latency networks.
 *
 * @par Supported Node Types
 * @c mqtt:// supports all six node types: @c kPublisher, @c kSubscriber, @c kServer,
 * @c kClient, @c kSetter, and @c kGetter.
 *
 * @par URL Format
 * @code
 *   mqtt://<address>[?event=<name>&domain=<N>&qos=<0|1|2>][#<fragment>]
 * @endcode
 *
 * | Component    | Description                                                               |
 * | ------------ | ------------------------------------------------------------------------- |
 * | @c address   | MQTT topic path; formed from @c host + @c "/" + @c path                   |
 * | @c event     | Optional secondary event filter (@c ?event=)                              |
 * | @c domain    | Domain/namespace identifier (@c ?domain=, default from @c MqttFactory)    |
 * | @c qos       | MQTT QoS level; default from @c MqttFactory: 0, 1, or 2                   |
 * | @c fragment  | Optional broker URI override (e.g. @c tcp://192.168.1.1:1883)             |
 *
 * @par Environment Variables
 *
 * | Variable               | Description                           | Default                  |
 * | ---------------------- | ------------------------------------- | ------------------------ |
 * | VLINK_MQTT_BROKER      | MQTT broker URI                       | tcp://localhost:1883     |
 * | VLINK_MQTT_DOMAIN      | Default domain ID                     | 0                        |
 * | VLINK_MQTT_QOS         | Default QoS level (0, 1, 2)           | 1                        |
 * | VLINK_MQTT_KEEPALIVE   | Keep-alive interval in seconds        | 60                       |
 * | VLINK_MQTT_CLIENT_ID   | Client ID prefix                      | vlink_mqtt               |
 *
 * @note This header is compiled only when @c VLINK_SUPPORT_MQTT is defined.
 * @note URL parsing uses @c MqttFactory::get_default_domain_id() and
 *       @c MqttFactory::get_default_qos() when @c domain or @c qos are absent;
 *       direct construction defaults remain @c domain=0 and @c qos=1.
 * @note @c is_valid() returns @c false if @c address is empty, @c domain is
 *       negative, or @c qos is outside @c [0, 2].
 */

#pragma once

#ifdef VLINK_SUPPORT_MQTT

#include <cstdint>
#include <map>
#include <shared_mutex>
#include <string>

#include "../impl/conf.h"

namespace vlink {

/**
 * @struct MqttConf
 * @brief Configuration for the @c mqtt:// MQTT transport.
 *
 * @details
 * Can be constructed directly or parsed from a URL string via @c Url.
 */
struct VLINK_EXPORT MqttConf final : public Conf {
  std::string address;   ///< MQTT topic path (host + "/" + path from URL).
  std::string event;     ///< Optional secondary event filter string.
  int32_t domain{0};     ///< Domain/namespace identifier (non-negative).
  int32_t qos{1};        ///< MQTT QoS level: 0, 1, or 2.
  std::string fragment;  ///< Optional broker URI override (URL fragment).

  /**
   * @brief Constructs a @c MqttConf with explicit parameters.
   *
   * @param _address   MQTT topic path.
   * @param _event     Optional event filter; empty by default.
   * @param _domain    Domain identifier; default 0.
   * @param _qos       MQTT QoS level; default 1.
   * @param _fragment  Optional broker URI override; empty by default.
   */
  explicit MqttConf(const std::string& _address, const std::string& _event = "", int32_t _domain = 0, int32_t _qos = 1,
                    const std::string& _fragment = "");

  /**
   * @brief Returns @c true if all fields equal those of @p conf.
   *
   * @param conf  Configuration to compare.
   * @return      @c true if all fields match.
   */
  [[nodiscard]] bool operator==(const MqttConf& conf) const noexcept;

  /**
   * @brief Returns @c true if any field differs from @p conf.
   *
   * @param conf  Configuration to compare.
   * @return      Logical negation of @c operator==.
   */
  [[nodiscard]] bool operator!=(const MqttConf& conf) const noexcept;

  /**
   * @brief Returns @c TransportType::kMqtt identifying this transport.
   *
   * @return @c TransportType::kMqtt.
   */
  [[nodiscard]] TransportType get_transport_type() const override;

  static constexpr const char* kRespSuffix{"___resp"};

#ifndef VLINK_ENABLE_C_INTERFACE
  VLINK_DECLARE_GLOBAL_PROPERTY()
#endif
  VLINK_ALLOW_IMPL_TYPE(kServer | kClient | kPublisher | kSubscriber | kSetter | kGetter)
  VLINK_CONF_IMPL(MqttConf)
};

////////////////////////////////////////////////////////////////
/// Details
////////////////////////////////////////////////////////////////

inline MqttConf::MqttConf(const std::string& _address, const std::string& _event, int32_t _domain, int32_t _qos,
                          const std::string& _fragment)
    : address(_address), event(_event), domain(_domain), qos(_qos), fragment(_fragment) {}

inline bool MqttConf::operator==(const MqttConf& conf) const noexcept {
  return address == conf.address && event == conf.event && domain == conf.domain && qos == conf.qos &&
         fragment == conf.fragment;
}

inline bool MqttConf::operator!=(const MqttConf& conf) const noexcept { return !(*this == conf); }

inline TransportType MqttConf::get_transport_type() const { return TransportType::kMqtt; }

}  // namespace vlink

#endif
