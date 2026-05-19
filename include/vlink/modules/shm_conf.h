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
 * @file shm_conf.h
 * @brief Transport configuration for the @c shm:// Iceoryx shared-memory backend.
 *
 * @details
 * @c ShmConf configures the Iceoryx-based shared-memory transport, which provides
 * zero-copy inter-process communication between processes on the same machine.
 * It requires the Iceoryx RouDi daemon to be running (either externally started or
 * initialised in-process via @c init_roudi()).
 *
 * @par Supported Node Types
 * @c shm:// supports all six node types: @c kPublisher, @c kSubscriber, @c kServer,
 * @c kClient, @c kSetter, and @c kGetter.
 *
 * @par URL Format
 * @code
 *   shm://<address>[?event=<name>&domain=<N>&depth=<N>&history=<N>&wait=<ms>]
 * @endcode
 *
 * | Component  | Description                                                          |
 * | ---------- | -------------------------------------------------------------------- |
 * | @c address | Service/topic name; formed from @c host + @c "/" + @c path           |
 * | @c event   | Optional secondary event name (@c ?event=)                           |
 * | @c domain  | Iceoryx domain ID (@c ?domain=, default 0)                           |
 * | @c depth   | Queue capacity override (@c ?depth=, default 0 uses backend default) |
 * | @c history | History count (@c ?history=); URL default 0, or 1 for field nodes    |
 * | @c wait    | Blocking-wait timeout in ms for pub/sub; not valid for RPC/field     |
 *
 * @par RouDi Lifecycle
 * RouDi is the Iceoryx memory manager daemon.  It can be started externally
 * (recommended for production) or embedded in the same process:
 * @code
 *   // Option A: in-process RouDi (single process only):
 *   vlink::ShmConf::init_roudi();
 *
 *   // Option B: connect to external RouDi:
 *   vlink::ShmConf::init_runtime("my_app");
 *
 *   // ... use SHM nodes ...
 *
 *   vlink::ShmConf::deinit_runtime();
 * @endcode
 *
 * @note This header is compiled only when @c VLINK_SUPPORT_SHM is defined.
 * @note The @c address and @c event strings must not exceed 80 characters each.
 * @note The @c wait mode is only valid for @c kPublisher / @c kSubscriber nodes;
 *       using it with RPC or field nodes causes @c parse_protocol() to return @c false.
 */

#pragma once

#ifdef VLINK_SUPPORT_SHM

#include <cstdint>
#include <string>

#include "../impl/conf.h"

namespace vlink {

/**
 * @struct ShmConf
 * @brief Configuration for the @c shm:// Iceoryx shared-memory transport.
 *
 * @details
 * Can be constructed directly or parsed from a URL string via @c Url.
 * All string fields are capped at 80 characters by Iceoryx naming constraints.
 */
struct VLINK_EXPORT ShmConf final : public Conf {
  std::string address;  ///< Service/topic address (host + "/" + path from URL); max 80 characters.
  std::string event;    ///< Optional secondary event name; max 80 characters.
  int32_t domain{0};    ///< Iceoryx domain identifier (non-negative).
  int32_t depth{0};     ///< Queue capacity override; 0 means use the backend default.
  int32_t history{0};   ///< History count; URL parsing defaults to 0, or 1 for setter/getter.
  int32_t wait{0};      ///< Blocking-wait timeout in ms; positive values enable pub/sub wait mode.

  /**
   * @brief Constructs a @c ShmConf with explicit parameters.
   *
   * @param _address  Service/topic address string; max 80 characters.
   * @param _event    Optional event name; max 80 characters; empty by default.
   * @param _domain   Domain identifier; default 0.
   * @param _depth    Queue capacity override; default 0.
   * @param _history  History count; default 0.
   * @param _wait     Blocking-wait timeout in ms; default 0 (disabled).
   */
  explicit ShmConf(const std::string& _address, const std::string& _event = "", int32_t _domain = 0, int32_t _depth = 0,
                   int32_t _history = 0, int32_t _wait = 0);

  /**
   * @brief Returns @c true if all fields equal those of @p conf.
   *
   * @param conf  Configuration to compare.
   * @return      @c true if all six fields match.
   */
  [[nodiscard]] bool operator==(const ShmConf& conf) const noexcept;

  /**
   * @brief Returns @c true if any field differs from @p conf.
   *
   * @param conf  Configuration to compare.
   * @return      Logical negation of @c operator==.
   */
  [[nodiscard]] bool operator!=(const ShmConf& conf) const noexcept;

  /**
   * @brief Returns @c TransportType::kShm identifying this transport.
   *
   * @return @c TransportType::kShm.
   */
  [[nodiscard]] TransportType get_transport_type() const override;

  /**
   * @brief Returns @c true if the in-process RouDi daemon has been initialised.
   *
   * @details
   * Delegates to @c ShmFactory::has_roudi_inited().  Only meaningful when
   * @c init_roudi() has been called from within this process.
   *
   * @return @c true if RouDi is running in-process.
   */
  [[nodiscard]] static bool has_roudi_inited();

  /**
   * @brief Returns @c true if the Iceoryx runtime has been initialised for this process.
   *
   * @details
   * Delegates to @c ShmFactory::has_runtime_inited().  Returns @c true after a
   * successful call to @c init_runtime() or after the @c global_init() path
   * initialises the runtime automatically.
   *
   * @return @c true if the Iceoryx runtime is ready.
   */
  [[nodiscard]] static bool has_runtime_inited();

  /**
   * @brief Cheap probe for whether an Iceoryx RouDi daemon is reachable.
   *
   * @details
   * Non-invasive check intended for CLI diagnostics, bench preflight, and UI
   * pre-launch hints. Does NOT create an Iceoryx runtime, does NOT open any
   * port, does NOT change any global state, and is safe to call repeatedly
   * from any thread.
   *
   * @return @c true if RouDi (or a process embedding RouDi such as
   *         @c vlink-proxy) is currently running on this host, otherwise
   *         @c false.
   *
   * @see ShmFactory::has_roudi_running()
   */
  [[nodiscard]] static bool has_roudi_running();

  /**
   * @brief One-line preflight: ensure RouDi is available for @c shm:// nodes.
   *
   * @details
   * POSIX: starts an in-process RouDi if none is running. Windows: when no
   * external RouDi is detected, returns @c false only if @p same_process_from_roudi
   * is @c true; otherwise it initializes the runtime without starting RouDi.
   * Result is cached across calls.
   *
   * @note Calls @c init_runtime() during this preflight and deinitializes it
   *       when the cached manager is destroyed.
   *
   * @param same_process_from_roudi  @c true when the RouDi runs in the same
   *                                 process (see @c init_roudi()).
   *
   * @return @c true if RouDi is ready, @c false otherwise.
   *
   * @see ShmFactory::auto_init_roudi()
   */
  [[nodiscard]] static bool auto_init_roudi(bool same_process_from_roudi = false);

  /**
   * @brief Starts an embedded Iceoryx RouDi daemon in the current process.
   *
   * @details
   * Use this when no external RouDi process is running and a single process
   * needs to act as both the memory manager and a participant.  Must be called
   * before any @c shm:// nodes are created.
   *
   * @param config_path        Path to a RouDi XML configuration file; empty for defaults.
   * @param memory_strategy    Memory pooling strategy (0 = default).
   * @param monitoring_enable  @c true to enable process-monitoring in RouDi.
   */
  static void init_roudi(const std::string& config_path = "", int memory_strategy = 0, bool monitoring_enable = true);

  /**
   * @brief Registers this process with the Iceoryx RouDi daemon.
   *
   * @details
   * Must be called once before creating any @c shm:// nodes when connecting
   * to an external RouDi.  The @p name should be unique across all processes
   * connecting to the same RouDi instance; Iceoryx truncates it to the runtime
   * name capacity.
   *
   * @param name                   Process registration name; empty uses a generated name.
   * @param same_process_from_roudi  @c true when the RouDi runs in the same process
   *                                 (see @c init_roudi()).
   */
  static void init_runtime(const std::string& name = "", bool same_process_from_roudi = false);

  /**
   * @brief Unregisters this process from the Iceoryx RouDi daemon.
   *
   * @details
   * Releases all Iceoryx resources associated with the current process.
   * Must be called before process exit when @c init_runtime() was used.
   */
  static void deinit_runtime();

#ifndef VLINK_ENABLE_C_INTERFACE
  VLINK_DECLARE_GLOBAL_PROPERTY()
#endif
  VLINK_ALLOW_IMPL_TYPE(kServer | kClient | kPublisher | kSubscriber | kSetter | kGetter)
  VLINK_CONF_IMPL(ShmConf)
};

////////////////////////////////////////////////////////////////
/// Details
////////////////////////////////////////////////////////////////

inline ShmConf::ShmConf(const std::string& _address, const std::string& _event, int32_t _domain, int32_t _depth,
                        int32_t _history, int32_t _wait)
    : address(_address), event(_event), domain(_domain), depth(_depth), history(_history), wait(_wait) {}

inline bool ShmConf::operator==(const ShmConf& conf) const noexcept {
  return address == conf.address && event == conf.event && domain == conf.domain && depth == conf.depth &&
         history == conf.history && wait == conf.wait;
}

inline bool ShmConf::operator!=(const ShmConf& conf) const noexcept { return !(*this == conf); }

inline TransportType ShmConf::get_transport_type() const { return TransportType::kShm; }

}  // namespace vlink

#endif
