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
 * @file abstract_factory.h
 * @brief Topic-keyed factory and multi-implementation callback registry for VLink nodes.
 *
 * @details
 * This header provides two cooperating templates used internally by every VLink
 * node type (Publisher, Subscriber, Client, Server, Setter, Getter) to multiplex
 * callbacks across multiple concurrent transport implementations sharing the same
 * logical topic:
 *
 * @par AbstractObject<FilterT>
 * A per-topic object that holds:
 * - A set of active @c NodeImpl* instances registered on this topic.
 * - Per-impl callback maps for all six callback types (server-connect, sub-connect,
 *   req/resp, msg, intra-msg, status).
 * - Traversal helpers that iterate over all registered callbacks while holding
 *   a @c std::recursive_mutex, with @c has_called() accounting that can ignore
 *   selected callbacks via @c ignore_called().
 *
 * @par AbstractFactory<FilterT>
 * A map-based factory keyed on @c FilterT (typically @c std::string topic name)
 * that creates and caches @c AbstractObject<FilterT> instances.  Objects are stored
 * as @c std::weak_ptr so they are automatically destroyed when no @c NodeImpl holds
 * a reference.
 *
 * @par Usage Model
 * @code
 *   // All Publisher<T> nodes on "dds://my_topic" share one AbstractObject:
 *   auto obj = factory.get_object<MyObject>("dds://my_topic");
 *   obj->add_impl(impl_ptr);
 *   obj->register_msg_callback(impl_ptr, [](const Bytes& bytes) {
 *     // process the received bytes
 *   });
 *
 *   // When a message arrives, dispatch to all registered impls:
 *   obj->traverse_msg_callback([&](NodeImpl* impl, const NodeImpl::MsgCallback& cb) {
 *     cb(msg_data);
 *   });
 * @endcode
 *
 * @note All public methods on @c AbstractObject are thread-safe; they acquire
 *       the internal @c std::recursive_mutex before modifying or reading state.
 *
 * @tparam FilterT  The key type used to look up objects in the factory
 *                  (e.g. @c std::string for topic URLs).
 */

#pragma once

#include <algorithm>
#include <atomic>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>

#include "../base/functional.h"
#include "../base/logger.h"
#include "./node_impl.h"

namespace vlink {

/**
 * @class AbstractObject
 * @brief Per-topic registry of @c NodeImpl instances and their associated callbacks.
 *
 * @details
 * Maintains an unordered set of active @c NodeImpl* pointers and six separate
 * callback maps (server-connect, subscriber-connect, req/resp, msg, intra-msg,
 * status).  All mutations and traversals are protected by a @c std::recursive_mutex
 * to allow safe use from multiple threads.
 *
 * @tparam FilterT  Key type used by the owning @c AbstractFactory to look up this
 *                  object (typically a topic URL string).
 */
template <typename FilterT>
class AbstractObject : public AbstractNode {
 public:
  using ImplList = std::unordered_set<NodeImpl*>;  ///< Set of registered impl pointers.

  using ConnectCallbackMap = std::unordered_map<NodeImpl*, NodeImpl::ConnectCallback>;  ///< Per-impl connect callbacks.
  using ReqRespCallbackMap =
      std::unordered_map<NodeImpl*, NodeImpl::ReqRespCallback>;                 ///< Per-impl req/resp callbacks.
  using MsgCallbackMap = std::unordered_map<NodeImpl*, NodeImpl::MsgCallback>;  ///< Per-impl message callbacks.
  using IntraMsgCallbackMap =
      std::unordered_map<NodeImpl*, NodeImpl::IntraMsgCallback>;                      ///< Per-impl intra-msg callbacks.
  using StatusCallbackMap = std::unordered_map<NodeImpl*, NodeImpl::StatusCallback>;  ///< Per-impl status callbacks.

  using FindConnectCallback =
      Function<void(NodeImpl*, const NodeImpl::ConnectCallback&)>;  ///< Traversal visitor for connect callbacks.
  using FindReqRespCallback =
      Function<void(NodeImpl*, const NodeImpl::ReqRespCallback&)>;  ///< Traversal visitor for req/resp callbacks.
  using FindMsgCallback =
      Function<void(NodeImpl*, const NodeImpl::MsgCallback&)>;  ///< Traversal visitor for message callbacks.
  using FindIntraMsgCallback =
      Function<void(NodeImpl*, const NodeImpl::IntraMsgCallback&)>;  ///< Traversal visitor for intra-msg callbacks.
  using FindStatusCallback =
      Function<void(NodeImpl*, const NodeImpl::StatusCallback&)>;  ///< Traversal visitor for status callbacks.

  /**
   * @brief Registers a @c NodeImpl instance with this topic object.
   *
   * @details
   * Inserts @p impl into the active implementation set and updates the cached
   * @c first_impl_ pointer.  Thread-safe.
   *
   * @param impl  Non-owning pointer to the @c NodeImpl to register.
   * @return      @c true if @p impl was newly inserted; @c false if it was already
   *              present.
   */
  bool add_impl(NodeImpl* impl);

  /**
   * @brief Unregisters a @c NodeImpl instance and removes all its callbacks.
   *
   * @details
   * Erases @p impl from the active set and removes its entries from all six
   * callback maps.  Thread-safe.
   *
   * @param impl  Non-owning pointer to the @c NodeImpl to unregister.
   * @return      @c true if @p impl was found and removed; @c false otherwise.
   */
  bool remove_impl(NodeImpl* impl);

  /**
   * @brief Returns the most recently added @c NodeImpl pointer.
   *
   * @details
   * The "first" impl is set to the last value passed to @c add_impl().  If the
   * current first impl is removed via @c remove_impl(), it is reassigned to an
   * arbitrary remaining impl, or @c nullptr if the set is empty.
   *
   * @return Pointer to the most recently registered @c NodeImpl, or @c nullptr.
   */
  [[nodiscard]] NodeImpl* get_first_impl() const;

  /**
   * @brief Returns @c true if @p impl is currently registered with this object.
   *
   * @param impl  Pointer to check.
   * @return      @c true if @p impl is in the active set; @c false otherwise.
   */
  [[nodiscard]] bool is_contains_impl(NodeImpl* impl) const;

  /**
   * @brief Returns @c true if at least one @c NodeImpl is currently registered.
   *
   * @return @c true if the active implementation set is non-empty.
   */
  [[nodiscard]] bool has_impl() const;

  /**
   * @brief Registers a server-side connect-change callback for @p impl.
   *
   * @details
   * Stores the callback in the server-connect map keyed by @p impl.  A subsequent
   * call to @c traverse_server_connect_callback() will invoke this callback.
   *
   * @param impl      The @c NodeImpl this callback belongs to.
   * @param callback  Callable @c void(bool) invoked on client-presence changes.
   * @return          @c true if the callback was inserted; @c false if one was
   *                  already registered for @p impl.
   */
  bool register_server_connect_callback(NodeImpl* impl, NodeImpl::ConnectCallback&& callback);

  /**
   * @brief Registers a subscriber-side connect-change callback for @p impl.
   *
   * @details
   * Stores the callback in the subscriber-connect map keyed by @p impl.
   *
   * @param impl      The @c NodeImpl this callback belongs to.
   * @param callback  Callable @c void(bool) invoked on subscriber-presence changes.
   * @return          @c true if the callback was inserted; @c false if already set.
   */
  bool register_sub_connect_callback(NodeImpl* impl, NodeImpl::ConnectCallback&& callback);

  /**
   * @brief Registers a request/response callback for @p impl.
   *
   * @param impl      The @c NodeImpl this callback belongs to.
   * @param callback  Callable invoked for each incoming RPC request.
   * @return          @c true if inserted; @c false if already registered.
   */
  bool register_req_resp_callback(NodeImpl* impl, NodeImpl::ReqRespCallback&& callback);

  /**
   * @brief Registers a serialised-message receive callback for @p impl.
   *
   * @param impl      The @c NodeImpl this callback belongs to.
   * @param callback  Callable @c void(const Bytes&) invoked on each message.
   * @return          @c true if inserted; @c false if already registered.
   */
  bool register_msg_callback(NodeImpl* impl, NodeImpl::MsgCallback&& callback);

  /**
   * @brief Registers an in-process zero-copy message callback for @p impl.
   *
   * @param impl      The @c NodeImpl this callback belongs to.
   * @param callback  Callable @c void(const IntraData&) invoked on each intra message.
   * @return          @c true if inserted; @c false if already registered.
   */
  bool register_intra_msg_callback(NodeImpl* impl, NodeImpl::IntraMsgCallback&& callback);

  /**
   * @brief Registers a transport-status callback for @p impl.
   *
   * @param impl      The @c NodeImpl this callback belongs to.
   * @param callback  Callable invoked on transport status changes.
   * @return          @c true if inserted; @c false if already registered.
   */
  bool register_status_callback(NodeImpl* impl, NodeImpl::StatusCallback&& callback);

  /**
   * @brief Returns @c true if no server-connect callbacks are registered.
   *
   * @return @c true when the server-connect callback map is empty.
   */
  [[nodiscard]] bool server_connect_map_is_empty() const;

  /**
   * @brief Returns @c true if no subscriber-connect callbacks are registered.
   *
   * @return @c true when the subscriber-connect callback map is empty.
   */
  [[nodiscard]] bool sub_connect_map_is_empty() const;

  /**
   * @brief Returns @c true if no request/response callbacks are registered.
   *
   * @return @c true when the req/resp callback map is empty.
   */
  [[nodiscard]] bool req_resp_map_is_empty() const;

  /**
   * @brief Returns @c true if no message callbacks are registered.
   *
   * @return @c true when the message callback map is empty.
   */
  [[nodiscard]] bool msg_map_is_empty() const;

  /**
   * @brief Returns @c true if no in-process message callbacks are registered.
   *
   * @return @c true when the intra-message callback map is empty.
   */
  [[nodiscard]] bool intra_msg_map_is_empty() const;

  /**
   * @brief Returns @c true if no status callbacks are registered.
   *
   * @return @c true when the status callback map is empty.
   */
  [[nodiscard]] bool status_map_is_empty() const;

  /**
   * @brief Invokes @p callback for each registered server-connect callback.
   *
   * @details
   * Iterates over all entries in the server-connect map while holding the mutex.
   * The visitor receives the @c NodeImpl* and the stored @c ConnectCallback.
   * A visitor can call @c ignore_called() to keep that particular callback from
   * setting the @c has_called() flag; traversal still continues for remaining
   * callbacks.
   *
   * @param callback  Visitor called as @c callback(impl, stored_callback) for each entry.
   */
  void traverse_server_connect_callback(const FindConnectCallback& callback);

  /**
   * @brief Invokes @p callback for each registered subscriber-connect callback.
   *
   * @param callback  Visitor called for each subscriber-connect entry.
   */
  void traverse_sub_connect_callback(const FindConnectCallback& callback);

  /**
   * @brief Invokes @p callback for each registered request/response callback.
   *
   * @param callback  Visitor called for each req/resp entry.
   */
  void traverse_req_resp_callback(const FindReqRespCallback& callback);

  /**
   * @brief Invokes @p callback for each registered serialised-message callback.
   *
   * @param callback  Visitor called for each message callback entry.
   */
  void traverse_msg_callback(const FindMsgCallback& callback);

  /**
   * @brief Invokes @p callback for each registered in-process message callback.
   *
   * @param callback  Visitor called for each intra-msg entry.
   */
  void traverse_intra_msg_callback(const FindIntraMsgCallback& callback);

  /**
   * @brief Invokes @p callback for each registered status callback.
   *
   * @param callback  Visitor called for each status entry.
   */
  void traverse_status_callback(const FindStatusCallback& callback);

 protected:
  AbstractObject();

  ~AbstractObject() override;

  [[nodiscard]] bool has_called() const;

  void ignore_called();

 private:
  template <typename CallbackMapT, typename CallbackT>
  void traverse_internal_callback(const CallbackMapT& map, const CallbackT& callback);

  bool has_called_{false};
  bool ignore_called_{false};
  ImplList impl_list_;
  mutable std::recursive_mutex mtx_;
  ConnectCallbackMap server_connect_callback_map_;
  ConnectCallbackMap sub_connect_callback_map_;
  ReqRespCallbackMap req_resp_callback_map_;
  MsgCallbackMap msg_callback_map_;
  IntraMsgCallbackMap intra_msg_callback_map_;
  StatusCallbackMap status_callback_map_;
  NodeImpl* first_impl_{nullptr};

  VLINK_DISALLOW_COPY_AND_ASSIGN(AbstractObject)
};

/**
 * @class AbstractFactory
 * @brief Topic-keyed factory that creates and caches @c AbstractObject instances.
 *
 * @details
 * Maintains a @c std::map<FilterT, std::weak_ptr<Object>> so that multiple
 * @c NodeImpl instances sharing the same topic key reuse the same
 * @c AbstractObject.  Objects are reference-counted: the entry is automatically
 * removed from the map when the last @c shared_ptr to the object is destroyed,
 * preventing stale entries from accumulating.
 *
 * @note This class is not copy-constructible or copy-assignable.
 *
 * @tparam FilterT  The key type used to identify topics (e.g. @c std::string).
 */
template <typename FilterT>
class AbstractFactory {
  using Object = AbstractObject<FilterT>;
  using Map = std::map<FilterT, std::weak_ptr<Object>>;
  using Set = std::unordered_set<Object*>;

 public:
  /**
   * @brief Returns @c true if @p ptr is a live object tracked by this factory.
   *
   * @details
   * Checks the internal set of raw pointers to verify that @p ptr points to an
   * object that was created by this factory and has not yet been destroyed.
   *
   * @param ptr  Raw pointer to check.
   * @return     @c true if the object is currently alive; @c false otherwise.
   */
  [[nodiscard]] bool has_object(Object* ptr) const;

  /**
   * @brief Retrieves or creates the @c AbstractObject for the given @p filter key.
   *
   * @details
   * If an object already exists for @p filter and is still alive (the @c weak_ptr
   * is valid), the existing @c shared_ptr is returned.  Otherwise a new
   * @c ObjectT is heap-allocated, wrapped in a @c shared_ptr with a custom deleter
   * that removes the entry from the internal map on destruction, and cached.
   *
   * @tparam ObjectT  Concrete subclass of @c AbstractObject<FilterT> to instantiate.
   *
   * @param filter  Topic key used to look up or create the object.
   * @return        A @c shared_ptr<ObjectT> for the given @p filter.
   */
  template <typename ObjectT>
  [[nodiscard]] std::shared_ptr<ObjectT> get_object(const FilterT& filter);

 protected:
  /**
   * @brief Protected default constructor.
   */
  AbstractFactory();

  /**
   * @brief Protected virtual destructor.
   */
  virtual ~AbstractFactory();

 private:
  Set set_;
  Map map_;
  mutable std::mutex mtx_;

  VLINK_DISALLOW_COPY_AND_ASSIGN(AbstractFactory)
};

////////////////////////////////////////////////////////////////
/// Details
////////////////////////////////////////////////////////////////

template <typename FilterT>
inline bool AbstractObject<FilterT>::add_impl(NodeImpl* impl) {
  std::lock_guard lock(mtx_);

  first_impl_ = impl;

  return impl_list_.emplace(impl).second;
}

template <typename FilterT>
inline bool AbstractObject<FilterT>::remove_impl(NodeImpl* impl) {
  std::lock_guard lock(mtx_);

  if VUNLIKELY (impl_list_.erase(impl) == 0) {
    return false;
  }

  if (first_impl_ == impl) {
    first_impl_ = impl_list_.empty() ? nullptr : *impl_list_.begin();
  }

  server_connect_callback_map_.erase(impl);
  sub_connect_callback_map_.erase(impl);
  req_resp_callback_map_.erase(impl);
  msg_callback_map_.erase(impl);
  intra_msg_callback_map_.erase(impl);
  status_callback_map_.erase(impl);
  return true;
}

template <typename FilterT>
NodeImpl* AbstractObject<FilterT>::get_first_impl() const {
  std::lock_guard lock(mtx_);
  return first_impl_;
}

template <typename FilterT>
inline bool AbstractObject<FilterT>::is_contains_impl(NodeImpl* impl) const {
  std::lock_guard lock(mtx_);
  return impl_list_.find(impl) != impl_list_.end();
}

template <typename FilterT>
inline bool AbstractObject<FilterT>::has_impl() const {
  std::lock_guard lock(mtx_);
  return !impl_list_.empty();
}

template <typename FilterT>
inline bool AbstractObject<FilterT>::register_server_connect_callback(NodeImpl* impl,
                                                                      NodeImpl::ConnectCallback&& callback) {
  std::lock_guard lock(this->mtx_);
  return server_connect_callback_map_.try_emplace(impl, std::move(callback)).second;
}

template <typename FilterT>
inline bool AbstractObject<FilterT>::register_sub_connect_callback(NodeImpl* impl,
                                                                   NodeImpl::ConnectCallback&& callback) {
  std::lock_guard lock(this->mtx_);
  return sub_connect_callback_map_.try_emplace(impl, std::move(callback)).second;
}

template <typename FilterT>
inline bool AbstractObject<FilterT>::register_req_resp_callback(NodeImpl* impl, NodeImpl::ReqRespCallback&& callback) {
  std::lock_guard lock(this->mtx_);
  return req_resp_callback_map_.try_emplace(impl, std::move(callback)).second;
}

template <typename FilterT>
inline bool AbstractObject<FilterT>::register_msg_callback(NodeImpl* impl, NodeImpl::MsgCallback&& callback) {
  std::lock_guard lock(this->mtx_);
  return msg_callback_map_.try_emplace(impl, std::move(callback)).second;
}

template <typename FilterT>
inline bool AbstractObject<FilterT>::register_intra_msg_callback(NodeImpl* impl,
                                                                 NodeImpl::IntraMsgCallback&& callback) {
  std::lock_guard lock(this->mtx_);
  return intra_msg_callback_map_.try_emplace(impl, std::move(callback)).second;
}

template <typename FilterT>
inline bool AbstractObject<FilterT>::register_status_callback(NodeImpl* impl, NodeImpl::StatusCallback&& callback) {
  std::lock_guard lock(this->mtx_);
  return status_callback_map_.try_emplace(impl, std::move(callback)).second;
}

template <typename FilterT>
inline bool AbstractObject<FilterT>::server_connect_map_is_empty() const {
  std::lock_guard lock(this->mtx_);
  return server_connect_callback_map_.empty();
}

template <typename FilterT>
inline bool AbstractObject<FilterT>::sub_connect_map_is_empty() const {
  std::lock_guard lock(this->mtx_);
  return sub_connect_callback_map_.empty();
}

template <typename FilterT>
inline bool AbstractObject<FilterT>::req_resp_map_is_empty() const {
  std::lock_guard lock(this->mtx_);
  return req_resp_callback_map_.empty();
}

template <typename FilterT>
inline bool AbstractObject<FilterT>::msg_map_is_empty() const {
  std::lock_guard lock(this->mtx_);
  return msg_callback_map_.empty();
}

template <typename FilterT>
inline bool AbstractObject<FilterT>::intra_msg_map_is_empty() const {
  std::lock_guard lock(this->mtx_);
  return intra_msg_callback_map_.empty();
}

template <typename FilterT>
inline bool AbstractObject<FilterT>::status_map_is_empty() const {
  std::lock_guard lock(this->mtx_);
  return status_callback_map_.empty();
}

template <typename FilterT>
inline void AbstractObject<FilterT>::traverse_server_connect_callback(const FindConnectCallback& callback) {
  this->traverse_internal_callback(server_connect_callback_map_, callback);
}

template <typename FilterT>
inline void AbstractObject<FilterT>::traverse_sub_connect_callback(const FindConnectCallback& callback) {
  this->traverse_internal_callback(sub_connect_callback_map_, callback);
}

template <typename FilterT>
inline void AbstractObject<FilterT>::traverse_req_resp_callback(const FindReqRespCallback& callback) {
  this->traverse_internal_callback(req_resp_callback_map_, callback);
}

template <typename FilterT>
inline void AbstractObject<FilterT>::traverse_msg_callback(const FindMsgCallback& callback) {
  this->traverse_internal_callback(msg_callback_map_, callback);
}

template <typename FilterT>
inline void AbstractObject<FilterT>::traverse_intra_msg_callback(const FindIntraMsgCallback& callback) {
  this->traverse_internal_callback(intra_msg_callback_map_, callback);
}

template <typename FilterT>
inline void AbstractObject<FilterT>::traverse_status_callback(const FindStatusCallback& callback) {
  this->traverse_internal_callback(status_callback_map_, callback);
}

template <typename FilterT>
inline AbstractObject<FilterT>::AbstractObject() = default;

template <typename FilterT>
inline AbstractObject<FilterT>::~AbstractObject() = default;

template <typename FilterT>
inline bool AbstractObject<FilterT>::has_called() const {
  return has_called_;
}

template <typename FilterT>
inline void AbstractObject<FilterT>::ignore_called() {
  ignore_called_ = true;
}

template <typename FilterT>
template <typename CallbackMapT, typename CallbackT>
inline void AbstractObject<FilterT>::traverse_internal_callback(const CallbackMapT& map, const CallbackT& callback) {
  std::lock_guard lock(mtx_);

  this->ignore_called_ = false;
  this->has_called_ = false;

  for (const auto& [impl, target_callback] : map) {
    callback(impl, target_callback);

    if VUNLIKELY (this->ignore_called_) {
      this->ignore_called_ = false;
    } else {
      this->has_called_ = true;
    }
  }
}

template <typename FilterT>
inline bool AbstractFactory<FilterT>::has_object(Object* ptr) const {
  std::lock_guard lock(mtx_);
  return set_.count(ptr) > 0;
}

template <typename FilterT>
template <typename ObjectT>
inline std::shared_ptr<ObjectT> AbstractFactory<FilterT>::get_object(const FilterT& filter) {
  static_assert(std::is_base_of_v<Object, ObjectT>, "ObjectT must be derived from AbstractObject");
  std::shared_ptr<ObjectT> obj;
  {
    std::unique_lock lock(mtx_);

    const auto& deleter = [this, filter](ObjectT* obj) {
      {
        std::lock_guard lock(mtx_);

        set_.erase(obj);

        auto iter = map_.find(filter);

        if (iter != map_.end() && iter->second.expired()) {
          map_.erase(iter);
        }
      }

      delete obj;
    };

    auto iter = map_.find(filter);

    if (iter != map_.end()) {
      obj = std::static_pointer_cast<ObjectT>(iter->second.lock());

      if VLIKELY (obj) {
        return obj;
      }

      map_.erase(iter);
    }

    {
      lock.unlock();
      auto* obj_ptr = new ObjectT(filter);
      lock.lock();

      auto [it, inserted] = map_.try_emplace(filter, std::weak_ptr<Object>());

      if (!inserted) {
        obj = std::static_pointer_cast<ObjectT>(it->second.lock());
      }

      if (inserted || !obj) {
        if (!inserted) {
          map_.erase(it);
          it = map_.try_emplace(filter, std::weak_ptr<Object>()).first;
        }

        obj = std::shared_ptr<ObjectT>(obj_ptr, deleter);
        it->second = obj;
        set_.emplace(obj_ptr);
      } else {
        // Another thread inserted while we were unlocked; discard our object.
        delete obj_ptr;
      }
    }
    return obj;
  }
}

template <typename FilterT>
inline AbstractFactory<FilterT>::AbstractFactory() = default;

template <typename FilterT>
inline AbstractFactory<FilterT>::~AbstractFactory() = default;

}  // namespace vlink
