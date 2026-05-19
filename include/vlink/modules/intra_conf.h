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
 * @file intra_conf.h
 * @brief Transport configuration for the @c intra:// in-process messaging backend.
 *
 * @details
 * @c IntraConf configures the in-process message queue transport, which delivers
 * messages between publishers and subscribers running in the same OS process.
 * Regular message types still use the normal @c Serializer path; the intra
 * @c IntraDataType shared-pointer special case can bypass serialization and
 * forward the pointer zero-copy.
 *
 * @par Supported Node Types
 * @c intra:// supports all six node types: @c kPublisher, @c kSubscriber,
 * @c kServer, @c kClient, @c kSetter, and @c kGetter.
 *
 * @par URL Format
 * @code
 *   intra://<address>[?event=<event_name>[&pipeline=<id>]][#<type>]
 * @endcode
 *
 * | Component          | Description                                            |
 * | ------------------ | ------------------------------------------------------ |
 * | @c address         | Topic name; formed from @c host + @c "/" + @c path     |
 * | @c event           | Optional secondary filter string (@c ?event=)          |
 * | @c pipeline        | Queue-mode pipeline ID (@c ?pipeline=id, default 0)    |
 * | @c type            | Delivery mode: @c "queue" or @c "direct" (fragment)    |
 *
 * @par Example
 * @code
 *   // In-process pub/sub (queue mode, default):
 *   vlink::Publisher<MyMsg> pub("intra://my_topic");
 *   vlink::Subscriber<MyMsg> sub("intra://my_topic");
 *
 *   // Direct delivery (bypasses queue):
 *   vlink::Publisher<MyMsg> pub("intra://my_topic#direct");
 *
 *   // With secondary event filter and queue-mode pipeline ID:
 *   vlink::Publisher<MyMsg> pub("intra://my_service?event=my_event&pipeline=4");
 * @endcode
 *
 * @note This header is compiled only when @c VLINK_SUPPORT_INTRA is defined.
 * @note The @c address string must not be empty.
 * @note @c is_valid() also requires @c type to be either @c "queue" or @c "direct".
 */

#pragma once

#ifdef VLINK_SUPPORT_INTRA

#include <cstdint>
#include <string>

#include "../impl/conf.h"

namespace vlink {

/**
 * @struct IntraConf
 * @brief Configuration for the @c intra:// in-process transport.
 *
 * @details
 * Can be constructed directly or parsed from a URL string via @c Url.
 * When parsed from a URL, @c address is derived from the host and path components,
 * @c event from @c ?event=, @c pipeline from @c ?pipeline=, and @c type from the
 * URL fragment (@c #queue or @c #direct).
 */
struct VLINK_EXPORT IntraConf final : public Conf {
  std::string address;        ///< Topic address (host + "/" + path from URL); must not be empty.
  std::string event;          ///< Optional secondary event filter string.
  int32_t pipeline{0};        ///< Queue-mode pipeline ID; 0 selects the default pipeline.
  std::string type{"queue"};  ///< Delivery mode: @c "queue" (buffered) or @c "direct" (bypass queue).

  /**
   * @brief Constructs an @c IntraConf with explicit parameters.
   *
   * @param _address   Topic address string; must not be empty.
   * @param _event     Optional secondary event filter; empty by default.
   * @param _pipeline  Queue-mode pipeline ID; default 0.
   * @param _type      Delivery mode: @c "queue" or @c "direct"; default @c "queue".
   */
  explicit IntraConf(const std::string& _address, const std::string& _event = "", int _pipeline = 0,
                     const std::string& _type = "queue");

  /**
   * @brief Returns @c true if all fields of this configuration are equal to @p conf.
   *
   * @param conf  Configuration to compare.
   * @return      @c true if @c address, @c event, @c pipeline, and @c type all match.
   */
  [[nodiscard]] bool operator==(const IntraConf& conf) const noexcept;

  /**
   * @brief Returns @c true if any field differs from @p conf.
   *
   * @param conf  Configuration to compare.
   * @return      Logical negation of @c operator==.
   */
  [[nodiscard]] bool operator!=(const IntraConf& conf) const noexcept;

  /**
   * @brief Returns @c TransportType::kIntra identifying this transport.
   *
   * @return @c TransportType::kIntra.
   */
  [[nodiscard]] TransportType get_transport_type() const override;

#ifndef VLINK_ENABLE_C_INTERFACE
  VLINK_DECLARE_GLOBAL_PROPERTY()
#endif
  VLINK_ALLOW_IMPL_TYPE(kServer | kClient | kPublisher | kSubscriber | kSetter | kGetter)
  VLINK_CONF_IMPL(IntraConf)
};

////////////////////////////////////////////////////////////////
/// Details
////////////////////////////////////////////////////////////////

inline IntraConf::IntraConf(const std::string& _address, const std::string& _event, int _pipeline,
                            const std::string& _type)
    : address(_address), event(_event), pipeline(_pipeline), type(_type) {}

inline bool IntraConf::operator==(const IntraConf& conf) const noexcept {
  return address == conf.address && event == conf.event && pipeline == conf.pipeline && type == conf.type;
}

inline bool IntraConf::operator!=(const IntraConf& conf) const noexcept { return !(*this == conf); }

inline TransportType IntraConf::get_transport_type() const { return TransportType::kIntra; }

}  // namespace vlink

#endif
