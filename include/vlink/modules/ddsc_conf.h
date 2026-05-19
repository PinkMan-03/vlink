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
 * @file ddsc_conf.h
 * @brief Transport configuration for the @c ddsc:// CycloneDDS backend.
 *
 * @details
 * @c DdscConf configures the Eclipse CycloneDDS transport, an open-source
 * DDS implementation that targets both embedded and cloud deployments.
 * It provides the same VLink API as @c DdsConf (Fast-DDS) but delegates to
 * CycloneDDS internally.
 *
 * @par Supported Node Types
 * @c ddsc:// supports all six node types: @c kPublisher, @c kSubscriber, @c kServer,
 * @c kClient, @c kSetter, and @c kGetter.
 *
 * @par URL Format
 * @code
 *   ddsc://<topic>[?domain=<N>&depth=<N>&qos=<name>]
 * @endcode
 *
 * | Component | Description                                                               |
 * | --------- | ------------------------------------------------------------------------- |
 * | @c topic  | CycloneDDS topic name; formed from @c host + @c "/" + @c path             |
 * | @c domain | DDS Domain ID (@c ?domain=, default from @c VLINK_DDS_DOMAIN env var)     |
 * | @c depth  | DDS history depth override; 0 keeps the selected QoS history depth         |
 * | @c qos    | Named QoS profile registered via @c register_qos() (@c ?qos=)             |
 *
 * @par QoS Registration
 * @code
 *   vlink::Qos my_qos;
 *   my_qos.reliability = vlink::Reliability::kReliable;
 *   vlink::DdscConf::register_qos("my_profile", my_qos);
 *
 *   vlink::Subscriber<MyMsg> sub("ddsc://my_topic?qos=my_profile");
 * @endcode
 *
 * @note This header is compiled only when @c VLINK_SUPPORT_DDSC is defined.
 * @note Unlike @c DdsConf, @c DdscConf does not support @c register_topic() or
 *       extended QoS maps (@c qos_ext).
 */

#pragma once

#ifdef VLINK_SUPPORT_DDSC

#include <cstdint>
#include <functional>
#include <map>
#include <shared_mutex>
#include <string>

#include "../extension/qos.h"
#include "../impl/conf.h"

namespace vlink {

/**
 * @struct DdscConf
 * @brief Configuration for the @c ddsc:// CycloneDDS transport.
 *
 * @details
 * Can be constructed directly or parsed from a URL string via @c Url.
 */
struct VLINK_EXPORT DdscConf final : public Conf {
  std::string topic;  ///< CycloneDDS topic name (host + "/" + path from URL).
  int32_t domain{0};  ///< DDS Domain Participant ID (non-negative).
  int32_t depth{0};   ///< DDS history depth override; 0 keeps the selected QoS history depth.
  std::string qos;    ///< Named QoS profile key registered via @c register_qos().

  /**
   * @brief Constructs a @c DdscConf with explicit parameters.
   *
   * @param _topic   CycloneDDS topic name.
   * @param _domain  Domain ID; default 0.
   * @param _depth   History depth override; default 0.
   * @param _qos     Named QoS profile key; empty by default.
   */
  explicit DdscConf(const std::string& _topic, int32_t _domain = 0, int32_t _depth = 0, const std::string& _qos = "");

  /**
   * @brief Returns @c true if all fields equal those of @p conf.
   *
   * @param conf  Configuration to compare.
   * @return      @c true if @c topic, @c domain, @c depth, and @c qos all match.
   */
  [[nodiscard]] bool operator==(const DdscConf& conf) const noexcept;

  /**
   * @brief Returns @c true if any field differs from @p conf.
   *
   * @param conf  Configuration to compare.
   * @return      Logical negation of @c operator==.
   */
  [[nodiscard]] bool operator!=(const DdscConf& conf) const noexcept;

  /**
   * @brief Returns @c TransportType::kDdsc identifying this transport.
   *
   * @return @c TransportType::kDdsc.
   */
  [[nodiscard]] TransportType get_transport_type() const override;

  /**
   * @brief Registers a named QoS profile for use by @c ddsc:// nodes.
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

  friend class DdscFactory;
  static std::map<std::string, Qos> qos_map_;
  static std::shared_mutex mtx_;
  static constexpr const char* kRespSuffix{"___resp"};
#ifndef VLINK_ENABLE_C_INTERFACE
  VLINK_DECLARE_GLOBAL_PROPERTY()
#endif
  VLINK_ALLOW_IMPL_TYPE(kServer | kClient | kPublisher | kSubscriber | kSetter | kGetter)
  VLINK_CONF_IMPL(DdscConf)
};

////////////////////////////////////////////////////////////////
/// Details
////////////////////////////////////////////////////////////////

inline DdscConf::DdscConf(const std::string& _topic, int32_t _domain, int32_t _depth, const std::string& _qos)
    : topic(_topic), domain(_domain), depth(_depth), qos(_qos) {}

inline bool DdscConf::operator==(const DdscConf& conf) const noexcept {
  return topic == conf.topic && domain == conf.domain && depth == conf.depth && qos == conf.qos;
}

inline bool DdscConf::operator!=(const DdscConf& conf) const noexcept { return !(*this == conf); }

inline TransportType DdscConf::get_transport_type() const { return TransportType::kDdsc; }

}  // namespace vlink

#endif
