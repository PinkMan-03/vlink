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
 * @file getter.h
 * @brief Type-safe field-model reader for VLink topics.
 *
 * @details
 * @c Getter<ValueT, SecT> is the read side of the VLink field model.
 * It maintains the latest value published by any matching @c Setter on the
 * same URL.  Unlike @c Subscriber, it retains only the most recent value
 * rather than queuing a history of updates.
 *
 * @par Field Model Overview
 * @code
 *  Setter<T>                 Transport Back-end              Getter<T>
 *      |                          |                               |
 *      |-- set(value) ----------> |                               |
 *      |   serialize(value)       |-- latest-value delivery ----> |
 *      |                          |  (overwrites previous)        |--> listen callback(value)
 *      |                          |                               |    value_ updated
 *      |                          |                               |--> get() returns latest
 * @endcode
 *
 * @par Key Differences: Getter vs Subscriber
 * | Feature                 | @c Getter<T>                           | @c Subscriber<T>               |
 * | ----------------------- | -------------------------------------- | ------------------------------ |
 * | Value retention         | Latest value cached                    | No caching                     |
 * | Transport role          | @c kGetter                             | @c kSubscriber                 |
 * | Blocking read           | @c wait_for_value()                    | N/A                            |
 * | Change-reporting filter | @c set_change_reporting(true)          | No built-in filter             |
 * | Cross-transport use     | As subscriber: @c mark_as_subscriber() | As getter: @c mark_as_getter() |
 *
 * @par Usage Patterns
 * @code
 * // Pattern 1: polling
 * Getter<int> g("shm://my_field");
 * if (auto v = g.get()) {
 *     std::cout << "value: " << *v << std::endl;
 * }
 *
 * // Pattern 2: blocking wait
 * if (g.wait_for_value()) {
 *     auto v = g.get();
 * }
 *
 * // Pattern 3: value-change callback (fires when Setter writes a new value)
 * g.listen([](const int& v) { std::cout << "updated: " << v << std::endl; });
 *
 * // Pattern 4: change-only reporting (suppress duplicate values)
 * g.set_change_reporting(true);
 * g.listen([](const int& v) { ... });
 * @endcode
 *
 * @note @c get() and @c wait_for_value() both require a prior Setter write to
 *       return a value.  Before any Setter has written, @c get() returns
 *       @c std::nullopt and @c wait_for_value() blocks until one does.
 *
 * @tparam ValueT  Value type.  Must satisfy @c Serializer::is_supported().
 * @tparam SecT    Security mode; defaults to @c SecurityType::kWithoutSecurity.
 */

#pragma once

#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <type_traits>

#include "./base/condition_variable.h"
#include "./base/functional.h"
#include "./impl/getter_impl.h"
#include "./node.h"

namespace vlink {

/**
 * @class Getter
 * @brief Type-safe field reader for the VLink field communication model.
 *
 * @tparam ValueT  Value type to read.
 * @tparam SecT    Security mode.
 */
template <typename ValueT, SecurityType SecT = SecurityType::kWithoutSecurity>
class Getter : public Node<GetterImpl, SecT> {
 public:
  /** @brief Unique-pointer alias. */
  using UniquePtr = std::unique_ptr<Getter<ValueT, SecT>>;

  /** @brief Shared-pointer alias. */
  using SharedPtr = std::shared_ptr<Getter<ValueT, SecT>>;

  /** @brief User-facing callback type for value-change notifications. */
  using MsgCallback = Function<void(const ValueT&)>;

  /** @brief Node role identifier (@c kGetter). */
  static constexpr ImplType kImplType = kGetter;

  /** @brief Serializer type resolved at compile time from @c ValueT. */
  static constexpr Serializer::Type kValueType = Serializer::get_type_of<ValueT>();

  static_assert(Serializer::is_supported(kValueType), "<ValueT> is not a supported Serializer type.");

  /**
   * @brief Creates a @c Getter on the heap wrapped in a @c unique_ptr.
   *
   * @param url_str  Field URL string (e.g. @c "shm://vehicle/gear").
   * @param type     @c kWithInit to call @c init() immediately (default).
   * @return         @c UniquePtr owning the new getter.
   */
  [[nodiscard]] static UniquePtr create_unique(const std::string& url_str, InitType type = InitType::kWithInit);

  /**
   * @brief Creates a @c Getter on the heap wrapped in a @c shared_ptr.
   *
   * @param url_str  Field URL string.
   * @param type     @c kWithInit to call @c init() immediately (default).
   * @return         @c SharedPtr owning the new getter.
   */
  [[nodiscard]] static SharedPtr create_shared(const std::string& url_str, InitType type = InitType::kWithInit);

  /**
   * @brief Constructs a getter from a typed transport configuration object.
   *
   * @tparam ConfT  @c Conf-derived configuration type.
   * @param conf    Populated configuration object.
   * @param type    @c kWithInit to call @c init() immediately (default).
   */
  // NOLINTNEXTLINE(modernize-use-constraints)
  template <typename ConfT, typename = std::enable_if_t<std::is_base_of_v<Conf, ConfT>>>
  explicit Getter(const ConfT& conf, InitType type = InitType::kWithInit);

  /**
   * @brief Constructs a getter from a URL string.
   *
   * @param url_str  Field URL (e.g. @c "shm://vehicle/gear").
   * @param type     @c kWithInit to call @c init() immediately (default).
   */
  explicit Getter(const std::string& url_str, InitType type = InitType::kWithInit);

  /**
   * @brief Destroys the getter and releases any subscribed resources.
   */
  ~Getter() override;

  /**
   * @brief Returns the latest cached value, if one has been received.
   *
   * @details
   * Thread-safe.  Returns @c std::nullopt if no @c Setter has written a value
   * yet since this getter was initialised.
   *
   * @return @c std::optional<ValueT> -- holds the latest value, or empty.
   */
  [[nodiscard]] std::optional<ValueT> get() const;

  /**
   * @brief Blocks until a value is received or the timeout expires.
   *
   * @details
   * Returns immediately if a value is already available.  A @p timeout of @c 0
   * is treated as infinite (a warning is logged).  Can be interrupted by
   * @c interrupt(), which causes this method to return @c false.
   *
   * After returning @c true, call @c get() to retrieve the value.
   *
   * @param timeout  Maximum wait duration.  Default: @c Timeout::kDefaultInterval.
   * @return         @c true if a value was received; @c false on timeout or interrupt.
   */
  bool wait_for_value(std::chrono::milliseconds timeout = Timeout::kDefaultInterval);

  /**
   * @brief Registers a callback invoked whenever a new value arrives.
   *
   * @details
   * The callback receives a pre-deserialized @c ValueT reference.  It is
   * invoked on every setter write unless @c set_change_reporting(true) is
   * active, in which case duplicate values (same raw bytes as the last) are
   * suppressed before the callback is called.
   *
   * Internally, @c Getter also maintains @c value_ and notifies @c wait_for_value().
   * The listen callback is stored once; calling @c listen() more than once is
   * a fatal error.
   *
   * @param callback  @c void(const ValueT&) invoked on each new value.
   * @return          @c true if the callback was stored; @c false if a callback
   *                  was already registered.
   */
  bool listen(MsgCallback&& callback);

  /**
   * @brief Enables or disables change-reporting (suppress duplicate values).
   *
   * @details
   * When @c true, incoming values whose raw serialized bytes are identical to
   * the previous delivery are silently dropped -- neither the callback nor
   * @c wait_for_value() is notified.  Useful for reducing CPU load when a
   * @c Setter writes the same value repeatedly.
   *
   * @param enable  @c true to enable change-only reporting; @c false to disable.
   */
  void set_change_reporting(bool enable);

  /**
   * @brief Enables or disables manual-unloan mode for zero-copy receives.
   *
   * @param manual_unloan  @c true to enable; @c false for automatic (default).
   */
  void set_manual_unloan(bool manual_unloan) override;

  /**
   * @brief Enables or disables per-value latency and sample-loss tracking.
   *
   * @param enable  @c true to start tracking; @c false to stop.
   */
  void set_latency_and_lost_enabled(bool enable);

  /**
   * @brief Returns @c true if latency and sample-loss tracking is active.
   *
   * @return @c true if @c set_latency_and_lost_enabled(true) was called.
   */
  [[nodiscard]] bool is_latency_and_lost_enabled() const;

  /**
   * @brief Returns the most recently measured end-to-end value latency.
   *
   * @return Latency in microseconds; @c 0 if tracking is disabled.
   */
  [[nodiscard]] int64_t get_latency() const;

  /**
   * @brief Returns cumulative sample delivery statistics.
   *
   * @return @c SampleLostInfo with @c total expected and @c lost value counts.
   */
  [[nodiscard]] SampleLostInfo get_lost() const;

  /**
   * @brief Returns @c true if change-reporting mode is currently active.
   *
   * @return @c true if duplicate suppression is enabled.
   */
  [[nodiscard]] bool get_change_reporting() const;

  /**
   * @brief Initialises the getter and registers the internal delivery callback.
   *
   * @details
   * Overrides @c Node::init() to also set up the internal @c listen_bytes()
   * callback that receives raw bytes, deserialises them into @c ValueT,
   * stores the result in @c value_, and fires the user @c listen() callback.
   *
   * @return @c true on first initialisation; @c false if already initialised.
   */
  bool init() override;

  /**
   * @brief Interrupts any blocking @c wait_for_value() call.
   *
   * @details
   * Overrides @c Node::interrupt() to additionally notify the getter's own
   * condition variable, causing @c wait_for_value() to return @c false.
   */
  void interrupt() override;

  /**
   * @brief Changes this getter's role to @c kSubscriber (event-receiver).
   *
   * @details
   * Updates @c impl_->impl_type from @c kGetter to @c kSubscriber so that
   * transport-specific event semantics are applied.  If called after @c init(),
   * the extension is automatically reinitialised.  Used internally when
   * routing a @c Getter through an event-capable transport.
   */
  void mark_as_subscriber();

 private:
  void listen_bytes(NodeImpl::MsgCallback&& callback);

  std::optional<ValueT> value_;
  mutable std::mutex mtx_;
  ConditionVariable cv_;
  MsgCallback callback_;
  Bytes last_cache_;
  bool has_value_notification_{false};
  bool change_reporting_{false};
};

/**
 * @class SecurityGetter
 * @brief Convenience alias for @c Getter with message security enabled.
 *
 * @details
 * Equivalent to @c Getter<ValueT, SecurityType::kWithSecurity>.  Decrypts
 * each incoming value using the configured security key or callbacks.
 *
 * @tparam ValueT  Value type to read.
 */
template <typename ValueT>
class SecurityGetter : public Getter<ValueT, SecurityType::kWithSecurity> {
 public:
  /** @brief Unique-pointer alias. */
  using UniquePtr = std::unique_ptr<SecurityGetter<ValueT>>;

  /** @brief Shared-pointer alias. */
  using SharedPtr = std::shared_ptr<SecurityGetter<ValueT>>;

  /**
   * @brief Creates a @c SecurityGetter on the heap wrapped in a @c unique_ptr.
   *
   * @param url_str  Field URL string (e.g. @c "shm://vehicle/gear").
   * @param sec_cfg  Security configuration aggregate (empty by default; must configure a usable slot before init).
   * @param type     @c kWithInit to call @c init() immediately (default).
   * @return         @c UniquePtr owning the new getter.
   */
  // NOLINTNEXTLINE(modernize-use-constraints)
  template <typename SecurityConfigT = Security::Config>
  [[nodiscard]] static UniquePtr create_unique(const std::string& url_str, SecurityConfigT&& sec_cfg = {},
                                               InitType type = InitType::kWithInit);

  /**
   * @brief Creates a @c SecurityGetter on the heap wrapped in a @c shared_ptr.
   *
   * @param url_str  Field URL string.
   * @param sec_cfg  Security configuration aggregate (empty by default; must configure a usable slot before init).
   * @param type     @c kWithInit to call @c init() immediately (default).
   * @return         @c SharedPtr owning the new getter.
   */
  // NOLINTNEXTLINE(modernize-use-constraints)
  template <typename SecurityConfigT = Security::Config>
  [[nodiscard]] static SharedPtr create_shared(const std::string& url_str, SecurityConfigT&& sec_cfg = {},
                                               InitType type = InitType::kWithInit);

  /**
   * @brief Constructs a @c SecurityGetter from a typed transport configuration object.
   *
   * @tparam ConfT  @c Conf-derived configuration type.
   * @param conf    Populated configuration object.
   * @param sec_cfg Security configuration aggregate (empty by default; must configure a usable slot before init).
   * @param type    @c kWithInit to call @c init() immediately (default).
   */
  // NOLINTNEXTLINE(modernize-use-constraints)
  template <typename ConfT, typename SecurityConfigT = Security::Config,
            typename = std::enable_if_t<std::is_base_of_v<Conf, ConfT>>>
  explicit SecurityGetter(const ConfT& conf, SecurityConfigT&& sec_cfg = {}, InitType type = InitType::kWithInit);

  /**
   * @brief Constructs a @c SecurityGetter and installs the security configuration in place.
   *
   * @details
   * Always builds the base @c Getter with @c InitType::kWithoutInit, then
   * forwards @p sec_cfg into @c enable_security().  @c init() requires that
   * @c NodeImpl::security was populated successfully; finally calls @c init()
   * unless the caller requests deferred initialisation.
   *
   * @param url_str  Field URL string.
   * @param sec_cfg  Security configuration aggregate (empty by default; must configure a usable slot before init).
   * @param type     @c kWithInit to call @c init() immediately (default).
   */
  // NOLINTNEXTLINE(modernize-use-constraints)
  template <typename SecurityConfigT = Security::Config>
  explicit SecurityGetter(const std::string& url_str, SecurityConfigT&& sec_cfg = {},
                          InitType type = InitType::kWithInit);
};

}  // namespace vlink

#include "./internal/getter-inl.h"
