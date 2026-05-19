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
 * @file setter_impl.h
 * @brief Abstract base class for all transport-specific setter (field writer) implementations.
 *
 * @details
 * @c SetterImpl is the intermediate layer between the generic @c Setter<T> template
 * and a concrete transport backend.  It inherits from @c NodeImpl and adds
 * field-setter semantics:
 *
 * - Value write via @c write() -- sends the serialised latest value to all registered
 *   getters on the same topic.
 * - Late-getter synchronisation via @c sync() -- registers a callback that can
 *   be fired when a getter connects and needs the cached latest value.
 *
 * @par Field Model Overview
 * Unlike the event model (publish/subscribe), the field model maintains a single
 * "latest value" per topic.  A @c Setter always overwrites the previous value; a
 * @c Getter can retrieve the most recent value at any time or be notified when it
 * changes.
 *
 * @note Concrete subclasses must implement @c write(const Bytes&) and
 *       @c sync(SyncCallback&&).
 */

#pragma once

#include "./node_impl.h"

namespace vlink {

/**
 * @class SetterImpl
 * @brief Transport-agnostic base for setter (field writer) node implementations.
 *
 * @details
 * Concrete backends override @c write() to push the serialised value onto the
 * transport and @c sync() to provide the transport-specific late-join
 * notification used by @c Setter<T> to re-send its cached value.
 */
class VLINK_EXPORT SetterImpl : public NodeImpl {
 public:
  /**
   * @brief Destructor.
   */
  ~SetterImpl() override;

  /**
   * @brief Writes a new field value to all connected getter nodes.
   *
   * @details
   * Must be implemented by each concrete transport backend.  @p msg_data contains
   * the fully serialised latest value produced by @c Serializer::serialize().
   * The write overwrites any previously stored value on the topic.
   *
   * @param msg_data  Serialised field value bytes to transmit.
   */
  virtual void write(const Bytes& msg_data) = 0;

  /**
   * @brief Registers the transport-specific late-getter sync callback.
   *
   * @details
   * Must be implemented by each concrete transport backend.  Backends that can
   * detect late getters invoke @p callback when the cached value should be sent
   * again; backends without that concept may ignore the callback.
   *
   * @param callback  Callable invoked when a late-getter sync is requested.
   */
  virtual void sync(SyncCallback&& callback) = 0;

 protected:
  /**
   * @brief Protected constructor; initialises the setter with @c kSetter role.
   */
  SetterImpl();

 private:
  VLINK_DISALLOW_COPY_AND_ASSIGN(SetterImpl)
};

}  // namespace vlink
