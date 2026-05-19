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
 * @file client.h
 * @brief Type-safe method-model client (caller side) for VLink RPC.
 *
 * @details
 * @c Client<ReqT, RespT, SecT> is the caller side of the VLink method model.
 * It serialises a request, delivers it to a matching @c Server, and returns
 * the deserialised response.
 *
 * @par Method Model Overview
 * @code
 *  Client<Req,Resp>                                        Server<Req,Resp>
 *      |                    Transport Back-end                    |
 *      |-- invoke(req) -------> |                                 |
 *      |   serialize(req)       |-- request delivery -----------> |
 *      |   [wait]               |                                 |--> callback(req,resp)
 *      |                        |<-- response delivery ---------- |
 *      |   deserialize(resp)    |                                 |
 *      |<-- returns resp -------|                                 |
 * @endcode
 *
 * @par Five Invocation Modes
 * | Method               | Signature                                 | Block | Notes                          |
 * | -------------------- | ----------------------------------------- | ----- | ------------------------------ |
 * | @c invoke (ref)      | @c invoke(req, resp&, timeout)            | Yes   | Returns @c true/false.         |
 * | @c invoke (optional) | @c invoke(req, timeout) -> optional<Resp> | Yes   | Returns @c nullopt on timeout. |
 * | @c invoke (callback) | @c invoke(req, RespCallback)              | No    | Callback on response.          |
 * | @c async_invoke      | @c async_invoke(req) -> future<Resp>      | No    | @c std::future based.          |
 * | @c send              | @c send(req)                              | No    | Fire-and-forget (no resp).     |
 *
 * @par Synchronous Invocation
 * @code
 * Client<Req, Resp> client("dds://my_service");
 * client.wait_for_connected();
 *
 * Resp resp;
 * if (client.invoke(Req{...}, resp)) {
 *     std::cout << "result: " << resp.value << std::endl;
 * }
 * // or using optional:
 * if (auto r = client.invoke(Req{...})) {
 *     std::cout << r->value << std::endl;
 * }
 * @endcode
 *
 * @par Asynchronous Invocation
 * @code
 * // callback-based
 * client.invoke(Req{...}, [](const Resp& resp) {
 *     std::cout << "async result: " << resp.value << std::endl;
 * });
 *
 * // future-based
 * auto future = client.async_invoke(Req{...});
 * auto resp = future.get();
 * @endcode
 *
 * @par Server Connection Detection
 * @code
 * Client<Req, Resp> client("dds://my_service");
 * client.detect_connected([](bool connected) { ... });  // async
 * client.wait_for_connected();                          // blocking
 * if (client.is_connected()) { ... }                    // non-blocking
 * @endcode
 *
 * @note A timeout of @c 0 is treated as infinite (a warning is logged).
 *       @c send() is only valid when @c RespT == @c EmptyType.
 *       @c invoke() / @c async_invoke() are only valid when @c kHasResp is @c true.
 *
 * @tparam ReqT  Request message type.  Must satisfy @c Serializer::is_supported().
 * @tparam RespT Response type.  Defaults to @c Traits::EmptyType (fire-and-forget).
 * @tparam SecT  Security mode; defaults to @c SecurityType::kWithoutSecurity.
 */

#pragma once

#include <chrono>
#include <future>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <type_traits>
#include <unordered_map>

#include "./base/functional.h"
#include "./impl/client_impl.h"
#include "./node.h"

namespace vlink {

/**
 * @class Client
 * @brief Type-safe client for the VLink method (RPC) communication model.
 *
 * @tparam ReqT  Request type.
 * @tparam RespT Response type (defaults to @c Traits::EmptyType -- fire-and-forget).
 * @tparam SecT  Security mode.
 */
template <typename ReqT, typename RespT = Traits::EmptyType, SecurityType SecT = SecurityType::kWithoutSecurity>
class Client : public Node<ClientImpl, SecT> {
 public:
  /** @brief Unique-pointer alias. */
  using UniquePtr = std::unique_ptr<Client<ReqT, RespT, SecT>>;

  /** @brief Shared-pointer alias. */
  using SharedPtr = std::shared_ptr<Client<ReqT, RespT, SecT>>;

  /** @brief Callback type fired when server connection state changes. */
  using ConnectCallback = NodeImpl::ConnectCallback;

  /** @brief Callback type for async response delivery. */
  using RespCallback = Function<void(const RespT&)>;

  /** @brief Node role identifier (@c kClient). */
  static constexpr ImplType kImplType = kClient;

  /** @brief @c true when @c RespT is not @c EmptyType (client expects a response). */
  static constexpr bool kHasResp = !std::is_same_v<RespT, Traits::EmptyType>;

  /** @brief Serializer type for @c ReqT. */
  static constexpr Serializer::Type kReqType = Serializer::get_type_of<ReqT>();

  /** @brief Serializer type for @c RespT. */
  static constexpr Serializer::Type kRespType = Serializer::get_type_of<RespT>();

  static_assert(Serializer::is_supported(kReqType), "<ReqT> is not a supported Serializer type.");
  static_assert(!kHasResp || Serializer::is_supported(kRespType), "<RespT> is not a supported Serializer type.");

  /**
   * @brief Creates a @c Client on the heap wrapped in a @c unique_ptr.
   *
   * @param url_str  Service URL string.
   * @param type     @c kWithInit to call @c init() immediately (default).
   * @return         @c UniquePtr owning the new client.
   */
  [[nodiscard]] static UniquePtr create_unique(const std::string& url_str, InitType type = InitType::kWithInit);

  /**
   * @brief Creates a @c Client on the heap wrapped in a @c shared_ptr.
   *
   * @param url_str  Service URL string.
   * @param type     @c kWithInit to call @c init() immediately (default).
   * @return         @c SharedPtr owning the new client.
   */
  [[nodiscard]] static SharedPtr create_shared(const std::string& url_str, InitType type = InitType::kWithInit);

  /**
   * @brief Constructs a client from a typed transport configuration object.
   *
   * @details
   * Accepts any @c Conf-derived configuration.  A compile-time @c static_assert
   * verifies the configuration supports the client role.
   *
   * @tparam ConfT  @c Conf-derived configuration type.
   * @param conf    Populated configuration object.
   * @param type    @c kWithInit to call @c init() immediately (default).
   */
  // NOLINTNEXTLINE(modernize-use-constraints)
  template <typename ConfT, typename = std::enable_if_t<std::is_base_of_v<Conf, ConfT>>>
  explicit Client(const ConfT& conf, InitType type = InitType::kWithInit);

  /**
   * @brief Constructs a client from a URL string.
   *
   * @param url_str  Service URL (e.g. @c "someip://30490/0x1/my_method").
   * @param type     @c kWithInit to call @c init() immediately (default).
   */
  explicit Client(const std::string& url_str, InitType type = InitType::kWithInit);

  /**
   * @brief Destroys the client and releases any associated resources.
   */
  ~Client() override;

  /**
   * @brief Registers a callback invoked when the server connection state changes.
   *
   * @details
   * Fires immediately (synchronously) if the server is already connected.
   * Otherwise fires asynchronously when the server first becomes available.
   *
   * @param callback  @c void(bool) -- @c true when connected to a server.
   */
  void detect_connected(ConnectCallback&& callback);

  /**
   * @brief Blocks until a server is available or the timeout expires.
   *
   * @details
   * A @p timeout of @c 0 is treated as infinite (a warning is logged).
   * A negative timeout also waits indefinitely.  Can be interrupted by
   * @c interrupt(), which causes this method to return @c false.
   *
   * @param timeout  Maximum wait duration.  Default: @c Timeout::kDefaultInterval.
   * @return         @c true if a server appeared; @c false on timeout or interrupt.
   */
  bool wait_for_connected(std::chrono::milliseconds timeout = Timeout::kDefaultInterval);

  /**
   * @brief Returns @c true if a server is currently available.
   *
   * @details
   * Non-blocking poll; reflects the transport's last known server state.
   *
   * @return @c true when connected to a server.
   */
  [[nodiscard]] bool is_connected() const;

  /**
   * @brief Sends a request and blocks until the response is received.
   *
   * @details
   * Only valid when @c kHasResp is @c true (enforced by @c static_assert).
   * Serialises @p req, sends it to the server, blocks for up to @p timeout,
   * and deserialises the response into @p resp.  A @p timeout of @c 0 is
   * treated as infinite.
   *
   * @param req      Request value to send.
   * @param resp     Output parameter filled with the deserialised response.
   * @param timeout  Maximum wait for the response.
   * @return         @c true if the response was received in time; @c false otherwise.
   */
  [[nodiscard]] bool invoke(const ReqT& req, RespT& resp,
                            std::chrono::milliseconds timeout = Timeout::kDefaultInterval);

  /**
   * @brief Sends a request and returns the response as @c std::optional.
   *
   * @details
   * Convenience overload that returns @c std::nullopt on timeout or error
   * instead of requiring an output parameter.  Only valid when @c kHasResp is
   * @c true.  A @p timeout of @c 0 is treated as infinite.
   *
   * @param req      Request value to send.
   * @param timeout  Maximum wait for the response.
   * @return         @c std::optional<RespT> -- @c nullopt on timeout/error.
   */
  [[nodiscard]] std::optional<RespT> invoke(const ReqT& req,
                                            std::chrono::milliseconds timeout = Timeout::kDefaultInterval);

  /**
   * @brief Sends a request and invokes @p callback asynchronously on the response.
   *
   * @details
   * Only valid when @c kHasResp is @c true.  The call returns immediately; the
   * @p callback is invoked on the transport thread (or on the attached
   * @c MessageLoop thread) when the response arrives.
   *
   * @param req       Request value to send.
   * @param callback  @c void(const RespT&) invoked with the response.
   * @return          @c true if the request was accepted by the transport; @c false on error.
   */
  bool invoke(const ReqT& req, RespCallback&& callback);

  /**
   * @brief Sends a request and returns a @c std::future for the response.
   *
   * @details
   * Only valid when @c kHasResp is @c true (enforced by @c static_assert).
   * The future is set when the response arrives.  If the call fails (serialisation
   * error, transport error, or deserialisation failure) the future's exception is
   * set with an @c Exception::RuntimeError.
   *
   * @param req  Request value to send.
   * @return     @c std::future<RespT> resolved when the response arrives.
   */
  [[nodiscard]] std::future<RespT> async_invoke(const ReqT& req);

  /**
   * @brief Sends a fire-and-forget request with no response.
   *
   * @details
   * Only valid when @c RespT == @c EmptyType (enforced by @c static_assert).
   * The call returns immediately after the transport has accepted the request;
   * no response is expected or awaited.
   *
   * @param req  Request value to send.
   * @return     @c true if the transport accepted the request; @c false on error.
   */
  bool send(const ReqT& req);

 private:
  bool call_bytes(const Bytes& req_data, NodeImpl::MsgCallback&& callback = nullptr,
                  std::chrono::milliseconds timeout = std::chrono::milliseconds(0));

  uint64_t future_seq_{0};
  std::mutex future_mtx_;
  std::unordered_map<std::uint64_t, std::shared_ptr<std::promise<RespT>>> future_map_;
};

/**
 * @class SecurityClient
 * @brief Convenience alias for @c Client with message security enabled.
 *
 * @details
 * Equivalent to @c Client<ReqT, RespT, SecurityType::kWithSecurity>.
 * Each outgoing request is encrypted and each incoming response is decrypted
 * using the configured security key or callbacks.
 *
 * @tparam ReqT  Request type.
 * @tparam RespT Response type (defaults to @c Traits::EmptyType).
 */
template <typename ReqT, typename RespT = Traits::EmptyType>
class SecurityClient : public Client<ReqT, RespT, SecurityType::kWithSecurity> {
 public:
  /** @brief Unique-pointer alias. */
  using UniquePtr = std::unique_ptr<SecurityClient<ReqT, RespT>>;

  /** @brief Shared-pointer alias. */
  using SharedPtr = std::shared_ptr<SecurityClient<ReqT, RespT>>;

  /**
   * @brief Creates a @c SecurityClient on the heap wrapped in a @c unique_ptr.
   *
   * @param url_str  Service URL string.
   * @param sec_cfg  Security configuration aggregate (empty by default; must configure a usable slot before init).
   * @param type     @c kWithInit to call @c init() immediately (default).
   * @return         @c UniquePtr owning the new client.
   */
  // NOLINTNEXTLINE(modernize-use-constraints)
  template <typename SecurityConfigT = Security::Config>
  [[nodiscard]] static UniquePtr create_unique(const std::string& url_str, SecurityConfigT&& sec_cfg = {},
                                               InitType type = InitType::kWithInit);

  /**
   * @brief Creates a @c SecurityClient on the heap wrapped in a @c shared_ptr.
   *
   * @param url_str  Service URL string.
   * @param sec_cfg  Security configuration aggregate (empty by default; must configure a usable slot before init).
   * @param type     @c kWithInit to call @c init() immediately (default).
   * @return         @c SharedPtr owning the new client.
   */
  // NOLINTNEXTLINE(modernize-use-constraints)
  template <typename SecurityConfigT = Security::Config>
  [[nodiscard]] static SharedPtr create_shared(const std::string& url_str, SecurityConfigT&& sec_cfg = {},
                                               InitType type = InitType::kWithInit);

  /**
   * @brief Constructs a @c SecurityClient from a typed transport configuration object.
   *
   * @tparam ConfT  @c Conf-derived configuration type.
   * @param conf    Populated configuration object.
   * @param sec_cfg Security configuration aggregate (empty by default; must configure a usable slot before init).
   * @param type    @c kWithInit to call @c init() immediately (default).
   */
  // NOLINTNEXTLINE(modernize-use-constraints)
  template <typename ConfT, typename SecurityConfigT = Security::Config,
            typename = std::enable_if_t<std::is_base_of_v<Conf, ConfT>>>
  explicit SecurityClient(const ConfT& conf, SecurityConfigT&& sec_cfg = {}, InitType type = InitType::kWithInit);

  /**
   * @brief Constructs a @c SecurityClient and installs the security configuration in place.
   *
   * @details
   * Always builds the base @c Client with @c InitType::kWithoutInit, then
   * forwards @p sec_cfg into @c enable_security().  @c init() requires that
   * @c NodeImpl::security was populated successfully; finally calls @c init()
   * unless the caller requests deferred initialisation.
   *
   * @param url_str  Service URL string.
   * @param sec_cfg  Security configuration aggregate (empty by default; must configure a usable slot before init).
   * @param type     @c kWithInit to call @c init() immediately (default).
   */
  // NOLINTNEXTLINE(modernize-use-constraints)
  template <typename SecurityConfigT = Security::Config>
  explicit SecurityClient(const std::string& url_str, SecurityConfigT&& sec_cfg = {},
                          InitType type = InitType::kWithInit);
};

}  // namespace vlink

#include "./internal/client-inl.h"
