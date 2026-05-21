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
 * @file dds_conf.h
 * @brief Transport configuration for the @c dds:// Fast-DDS RTPS backend.
 *
 * @details
 * @c DdsConf configures the eProsima Fast-DDS (Fast-RTPS) transport, which provides
 * standards-compliant DDS pub/sub and RPC over Ethernet.  It is the primary
 * cross-machine transport in VLink and is suitable for both LAN and WAN deployments.
 *
 * @par Supported Node Types
 * @c dds:// supports all six node types: @c kPublisher, @c kSubscriber, @c kServer,
 * @c kClient, @c kSetter, and @c kGetter.
 *
 * @par URL Format
 * @code
 *   dds://<topic>[?domain=<N>&depth=<N>&qos=<name>]
 *   dds://<topic>[?domain=<N>&part=<v>&topic=<v>&pub=<v>&sub=<v>&writer=<v>&reader=<v>]
 * @endcode
 *
 * | Component  | Description                                                               |
 * | ---------- | ------------------------------------------------------------------------- |
 * | @c topic   | DDS topic name; formed from @c host + @c "/" + @c path                    |
 * | @c domain  | DDS Domain ID (@c ?domain=, default from @c VLINK_DDS_DOMAIN env var)     |
 * | @c depth   | DDS history depth override; 0 keeps the selected QoS history depth        |
 * | @c qos     | Named QoS profile registered via @c register_qos() (@c ?qos=)             |
 * | @c qos_ext | Remaining query map after @c domain, @c depth, and @c qos are removed     |
 *
 * @par QoS Registration
 * Named QoS profiles must be registered before creating any @c dds:// nodes:
 * @code
 *   vlink::Qos my_qos;
 *   my_qos.reliability.kind = vlink::Qos::Reliability::kReliable;
 *   my_qos.durability.kind  = vlink::Qos::Durability::kTransientLocal;
 *   vlink::DdsConf::register_qos("my_profile", my_qos);
 *
 *   vlink::Subscriber<MyMsg> sub("dds://my_topic?qos=my_profile");
 * @endcode
 *
 * @par Type Support Registration
 * For CDR-serialised types the DDS type support factory must be registered
 * before any publisher or subscriber is created on that topic:
 * @code
 *   // Register request type only (for pub/sub):
 *   vlink::DdsConf::register_topic<MyMsgPubSubType>("my_topic");
 *
 *   // Register request + response types (for RPC):
 *   vlink::DdsConf::register_topic<MyReqPubSubType, MyRespPubSubType>("my_rpc_topic");
 *
 *   // Register from a full URL:
 *   vlink::DdsConf::register_url<MyMsgPubSubType>("dds://my_topic?domain=1");
 * @endcode
 *
 * @par Global QoS File
 * An XML QoS profile file can be loaded at startup so that DDS participants and
 * endpoints inherit profiles by name:
 * @code
 *   vlink::DdsConf::load_global_qos_file("/etc/vlink/dds_profile.xml");
 * @endcode
 *
 * @note This header is compiled only when @c VLINK_SUPPORT_DDS is defined.
 * @note @c qos and @c qos_ext are mutually exclusive; setting both causes
 *       @c is_valid() to return @c false.
 * @note Response topics are automatically registered with a @c "___resp" suffix.
 */

#pragma once

#ifdef VLINK_SUPPORT_DDS

#include <cstdint>
#include <map>
#include <shared_mutex>
#include <string>
#include <tuple>
#include <vector>

#include "../base/functional.h"
#include "../extension/qos.h"
#include "../impl/conf.h"

namespace eprosima::fastdds::dds {
class TopicDataType;
}

namespace vlink {

/**
 * @struct DdsConf
 * @brief Configuration for the @c dds:// Fast-DDS RTPS transport.
 *
 * @details
 * Holds the DDS topic name, domain ID, history depth, and QoS settings.
 * Can be constructed directly or parsed from a URL string via @c Url.
 */
struct VLINK_EXPORT DdsConf final : public Conf {
  std::string topic;  ///< DDS topic name (host + "/" + path from URL).
  int32_t domain{0};  ///< DDS Domain Participant ID (non-negative).
  int32_t depth{0};   ///< DDS history depth override; 0 keeps the selected QoS history depth.
  std::string qos;    ///< Named QoS profile key registered via @c register_qos().
  PropertiesMap
      qos_ext;  ///< Query map after removing @c domain, @c depth, and @c qos; unknown keys are kept but warned.

  /**
   * @brief Constructs a @c DdsConf with topic, domain, depth, and named QoS.
   *
   * @param _topic   DDS topic name.
   * @param _domain  DDS domain ID; default 0.
   * @param _depth   History depth override; default 0.
   * @param _qos     Named QoS profile key; empty by default.
   */
  explicit DdsConf(const std::string& _topic, int32_t _domain = 0, int32_t _depth = 0, const std::string& _qos = "");

  /**
   * @brief Constructs a @c DdsConf with topic, domain, and extended QoS map.
   *
   * @details
   * Use this constructor when fine-grained per-entity QoS is required.
   * @c qos and @c qos_ext are mutually exclusive; @c is_valid() returns @c false
   * if both are non-empty.
   *
   * @param _topic    DDS topic name.
   * @param _domain   DDS domain ID.
   * @param _qos_ext  Per-entity QoS properties map.
   */
  explicit DdsConf(const std::string& _topic, int32_t _domain, const PropertiesMap& _qos_ext);

  /**
   * @brief Returns @c true if all fields equal those of @p conf.
   *
   * @param conf  Configuration to compare.
   * @return      @c true if @c topic, @c domain, @c depth, @c qos, and @c qos_ext all match.
   */
  [[nodiscard]] bool operator==(const DdsConf& conf) const noexcept;

  /**
   * @brief Returns @c true if any field differs from @p conf.
   *
   * @param conf  Configuration to compare.
   * @return      Logical negation of @c operator==.
   */
  [[nodiscard]] bool operator!=(const DdsConf& conf) const noexcept;

  /**
   * @brief Returns @c TransportType::kDds identifying this transport.
   *
   * @return @c TransportType::kDds.
   */
  [[nodiscard]] TransportType get_transport_type() const override;

  /**
   * @brief Returns the list of currently discovered DDS topics on the given domain.
   *
   * @details
   * Queries the @c DdsFactory for live topic discovery information.  Each entry
   * in the returned vector is a @c (topic_name, type_name) pair.
   *
   * @param _domain  DDS domain ID to query.
   * @return         Vector of @c (topic_name, type_name) tuples; may be empty.
   */
  [[nodiscard]] static std::vector<std::tuple<std::string, std::string>> get_discovered_topics(int32_t _domain);

  /**
   * @brief Loads a Fast-DDS XML QoS profile file as the global default.
   *
   * @details
   * Must be called before any @c dds:// participants are created.  The profiles
   * defined in the file are available to all Fast-DDS endpoints in the process.
   *
   * @param filepath  Absolute or relative path to the XML QoS profile file.
   * @return          @c true if the file was loaded successfully; @c false otherwise.
   */
  static bool load_global_qos_file(const std::string& filepath);

  /**
   * @brief Registers a Fast-DDS type support factory for a topic by name.
   *
   * @details
   * Required for CDR-serialised (non-Protobuf) message types.  Must be called
   * once per topic before any publisher or subscriber is created on that topic.
   * An optional response type (@c TypeSupportRespT) can be registered simultaneously
   * for RPC topics; it is stored under @c name + @c "___resp".
   *
   * @tparam TypeSupportT      Fast-DDS @c TopicDataType subclass for the request/message type.
   * @tparam TypeSupportRespT  Fast-DDS @c TopicDataType subclass for the response type;
   *                           defaults to @c void (no response type registered).
   * @param name               DDS topic name to associate the type support with.
   */
  template <typename TypeSupportT, typename TypeSupportRespT = void>
  static void register_topic(const std::string& name);

  /**
   * @brief Registers a Fast-DDS type support factory for a topic derived from a URL.
   *
   * @details
   * Convenience wrapper that extracts the topic name from @p name using
   * @c UrlParser and then calls @c register_topic<TypeSupportT, TypeSupportRespT>().
   *
   * @tparam TypeSupportT      Type support for the request/message type.
   * @tparam TypeSupportRespT  Type support for the response type; @c void if not needed.
   * @param name               Full URL string (e.g. @c "dds://my_topic?domain=1").
   */
  template <typename TypeSupportT, typename TypeSupportRespT = void>
  static void register_url(const std::string& name);

  /**
   * @brief Registers a named QoS profile for use by @c dds:// nodes.
   *
   * @details
   * The @p name is associated with the @p qos object and can then be referenced
   * in URL query strings as @c ?qos=name.  Names that conflict with reserved
   * DDS entity keys (@c part, @c topic, @c pub, @c sub, @c writer, @c reader, @c depth)
   * or that are already registered cause a fatal log and are rejected.
   *
   * @param name  Unique profile name; must not be one of the reserved keys.
   * @param qos   @c Qos object describing the quality-of-service settings.
   */
  static void register_qos(const std::string& name, const Qos& qos);

 private:
  template <typename TypeSupportT>
  static void register_topic_internal(const std::string& name);

  static void register_qos_internal(const std::string& name, const Qos& qos);

  static Function<void*()> find_type_support(const std::string& name);

  static const Qos& find_qos(const std::string& name);

  static std::string get_topic_for_url(const std::string& url);

  friend class DdsFactory;
  static std::map<std::string, Function<void*()>> type_support_map_;
  static std::map<std::string, Qos> qos_map_;
  static std::shared_mutex mtx_;
  static constexpr const char* kRespSuffix{"___resp"};
#ifndef VLINK_ENABLE_C_INTERFACE
  VLINK_DECLARE_GLOBAL_PROPERTY()
#endif
  VLINK_ALLOW_IMPL_TYPE(kServer | kClient | kPublisher | kSubscriber | kSetter | kGetter)
  VLINK_CONF_IMPL(DdsConf)
};

////////////////////////////////////////////////////////////////
/// Details
////////////////////////////////////////////////////////////////

inline DdsConf::DdsConf(const std::string& _topic, int32_t _domain, int32_t _depth, const std::string& _qos)
    : topic(_topic), domain(_domain), depth(_depth), qos(_qos) {}

inline DdsConf::DdsConf(const std::string& _topic, int32_t _domain, const PropertiesMap& _qos_ext)
    : topic(_topic), domain(_domain), qos_ext(_qos_ext) {}

inline bool DdsConf::operator==(const DdsConf& conf) const noexcept {
  return topic == conf.topic && domain == conf.domain && depth == conf.depth && qos == conf.qos &&
         qos_ext == conf.qos_ext;
}

inline bool DdsConf::operator!=(const DdsConf& conf) const noexcept { return !(*this == conf); }

inline TransportType DdsConf::get_transport_type() const { return TransportType::kDds; }

template <typename TypeSupportT, typename TypeSupportRespT>
inline void DdsConf::register_topic(const std::string& name) {
  std::lock_guard lock(mtx_);
  register_topic_internal<TypeSupportT>(name);
  if constexpr (!std::is_same_v<TypeSupportRespT, void>) {
    register_topic_internal<TypeSupportRespT>(name + kRespSuffix);
  }
}

template <typename TypeSupportT, typename TypeSupportRespT>
inline void DdsConf::register_url(const std::string& name) {
  register_topic<TypeSupportT, TypeSupportRespT>(get_topic_for_url(name));
}

template <typename TypeSupportT>
inline void DdsConf::register_topic_internal(const std::string& name) {
  static_assert(std::is_base_of_v<eprosima::fastdds::dds::TopicDataType, TypeSupportT>, "Must be dds type.");

  type_support_map_[name] = [] { return new TypeSupportT(); };
}

}  // namespace vlink

#endif
