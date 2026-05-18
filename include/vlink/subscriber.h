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
 * @file subscriber.h
 * @brief Type-safe event-model subscriber for VLink topics.
 *
 * @details
 * @c Subscriber<MsgT, SecT> is the read side of the VLink event model.
 * It registers a callback that is invoked with each deserialized @c MsgT
 * message delivered from any matching @c Publisher on the same URL.
 *
 * @par Event Model Data Flow
 * @code
 *  Publisher<T>              Transport Back-end            Subscriber<T>
 *      |-- publish(msg) --------> |                             |
 *      |   serialize(msg)         |-- bytes delivery ---------> |
 *      |                          |                             |--> callback(msg)
 *      |                          |                             |    deserialize(bytes)
 * @endcode
 *
 * @par Supported Message Types
 * | Category        | Type                        | Serializer       |
 * | --------------- | --------------------------- | ---------------- |
 * | Raw bytes       | @c Bytes                    | kBytesType       |
 * | Protobuf        | @c MyProto (MessageLite)    | kProtoType       |
 * | Protobuf ptr    | @c MyProto* (Arena-managed) | kProtoPtrType    |
 * | FlatBuffers     | @c MyTableT (NativeTable)   | kFlatTableType   |
 * | FlatBuffers     | @c MyTable* (Table ptr)     | kFlatPtrType     |
 * | CDR (DDS only)  | @c MyCdrType                | kCdrType         |
 * | Standard layout | POD struct / trivial type   | kStandardType    |
 * | String          | @c std::string              | kStringType      |
 * | Custom          | @c T (has operator>>/<<)    | kCustomType      |
 *
 * @par Basic Usage
 * @code
 * Subscriber<MyMsg> sub("dds://vehicle/speed");
 * sub.listen([](const MyMsg& msg) {
 *     std::cout << "speed: " << msg.value << std::endl;
 * });
 * @endcode
 *
 * @par Zero-copy Intra Transport
 * When @c MsgT is a shared pointer type whose @c element_type derives from
 * @c IntraDataType (generated via @c VLINK_INTRA_DATA_DECLARE) and the URL
 * uses @c intra://, the shared pointer is forwarded zero-copy (no
 * serialization):
 * @code
 * VLINK_INTRA_DATA_DECLARE(MyProtoMsg, MyIntra)
 * Subscriber<MyIntra> sub("intra://my_topic");
 * sub.listen([](const MyIntra& data) { ... });
 * @endcode
 *
 * @par Latency and Sample-loss Tracking
 * @code
 * Subscriber<MyMsg> sub("dds://my_topic");
 * sub.set_latency_and_lost_enabled(true);
 * // ... after receiving messages:
 * int64_t latency_us = sub.get_latency();
 * SampleLostInfo lost = sub.get_lost();
 * std::cout << "lost: " << lost.lost << " / " << lost.total << std::endl;
 * @endcode
 *
 * @note Calling @c listen() more than once is a fatal error.  The subscriber
 *       must be initialised before @c listen() is called.
 *
 * @tparam MsgT  Message type.  Must satisfy @c Serializer::is_supported().
 * @tparam SecT  Security mode; defaults to @c SecurityType::kWithoutSecurity.
 */

#pragma once

#include <memory>
#include <string>

#include "./base/functional.h"
#include "./impl/subscriber_impl.h"
#include "./node.h"

namespace vlink {

/**
 * @class Subscriber
 * @brief Type-safe subscriber for the VLink event communication model.
 *
 * @tparam MsgT  Message type.
 * @tparam SecT  Security mode.
 */
template <typename MsgT, SecurityType SecT = SecurityType::kWithoutSecurity>
class Subscriber : public Node<SubscriberImpl, SecT> {
 public:
  /** @brief Unique-pointer alias for heap allocation. */
  using UniquePtr = std::unique_ptr<Subscriber<MsgT, SecT>>;

  /** @brief Shared-pointer alias for heap allocation. */
  using SharedPtr = std::shared_ptr<Subscriber<MsgT, SecT>>;

  /** @brief User-facing callback type for received messages. */
  using MsgCallback = Function<void(const MsgT&)>;

  /** @brief Node role identifier (@c kSubscriber). */
  static constexpr ImplType kImplType = kSubscriber;

  /** @brief Serializer type resolved at compile time from @c MsgT. */
  static constexpr Serializer::Type kMsgType = Serializer::get_type_of<MsgT>();

  static_assert(Serializer::is_supported(kMsgType), "<MsgT> is not a supported Serializer type.");

  /**
   * @brief Creates a @c Subscriber on the heap wrapped in a @c unique_ptr.
   *
   * @param url_str  Topic URL string (e.g. @c "dds://vehicle/speed").
   * @param type     @c kWithInit to call @c init() immediately (default).
   * @return         @c UniquePtr owning the new subscriber.
   */
  [[nodiscard]] static UniquePtr create_unique(const std::string& url_str, InitType type = InitType::kWithInit);

  /**
   * @brief Creates a @c Subscriber on the heap wrapped in a @c shared_ptr.
   *
   * @param url_str  Topic URL string.
   * @param type     @c kWithInit to call @c init() immediately (default).
   * @return         @c SharedPtr owning the new subscriber.
   */
  [[nodiscard]] static SharedPtr create_shared(const std::string& url_str, InitType type = InitType::kWithInit);

  /**
   * @brief Constructs a subscriber from a typed transport configuration object.
   *
   * @details
   * Accepts any @c Conf-derived configuration (e.g. @c DdsConf, @c ShmConf).
   * A compile-time @c static_assert verifies the configuration supports the
   * subscriber role.
   *
   * @tparam ConfT  @c Conf-derived configuration type.
   * @param conf    Populated configuration object.
   * @param type    @c kWithInit to call @c init() immediately (default).
   */
  // NOLINTNEXTLINE(modernize-use-constraints)
  template <typename ConfT, typename = std::enable_if_t<std::is_base_of_v<Conf, ConfT>>>
  explicit Subscriber(const ConfT& conf, InitType type = InitType::kWithInit);

  /**
   * @brief Constructs a subscriber from a URL string.
   *
   * @param url_str  Topic URL (e.g. @c "shm://vehicle/speed").
   * @param type     @c kWithInit to call @c init() immediately (default).
   */
  explicit Subscriber(const std::string& url_str, InitType type = InitType::kWithInit);

  /**
   * @brief Registers the receive callback for incoming messages.
   *
   * @details
   * The callback is invoked on every delivery, already deserialized into @c MsgT.
   * For @c intra:// with an @c IntraDataType-derived shared pointer type
   * (generated via @c VLINK_INTRA_DATA_DECLARE) the pointer is forwarded
   * zero-copy.  The callback runs on the transport thread unless the
   * node is @c attach()ed to a @c MessageLoop.
   *
   * Calling @c listen() more than once is a fatal error.
   *
   * @warning The deserialized message object is @c thread_local and reused
   *          across invocations.  Do not store references or pointers to the
   *          callback argument beyond the callback scope; copy the data if
   *          you need to retain it.  This does not apply to @c Bytes or
   *          @c IntraData message types, which are passed by value/shared
   *          pointer.
   *
   * @note The subscriber must be initialised (either via @c kWithInit in the
   *       constructor or by calling @c init() explicitly) before @c listen()
   *       is called.  Calling @c listen() on an uninitialised subscriber
   *       triggers a fatal error.
   *
   * @param callback  @c void(const MsgT&) invoked for each received message.
   * @return          @c true if registered successfully; @c false on error.
   */
  bool listen(MsgCallback&& callback);

  /**
   * @brief Enables or disables manual-unloan mode for zero-copy receives.
   *
   * @details
   * When enabled, the user must call @c return_loan() after consuming a received
   * loaned buffer.  Only meaningful on loan-capable transports (e.g. @c shm://).
   *
   * @param manual_unloan  @c true to enable; @c false for automatic (default).
   */
  void set_manual_unloan(bool manual_unloan) override;

  /**
   * @brief Enables or disables per-message latency and sample-loss tracking.
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
   * @brief Returns the most recently measured end-to-end message latency.
   *
   * @details
   * Only meaningful when tracking is enabled.  Measured from publication
   * timestamp to receive timestamp.
   *
   * @return Latency in microseconds; @c 0 if tracking is disabled.
   */
  [[nodiscard]] int64_t get_latency() const;

  /**
   * @brief Returns cumulative sample delivery statistics.
   *
   * @details
   * Only meaningful when tracking is enabled.
   *
   * @return @c SampleLostInfo with @c total expected and @c lost sample counts.
   */
  [[nodiscard]] SampleLostInfo get_lost() const;

  /**
   * @brief Changes this subscriber's role to @c kGetter (field-reader).
   *
   * @details
   * Updates @c impl_->impl_type from @c kSubscriber to @c kGetter so that
   * transport-specific field semantics (latest-value delivery) are applied.
   * If called after @c init(), the extension is automatically reinitialised.
   * Used internally by @c Getter.
   */
  void mark_as_getter();

 private:
  bool listen_bytes(NodeImpl::MsgCallback&& callback);

  bool listen_intra(NodeImpl::IntraMsgCallback&& callback);
};

/**
 * @class SecuritySubscriber
 * @brief Convenience alias for @c Subscriber with message security enabled.
 *
 * @details
 * Equivalent to @c Subscriber<MsgT, SecurityType::kWithSecurity>.  Decrypts
 * every incoming message using the configured security key or callbacks.
 *
 * @note Not supported on @c intra:// or @c dds:// CDR transport.
 *
 * @tparam MsgT  Message type.
 */
template <typename MsgT>
class SecuritySubscriber : public Subscriber<MsgT, SecurityType::kWithSecurity> {
 public:
  /** @brief Unique-pointer alias for heap allocation. */
  using UniquePtr = std::unique_ptr<SecuritySubscriber<MsgT>>;

  /** @brief Shared-pointer alias for heap allocation. */
  using SharedPtr = std::shared_ptr<SecuritySubscriber<MsgT>>;

  /**
   * @brief Creates a @c SecuritySubscriber on the heap wrapped in a @c unique_ptr.
   *
   * @param url_str  Topic URL string (e.g. @c "dds://vehicle/speed").
   * @param sec_cfg  Security configuration aggregate (empty by default → drops inbound messages).
   * @param type     @c kWithInit to call @c init() immediately (default).
   * @return         @c UniquePtr owning the new subscriber.
   */
  [[nodiscard]] static UniquePtr create_unique(const std::string& url_str, const Security::Config& sec_cfg = {},
                                               InitType type = InitType::kWithInit);

  /**
   * @brief Creates a @c SecuritySubscriber on the heap wrapped in a @c shared_ptr.
   *
   * @param url_str  Topic URL string.
   * @param sec_cfg  Security configuration aggregate (empty by default → drops inbound messages).
   * @param type     @c kWithInit to call @c init() immediately (default).
   * @return         @c SharedPtr owning the new subscriber.
   */
  [[nodiscard]] static SharedPtr create_shared(const std::string& url_str, const Security::Config& sec_cfg = {},
                                               InitType type = InitType::kWithInit);

  /**
   * @brief Constructs a @c SecuritySubscriber from a typed transport configuration object.
   *
   * @tparam ConfT  @c Conf-derived configuration type.
   * @param conf    Populated configuration object.
   * @param sec_cfg Security configuration aggregate (empty by default).
   * @param type    @c kWithInit to call @c init() immediately (default).
   */
  // NOLINTNEXTLINE(modernize-use-constraints)
  template <typename ConfT, typename = std::enable_if_t<std::is_base_of_v<Conf, ConfT>>>
  explicit SecuritySubscriber(const ConfT& conf, const Security::Config& sec_cfg = {},
                              InitType type = InitType::kWithInit);

  /**
   * @brief Constructs a @c SecuritySubscriber and installs the security configuration in place.
   *
   * @details
   * Always builds the base @c Subscriber with @c InitType::kWithoutInit, then
   * calls the inherited @c enable_security(sec_cfg) so that @c security_ is
   * either populated or left empty.  Finally calls @c init() unless the
   * caller requests deferred initialisation.
   *
   * @param url_str  Topic URL string.
   * @param sec_cfg  Security configuration aggregate (empty by default → drops inbound messages).
   * @param type     @c kWithInit to call @c init() immediately (default).
   */
  explicit SecuritySubscriber(const std::string& url_str, const Security::Config& sec_cfg = {},
                              InitType type = InitType::kWithInit);
};

}  // namespace vlink

#include "./internal/subscriber-inl.h"
