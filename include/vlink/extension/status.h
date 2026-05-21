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
 * @file status.h
 * @brief DDS-compatible status type hierarchy for VLink publisher and subscriber callbacks.
 *
 * @details
 * The @c Status namespace defines an event-driven status reporting model that mirrors the
 * DDS status change notification system.  When a transport-level event occurs (e.g., a
 * new subscriber appears, a deadline is missed), the middleware creates a concrete
 * @c Status::Base subclass and delivers it to the registered status callback.
 *
 * Status types are divided into two groups:
 *
 * Writer-side (DataWriter / Publisher / Server / Setter):
 *
 * | Type                       | Triggered when                                          |
 * | -------------------------- | ------------------------------------------------------- |
 * | @c kPublicationMatched     | A matching subscriber appeared or disappeared           |
 * | @c kOfferedDeadlineMissed  | Writer failed to publish within its declared deadline   |
 * | @c kOfferedIncompatibleQos | A subscriber with incompatible QoS was discovered       |
 * | @c kLivelinessLost         | Writer failed to assert liveliness within its duration  |
 *
 * Reader-side (DataReader / Subscriber / Client / Getter):
 *
 * | Type                       | Triggered when                                           |
 * | -------------------------- | -------------------------------------------------------- |
 * | @c kSubscriptionMatched    | A matching publisher appeared or disappeared             |
 * | @c kRequestedDeadlineMissed| Reader did not receive data within its declared deadline |
 * | @c kLivelinessChanged      | A writer's liveliness status changed                     |
 * | @c kSampleRejected         | An incoming sample was rejected (resource limit hit)     |
 * | @c kRequestedIncompatibleQos | A publisher with incompatible QoS was discovered       |
 * | @c kSampleLost             | A sample was lost before it could be delivered           |
 *
 * Concrete status structs (counters, handles, etc.) are defined in @c status_detail.h.
 *
 * @par Usage
 * @code
 * auto sub = vlink::Subscriber<MyMsg>::create("dds://my_topic");
 * sub->register_status_callback([](vlink::Status::BasePtr status) {
 *     if (status->get_type() == vlink::Status::kSubscriptionMatched) {
 *         auto matched = status->as<vlink::Status::SubscriptionMatched>();
 *         VLOG_I("Matched publishers: ", matched->current_count);
 *     }
 * });
 * @endcode
 */

#pragma once

#include <cstdint>
#include <iostream>
#include <memory>
#include <string>
#include <type_traits>

#include "../base/exception.h"
#include "../base/macros.h"

namespace vlink {

/**
 * @namespace vlink::Status
 * @brief DDS-compatible status type enumeration, base class, and type-safe cast utilities.
 */
namespace Status {  // NOLINT(readability-identifier-naming)

/**
 * @brief Discriminator for concrete status event types.
 *
 * @details
 * Values below @c kSubscriptionMatched are writer-side events; values from
 * @c kSubscriptionMatched onwards are reader-side events.
 * Use @c is_for_writer() / @c is_for_reader() for category tests.
 */
enum Type : uint8_t {
  kUnknown = 0,  ///< Unknown or uninitialised status.
  // -- For writer
  kPublicationMatched = 1,      ///< A matching subscriber appeared or was removed.
  kOfferedDeadlineMissed = 2,   ///< Writer missed its offered deadline.
  kOfferedIncompatibleQos = 3,  ///< Subscriber with incompatible QoS discovered.
  kLivelinessLost = 4,          ///< Writer liveliness assertion failed.
  // -- For reader
  kSubscriptionMatched = 5,       ///< A matching publisher appeared or was removed.
  kRequestedDeadlineMissed = 6,   ///< Reader did not receive data within its deadline.
  kLivelinessChanged = 7,         ///< Publisher liveliness state changed.
  kSampleRejected = 8,            ///< Incoming sample was rejected (resource limit).
  kRequestedIncompatibleQos = 9,  ///< Publisher with incompatible QoS discovered.
  kSampleLost = 10,               ///< Sample was lost before delivery.
};

/**
 * @brief Returns @c true if @p type is a writer-side status event.
 *
 * @param type  Status type to classify.
 * @return @c true for any type with a value below @c kSubscriptionMatched
 *         (excluding @c kUnknown, which returns @c false).
 */
[[nodiscard]] [[maybe_unused]] static constexpr bool is_for_writer(Type type) noexcept {
  return type != kUnknown && type < kSubscriptionMatched;
}

/**
 * @brief Returns @c true if @p type is a reader-side status event.
 *
 * @param type  Status type to classify.
 * @return @c true for types @c kSubscriptionMatched and above.
 */
[[nodiscard]] [[maybe_unused]] static constexpr bool is_for_reader(Type type) noexcept {
  return type >= kSubscriptionMatched;
}

/**
 * @brief Opaque handle type for DDS instance identifiers.
 *
 * @details
 * Carries a transport-level pointer to the matched publication or subscription
 * endpoint.  The exact value is transport-specific and should be treated as opaque.
 */
using InstanceHandle = const void*;

/**
 * @struct Base
 * @brief Abstract base class for all VLink status event objects.
 *
 * @details
 * All concrete status types (PublicationMatched, SampleRejected, etc.) derive from
 * @c Base and are delivered as @c std::shared_ptr<Status::Base> via status callbacks.
 * Use @c as<T>() to safely downcast to a specific concrete type.
 *
 * Inherits @c std::enable_shared_from_this to support safe @c shared_ptr construction
 * from within member functions.
 */
struct VLINK_EXPORT Base : public std::enable_shared_from_this<Base> {
 protected:
  Base();

  virtual ~Base();

 public:
  /**
   * @brief Returns the concrete status type discriminator.
   *
   * @return One of the @c Status::Type enum values.
   */
  [[nodiscard]] virtual Type get_type() const = 0;

  /**
   * @brief Returns the status event name.
   *
   * @return String name of the concrete status type.  Use @c operator<< to include field values.
   */
  [[nodiscard]] virtual std::string get_string() const = 0;

  /**
   * @brief Safely downcasts this status to a concrete type @p T.
   *
   * @details
   * Performs a @c dynamic_pointer_cast.  If the status type is @c kUnknown,
   * throws @c Exception::RuntimeError.
   *
   * @tparam T  Concrete status struct type (e.g., @c Status::SubscriptionMatched).
   *            Must be derived from @c Base and not be @c Status::Unknown.
   * @return @c shared_ptr<T> to the concrete status.
   * @throws Exception::RuntimeError if the status type is @c kUnknown.
   */
  template <typename T>
  [[nodiscard]] std::shared_ptr<T> as() const;

  /**
   * @brief Writes the human-readable status description to @p ostream.
   *
   * @param ostream  Output stream.
   * @param status   Status object to print.
   * @return Reference to @p ostream.
   */
  VLINK_EXPORT friend std::ostream& operator<<(std::ostream& ostream, const Base& status) noexcept;
};

/**
 * @struct Unknown
 * @brief Placeholder status returned when the transport reports an unrecognised event type.
 *
 * @details
 * Callers should check @c get_type() == @c kUnknown and skip processing.
 */
struct VLINK_EXPORT Unknown final : public Base {
 public:
  /**
   * @brief Returns @c kUnknown.
   *
   * @return Always @c kUnknown.
   */
  [[nodiscard]] Type get_type() const override;

  /**
   * @brief Returns "Unknown".
   *
   * @return Descriptive string.
   */
  [[nodiscard]] std::string get_string() const override;

  /**
   * @brief Writes "Unknown" to @p ostream.
   *
   * @param ostream  Output stream.
   * @param status   This @c Unknown status.
   * @return Reference to @p ostream.
   */
  VLINK_EXPORT friend std::ostream& operator<<(std::ostream& ostream, const Unknown& status) noexcept;
};

/**
 * @brief Type alias for a shared pointer to a base status event.
 *
 * @details
 * Used as the parameter type for status callbacks registered on publishers,
 * subscribers, clients, servers, getters, and setters.
 */
using BasePtr = std::shared_ptr<Status::Base>;

/**
 * @brief Writes the human-readable description of @p status to @p ostream.
 *
 * @param ostream  Output stream.
 * @param status   Shared pointer to a status event.
 * @return Reference to @p ostream.
 */
VLINK_EXPORT std::ostream& operator<<(std::ostream& ostream, const BasePtr& status) noexcept;

////////////////////////////////////////////////////////////////
/// Details
////////////////////////////////////////////////////////////////

template <typename T>
inline std::shared_ptr<T> Base::as() const {
  static_assert(std::is_base_of_v<Base, T> && !std::is_same_v<struct Unknown, T>,
                "Can not convert target status type.");

  if VUNLIKELY (get_type() == kUnknown) {
    throw Exception::RuntimeError("Target status is unknown");
  }

#if defined(NDEBUG) || defined(__ANDROID__)
  return std::static_pointer_cast<T>(const_cast<Base*>(this)->shared_from_this());
#else
  return std::dynamic_pointer_cast<T>(const_cast<Base*>(this)->shared_from_this());
#endif
}

}  // namespace Status

}  // namespace vlink
