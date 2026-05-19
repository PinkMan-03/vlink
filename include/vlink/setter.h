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
 * @file setter.h
 * @brief Type-safe field-model writer for VLink topics.
 *
 * @details
 * @c Setter<ValueT, SecT> is the write side of the VLink field model.
 * Each call to @c set() serialises the value and writes it to the transport
 * as the new "latest value" for the topic.  All @c Getter nodes on the same
 * URL will be notified of the update.
 *
 * @par Field Model Overview
 * @code
 *  Setter<T>                 Transport Back-end              Getter<T>
 *      |                          |                               |
 *      |-- set(value) ----------> |                               |
 *      |   serialize(value)       |                               |
 *      |                          |-- latest-value delivery ----> |
 *      |                          |  (overwrites previous)        |--> callback(value)
 *      |                          |                               |--> get() => value
 * @endcode
 *
 * @par Key Differences: Setter vs Publisher
 * | Feature                  | @c Setter<T>                         | @c Publisher<T>                |
 * | ------------------------ | ------------------------------------ | ------------------------------ |
 * | Transport role           | @c kSetter                           | @c kPublisher                  |
 * | Value retention          | Last value re-sent to late getters   | No re-send to late subs        |
 * | Sync callback on connect | Yes -- @c sync() callback            | No                             |
 * | Cross-transport use      | As publisher: @c mark_as_publisher() | As setter: @c mark_as_setter() |
 *
 * @par Sync on Late Connect
 * When a @c Getter connects after the @c Setter has already written, the transport
 * fires the @c sync() callback registered internally.  The @c Setter re-sends
 * its cached value so the late @c Getter receives the current state immediately:
 * @code
 * Setter<int> s("shm://my_field");
 * s.set(42);          // Getter joins later and immediately receives 42
 * @endcode
 * @par Usage
 * @code
 * Setter<MyStruct> setter("dds://vehicle/gear");
 * setter.set(MyStruct{3, "Drive"});  // write a new field value
 * @endcode
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

#include "./impl/setter_impl.h"
#include "./node.h"

namespace vlink {

/**
 * @class Setter
 * @brief Type-safe field writer for the VLink field communication model.
 *
 * @tparam ValueT  Value type to write.
 * @tparam SecT    Security mode.
 */
template <typename ValueT, SecurityType SecT = SecurityType::kWithoutSecurity>
class Setter : public Node<SetterImpl, SecT> {
 public:
  /** @brief Unique-pointer alias. */
  using UniquePtr = std::unique_ptr<Setter<ValueT, SecT>>;

  /** @brief Shared-pointer alias. */
  using SharedPtr = std::shared_ptr<Setter<ValueT, SecT>>;

  /** @brief Node role identifier (@c kSetter). */
  static constexpr ImplType kImplType = kSetter;

  /** @brief Serializer type resolved at compile time from @c ValueT. */
  static constexpr Serializer::Type kValueType = Serializer::get_type_of<ValueT>();

  static_assert(Serializer::is_supported(kValueType), "<ValueT> is not a supported Serializer type.");

  /**
   * @brief Creates a @c Setter on the heap wrapped in a @c unique_ptr.
   *
   * @param url_str  Field URL string (e.g. @c "shm://vehicle/gear").
   * @param type     @c kWithInit to call @c init() immediately (default).
   * @return         @c UniquePtr owning the new setter.
   */
  [[nodiscard]] static UniquePtr create_unique(const std::string& url_str, InitType type = InitType::kWithInit);

  /**
   * @brief Creates a @c Setter on the heap wrapped in a @c shared_ptr.
   *
   * @param url_str  Field URL string.
   * @param type     @c kWithInit to call @c init() immediately (default).
   * @return         @c SharedPtr owning the new setter.
   */
  [[nodiscard]] static SharedPtr create_shared(const std::string& url_str, InitType type = InitType::kWithInit);

  /**
   * @brief Constructs a setter from a typed transport configuration object.
   *
   * @details
   * Accepts any @c Conf-derived configuration.  After @c init(), registers an
   * internal @c sync() callback with the transport so that when a late @c Getter
   * connects the cached value is re-sent automatically.
   *
   * @tparam ConfT  @c Conf-derived configuration type.
   * @param conf    Populated configuration object.
   * @param type    @c kWithInit to call @c init() immediately (default).
   */
  // NOLINTNEXTLINE(modernize-use-constraints)
  template <typename ConfT, typename = std::enable_if_t<std::is_base_of_v<Conf, ConfT>>>
  explicit Setter(const ConfT& conf, InitType type = InitType::kWithInit);

  /**
   * @brief Constructs a setter from a URL string.
   *
   * @param url_str  Field URL (e.g. @c "dds://vehicle/gear").
   * @param type     @c kWithInit to call @c init() immediately (default).
   */
  explicit Setter(const std::string& url_str, InitType type = InitType::kWithInit);

  /**
   * @brief Destroys the setter, calling @c deinit() to drain any in-flight transport
   *        callbacks before this @c Setter's locked state is destroyed.
   *
   * @details
   * Mirrors the lifecycle contract used by @c Client and @c Getter: without this
   * destructor the @c sync() callback installed in @c init() can fire on the
   * transport thread after @c mtx_ / @c value_ have already been destroyed.
   */
  ~Setter() override;

  /**
   * @brief Initializes the setter transport and registers the late-getter sync callback.
   *
   * @details
   * Calls the base @c Node initialization, then installs a transport @c sync()
   * callback that re-sends the cached latest value when a new @c Getter joins
   * after this setter has already been written to.
   *
   * @return @c true on success, @c false if transport initialization fails.
   */
  bool init() override;

  /**
   * @brief Deinitializes the setter and releases the underlying transport resources.
   *
   * @details
   * Delegates to the base @c Node deinitialization path. After deinitialization,
   * the setter stops participating in transport sync and write operations until
   * it is initialized again.
   *
   * @return @c true on success, @c false if the underlying transport reports failure.
   */
  bool deinit() override;

  /**
   * @brief Writes a new field value and notifies all connected @c Getter nodes.
   *
   * @details
   * Caches @p value internally (guarded by mutex), then serializes and writes
   * it to the transport.  If security is enabled the serialized bytes are
   * encrypted before transmission.  The cached value is automatically re-sent
   * when a late @c Getter connects.
   *
   * @param value  The new field value to write.
   */
  void set(const ValueT& value);

  /**
   * @brief Changes this setter's role to @c kPublisher (event-emitter).
   *
   * @details
   * Updates @c impl_->impl_type from @c kSetter to @c kPublisher so that
   * event-model transport semantics are applied (no last-value retention).
   * If called after @c init(), the extension is automatically reinitialised.
   * Used when a @c Setter should behave as a plain publisher on transports
   * that do not natively distinguish the two roles.
   */
  void mark_as_publisher();

 private:
  void write(const ValueT& value);

  void write_bytes(const Bytes& data);

  std::optional<ValueT> value_;
  std::mutex mtx_;
};

/**
 * @class SecuritySetter
 * @brief Convenience alias for @c Setter with message security enabled.
 *
 * @details
 * Equivalent to @c Setter<ValueT, SecurityType::kWithSecurity>.  Encrypts
 * each outgoing value using the configured security key or callbacks.
 *
 * @tparam ValueT  Value type to write.
 */
template <typename ValueT>
class SecuritySetter : public Setter<ValueT, SecurityType::kWithSecurity> {
 public:
  /** @brief Unique-pointer alias. */
  using UniquePtr = std::unique_ptr<SecuritySetter<ValueT>>;

  /** @brief Shared-pointer alias. */
  using SharedPtr = std::shared_ptr<SecuritySetter<ValueT>>;

  /**
   * @brief Creates a @c SecuritySetter on the heap wrapped in a @c unique_ptr.
   *
   * @param url_str  Field URL string (e.g. @c "shm://vehicle/gear").
   * @param sec_cfg  Security configuration aggregate (empty by default → drops outbound updates).
   * @param type     @c kWithInit to call @c init() immediately (default).
   * @return         @c UniquePtr owning the new setter.
   */
  // NOLINTNEXTLINE(modernize-use-constraints)
  template <typename SecurityConfigT = Security::Config>
  [[nodiscard]] static UniquePtr create_unique(const std::string& url_str, SecurityConfigT&& sec_cfg = {},
                                               InitType type = InitType::kWithInit);

  /**
   * @brief Creates a @c SecuritySetter on the heap wrapped in a @c shared_ptr.
   *
   * @param url_str  Field URL string.
   * @param sec_cfg  Security configuration aggregate (empty by default → drops outbound updates).
   * @param type     @c kWithInit to call @c init() immediately (default).
   * @return         @c SharedPtr owning the new setter.
   */
  // NOLINTNEXTLINE(modernize-use-constraints)
  template <typename SecurityConfigT = Security::Config>
  [[nodiscard]] static SharedPtr create_shared(const std::string& url_str, SecurityConfigT&& sec_cfg = {},
                                               InitType type = InitType::kWithInit);

  /**
   * @brief Constructs a @c SecuritySetter from a typed transport configuration object.
   *
   * @tparam ConfT  @c Conf-derived configuration type.
   * @param conf    Populated configuration object.
   * @param sec_cfg Security configuration aggregate (empty by default).
   * @param type    @c kWithInit to call @c init() immediately (default).
   */
  // NOLINTNEXTLINE(modernize-use-constraints)
  template <typename ConfT, typename SecurityConfigT = Security::Config,
            typename = std::enable_if_t<std::is_base_of_v<Conf, ConfT>>>
  explicit SecuritySetter(const ConfT& conf, SecurityConfigT&& sec_cfg = {}, InitType type = InitType::kWithInit);

  /**
   * @brief Constructs a @c SecuritySetter and installs the security configuration in place.
   *
   * @details
   * Always builds the base @c Setter with @c InitType::kWithoutInit, then
   * forwards @p sec_cfg into @c enable_security() so that @c NodeImpl::security is
   * either populated or left empty.  Finally calls @c init() unless the
   * caller requests deferred initialisation.
   *
   * @param url_str  Field URL string.
   * @param sec_cfg  Security configuration aggregate (empty by default → drops outbound updates).
   * @param type     @c kWithInit to call @c init() immediately (default).
   */
  // NOLINTNEXTLINE(modernize-use-constraints)
  template <typename SecurityConfigT = Security::Config>
  explicit SecuritySetter(const std::string& url_str, SecurityConfigT&& sec_cfg = {},
                          InitType type = InitType::kWithInit);
};

}  // namespace vlink

#include "./internal/setter-inl.h"
