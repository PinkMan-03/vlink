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
 * @file zenoh_conf.h
 * @brief Transport configuration for the @c zenoh:// Zenoh protocol backend.
 *
 * @details
 * @c ZenohConf configures the Eclipse Zenoh transport, a protocol designed for
 * unified data management across robots, edge nodes, and cloud infrastructure.
 * Zenoh supports routed pub/sub and queryable patterns with optional peer-to-peer
 * connectivity.
 *
 * @par Supported Node Types
 * @c zenoh:// supports all six node types: @c kPublisher, @c kSubscriber, @c kServer,
 * @c kClient, @c kSetter, and @c kGetter.
 *
 * @par URL Format
 * @code
 *   zenoh://<address>[?event=<name>&domain=<N>&qos=<name>&depth=<N>&shm=<bool>
 *                    &shm_mode=<lazy|init>&shm_size=<N>&shm_threshold=<N>
 *                    &shm_loan_threshold=<N>&shm_blocking=<bool>][#<fragment>]
 * @endcode
 *
 * | Component    | Description                                                               |
 * | ------------ | ------------------------------------------------------------------------- |
 * | @c address   | Zenoh key expression; formed from @c host + @c "/" + @c path              |
 * | @c event     | Optional secondary event filter (@c ?event=)                              |
 * | @c domain    | Zenoh domain/session identifier (@c ?domain=, default from factory)       |
 * | @c qos       | Named QoS profile registered via @c register_qos() (@c ?qos=)             |
 * | @c depth     | Optional TX queue override; 0 uses selected QoS history depth             |
 * | @c shm       | Optional Zenoh shared-memory transport optimization enable (@c ?shm=)     |
 * | @c shm_mode  | Optional Zenoh SHM init mode: @c lazy or @c init                          |
 * | @c shm_size  | Optional SHM pool size; accepts bytes, K, M, or G suffixes                |
 * | @c shm_threshold | Optional SHM optimization message-size threshold                      |
 * | @c shm_loan_threshold | Optional minimum size for VLink SHM loan buffers                 |
 * | @c shm_blocking | Optional blocking allocation mode for @c loan()                       |
 * | @c fragment  | Optional transport hint or config fragment passed to the Zenoh session    |
 *
 * @par QoS Registration
 * @code
 *   vlink::Qos my_qos;
 *   my_qos.reliability = vlink::Reliability::kReliable;
 *   vlink::ZenohConf::register_qos("my_profile", my_qos);
 *
 *   vlink::Publisher<MyMsg> pub("zenoh://vehicle/speed?qos=my_profile");
 * @endcode
 *
 * @note This header is compiled only when @c VLINK_SUPPORT_ZENOH is defined.
 * @note @c is_valid() returns @c false if @c address is empty or @c domain is negative.
 */

#pragma once

#ifdef VLINK_SUPPORT_ZENOH

#include <cstdint>
#include <functional>
#include <map>
#include <shared_mutex>
#include <string>

#include "../extension/qos.h"
#include "../impl/conf.h"

namespace vlink {

/**
 * @struct ZenohConf
 * @brief Configuration for the @c zenoh:// Zenoh transport.
 *
 * @details
 * Can be constructed directly or parsed from a URL string via @c Url.
 */
struct VLINK_EXPORT ZenohConf final : public Conf {
  std::string address;             ///< Zenoh key expression (host + "/" + path from URL).
  std::string event;               ///< Optional secondary event filter string.
  int32_t domain{0};               ///< Zenoh session/domain identifier (non-negative).
  int32_t depth{0};                ///< Optional TX queue override; 0 means use selected QoS history depth.
  std::string qos;                 ///< Named QoS profile key registered via @c register_qos().
  std::string fragment;            ///< Optional transport hint passed as the URL fragment.
  std::string shm;                 ///< Optional SHM enable override from @c ?shm=.
  std::string shm_mode;            ///< Optional SHM init mode (@c lazy or @c init).
  std::string shm_size;            ///< Optional SHM transport pool size in bytes, K, M, or G.
  std::string shm_threshold;       ///< Optional SHM transport optimization threshold.
  std::string shm_loan_threshold;  ///< Optional minimum size for VLink SHM loan buffers.
  std::string shm_blocking;        ///< Optional blocking allocation mode for @c loan().

  /**
   * @brief Constructs a @c ZenohConf with explicit parameters.
   *
   * @param _address   Zenoh key expression.
   * @param _event     Optional event filter; empty by default.
   * @param _domain    Domain identifier; default 0.
   * @param _qos       Named QoS profile key; empty by default.
   * @param _fragment  Optional transport hint fragment; empty by default.
   */
  explicit ZenohConf(const std::string& _address, const std::string& _event = "", int32_t _domain = 0,
                     const std::string& _qos = "", const std::string& _fragment = "");

  /**
   * @brief Returns @c true if all fields equal those of @p conf.
   *
   * @param conf  Configuration to compare.
   * @return      @c true if all fields (including @c depth and the @c shm* options) match.
   */
  [[nodiscard]] bool operator==(const ZenohConf& conf) const noexcept;

  /**
   * @brief Returns @c true if any field differs from @p conf.
   *
   * @param conf  Configuration to compare.
   * @return      Logical negation of @c operator==.
   */
  [[nodiscard]] bool operator!=(const ZenohConf& conf) const noexcept;

  /**
   * @brief Returns @c TransportType::kZenoh identifying this transport.
   *
   * @return @c TransportType::kZenoh.
   */
  [[nodiscard]] TransportType get_transport_type() const override;

  /**
   * @brief Adds URL-level Zenoh tuning options to a property map.
   *
   * @details
   * This keeps URL query parameters and @c set_property("zenoh.*", ...) on the
   * same factory path without expanding the factory object key shape.
   */
  void append_properties(PropertiesMap& properties) const;

  /**
   * @brief Registers a named QoS profile for use by @c zenoh:// nodes.
   *
   * @details
   * The @p name is associated with the @p qos object and can be referenced in URL
   * query strings as @c ?qos=name.  Names that conflict with reserved keys
   * (@c part, @c topic, @c pub, @c sub, @c writer, @c reader) or that are already
   * registered cause a fatal log and are rejected.
   *
   * @param name  Unique profile name; must not be one of the reserved keys.
   * @param qos   @c Qos object describing the quality-of-service settings.
   */
  static void register_qos(const std::string& name, const Qos& qos);

 private:
  static void register_qos_internal(const std::string& name, const Qos& qos);

  static const Qos& find_qos(const std::string& name);

  friend class ZenohFactory;
  static std::map<std::string, Qos> qos_map_;
  static std::shared_mutex mtx_;
  static constexpr const char* kRespSuffix{"___resp"};
#ifndef VLINK_ENABLE_C_INTERFACE
  VLINK_DECLARE_GLOBAL_PROPERTY()
#endif
  VLINK_ALLOW_IMPL_TYPE(kServer | kClient | kPublisher | kSubscriber | kSetter | kGetter)
  VLINK_CONF_IMPL(ZenohConf)
};

////////////////////////////////////////////////////////////////
/// Details
////////////////////////////////////////////////////////////////

inline ZenohConf::ZenohConf(const std::string& _address, const std::string& _event, int32_t _domain,
                            const std::string& _qos, const std::string& _fragment)
    : address(_address), event(_event), domain(_domain), qos(_qos), fragment(_fragment) {}

inline bool ZenohConf::operator==(const ZenohConf& conf) const noexcept {
  return address == conf.address && event == conf.event && domain == conf.domain && depth == conf.depth &&
         qos == conf.qos && fragment == conf.fragment && shm == conf.shm && shm_mode == conf.shm_mode &&
         shm_size == conf.shm_size && shm_threshold == conf.shm_threshold &&
         shm_loan_threshold == conf.shm_loan_threshold && shm_blocking == conf.shm_blocking;
}

inline bool ZenohConf::operator!=(const ZenohConf& conf) const noexcept { return !(*this == conf); }

inline TransportType ZenohConf::get_transport_type() const { return TransportType::kZenoh; }

inline void ZenohConf::append_properties(PropertiesMap& properties) const {
  if (!shm.empty()) {
    properties["zenoh.shm"] = shm;
  }

  if (!shm_mode.empty()) {
    properties["zenoh.shm_mode"] = shm_mode;
  }

  if (!shm_size.empty()) {
    properties["zenoh.shm_size"] = shm_size;
  }

  if (!shm_threshold.empty()) {
    properties["zenoh.shm_threshold"] = shm_threshold;
  }

  if (!shm_loan_threshold.empty()) {
    properties["zenoh.shm_loan_threshold"] = shm_loan_threshold;
  }

  if (!shm_blocking.empty()) {
    properties["zenoh.shm_blocking"] = shm_blocking;
  }
}

}  // namespace vlink

#endif
