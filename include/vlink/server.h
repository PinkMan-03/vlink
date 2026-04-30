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
 * @file server.h
 * @brief Type-safe method-model server (handler side) for VLink RPC.
 *
 * @details
 * @c Server<ReqT, RespT, SecT> is the handler side of the VLink method model.
 * It registers a callback that is invoked for each incoming request and
 * optionally fills a response.
 *
 * @par Method Model Overview
 * @code
 *  Client<Req,Resp>                                       Server<Req,Resp>
 *      |                   Transport Back-end                   |
 *      |-- invoke(req) -------> |                               |
 *      |   serialize(req)       |-- request delivery ---------> |
 *      |                        |                               |--> callback(req, resp)
 *      |                        | <-- reply(resp) ------------- |
 *      |   deserialize(resp)    | <-- response delivery ------- |
 *      |<-- resp ----------------                               |
 * @endcode
 *
 * @par Three Listen Modes
 * | Method                                    | When to use                            |
 * | ----------------------------------------- | -------------------------------------- |
 * | @c listen(ReqCallback)                    | Fire-and-forget.                       |
 * | @c listen(ReqRespCallback)                | Synchronous reply inside the callback. |
 * | @c listen_for_reply(ReqAsyncRespCallback) | Async reply via @c reply().            |
 *
 * @par Synchronous Reply Example
 * @code
 * Server<Req, Resp> server("dds://my_service");
 * server.listen([](const Req& req, Resp& resp) {
 *     resp.result = process(req);       // fill resp inside callback
 * });
 * @endcode
 *
 * @par Asynchronous Reply Example
 * @code
 * Server<Req, Resp> server("dds://my_service");
 * uint64_t saved_req_id = 0;
 * server.listen_for_reply([&saved_req_id](uint64_t req_id, const Req& req) {
 *     saved_req_id = req_id;            // save request ID for later
 * });
 * // ... later, from any thread:
 * server.reply(saved_req_id, Resp{...});
 * @endcode
 *
 * @par Fire-and-forget Example (no response)
 * @code
 * Server<Req> server("dds://my_service");   // RespT defaults to EmptyType
 * server.listen([](const Req& req) {
 *     handle(req);
 * });
 * @endcode
 *
 * @note Calling @c listen() / @c listen_for_reply() more than once is fatal.
 *       @c reply() must only be called after @c listen_for_reply(); calling it
 *       after a synchronous @c listen() triggers a fatal log.
 *
 * @tparam ReqT  Request message type.  Must satisfy @c Serializer::is_supported().
 * @tparam RespT Response message type.  Defaults to @c Traits::EmptyType (no response).
 * @tparam SecT  Security mode; defaults to @c SecurityType::kWithoutSecurity.
 */

#pragma once

#include <functional>
#include <memory>
#include <string>

#include "./impl/server_impl.h"
#include "./node.h"

namespace vlink {

/**
 * @class Server
 * @brief Type-safe server for the VLink method (RPC) communication model.
 *
 * @tparam ReqT  Request type.
 * @tparam RespT Response type (defaults to @c Traits::EmptyType -- no response).
 * @tparam SecT  Security mode.
 */
template <typename ReqT, typename RespT = Traits::EmptyType, SecurityType SecT = SecurityType::kWithoutSecurity>
class Server : public Node<ServerImpl, SecT> {
 public:
  /** @brief Unique-pointer alias. */
  using UniquePtr = std::unique_ptr<Server<ReqT, RespT, SecT>>;

  /** @brief Shared-pointer alias. */
  using SharedPtr = std::shared_ptr<Server<ReqT, RespT, SecT>>;

  /** @brief Fire-and-forget callback -- no response (@c RespT must be @c EmptyType). */
  using ReqCallback = std::function<void(const ReqT&)>;

  /** @brief Synchronous callback -- response filled in-place inside the callback. */
  using ReqRespCallback = std::function<void(const ReqT&, RespT&)>;

  /**
   * @brief Asynchronous callback -- response sent later via @c reply(req_id, resp).
   *
   * @details
   * The first parameter is the opaque request ID that must be passed to
   * @c reply() to deliver the response to the waiting client.
   */
  using ReqAsyncRespCallback = std::function<void(uint64_t, const ReqT&)>;

  /** @brief Node role identifier (@c kServer). */
  static constexpr ImplType kImplType = kServer;

  /** @brief @c true when @c RespT is not @c EmptyType (server has a response). */
  static constexpr bool kHasResp = !std::is_same_v<RespT, Traits::EmptyType>;

  /** @brief Serializer type for @c ReqT. */
  static constexpr Serializer::Type kReqType = Serializer::get_type_of<ReqT>();

  /** @brief Serializer type for @c RespT. */
  static constexpr Serializer::Type kRespType = Serializer::get_type_of<RespT>();

  static_assert(Serializer::is_supported(kReqType), "<ReqT> is not a supported Serializer type.");
  static_assert(!kHasResp || Serializer::is_supported(kRespType), "<RespT> is not a supported Serializer type.");

  /**
   * @brief Creates a @c Server on the heap wrapped in a @c unique_ptr.
   *
   * @param url_str  Service URL string (e.g. @c "dds://my_service").
   * @param type     @c kWithInit to call @c init() immediately (default).
   * @return         @c UniquePtr owning the new server.
   */
  [[nodiscard]] static UniquePtr create_unique(const std::string& url_str, InitType type = InitType::kWithInit);

  /**
   * @brief Creates a @c Server on the heap wrapped in a @c shared_ptr.
   *
   * @param url_str  Service URL string.
   * @param type     @c kWithInit to call @c init() immediately (default).
   * @return         @c SharedPtr owning the new server.
   */
  [[nodiscard]] static SharedPtr create_shared(const std::string& url_str, InitType type = InitType::kWithInit);

  /**
   * @brief Constructs a server from a typed transport configuration object.
   *
   * @details
   * Accepts any @c Conf-derived configuration.  A compile-time @c static_assert
   * verifies the configuration supports the server role.
   *
   * @tparam ConfT  @c Conf-derived configuration type.
   * @param conf    Populated configuration object.
   * @param type    @c kWithInit to call @c init() immediately (default).
   */
  // NOLINTNEXTLINE(modernize-use-constraints)
  template <typename ConfT, typename = std::enable_if_t<std::is_base_of_v<Conf, ConfT>>>
  explicit Server(const ConfT& conf, InitType type = InitType::kWithInit);

  /**
   * @brief Constructs a server from a URL string.
   *
   * @param url_str  Service URL (e.g. @c "someip://30490/0x1/my_method").
   * @param type     @c kWithInit to call @c init() immediately (default).
   */
  explicit Server(const std::string& url_str, InitType type = InitType::kWithInit);

  /**
   * @brief Registers a fire-and-forget request callback (no response).
   *
   * @details
   * Only valid when @c RespT == @c EmptyType (enforced by @c static_assert).
   * The callback is invoked for every incoming request; no reply is sent.
   * Calling @c listen() more than once is a fatal error.
   *
   * @param callback  @c void(const ReqT&) invoked for each request.
   * @return          @c true if registration succeeded; @c false on error.
   */
  bool listen(ReqCallback&& callback);

  /**
   * @brief Registers a synchronous request/response callback.
   *
   * @details
   * Only valid when @c kHasResp is @c true (enforced by @c static_assert).
   * The callback must fill @p resp before returning.  The framework
   * serialises and sends the response immediately after the callback returns.
   *
   * @param callback  @c void(const ReqT&, RespT&) -- fills @c resp in-place.
   * @return          @c true if registration succeeded; @c false on error.
   */
  bool listen(ReqRespCallback&& callback);

  /**
   * @brief Registers an asynchronous request callback (reply sent later).
   *
   * @details
   * Only valid when @c kHasResp is @c true (enforced by @c static_assert).
   * The callback receives an opaque @c req_id.  The handler must eventually
   * call @c reply(req_id, resp) from any thread to send the response.
   *
   * @param callback  @c void(uint64_t req_id, const ReqT&) -- stores @c req_id for later.
   * @return          @c true if registration succeeded; @c false on error.
   */
  bool listen_for_reply(ReqAsyncRespCallback&& callback);

  /**
   * @brief Sends an asynchronous response for a previously received request.
   *
   * @details
   * Must only be called after @c listen_for_reply() (calling after a synchronous
   * @c listen() triggers a fatal log).  The @p req_id must match the value
   * passed to the async callback; an unrecognised ID is silently ignored by
   * the transport.
   *
   * @param req_id  Opaque request identifier received in the async callback.
   * @param resp    Response value to serialise and send back to the client.
   * @return        @c true if the transport accepted the response; @c false on error.
   */
  bool reply(uint64_t req_id, const RespT& resp);

 private:
  [[nodiscard]] bool has_clients() const;

  bool listen_bytes(NodeImpl::ReqRespCallback&& callback);

  template <bool HasPtrT>
  bool reply_bytes(uint64_t req_id, const Bytes& resp_data, bool is_sync, Bytes* resp_data_ptr = nullptr);
};

/**
 * @class SecurityServer
 * @brief Convenience alias for @c Server with message security enabled.
 *
 * @details
 * Equivalent to @c Server<ReqT, RespT, SecurityType::kWithSecurity>.
 * Each incoming request is decrypted before dispatch to the callback, and
 * each outgoing response is encrypted before transmission.
 *
 * @tparam ReqT  Request type.
 * @tparam RespT Response type (defaults to @c Traits::EmptyType).
 */
template <typename ReqT, typename RespT = Traits::EmptyType>
class SecurityServer : public Server<ReqT, RespT, SecurityType::kWithSecurity> {
 public:
  using Server<ReqT, RespT, SecurityType::kWithSecurity>::Server;
};

}  // namespace vlink

#include "./internal/server-inl.h"
