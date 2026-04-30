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
 * @file shm2_conf.h
 * @brief Transport configuration for the @c shm2:// Iceoryx2 shared-memory backend.
 *
 * @details
 * @c Shm2Conf configures the Iceoryx2-based shared-memory transport, which is the
 * next-generation successor to the @c shm:// Iceoryx backend.  Like @c ShmConf it
 * provides zero-copy inter-process communication on the same machine, but uses the
 * Iceoryx2 API and does not require a separate RouDi daemon process.
 *
 * @par Supported Node Types
 * @c shm2:// supports all six node types: @c kPublisher, @c kSubscriber, @c kServer,
 * @c kClient, @c kSetter, and @c kGetter.
 *
 * @par URL Format
 * @code
 *   shm2://<address>[?event=<name>&domain=<N>&depth=<N>&history=<N>&wait=<0|1>][#<size>]
 * @endcode
 *
 * | Component  | Description                                                          |
 * | ---------- | -------------------------------------------------------------------- |
 * | @c address | Service/topic name; formed from @c host + @c "/" + @c path           |
 * | @c event   | Optional secondary event name (@c ?event=)                           |
 * | @c domain  | Domain ID (@c ?domain=, default 0)                                   |
 * | @c depth   | History buffer depth (@c ?depth=, default 0)                         |
 * | @c history | History count; defaults to 0 for pub/sub and 1 for field nodes       |
 * | @c wait    | Blocking-wait mode for pub/sub only (@c ?wait=1)                     |
 * | @c size    | Shared-memory allocation size from URL fragment (see below)          |
 *
 * @par Size Fragment Syntax
 * The URL fragment sets the shared-memory region size per message.  Supported
 * unit suffixes (case-insensitive): @c B, @c K/@c KB, @c M/@c MB, @c G/@c GB.
 * The value must be in the range @c (0, @c kMaxMemSize].
 * @code
 *   shm2://my_topic#1M    // 1 MiB per message
 *   shm2://my_topic#512K  // 512 KiB per message
 *   shm2://my_topic       // default: 128 bytes
 * @endcode
 *
 * @note This header is compiled only when @c VLINK_SUPPORT_SHM2 is defined.
 * @note Address and event strings must not exceed 80 characters each.
 * @note The @c wait mode is only valid for @c kPublisher / @c kSubscriber nodes.
 */

#pragma once

#ifdef VLINK_SUPPORT_SHM2

#include <cstdint>
#include <string>

#include "../impl/conf.h"

namespace vlink {

/**
 * @struct Shm2Conf
 * @brief Configuration for the @c shm2:// Iceoryx2 shared-memory transport.
 *
 * @details
 * Extends the @c ShmConf field set with a @c size parameter that controls the
 * shared-memory allocation granularity per message.  Can be constructed directly
 * or parsed from a URL string via @c Url.
 */
struct VLINK_EXPORT Shm2Conf final : public Conf {
  std::string address;             ///< Topic address (host + "/" + path from URL); max 80 characters.
  std::string event;               ///< Optional secondary event name; max 80 characters.
  int32_t domain{0};               ///< Domain identifier (non-negative).
  int32_t depth{0};                ///< History buffer depth; 0 means no buffering.
  int32_t history{0};              ///< History count; defaults to 0 for pub/sub and 1 for field nodes.
  int32_t wait{0};                 ///< Non-zero enables blocking-wait mode (pub/sub only).
  uint64_t size{kDefaultMemSize};  ///< Per-message shared-memory region size in bytes.

  /**
   * @brief Constructs a @c Shm2Conf with explicit parameters.
   *
   * @param _address  Topic address string; max 80 characters.
   * @param _event    Optional event name; max 80 characters; empty by default.
   * @param _domain   Domain identifier; default 0.
   * @param _depth    Buffer depth; default 0.
   * @param _history  History count; default 0.
   * @param _wait     Blocking-wait flag; default 0 (disabled).
   * @param _size     Memory region size in bytes; default @c kDefaultMemSize (128 B).
   */
  explicit Shm2Conf(const std::string& _address, const std::string& _event = "", int32_t _domain = 0,
                    int32_t _depth = 0, int32_t _history = 0, int32_t _wait = 0, uint64_t _size = kDefaultMemSize);

  /**
   * @brief Returns @c true if all fields equal those of @p conf.
   *
   * @param conf  Configuration to compare.
   * @return      @c true if all seven fields match.
   */
  [[nodiscard]] bool operator==(const Shm2Conf& conf) const noexcept;

  /**
   * @brief Returns @c true if any field differs from @p conf.
   *
   * @param conf  Configuration to compare.
   * @return      Logical negation of @c operator==.
   */
  [[nodiscard]] bool operator!=(const Shm2Conf& conf) const noexcept;

  /**
   * @brief Returns @c TransportType::kShm2 identifying this transport.
   *
   * @return @c TransportType::kShm2.
   */
  [[nodiscard]] TransportType get_transport_type() const override;

  static constexpr size_t kDefaultMemSize = 128U;              ///< Default memory size per message: 128 bytes.
  static constexpr size_t kMaxMemSize = 1024UL * 1024UL * 32;  ///< Maximum allowed memory size: 32 MiB.

#ifndef VLINK_ENABLE_C_INTERFACE
  VLINK_DECLARE_GLOBAL_PROPERTY()
#endif
  VLINK_ALLOW_IMPL_TYPE(kServer | kClient | kPublisher | kSubscriber | kSetter | kGetter)
  VLINK_CONF_IMPL(Shm2Conf)
};

////////////////////////////////////////////////////////////////
/// Details
////////////////////////////////////////////////////////////////

inline Shm2Conf::Shm2Conf(const std::string& _address, const std::string& _event, int32_t _domain, int32_t _depth,
                          int32_t _history, int32_t _wait, uint64_t _size)
    : address(_address), event(_event), domain(_domain), depth(_depth), history(_history), wait(_wait), size(_size) {}

inline bool Shm2Conf::operator==(const Shm2Conf& conf) const noexcept {
  return address == conf.address && event == conf.event && domain == conf.domain && depth == conf.depth &&
         history == conf.history && wait == conf.wait && size == conf.size;
}

inline bool Shm2Conf::operator!=(const Shm2Conf& conf) const noexcept { return !(*this == conf); }

inline TransportType Shm2Conf::get_transport_type() const { return TransportType::kShm2; }

}  // namespace vlink

#endif
