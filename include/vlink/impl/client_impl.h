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
 * @file client_impl.h
 * @brief Abstract base class for all transport-specific client (RPC caller) implementations.
 *
 * @details
 * @c ClientImpl is the intermediate layer between the generic @c Client<Req,Resp>
 * template and a concrete transport backend (e.g. @c DdsClientImpl,
 * @c ShmClientImpl).  It inherits from @c NodeImpl and adds method-client semantics:
 *
 * - Server-connection detection and notification via @c detect_connected() /
 *   @c update_connected().
 * - Blocking wait for server availability with @c wait_for_connected().
 * - Synchronous (blocking) RPC call via @c call().
 *
 * @par Connection Detection Flow
 * @code
 *   // Transport detects server appearance or disappearance:
 *   client_impl->update_connected();
 *   //   -> compares is_connected() against cached state
 *   //   -> notifies the condition variable
 *   //   -> fires the ConnectCallback registered via detect_connected()
 * @endcode
 *
 * @note Concrete subclasses must implement @c is_connected() and @c call().
 */

#pragma once

#include <chrono>
#include <memory>

#include "./node_impl.h"

namespace vlink {

/**
 * @class ClientImpl
 * @brief Transport-agnostic base for client (RPC caller) node implementations.
 *
 * @details
 * Provides the server-connection-detection infrastructure (condition variable +
 * callback) used by @c Client<Req,Resp>::wait_for_connected() and
 * @c Client<Req,Resp>::detect_connected().  Concrete backends override
 * @c is_connected() to query the transport layer and call @c update_connected()
 * whenever the server connection state changes.
 */
class VLINK_EXPORT ClientImpl : public NodeImpl {
 public:
  /**
   * @brief Destructor.
   */
  ~ClientImpl() override;

  /**
   * @brief Interrupts the client, waking any blocked @c wait_for_connected() or
   *        @c call() operation.
   *
   * @details
   * Calls @c NodeImpl::interrupt() to set the interrupted flag, then
   * @c notify_all() on the internal condition variable so that any thread blocked
   * in @c wait_for_connected() returns immediately.  Concrete backends typically
   * also forward the interrupt to any pending @c AckManager requests.
   */
  void interrupt() override;

  /**
   * @brief Registers a callback to be fired when the server connection state changes.
   *
   * @details
   * The @p callback is stored and invoked with @c true when the server becomes
   * reachable and @c false when it disconnects.  If the server is already connected
   * at registration time the callback is fired immediately with @c true before this
   * function returns.
   *
   * @param callback  Callable @c void(bool) to invoke on connection state change.
   */
  virtual void detect_connected(ConnectCallback&& callback);

  /**
   * @brief Blocks until the server is reachable or the timeout elapses.
   *
   * @details
   * Returns immediately if @c is_connected() is already @c true.  Otherwise waits
   * on an internal condition variable that is notified by @c update_connected() and
   * @c interrupt().
   *
   * - @p timeout < 0 (e.g. @c Timeout::kInfinite): waits indefinitely.
   * - @p timeout >= 0: returns @c false if no server appears within the period.
   *
   * @param timeout  Maximum time to wait; negative value means wait forever.
   * @return         @c true if the server was detected; @c false on timeout or
   *                 interruption.
   */
  virtual bool wait_for_connected(std::chrono::milliseconds timeout);

  /**
   * @brief Returns @c true when the transport is connected to a server.
   *
   * @details
   * Must be implemented by each concrete transport backend.  Called by
   * @c wait_for_connected() and @c update_connected() to determine whether the
   * connection state has changed.
   *
   * @return @c true if a server is reachable; @c false otherwise.
   */
  [[nodiscard]] virtual bool is_connected() const = 0;

  /**
   * @brief Sends a request and blocks until the response arrives or the timeout elapses.
   *
   * @details
   * Must be implemented by each concrete transport backend.  @p req_data contains
   * the serialised request payload.  The @p callback is invoked with the response
   * bytes once the round-trip completes.
   *
   * @param req_data  Serialised request payload.
   * @param callback  Callable @c void(const Bytes&) invoked with the response bytes.
   * @param timeout   Maximum time to wait for the response; negative = infinite.
   * @return          @c true if a response was received within the timeout;
   *                  @c false on timeout, interruption, or transport error.
   */
  virtual bool call(const Bytes& req_data, MsgCallback&& callback, std::chrono::milliseconds timeout) = 0;

  /**
   * @brief Notifies the connection-detection subsystem that connectivity may have changed.
   *
   * @details
   * Called by the concrete transport backend whenever a server connects or disconnects.
   * Compares the current @c is_connected() result against the cached state; if it
   * differs, the condition variable is notified and the registered
   * @c ConnectCallback is fired.
   *
   * @note This method is intended to be called from the transport's internal
   *       discovery thread, not from user code.
   */
  void update_connected();

  bool is_resp_type{false};  ///< @c true when the call expects a response (vs fire-and-forget).

 protected:
  /**
   * @brief Protected constructor; initialises the client with @c kClient role.
   */
  ClientImpl();

 private:
  std::unique_ptr<struct ClientImplHelper> helper_;

  VLINK_DISALLOW_COPY_AND_ASSIGN(ClientImpl)
};

}  // namespace vlink
