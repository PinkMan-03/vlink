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
 * @file ack_manager.h
 * @brief Request/acknowledgement synchronisation manager for blocking RPC calls.
 *
 * @details
 * @c AckManager provides the blocking/notify mechanism used by the VLink method
 * model (@c Client / @c Server) to implement synchronous request/response round-trips
 * across transports.  A caller creates a @c Request token, invokes @c process()
 * to send the request and wait for its acknowledgement, and either the transport
 * callback calls @c notify() on the same request or the caller can cancel it via
 * @c remove().
 *
 * @par Lifecycle
 * @code
 *               Thread A (caller)               Thread B (transport callback)
 *
 * request = create_request()
 * process(request, timeout_ms, send_fn)
 *   -> send_fn() publishes the request
 *   -> blocks on condition variable
 *                                          notify(request, fill_response_fn)
 *                                            -> erase request from set
 *                                            -> call fill_response_fn()
 *                                            -> notify_one on the cv
 *   -> wakes up, returns true
 * @endcode
 *
 * @par Interruption
 * @c clear() marks the manager as interrupted and wakes all waiting @c process()
 * callers, causing them to return @c false.  This is used during shutdown to
 * unblock any in-flight RPC calls.
 *
 * @par Thread Safety
 * All public methods are thread-safe.  @c process() may be called from multiple
 * threads simultaneously; each call tracks its own @c RequestPtr.
 */

#pragma once

#include <memory>
#include <mutex>
#include <set>

#include "../base/condition_variable.h"
#include "../base/functional.h"
#include "../base/macros.h"

namespace vlink {

/**
 * @class AckManager
 * @brief Thread-safe request/acknowledgement synchronisation manager.
 *
 * @details
 * Manages a set of in-flight requests, each represented by a @c RequestPtr.
 * Used internally by @c ClientImpl implementations to implement blocking
 * @c call() semantics.
 */
class VLINK_EXPORT AckManager final {
 private:
  struct Request;

 public:
  /**
   * @brief Callback invoked by @c process() to send the request over the transport.
   *
   * @details
   * Called while the request is already registered in the pending set.
   * Should return @c false if the send fails (e.g. no server connected),
   * causing @c process() to remove the request and return @c false immediately.
   */
  using ProcessCallback = MoveFunction<bool()>;

  /**
   * @brief Optional callback invoked inside @c notify() while holding the request lock.
   *
   * @details
   * Provides the responder a chance to fill the response buffer before the
   * waiting @c process() thread is woken.  May be @c nullptr.
   */
  using NotifyCallback = MoveFunction<void()>;

  /**
   * @brief Shared ownership handle for an in-flight request token.
   *
   * @details
   * Returned by @c create_request() and passed to @c process(), @c notify(),
   * and @c remove().  Requests are ordered by a monotonic sequence number.
   */
  using RequestPtr = std::shared_ptr<Request>;

  /**
   * @brief Default constructor.
   */
  AckManager() noexcept;

  /**
   * @brief Destructor.
   */
  ~AckManager() noexcept;

  /**
   * @brief Allocates a new in-flight request token with a unique sequence number.
   *
   * @details
   * The returned @c RequestPtr must be passed to @c process() to register
   * the request and block until the corresponding @c notify() call.
   *
   * @return A new @c RequestPtr with a monotonically increasing sequence number.
   */
  [[nodiscard]] RequestPtr create_request() noexcept;

  /**
   * @brief Registers the request, invokes the send callback, and blocks until acknowledged.
   *
   * @details
   * Steps:
   * -# Adds @p request to the pending set (returns @c false immediately if the
   *    manager is interrupted).
   * -# Calls @c process_callback(); if it returns @c false the request is removed
   *    and @c process() returns @c false.
   * -# Blocks on a per-request condition variable:
   *    - If @p ms < 0: waits indefinitely.
   *    - If @p ms >= 0: waits for at most @p ms milliseconds.
   * -# Returns @c true if woken by @c notify(); @c false on timeout or interruption.
   *
   * @param request           Token returned by @c create_request().
   * @param ms                Wait timeout in milliseconds; negative = infinite.
   * @param process_callback  Callable that sends the request; returns @c false to abort.
   * @return                  @c true if notified successfully; @c false on failure or timeout.
   */
  [[nodiscard]] bool process(RequestPtr request, int ms, ProcessCallback&& process_callback) noexcept;

  /**
   * @brief Acknowledges a pending request and optionally fills the response.
   *
   * @details
   * Removes @p request from the pending set, calls @p notify_callback (if set)
   * while holding the request lock, then signals the condition variable to wake
   * the blocked @c process() call.
   *
   * Returns @c false when @p request is not found in the pending set (e.g. already
   * timed out or removed).
   *
   * @param request          Token of the request to acknowledge.
   * @param notify_callback  Optional callback to fill the response before waking the caller.
   * @return                 @c true if the request was found and notified; @c false otherwise.
   */
  bool notify(RequestPtr request, NotifyCallback&& notify_callback = nullptr) noexcept;

  /**
   * @brief Removes a pending request without notifying the waiting caller.
   *
   * @details
   * Use this to cancel a request before it is acknowledged, e.g. when the
   * transport determines the request cannot be delivered.
   *
   * @param request  Token to remove.
   * @return         @c true if the request was found and erased; @c false otherwise.
   */
  bool remove(RequestPtr request) noexcept;

  /**
   * @brief Interrupts all pending requests and wakes all blocked @c process() calls.
   *
   * @details
   * Sets the interrupted flag so that new @c process() calls return @c false
   * immediately, swaps out the pending set, and @c notify_all() on every
   * pending request's condition variable.  Called during node shutdown to
   * avoid deadlocks.
   */
  void clear() noexcept;

 private:
  struct Request final {
    int64_t seq{0};
    std::mutex mtx;
    ConditionVariable cv;

    struct Compare final {
      bool operator()(const RequestPtr& left, const RequestPtr& right) const noexcept {
        if (!left || !right) {
          return left < right;
        }

        return left->seq < right->seq;
      }
    };
  };

  bool is_interrupted_{false};
  int64_t request_seq_{0};
  mutable std::mutex mtx_;
  std::set<RequestPtr, Request::Compare> request_set_;

  VLINK_DISALLOW_COPY_AND_ASSIGN(AckManager)
};

}  // namespace vlink
