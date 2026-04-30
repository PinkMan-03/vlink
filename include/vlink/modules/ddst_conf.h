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
 * @file ddst_conf.h
 * @brief Transport configuration for the @c ddst:// TravoDDS backend.
 *
 * @details
 * @c DdstConf configures the TravoDDS transport.  TravoDDS is a domestic
 * (open-source, https://gitee.com/agiros/travodds) DDS implementation; its API
 * surface exposed by VLink is identical to @c DdsConf (Fast-DDS), but the
 * underlying runtime is provided by TravoDDS.
 *
 * @par Supported Node Types
 * @c ddst:// supports all six node types: @c kPublisher, @c kSubscriber, @c kServer,
 * @c kClient, @c kSetter, and @c kGetter.
 *
 * @par URL Format
 * @code
 *   ddst://<topic>[?domain=<N>&depth=<N>&qos=<name>]
 *   ddst://<topic>[?domain=<N>&part=<v>&topic=<v>&pub=<v>&sub=<v>&writer=<v>&reader=<v>]
 * @endcode
 *
 * | Component  | Description                                                               |
 * | ---------- | ------------------------------------------------------------------------- |
 * | @c topic   | TravoDDS topic name; formed from @c host + @c "/" + @c path               |
 * | @c domain  | DDS Domain ID (@c ?domain=, default from @c VLINK_DDS_DOMAIN env var)     |
 * | @c depth   | History depth (@c ?depth=, default 0)                                     |
 * | @c qos     | Named QoS profile registered via @c register_qos() (@c ?qos=)             |
 * | @c qos_ext | Extended QoS map: @c part, @c topic, @c pub, @c sub, @c writer, @c reader |
 *
 * @par Global QoS File
 * @code
 *   vlink::DdstConf::load_global_qos_file("/etc/vlink/ddst_profile.xml");
 * @endcode
 *
 * @note This header is compiled only when @c VLINK_SUPPORT_DDST is defined.
 * @note @c qos and @c qos_ext are mutually exclusive; setting both causes
 *       @c is_valid() to return @c false.
 * @note Response topics for RPC are stored with a @c "___resp" suffix.
 */

#pragma once

#ifdef VLINK_SUPPORT_DDST

#include <cstdint>
#include <functional>
#include <map>
#include <shared_mutex>
#include <string>
#include <tuple>
#include <vector>

#include "../extension/qos.h"
#include "../impl/conf.h"

namespace vlink {

/**
 * @struct DdstConf
 * @brief Configuration for the @c ddst:// TravoDDS transport.
 *
 * @details
 * Can be constructed directly or parsed from a URL string via @c Url.
 */
struct VLINK_EXPORT DdstConf final : public Conf {
  std::string topic;  ///< TravoDDS topic name (host + "/" + path from URL).
  int32_t domain{0};  ///< DDS Domain Participant ID (non-negative).
  int32_t depth{0};   ///< DDS history depth for the endpoint; 0 means transport default.
  std::string qos;    ///< Named QoS profile key registered via @c register_qos().
  PropertiesMap
      qos_ext;  ///< Extended per-entity QoS map (keys: @c part, @c topic, @c pub, @c sub, @c writer, @c reader).

  /**
   * @brief Constructs a @c DdstConf with topic, domain, depth, and named QoS.
   *
   * @param _topic   TravoDDS topic name.
   * @param _domain  Domain ID; default 0.
   * @param _depth   History depth; default 0.
   * @param _qos     Named QoS profile key; empty by default.
   */
  explicit DdstConf(const std::string& _topic, int32_t _domain = 0, int32_t _depth = 0, const std::string& _qos = "");

  /**
   * @brief Constructs a @c DdstConf with topic, domain, and extended QoS map.
   *
   * @param _topic    TravoDDS topic name.
   * @param _domain   Domain ID.
   * @param _qos_ext  Per-entity QoS properties map.
   */
  explicit DdstConf(const std::string& _topic, int32_t _domain, const PropertiesMap& _qos_ext);

  /**
   * @brief Returns @c true if all fields equal those of @p conf.
   *
   * @param conf  Configuration to compare.
   * @return      @c true if @c topic, @c domain, @c depth, @c qos, and @c qos_ext all match.
   */
  [[nodiscard]] bool operator==(const DdstConf& conf) const noexcept;

  /**
   * @brief Returns @c true if any field differs from @p conf.
   *
   * @param conf  Configuration to compare.
   * @return      Logical negation of @c operator==.
   */
  [[nodiscard]] bool operator!=(const DdstConf& conf) const noexcept;

  /**
   * @brief Returns @c TransportType::kDdst identifying this transport.
   *
   * @return @c TransportType::kDdst.
   */
  [[nodiscard]] TransportType get_transport_type() const override;

  /**
   * @brief Returns the list of currently discovered topics on the given domain.
   *
   * @details
   * Queries @c DdstFactory for live topic discovery information.  Each entry is
   * a @c (topic_name, type_name) pair.
   *
   * @param _domain  DDS domain ID to query.
   * @return         Vector of @c (topic_name, type_name) tuples; may be empty.
   */
  [[nodiscard]] static std::vector<std::tuple<std::string, std::string>> get_discovered_topics(int32_t _domain);

  /**
   * @brief Loads a TravoDDS XML QoS profile file as the global default.
   *
   * @details
   * Must be called before any @c ddst:// participants are created.
   *
   * @param filepath  Path to the XML QoS profile file.
   * @return          @c true if loaded successfully; @c false otherwise.
   */
  static bool load_global_qos_file(const std::string& filepath);

  /**
   * @brief Registers a named QoS profile for use by @c ddst:// nodes.
   *
   * @details
   * The @p name is associated with the @p qos object and can be referenced in URL
   * query strings as @c ?qos=name.  Reserved keys (@c part, @c topic, @c pub,
   * @c sub, @c writer, @c reader, @c depth) and duplicate names are rejected
   * with a fatal log.
   *
   * @param name  Unique profile name; must not conflict with reserved keys.
   * @param qos   @c Qos object describing the quality-of-service settings.
   */
  static void register_qos(const std::string& name, const Qos& qos);

 private:
  static void register_qos_internal(const std::string& name, const Qos& qos);

  static const Qos& find_qos(const std::string& name);

  friend class DdstFactory;
  static std::map<std::string, std::function<void*()>> type_support_map_;
  static std::map<std::string, Qos> qos_map_;
  static std::shared_mutex mtx_;
  static constexpr const char* kRespSuffix{"___resp"};
#ifndef VLINK_ENABLE_C_INTERFACE
  VLINK_DECLARE_GLOBAL_PROPERTY()
#endif
  VLINK_ALLOW_IMPL_TYPE(kServer | kClient | kPublisher | kSubscriber | kSetter | kGetter)
  VLINK_CONF_IMPL(DdstConf)
};

////////////////////////////////////////////////////////////////
/// Details
////////////////////////////////////////////////////////////////

inline DdstConf::DdstConf(const std::string& _topic, int32_t _domain, int32_t _depth, const std::string& _qos)
    : topic(_topic), domain(_domain), depth(_depth), qos(_qos) {}

inline DdstConf::DdstConf(const std::string& _topic, int32_t _domain, const PropertiesMap& _qos_ext)
    : topic(_topic), domain(_domain), qos_ext(_qos_ext) {}

inline bool DdstConf::operator==(const DdstConf& conf) const noexcept {
  return topic == conf.topic && domain == conf.domain && depth == conf.depth && qos == conf.qos &&
         qos_ext == conf.qos_ext;
}

inline bool DdstConf::operator!=(const DdstConf& conf) const noexcept { return !(*this == conf); }

inline TransportType DdstConf::get_transport_type() const { return TransportType::kDdst; }

}  // namespace vlink

#endif
