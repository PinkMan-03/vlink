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
 * @file publisher_impl.h
 * @brief Abstract base class for all transport-specific publisher implementations.
 *
 * @details
 * @c PublisherImpl is the intermediate layer between the generic @c Publisher<T> template
 * and a concrete transport backend (e.g. @c DdsPublisherImpl, @c ShmPublisherImpl).
 * It inherits from @c NodeImpl and adds publish-side semantics:
 *
 * - Subscriber discovery and change notification via @c detect_subscribers() /
 *   @c update_subscribers().
 * - Blocking wait for at least one subscriber with @c wait_for_subscribers().
 * - Two write overloads: serialised @c Bytes for network transports and zero-copy
 *   @c IntraData for the @c intra:// backend.
 *
 * @par Subscriber Discovery Flow
 * @code
 *   // Transport detects subscriber appearance or disappearance:
 *   publisher_impl->update_subscribers();
 *   //   -> compares has_subscribers() against cached state
 *   //   -> notifies the condition variable
 *   //   -> fires the ConnectCallback registered via detect_subscribers()
 * @endcode
 *
 * @note Concrete subclasses must implement @c has_subscribers() and
 *       @c write(const Bytes&).  @c write(const IntraData&) is only overridden by
 *       the @c intra:// backend; all other transports inherit the base no-op that
 *       logs a warning and returns @c false.
 */

#pragma once

#include <chrono>
#include <memory>

#include "./node_impl.h"

namespace vlink {

/**
 * @class PublisherImpl
 * @brief Transport-agnostic base for publisher node implementations.
 *
 * @details
 * Provides the subscriber-detection infrastructure (condition variable + callback)
 * used by @c Publisher<T>::wait_for_subscribers() and
 * @c Publisher<T>::detect_subscribers().  Concrete backends override
 * @c has_subscribers() to query the transport layer and call @c update_subscribers()
 * whenever the subscriber count changes.
 */
class VLINK_EXPORT PublisherImpl : public NodeImpl {
 public:
  /**
   * @brief Destructor.
   */
  ~PublisherImpl() override;

  /**
   * @brief Interrupts the publisher, waking any blocked @c wait_for_subscribers() call.
   *
   * @details
   * Calls @c NodeImpl::interrupt() to set the interrupted flag, then
   * @c notify_all() on the internal condition variable so that any thread blocked
   * in @c wait_for_subscribers() returns immediately with @c false.
   */
  void interrupt() override;

  /**
   * @brief Registers a callback to be fired when the subscriber presence changes.
   *
   * @details
   * The @p callback is stored and invoked with @c true when the first subscriber
   * appears and @c false when the last one disconnects.  If subscribers are already
   * present at registration time the callback is fired immediately with @c true
   * before this function returns.
   *
   * @param callback  Callable @c void(bool) to invoke on subscriber change.
   */
  virtual void detect_subscribers(ConnectCallback&& callback);

  /**
   * @brief Blocks until at least one subscriber is present or the timeout elapses.
   *
   * @details
   * Returns immediately if @c has_subscribers() is already @c true.  Otherwise
   * waits on an internal condition variable that is notified by
   * @c update_subscribers() and @c interrupt().
   *
   * - @p timeout < 0 (e.g. @c Timeout::kInfinite): waits indefinitely.
   * - @p timeout >= 0: returns @c false if no subscriber arrives within the period.
   *
   * @param timeout  Maximum time to wait; negative value means wait forever.
   * @return         @c true if a subscriber was detected; @c false on timeout
   *                 or interruption.
   */
  virtual bool wait_for_subscribers(std::chrono::milliseconds timeout);

  /**
   * @brief Returns @c true when at least one subscriber is currently connected.
   *
   * @details
   * Must be implemented by each concrete transport backend.  Called by
   * @c wait_for_subscribers() and @c update_subscribers() to determine whether
   * the subscriber presence state has changed.
   *
   * @return @c true if one or more subscribers are connected; @c false otherwise.
   */
  [[nodiscard]] virtual bool has_subscribers() const = 0;

  /**
   * @brief Publishes a serialised message to all connected subscribers.
   *
   * @details
   * Must be implemented by each concrete transport backend.  @p msg_data contains
   * the fully serialised payload produced by @c Serializer::serialize().
   *
   * @param msg_data  Serialised message bytes to transmit.
   * @return          @c true if the message was delivered (or queued) successfully;
   *                  @c false on error.
   */
  virtual bool write(const Bytes& msg_data) = 0;

  /**
   * @brief Publishes an in-process zero-copy message.
   *
   * @details
   * Used exclusively on the @c intra:// transport to pass @c IntraData directly
   * to subscribers in the same process without serialisation.  The default
   * implementation logs a warning and returns @c false; only @c IntraPublisherImpl
   * overrides this method.
   *
   * @param intra_data  Shared pointer to the in-process message payload.
   * @return            @c true if the message was delivered; @c false if this
   *                    transport does not support @c IntraData.
   */
  virtual bool write(const IntraData& intra_data);

  /**
   * @brief Notifies the subscriber-detection subsystem that subscriber presence may
   *        have changed.
   *
   * @details
   * Called by the concrete transport backend whenever a subscriber connects or
   * disconnects.  Compares the current @c has_subscribers() result against the
   * cached state; if it differs, the condition variable is notified and the
   * registered @c ConnectCallback is fired.
   *
   * @note
   * This method is intended to be called from the transport's internal discovery
   * thread, not from user code.
   */
  void update_subscribers();

 protected:
  /**
   * @brief Protected constructor; initialises the publisher with @c kPublisher role.
   */
  PublisherImpl();

 private:
  std::unique_ptr<struct PublisherImplHelper> helper_;

  VLINK_DISALLOW_COPY_AND_ASSIGN(PublisherImpl)
};

}  // namespace vlink
