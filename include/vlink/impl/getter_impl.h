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
 * @file getter_impl.h
 * @brief Abstract base class for all transport-specific getter (field reader) implementations.
 *
 * @details
 * @c GetterImpl is the intermediate layer between the generic @c Getter<T> template
 * and a concrete transport backend.  It inherits from @c NodeImpl and adds
 * field-getter semantics:
 *
 * - Change notification via @c listen() -- the callback is invoked whenever a new
 *   value is written by the corresponding @c Setter on the same topic.
 * - Optional latency and sample-loss tracking, identical in interface to
 *   @c SubscriberImpl.
 *
 * @par Field Model Overview
 * Unlike the event model, the field model maintains a single "latest value" per
 * topic.  A @c Getter receives the most recent value; it does not buffer a queue
 * of historic messages.
 *
 * @note Concrete subclasses must implement @c listen(MsgCallback&&).
 */

#pragma once

#include "./node_impl.h"

namespace vlink {

/**
 * @class GetterImpl
 * @brief Transport-agnostic base for getter (field reader) node implementations.
 *
 * @details
 * Provides default (no-op) implementations for the optional latency/loss tracking
 * interface.  Concrete transport backends override @c listen(MsgCallback&&) to
 * register their value-change callback.
 */
class VLINK_EXPORT GetterImpl : public NodeImpl {
 public:
  /**
   * @brief Destructor.
   */
  ~GetterImpl() override;

  /**
   * @brief Registers the value-change callback for field updates.
   *
   * @details
   * Must be implemented by each concrete transport backend.  The callback is
   * invoked with a @c Bytes buffer containing the serialised latest value each
   * time the corresponding @c Setter writes a new value.  After registration
   * @c is_listened is set to @c true by the @c Getter<T> layer.
   *
   * @param callback  Callable @c void(const Bytes&) invoked on each value update.
   * @return          @c true if registration succeeded; @c false on error.
   */
  virtual bool listen(MsgCallback&& callback) = 0;

  /**
   * @brief Enables or disables per-update latency and sample-loss tracking.
   *
   * @details
   * When enabled, the transport backend tracks value timestamps (for latency) and
   * sequence numbers (for loss detection).  The default implementation is a no-op;
   * transports that support tracking override this method.
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
   * @brief Returns the most recently measured end-to-end field update latency in nanoseconds.
   *
   * @details
   * Only meaningful when @c is_latency_and_lost_enabled() returns @c true.  The
   * default implementation returns @c 0.
   *
   * @return Latency in nanoseconds, or @c 0 if tracking is disabled or unsupported.
   */
  [[nodiscard]] virtual int64_t get_latency() const;

  /**
   * @brief Returns cumulative sample delivery statistics for field updates.
   *
   * @details
   * Returns the total number of expected updates and the number that were lost.
   * Only meaningful when @c is_latency_and_lost_enabled() returns @c true.  The
   * default implementation returns a zero-initialised @c SampleLostInfo.
   *
   * @return @c SampleLostInfo containing @c total and @c lost counts.
   */
  [[nodiscard]] virtual SampleLostInfo get_lost() const;

  bool is_listened{false};  ///< @c true after @c listen() has been successfully called.

 protected:
  /**
   * @brief Protected constructor; initialises the getter with @c kGetter role.
   */
  GetterImpl();

 private:
  VLINK_DISALLOW_COPY_AND_ASSIGN(GetterImpl)
};

}  // namespace vlink
