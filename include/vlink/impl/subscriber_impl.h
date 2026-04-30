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
 * @file subscriber_impl.h
 * @brief Abstract base class for all transport-specific subscriber implementations.
 *
 * @details
 * @c SubscriberImpl is the intermediate layer between the generic @c Subscriber<T>
 * template and a concrete transport backend (e.g. @c DdsSubscriberImpl,
 * @c ShmSubscriberImpl).  It inherits from @c NodeImpl and adds receive-side
 * semantics:
 *
 * - Two listen overloads: a serialised @c MsgCallback for network transports and
 *   a zero-copy @c IntraMsgCallback for the @c intra:// backend.
 * - Optional latency and sample-loss tracking via @c CalculateSample.
 * - A public @c is_listened flag indicating whether a listener has been installed.
 *
 * @note Concrete subclasses must implement @c listen(MsgCallback&&).
 *       @c listen(IntraMsgCallback&&) is only overridden by @c IntraSubscriberImpl;
 *       all other transports inherit the base no-op that logs a warning and returns
 *       @c false.
 */

#pragma once

#include "./node_impl.h"

namespace vlink {

/**
 * @class SubscriberImpl
 * @brief Transport-agnostic base for subscriber node implementations.
 *
 * @details
 * Provides default (no-op) implementations for the optional latency/loss tracking
 * interface.  Concrete transport backends override @c listen(MsgCallback&&) to
 * register their receive callback and, optionally, override the latency/loss
 * methods if the transport provides that information.
 */
class VLINK_EXPORT SubscriberImpl : public NodeImpl {
 public:
  /**
   * @brief Destructor.
   */
  ~SubscriberImpl() override;

  /**
   * @brief Registers the serialised-message receive callback.
   *
   * @details
   * Must be implemented by each concrete transport backend.  The callback is
   * invoked with a @c Bytes buffer containing the raw serialised message each
   * time a new message arrives.  After registration @c is_listened is set to
   * @c true by the @c Subscriber<T> layer.
   *
   * @param callback  Callable @c void(const Bytes&) invoked on every received message.
   * @return          @c true if registration succeeded; @c false on error.
   */
  virtual bool listen(MsgCallback&& callback) = 0;

  /**
   * @brief Registers the zero-copy in-process receive callback.
   *
   * @details
   * Used exclusively on the @c intra:// transport to receive @c IntraData directly
   * from a co-located publisher without serialisation.  The default implementation
   * logs a warning and returns @c false; only @c IntraSubscriberImpl overrides this.
   *
   * @param callback  Callable @c void(const IntraData&) invoked on every received
   *                  in-process message.
   * @return          @c true if registration succeeded; @c false if this transport
   *                  does not support @c IntraData.
   */
  virtual bool listen(IntraMsgCallback&& callback);

  /**
   * @brief Enables or disables per-message latency and sample-loss tracking.
   *
   * @details
   * When enabled, the transport backend tracks message timestamps (for latency)
   * and sequence numbers (for loss detection).  The default implementation is a
   * no-op; transports that support tracking override this method.
   *
   * @param enable  @c true to enable tracking; @c false to disable.
   */
  virtual void set_latency_and_lost_enabled(bool enable);

  /**
   * @brief Returns whether latency and sample-loss tracking is currently enabled.
   *
   * @details
   * The default implementation always returns @c false.  Transports that support
   * tracking override this to reflect the current enabled state.
   *
   * @return @c true if tracking is active; @c false otherwise.
   */
  [[nodiscard]] virtual bool is_latency_and_lost_enabled() const;

  /**
   * @brief Returns the most recently measured end-to-end message latency in microseconds.
   *
   * @details
   * Only meaningful when @c is_latency_and_lost_enabled() returns @c true.  The
   * default implementation returns @c 0.  Transports that support latency measurement
   * override this to return the measured value.
   *
   * @return Latency in microseconds, or @c 0 if tracking is disabled or unsupported.
   */
  [[nodiscard]] virtual int64_t get_latency() const;

  /**
   * @brief Returns cumulative sample delivery statistics.
   *
   * @details
   * Returns the total number of expected samples and the number that were lost due
   * to queue overflow or network gaps.  Only meaningful when
   * @c is_latency_and_lost_enabled() returns @c true.  The default implementation
   * returns a zero-initialised @c SampleLostInfo.
   *
   * @return @c SampleLostInfo containing @c total and @c lost counts.
   */
  [[nodiscard]] virtual SampleLostInfo get_lost() const;

  bool is_listened{false};  ///< @c true after @c listen() has been successfully called.

 protected:
  /**
   * @brief Protected constructor; initialises the subscriber with @c kSubscriber role.
   */
  SubscriberImpl();

 private:
  VLINK_DISALLOW_COPY_AND_ASSIGN(SubscriberImpl)
};

}  // namespace vlink
