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
 * @file qnx_conf.h
 * @brief Transport configuration for the @c qnx:// QNX IPC backend.
 *
 * @details
 * @c QnxConf configures the QNX IPC transport, which uses QNX native inter-process
 * communication primitives for same-machine message passing on QNX Neutrino RTOS
 * targets.  It provides deterministic, real-time message delivery without relying
 * on a third-party middleware layer.
 *
 * @par Supported Node Types
 * @c qnx:// supports all six node types: @c kPublisher, @c kSubscriber, @c kServer,
 * @c kClient, @c kSetter, and @c kGetter.
 *
 * @par URL Format
 * @code
 *   qnx://<address>[?event=<name>]
 * @endcode
 *
 * | Component  | Description                                                        |
 * | ---------- | ------------------------------------------------------------------ |
 * | @c address | QNX channel/topic name; formed from @c host + @c "/" + @c path     |
 * | @c event   | Optional secondary event filter (@c ?event=)                       |
 *
 * @par Example
 * @code
 *   vlink::Publisher<MyMsg>  pub("qnx://my_topic");
 *   vlink::Subscriber<MyMsg> sub("qnx://my_topic");
 * @endcode
 *
 * @note This header is compiled only when @c VLINK_SUPPORT_QNX is defined.
 * @note This transport is only available on QNX targets; it is not usable on Linux.
 * @note @c is_valid() returns @c false if @c address is empty.
 */

#pragma once

#ifdef VLINK_SUPPORT_QNX

#include <cstdint>
#include <string>

#include "../impl/conf.h"

namespace vlink {

/**
 * @struct QnxConf
 * @brief Configuration for the @c qnx:// QNX native IPC transport.
 *
 * @details
 * Can be constructed directly or parsed from a URL string via @c Url.
 */
struct VLINK_EXPORT QnxConf final : public Conf {
  std::string address;  ///< QNX channel/topic address (host + "/" + path from URL).
  std::string event;    ///< Optional secondary event filter string.

  /**
   * @brief Constructs a @c QnxConf with explicit parameters.
   *
   * @param _address  Channel/topic address string.
   * @param _event    Optional event filter; empty by default.
   */
  explicit QnxConf(const std::string& _address, const std::string& _event = "");

  /**
   * @brief Returns @c true if both fields equal those of @p conf.
   *
   * @param conf  Configuration to compare.
   * @return      @c true if @c address and @c event are equal.
   */
  [[nodiscard]] bool operator==(const QnxConf& conf) const noexcept;

  /**
   * @brief Returns @c true if either field differs from @p conf.
   *
   * @param conf  Configuration to compare.
   * @return      Logical negation of @c operator==.
   */
  [[nodiscard]] bool operator!=(const QnxConf& conf) const noexcept;

  /**
   * @brief Returns @c TransportType::kQnx identifying this transport.
   *
   * @return @c TransportType::kQnx.
   */
  [[nodiscard]] TransportType get_transport_type() const override;

#ifndef VLINK_ENABLE_C_INTERFACE
  VLINK_DECLARE_GLOBAL_PROPERTY()
#endif
  VLINK_ALLOW_IMPL_TYPE(kServer | kClient | kPublisher | kSubscriber | kSetter | kGetter)
  VLINK_CONF_IMPL(QnxConf)
};

////////////////////////////////////////////////////////////////
/// Details
////////////////////////////////////////////////////////////////

inline QnxConf::QnxConf(const std::string& _address, const std::string& _event) : address(_address), event(_event) {}

inline bool QnxConf::operator==(const QnxConf& conf) const noexcept {
  return address == conf.address && event == conf.event;
}

inline bool QnxConf::operator!=(const QnxConf& conf) const noexcept { return !(*this == conf); }

inline TransportType QnxConf::get_transport_type() const { return TransportType::kQnx; }

}  // namespace vlink

#endif
