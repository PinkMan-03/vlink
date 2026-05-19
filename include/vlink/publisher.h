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
 * @file publisher.h
 * @brief Type-safe event-model publisher for VLink topics.
 *
 * @details
 * @c Publisher<MsgT, SecT> is the write side of the VLink event model.
 * It serialises a @c MsgT object and delivers it to all @c Subscriber nodes
 * that share the same URL.
 *
 * @par Event Model Data Flow
 * @code
 *  Publisher<T>              Transport Back-end            Subscriber<T>
 *      |                          |                             |
 *      |-- publish(msg) --------> |                             |
 *      |   serialize(msg)         |                             |
 *      |   [loan if supported]    |-- bytes delivery ---------> |
 *      |                          |                             |--> callback(msg)
 *      |                          |                             |    deserialize(bytes)
 * @endcode
 *
 * @par Supported Message Types
 * | Category        | Type                           | Serializer       |
 * | --------------- | ------------------------------ | ---------------- |
 * | Raw bytes       | @c Bytes                       | kBytesType       |
 * | Protobuf        | @c MyProto (MessageLite)       | kProtoType       |
 * | Protobuf ptr    | @c MyProto* (Arena-managed)    | kProtoPtrType    |
 * | FlatBuffers     | @c MyTableT (NativeTable)      | kFlatTableType   |
 * | FlatBuffers     | @c MyBuilder (has fbb_+Finish) | kFlatBuilderType |
 * | CDR (DDS only)  | @c MyCdrType (serialize/des.)  | kCdrType         |
 * | Standard layout | POD struct / trivial type      | kStandardType    |
 * | String          | @c std::string                 | kStringType      |
 * | Custom          | @c T (has operator>>/<<)       | kCustomType      |
 *
 * @par Subscriber Detection
 * @code
 * Publisher<MyMsg> pub("dds://topic");
 * pub.detect_subscribers([](bool has) {       // async notification
 *     if (has) pub.publish(MyMsg{});
 * });
 * pub.wait_for_subscribers();                 // blocking until subscriber appears
 * if (pub.has_subscribers()) { ... }          // non-blocking query
 * @endcode
 *
 * @par Zero-copy on loan-capable transports
 * @code
 * Publisher<Bytes> pub("shm://topic");
 * if (pub.is_support_loan()) {
 *     Bytes buf = pub.loan(sizeof(MyStruct));
 *     new (buf.data()) MyStruct{...};
 *     pub.publish(buf);                       // no copy; loan returned automatically
 * }
 * @endcode
 *
 * @note Pass @p force = @c true to @c publish() to send even when no subscribers
 *       are present (e.g. for guaranteed-delivery or field-mode semantics).
 *
 * @tparam MsgT  Message type.  Must satisfy @c Serializer::is_supported().
 * @tparam SecT  Security mode; defaults to @c SecurityType::kWithoutSecurity.
 */

#pragma once

#include <memory>
#include <string>
#include <type_traits>

#include "./impl/publisher_impl.h"
#include "./node.h"

namespace vlink {

/**
 * @class Publisher
 * @brief Type-safe publisher for the VLink event communication model.
 *
 * @tparam MsgT  Message type.
 * @tparam SecT  Security mode.
 */
template <typename MsgT, SecurityType SecT = SecurityType::kWithoutSecurity>
class Publisher : public Node<PublisherImpl, SecT> {
 public:
  /** @brief Unique-pointer alias for heap allocation. */
  using UniquePtr = std::unique_ptr<Publisher<MsgT, SecT>>;

  /** @brief Shared-pointer alias for heap allocation. */
  using SharedPtr = std::shared_ptr<Publisher<MsgT, SecT>>;

  /** @brief Callback type fired when subscriber presence changes. */
  using ConnectCallback = NodeImpl::ConnectCallback;

  /** @brief Node role identifier (@c kPublisher). */
  static constexpr ImplType kImplType = kPublisher;

  /** @brief Serializer type resolved at compile time from @c MsgT. */
  static constexpr Serializer::Type kMsgType = Serializer::get_type_of<MsgT>();

  static_assert(Serializer::is_supported(kMsgType), "<MsgT> is not a supported Serializer type.");

  /**
   * @brief Creates a @c Publisher on the heap wrapped in a @c unique_ptr.
   *
   * @param url_str  Topic URL string (e.g. @c "dds://vehicle/speed").
   * @param type     @c kWithInit to call @c init() immediately (default).
   * @return         @c UniquePtr owning the new publisher instance.
   */
  [[nodiscard]] static UniquePtr create_unique(const std::string& url_str, InitType type = InitType::kWithInit);

  /**
   * @brief Creates a @c Publisher on the heap wrapped in a @c shared_ptr.
   *
   * @param url_str  Topic URL string.
   * @param type     @c kWithInit to call @c init() immediately (default).
   * @return         @c SharedPtr owning the new publisher instance.
   */
  [[nodiscard]] static SharedPtr create_shared(const std::string& url_str, InitType type = InitType::kWithInit);

  /**
   * @brief Constructs a publisher from a typed transport configuration object.
   *
   * @details
   * Accepts any @c Conf-derived configuration (e.g. @c DdsConf, @c ShmConf).
   * A compile-time @c static_assert checks that the @c Conf supports the
   * publisher role.  Parses and validates the conf; logs a fatal error if the
   * conf is invalid or if the transport factory returns @c nullptr.
   *
   * @tparam ConfT  @c Conf-derived configuration type.
   * @param conf    Populated configuration object.
   * @param type    @c kWithInit to call @c init() immediately (default).
   */
  // NOLINTNEXTLINE(modernize-use-constraints)
  template <typename ConfT, typename = std::enable_if_t<std::is_base_of_v<Conf, ConfT>>>
  explicit Publisher(const ConfT& conf, InitType type = InitType::kWithInit);

  /**
   * @brief Constructs a publisher from a URL string.
   *
   * @details
   * The transport prefix in the URL selects the transport back-end automatically (e.g.
   * @c "shm://" => Iceoryx, @c "dds://" => FastDDS).  Internally wraps the
   * string in a @c Url object and delegates to the @c ConfT constructor.
   *
   * @param url_str  Topic URL (e.g. @c "shm://vehicle/speed").
   * @param type     @c kWithInit to call @c init() immediately (default).
   */
  explicit Publisher(const std::string& url_str, InitType type = InitType::kWithInit);

  /**
   * @brief Registers a callback invoked when subscriber presence changes.
   *
   * @details
   * Fires immediately (synchronously) if a subscriber is already present at
   * registration time.  Otherwise fires asynchronously the first time a
   * subscriber appears.  The callback receives @c true when at least one
   * subscriber is present and @c false when the last subscriber disconnects.
   *
   * @param callback  @c void(bool) callable -- @c true = subscriber(s) present.
   */
  void detect_subscribers(ConnectCallback&& callback);

  /**
   * @brief Blocks until at least one subscriber is present or the timeout expires.
   *
   * @details
   * A timeout of @c 0 is treated as infinite (a warning is logged).  A
   * negative timeout also waits indefinitely.  Can be interrupted by calling
   * @c interrupt(), which causes this method to return @c false.
   *
   * @param timeout  Maximum wait duration.  Default: @c Timeout::kDefaultInterval.
   * @return         @c true if a subscriber appeared; @c false on timeout or interrupt.
   */
  bool wait_for_subscribers(std::chrono::milliseconds timeout = Timeout::kDefaultInterval);

  /**
   * @brief Returns @c true if at least one subscriber is currently present.
   *
   * @details
   * Non-blocking poll; reflects the transport's last known peer state.
   *
   * @return @c true when one or more subscribers are known to exist.
   */
  [[nodiscard]] bool has_subscribers() const;

  /**
   * @brief Serialises and publishes @p msg to all current subscribers.
   *
   * @details
   * Serialization is performed according to @c kMsgType.  On loan-capable
   * transports the output buffer may be a loaned segment to avoid an extra
   * copy.  Loaned buffers are not used when security is
   * enabled (@c kWithSecurity) because the encrypted payload size differs
   * from the serialized size.
   *
   * By default (@p force = @c false) the call is skipped (returns @c false)
   * when no subscribers are present; pass @c true to force-write regardless
   * (useful for field-mode or recording-only scenarios).
   *
   * For @c intra:// with a message type whose @c element_type derives from
   * @c IntraDataType (generated via @c VLINK_INTRA_DATA_DECLARE), the pointer
   * is forwarded zero-copy and no serialization occurs.
   *
   * @param msg    Message value to publish.
   * @param force  @c true to publish even with no subscribers.
   * @return       @c true if the transport accepted the message; @c false on error.
   */
  bool publish(const MsgT& msg, bool force = false);

  /**
   * @brief Publishes a pre-finished @c FlatBufferBuilder buffer directly.
   *
   * @details
   * Accepts a pointer to a @c flatbuffers::FlatBufferBuilder whose @c Finish()
   * has already been called.  The raw bytes are shallow-copied and sent without
   * re-serialisation.  Useful in pipeline scenarios where the FlatBuffers object
   * is constructed externally.
   *
   * @param fbb    Pointer to a finished @c flatbuffers::FlatBufferBuilder.
   * @param force  @c true to publish even with no subscribers.
   * @return       @c true on success; @c false on error.
   */
  bool publish_fbb(const void* fbb, bool force = false);

  /**
   * @brief Changes this publisher's role to @c kSetter (field-writer).
   *
   * @details
   * Updates @c impl_->impl_type from @c kPublisher to @c kSetter so that
   * transport-specific field semantics are applied (e.g. last-value retention
   * for late-joining getters).  If called after @c init() the extension is
   * automatically reinitialised.  Used internally by @c Setter.
   */
  void mark_as_setter();

 private:
  bool write_bytes(const Bytes& data);

  bool write_intra(const IntraData& intra_data);
};

/**
 * @class SecurityPublisher
 * @brief Convenience alias for @c Publisher with message security enabled.
 *
 * @details
 * Equivalent to @c Publisher<MsgT, SecurityType::kWithSecurity>.  Encrypts
 * every outgoing message with the @c Security::Config passed at construction
 * (second constructor argument; defaults to an empty config).
 *
 * @note Not supported on @c intra:// or @c dds:// CDR transport.
 *
 * @tparam MsgT  Message type to publish.
 */
template <typename MsgT>
class SecurityPublisher : public Publisher<MsgT, SecurityType::kWithSecurity> {
 public:
  /** @brief Unique-pointer alias for heap allocation. */
  using UniquePtr = std::unique_ptr<SecurityPublisher<MsgT>>;

  /** @brief Shared-pointer alias for heap allocation. */
  using SharedPtr = std::shared_ptr<SecurityPublisher<MsgT>>;

  /**
   * @brief Creates a @c SecurityPublisher on the heap wrapped in a @c unique_ptr.
   *
   * @param url_str  Topic URL string (e.g. @c "dds://vehicle/speed").
   * @param sec_cfg  Security configuration aggregate (empty by default; must configure a usable slot before init).
   * @param type     @c kWithInit to call @c init() immediately (default).
   * @return         @c UniquePtr owning the new publisher instance.
   */
  // NOLINTNEXTLINE(modernize-use-constraints)
  template <typename SecurityConfigT = Security::Config>
  [[nodiscard]] static UniquePtr create_unique(const std::string& url_str, SecurityConfigT&& sec_cfg = {},
                                               InitType type = InitType::kWithInit);

  /**
   * @brief Creates a @c SecurityPublisher on the heap wrapped in a @c shared_ptr.
   *
   * @param url_str  Topic URL string.
   * @param sec_cfg  Security configuration aggregate (empty by default; must configure a usable slot before init).
   * @param type     @c kWithInit to call @c init() immediately (default).
   * @return         @c SharedPtr owning the new publisher instance.
   */
  // NOLINTNEXTLINE(modernize-use-constraints)
  template <typename SecurityConfigT = Security::Config>
  [[nodiscard]] static SharedPtr create_shared(const std::string& url_str, SecurityConfigT&& sec_cfg = {},
                                               InitType type = InitType::kWithInit);

  /**
   * @brief Constructs a @c SecurityPublisher from a typed transport configuration object.
   *
   * @details
   * Builds the base @c Publisher with @c InitType::kWithoutInit using @p conf,
   * then forwards @p sec_cfg into @c enable_security().  @c init() requires that
   * @c NodeImpl::security was populated successfully; finally calls @c init()
   * unless the caller requests deferred initialisation.
   *
   * @tparam ConfT  @c Conf-derived configuration type.
   * @param conf    Populated configuration object.
   * @param sec_cfg Security configuration aggregate (empty by default; must configure a usable slot before init).
   * @param type    @c kWithInit to call @c init() immediately (default).
   */
  // NOLINTNEXTLINE(modernize-use-constraints)
  template <typename ConfT, typename SecurityConfigT = Security::Config,
            typename = std::enable_if_t<std::is_base_of_v<Conf, ConfT>>>
  explicit SecurityPublisher(const ConfT& conf, SecurityConfigT&& sec_cfg = {}, InitType type = InitType::kWithInit);

  /**
   * @brief Constructs a @c SecurityPublisher and installs the security configuration in place.
   *
   * @details
   * Always builds the base @c Publisher with @c InitType::kWithoutInit, then
   * forwards @p sec_cfg into @c enable_security().  @c init() requires that
   * @c NodeImpl::security was populated successfully; finally calls @c init()
   * unless the caller requests deferred initialisation.
   *
   * @param url_str  Topic URL string.
   * @param sec_cfg  Security configuration aggregate (empty by default; must configure a usable slot before init).
   * @param type     @c kWithInit to call @c init() immediately (default).
   */
  // NOLINTNEXTLINE(modernize-use-constraints)
  template <typename SecurityConfigT = Security::Config>
  explicit SecurityPublisher(const std::string& url_str, SecurityConfigT&& sec_cfg = {},
                             InitType type = InitType::kWithInit);
};

}  // namespace vlink

#include "./internal/publisher-inl.h"
