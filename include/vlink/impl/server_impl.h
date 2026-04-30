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
 * @file server_impl.h
 * @brief Abstract base class for all transport-specific server (RPC responder) implementations.
 *
 * @details
 * @c ServerImpl is the intermediate layer between the generic @c Server<Req,Resp>
 * template and a concrete transport backend (e.g. @c DdsServerImpl,
 * @c ShmServerImpl).  It inherits from @c NodeImpl and adds method-server semantics:
 *
 * - Request listening via @c listen(ReqRespCallback&&) -- the transport invokes the
 *   callback for each incoming RPC request, passing the request bytes and a unique
 *   @c req_id.
 * - Asynchronous response dispatch via @c reply() -- the application calls this
 *   after processing the request.
 * - Optional client-presence query via @c has_clients().
 *
 * @par Synchronous vs Asynchronous Responses
 * The @c is_sync_type flag controls whether the server uses a synchronous reply
 * path (reply is sent inside the callback, @c is_sync_type = true) or an asynchronous
 * path where the server stores the request and calls @c reply() later.  The base
 * @c reply() implementation only warns when @c is_sync is @c false and always
 * returns @c false; transports that support asynchronous replies override it.
 */

#pragma once

#include "./node_impl.h"

namespace vlink {

/**
 * @class ServerImpl
 * @brief Transport-agnostic base for server (RPC responder) node implementations.
 *
 * @details
 * Concrete backends override @c listen(ReqRespCallback&&) to register the request
 * handler, and optionally override @c has_clients() and @c reply() for client
 * discovery and asynchronous response support respectively.
 */
class VLINK_EXPORT ServerImpl : public NodeImpl {
 public:
  /**
   * @brief Destructor.
   */
  ~ServerImpl() override;

  /**
   * @brief Registers the request handler callback.
   *
   * @details
   * Must be implemented by each concrete transport backend.  The callback is
   * invoked for every incoming RPC request with the request bytes and a unique
   * identifier.  After registration @c is_listened is set to @c true by the
   * @c Server<Req,Resp> layer.
   *
   * @param callback  Callable @c void(uint64_t req_id, const Bytes& req_data, Bytes* resp_data)
   *                  invoked for each incoming request.  The handler writes
   *                  its response into @c *resp_data; @c nullptr in fire-and-forget mode.
   * @return          @c true if registration succeeded; @c false on error.
   */
  virtual bool listen(ReqRespCallback&& callback) = 0;

  /**
   * @brief Returns @c true when at least one client is currently connected.
   *
   * @details
   * The default implementation returns @c false.  Transports that support client
   * discovery (e.g. DDS matched publications) override this method.
   *
   * @return @c true if one or more clients are connected; @c false otherwise.
   */
  [[nodiscard]] virtual bool has_clients() const;

  /**
   * @brief Sends a response for a previously received request.
   *
   * @details
   * Used in asynchronous server mode (@c is_sync_type = @c false) to deliver a
   * response after the request callback has returned.  @p req_id must match the
   * identifier provided to the request callback; @p resp_data contains the
   * serialised response payload.
   *
   * The default implementation logs a warning when @c is_sync is @c false and
   * always returns @c false.  Transports that support asynchronous replies
   * override this method.
   *
   * @param req_id     Unique identifier of the request to respond to.
   * @param resp_data  Serialised response payload.
   * @param is_sync    @c true if called synchronously inside the request callback;
   *                   @c false for deferred (post-callback) replies.
   * @return           @c true if the response was queued or sent; @c false on error.
   */
  virtual bool reply(uint64_t req_id, const Bytes& resp_data, bool is_sync);

  bool is_listened{false};   ///< @c true after @c listen() has been successfully called.
  bool is_resp_type{false};  ///< @c true when the server sends a response (vs fire-and-forget).
  bool is_sync_type{false};  ///< @c true when the reply is sent synchronously inside the callback.

 protected:
  /**
   * @brief Protected constructor; initialises the server with @c kServer role.
   */
  ServerImpl();

 private:
  VLINK_DISALLOW_COPY_AND_ASSIGN(ServerImpl)
};

}  // namespace vlink
