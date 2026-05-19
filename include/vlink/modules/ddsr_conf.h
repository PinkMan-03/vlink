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
 * @file ddsr_conf.h
 * @brief Transport configuration for the @c ddsr:// RTI Connext DDS backend.
 *
 * @details
 * @c DdsrConf configures the RTI Connext DDS transport, a commercial-grade DDS
 * implementation widely used in safety-critical and real-time embedded systems.
 * It presents the same VLink API as @c DdsConf (Fast-DDS) but delegates to
 * RTI Connext internally.
 *
 * @par Supported Node Types
 * @c ddsr:// supports all six node types: @c kPublisher, @c kSubscriber, @c kServer,
 * @c kClient, @c kSetter, and @c kGetter.
 *
 * @par URL Format
 * @code
 *   ddsr://<topic>[?domain=<N>&depth=<N>&qos=<name>]
 *   ddsr://<topic>[?domain=<N>&part=<v>&topic=<v>&pub=<v>&sub=<v>&writer=<v>&reader=<v>]
 * @endcode
 *
 * | Component  | Description                                                               |
 * | ---------- | ------------------------------------------------------------------------- |
 * | @c topic   | RTI DDS topic name; formed from @c host + @c "/" + @c path                |
 * | @c domain  | DDS Domain ID (@c ?domain=, default from @c VLINK_DDS_DOMAIN env var)     |
 * | @c depth   | DDS history depth override; 0 keeps the selected QoS history depth        |
 * | @c qos     | Named QoS profile registered via @c register_qos() (@c ?qos=)             |
 * | @c qos_ext | Remaining query map after @c domain, @c depth, and @c qos are removed     |
 *
 * @note This header is compiled only when @c VLINK_SUPPORT_DDSR is defined.
 * @note @c qos and @c qos_ext are mutually exclusive.
 * @note RPC response topic names are derived by appending a @c "___resp" suffix.
 */

#pragma once

#ifdef VLINK_SUPPORT_DDSR

#include <cstdint>
#include <functional>
#include <map>
#include <shared_mutex>
#include <string>

#include "../extension/qos.h"
#include "../impl/conf.h"

namespace vlink {

/**
 * @struct DdsrConf
 * @brief Configuration for the @c ddsr:// RTI Connext DDS transport.
 *
 * @details
 * Can be constructed directly or parsed from a URL string via @c Url.
 */
struct VLINK_EXPORT DdsrConf final : public Conf {
  std::string topic;  ///< RTI DDS topic name (host + "/" + path from URL).
  int32_t domain{0};  ///< DDS Domain Participant ID (non-negative).
  int32_t depth{0};   ///< DDS history depth override; 0 keeps the selected QoS history depth.
  std::string qos;    ///< Named QoS profile key registered via @c register_qos().
  PropertiesMap
      qos_ext;  ///< Query map after removing @c domain, @c depth, and @c qos; unknown keys are kept but warned.

  /**
   * @brief Constructs a @c DdsrConf with topic, domain, depth, and named QoS.
   *
   * @param _topic   RTI DDS topic name.
   * @param _domain  Domain ID; default 0.
   * @param _depth   History depth override; default 0.
   * @param _qos     Named QoS profile key; empty by default.
   */
  explicit DdsrConf(const std::string& _topic, int32_t _domain = 0, int32_t _depth = 0, const std::string& _qos = "");

  /**
   * @brief Constructs a @c DdsrConf with topic, domain, and extended QoS map.
   *
   * @param _topic    RTI DDS topic name.
   * @param _domain   Domain ID.
   * @param _qos_ext  Per-entity QoS properties map.
   */
  explicit DdsrConf(const std::string& _topic, int32_t _domain, const PropertiesMap& _qos_ext);

  /**
   * @brief Returns @c true if all fields equal those of @p conf.
   *
   * @param conf  Configuration to compare.
   * @return      @c true if @c topic, @c domain, @c depth, @c qos, and @c qos_ext match.
   */
  [[nodiscard]] bool operator==(const DdsrConf& conf) const noexcept;

  /**
   * @brief Returns @c true if any field differs from @p conf.
   *
   * @param conf  Configuration to compare.
   * @return      Logical negation of @c operator==.
   */
  [[nodiscard]] bool operator!=(const DdsrConf& conf) const noexcept;

  /**
   * @brief Returns @c TransportType::kDdsr identifying this transport.
   *
   * @return @c TransportType::kDdsr.
   */
  [[nodiscard]] TransportType get_transport_type() const override;

  /**
   * @brief Registers a named QoS profile for use by @c ddsr:// nodes.
   *
   * @details
   * The @p name is associated with the @p qos object and can be referenced in URL
   * query strings as @c ?qos=name.  Names that conflict with reserved keys
   * (@c part, @c topic, @c pub, @c sub, @c writer, @c reader, @c depth) or that
   * are already registered cause a fatal log and are rejected.
   *
   * @param name  Unique profile name; must not be one of the reserved keys.
   * @param qos   @c Qos object describing the quality-of-service settings.
   */
  static void register_qos(const std::string& name, const Qos& qos);

 private:
  static void register_qos_internal(const std::string& name, const Qos& qos);

  static const Qos& find_qos(const std::string& name);

  friend class DdsrFactory;
  static std::map<std::string, Qos> qos_map_;
  static std::shared_mutex mtx_;
  static constexpr const char* kRespSuffix{"___resp"};
#ifndef VLINK_ENABLE_C_INTERFACE
  VLINK_DECLARE_GLOBAL_PROPERTY()
#endif
  VLINK_ALLOW_IMPL_TYPE(kServer | kClient | kPublisher | kSubscriber | kSetter | kGetter)
  VLINK_CONF_IMPL(DdsrConf)
};

////////////////////////////////////////////////////////////////
/// Details
////////////////////////////////////////////////////////////////

inline DdsrConf::DdsrConf(const std::string& _topic, int32_t _domain, int32_t _depth, const std::string& _qos)
    : topic(_topic), domain(_domain), depth(_depth), qos(_qos) {}

inline DdsrConf::DdsrConf(const std::string& _topic, int32_t _domain, const PropertiesMap& _qos_ext)
    : topic(_topic), domain(_domain), qos_ext(_qos_ext) {}

inline bool DdsrConf::operator==(const DdsrConf& conf) const noexcept {
  return topic == conf.topic && domain == conf.domain && depth == conf.depth && qos == conf.qos &&
         qos_ext == conf.qos_ext;
}

inline bool DdsrConf::operator!=(const DdsrConf& conf) const noexcept { return !(*this == conf); }

inline TransportType DdsrConf::get_transport_type() const { return TransportType::kDdsr; }

}  // namespace vlink

#endif
