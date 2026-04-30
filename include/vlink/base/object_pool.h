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
 * @file object_pool.h
 * @brief Thread-safe generic object pool with configurable reset policy and RAII ownership.
 *
 * @details
 * @c ObjectPool<T> maintains a free-list of pre-allocated @c T objects.  Callers acquire an
 * object, use it, then return it to the pool automatically (RAII) or manually.  Objects are
 * recycled rather than destroyed, reducing heap pressure in hot paths.
 *
 * Acquisition API:
 *
 * | Method        | Return type                       | Auto-return on destruction        |
 * | ------------- | --------------------------------- | --------------------------------- |
 * | @c get()      | unique_ptr<T, PoolDeleter>        | Yes (via PoolDeleter)             |
 * | @c get_shared | shared_ptr<T>   (PoolDeleter)     | Yes (via shared_ptr deleter)      |
 * | @c borrow()   | T*  (raw pointer)                 | No -- caller must call give_back() |
 *
 * Reset policy controls when the optional @c ResetCallback is invoked:
 *
 * | Policy             | Reset on acquire | Reset on release | Use case                         |
 * | ------------------ | ---------------- | ---------------- | -------------------------------- |
 * | @c kPolicyNone     | No               | No               | Immutable / stateless objects    |
 * | @c kPolicyRelease  | No               | Yes              | Clean before returning (default) |
 * | @c kPolicyAcquire  | Yes              | No               | Clean before use                 |
 * | @c kPolicyBoth     | Yes              | Yes              | Clean on both sides              |
 *
 * @par Thread safety
 * All public methods are protected by an internal mutex and are safe to call concurrently.
 *
 * @note
 * - When @c max_size is 0 (default), the pool grows without bound.
 * - When the pool is exhausted (@c max_size > 0 and the number of live objects reaches @c max_size),
 *   @c get(), @c get_shared(), and @c borrow() throw @c std::runtime_error.
 * - If @c FactoryCallback returns @c nullptr, a @c std::runtime_error is thrown.
 *
 * @par Example
 * @code
 * auto pool = std::make_shared<vlink::ObjectPool<Buffer>>(
 *     []{ return std::make_unique<Buffer>(4096); },  // factory
 *     4,                                              // initial_size
 *     16,                                             // max_size
 *     [](Buffer& b){ b.reset(); },                   // reset callback
 *     vlink::ObjectPool<Buffer>::kPolicyRelease       // reset on return
 * );
 *
 * {
 *     auto buf = pool->get();   // auto-returned when buf goes out of scope
 *     buf->write(data, len);
 * }
 *
 * // Manual acquire/release:
 * Buffer* raw = pool->borrow();
 * raw->write(data, len);
 * pool->give_back(raw);
 * @endcode
 *
 * @tparam T  Type of objects managed by the pool.
 */

#pragma once

#include <functional>
#include <memory>
#include <mutex>
#include <sstream>
#include <stdexcept>
#include <utility>
#include <vector>

#include "./macros.h"

namespace vlink {

/**
 * @class ObjectPool
 * @brief Thread-safe object pool for type @p T with RAII acquisition and configurable reset policy.
 *
 * @details
 * Must be heap-allocated and managed via @c std::shared_ptr, because @c PoolDeleter holds a
 * @c std::weak_ptr to the pool.  Create with @c std::make_shared<ObjectPool<T>>(...).
 *
 * @tparam T  Type of pooled objects.  Must be default-constructible unless a custom
 *            @c FactoryCallback is supplied.
 */
template <typename T>
class ObjectPool : public std::enable_shared_from_this<ObjectPool<T>> {
 public:
  /**
   * @brief Callback type for creating a new instance of @p T.
   *
   * @details
   * Must return a non-null @c unique_ptr<T>.  Throwing or returning @c nullptr causes
   * the acquisition call to propagate the error to the caller.
   */
  using FactoryCallback = std::function<std::unique_ptr<T>()>;

  /**
   * @brief Callback type for resetting an object before acquisition or after release.
   *
   * @details
   * Called with a reference to the object.  Exceptions thrown here are caught; on release,
   * the object is discarded (not returned to the pool) if the reset throws.
   */
  using ResetCallback = std::function<void(T&)>;

  /**
   * @brief Controls when the @c ResetCallback is invoked relative to acquire and release.
   */
  enum Policy : uint8_t {
    kPolicyNone = 0,     ///< Never invoke reset callback.
    kPolicyRelease = 1,  ///< Invoke reset callback when object is returned to the pool.
    kPolicyAcquire = 2,  ///< Invoke reset callback when object is acquired from the pool.
    kPolicyBoth = 3,     ///< Invoke reset callback on both acquire and release.
  };

  /**
   * @brief Snapshot of pool statistics at a point in time.
   */
  struct Stats final {
    size_t pool_size{0};      ///< Number of objects currently idle in the pool.
    size_t borrowed{0};       ///< Number of objects currently held by callers.
    size_t total_created{0};  ///< Total objects ever created (pool_size + borrowed + destroyed).
    size_t max_size{0};       ///< Maximum allowed total objects.  0 means unlimited.
  };

  /**
   * @struct PoolDeleter
   * @brief Custom deleter for RAII handles returned by @c get() and @c get_shared().
   *
   * @details
   * Holds a @c weak_ptr to the parent pool.  On destruction of the RAII handle,
   * @c operator() is called:
   * - If the pool is still alive, the object is returned to it via @c release().
   * - If the pool has been destroyed, the object is deleted with @c delete.
   */
  struct PoolDeleter final {
    /**
     * @brief Returns @p ptr to the pool, or deletes it if the pool is gone.
     *
     * @param ptr  Raw pointer to the object being released.  Ignored if @c nullptr.
     */
    void operator()(T* ptr) const noexcept;
    std::weak_ptr<ObjectPool<T>> weak_pool;  ///< Non-owning reference to the parent pool.
  };

  /**
   * @brief Constructs the pool and optionally pre-populates it with objects.
   *
   * @param factory_callback  Factory used to create new @p T instances.
   *                          Default: @c std::make_unique<T>().
   * @param initial_size      Number of objects to pre-allocate at construction.
   *                          The reset callback (if @c kPolicyRelease or @c kPolicyBoth) is
   *                          invoked on each pre-allocated object.
   * @param max_size          Upper bound on total objects (idle + borrowed).
   *                          0 = unlimited.
   * @param reset_callback    Optional callback invoked to reset objects per @p policy.
   * @param policy            When to invoke @p reset_callback (default @c kPolicyRelease).
   *
   * @throws std::invalid_argument if @p initial_size > @p max_size and @p max_size > 0.
   * @throws std::runtime_error    if @p factory_callback returns @c nullptr during pre-fill.
   *
   * @note Must be used via @c std::make_shared<ObjectPool<T>>(...) to enable @c PoolDeleter.
   */
  explicit ObjectPool(FactoryCallback factory_callback = get_default_factory(), size_t initial_size = 0,
                      size_t max_size = 0, ResetCallback reset_callback = nullptr, Policy policy = kPolicyRelease);

  /**
   * @brief Acquires an object and returns it as a @c unique_ptr with automatic pool return.
   *
   * @details
   * If the pool is non-empty, the most recently returned object is popped (LIFO).
   * Otherwise a new object is created via the factory callback.
   * If @c kPolicyAcquire or @c kPolicyBoth is set, the reset callback is applied before
   * returning the pointer.
   *
   * @return A @c unique_ptr<T, PoolDeleter> whose destruction returns the object to the pool.
   *
   * @throws std::runtime_error if the pool is exhausted (@c max_size > 0 and all objects are
   *                            in use), or if the factory callback returns @c nullptr.
   */
  [[nodiscard]] std::unique_ptr<T, typename ObjectPool<T>::PoolDeleter> get();

  /**
   * @brief Acquires an object and returns it as a @c shared_ptr with automatic pool return.
   *
   * @details
   * Behaves identically to @c get() but returns a @c shared_ptr.  The custom deleter is
   * @c PoolDeleter, so the object is returned to the pool when the last @c shared_ptr copy
   * is destroyed.
   *
   * @return A @c shared_ptr<T> that returns the object to the pool on last-reference destruction.
   *
   * @throws std::runtime_error if the pool is exhausted or the factory returns @c nullptr.
   */
  [[nodiscard]] std::shared_ptr<T> get_shared();

  /**
   * @brief Acquires an object and returns a raw pointer; caller is responsible for returning it.
   *
   * @details
   * Unlike @c get() and @c get_shared(), this method does NOT use RAII.  The caller
   * MUST call @c give_back() to return the object.  Failure to do so causes a resource leak
   * and may prevent the pool from reaching @c max_size when needed.
   *
   * @return Raw pointer to the acquired object.  Never @c nullptr on success.
   *
   * @throws std::runtime_error if the pool is exhausted or the factory returns @c nullptr.
   *
   * @warning Pair every @c borrow() with a corresponding @c give_back() call.
   */
  [[nodiscard]] T* borrow();

  /**
   * @brief Returns a raw-pointer object previously obtained via @c borrow() to the pool.
   *
   * @details
   * If the reset policy includes @c kPolicyRelease, the reset callback is invoked before
   * the object re-enters the pool.  If the reset callback throws, the object is discarded
   * rather than returned.
   *
   * @param ptr  Pointer returned by @c borrow().  Passing @c nullptr is a no-op.
   *
   * @note Do NOT call @c give_back() on pointers obtained from @c get() or @c get_shared();
   *       those are managed by their respective deleters.
   */
  void give_back(T* ptr);

  /**
   * @brief Returns a snapshot of all pool statistics (thread-safe).
   *
   * @return @c Stats struct containing pool_size, borrowed, total_created, max_size.
   */
  [[nodiscard]] Stats stats() const;

  /**
   * @brief Returns the number of idle objects currently in the pool.
   *
   * @return Number of available objects that can be acquired without allocation.
   */
  [[nodiscard]] size_t size() const;

  /**
   * @brief Returns the number of objects currently held by callers.
   *
   * @return Count of objects acquired and not yet returned.
   */
  [[nodiscard]] size_t borrowed() const;

  /**
   * @brief Returns the total number of objects ever created by this pool.
   *
   * @details
   * Includes objects currently idle, borrowed, and any that were discarded due to
   * failed reset callbacks.  Never decreases.
   *
   * @return Cumulative creation count.
   */
  [[nodiscard]] size_t total_created() const;

  /**
   * @brief Returns the maximum total object count allowed by this pool.
   *
   * @return Max size limit.  Returns 0 if the pool is unbounded.
   */
  [[nodiscard]] size_t max_size() const;

 private:
  static FactoryCallback get_default_factory();

  std::unique_ptr<T> acquire();

  void release(std::unique_ptr<T> obj) noexcept;

  void safe_dec_borrowed_and_live() noexcept;

  bool should_reset_on_acquire() const noexcept;

  bool should_reset_on_release() const noexcept;

  [[nodiscard]] std::runtime_error exhausted_error_locked() const;

  FactoryCallback factory_callback_;
  ResetCallback reset_callback_;
  Policy policy_{kPolicyNone};
  size_t max_size_{0};

  mutable std::mutex mutex_;
  std::vector<std::unique_ptr<T>> pool_;

  size_t borrowed_{0};
  size_t live_count_{0};
  size_t total_created_{0};

  VLINK_DISALLOW_COPY_AND_ASSIGN(ObjectPool)
};

////////////////////////////////////////////////////////////////
/// Details
////////////////////////////////////////////////////////////////

template <typename T>
inline ObjectPool<T>::ObjectPool(FactoryCallback factory_callback, size_t initial_size, size_t max_size,
                                 ResetCallback reset_callback, Policy policy)
    : factory_callback_(std::move(factory_callback)),
      reset_callback_(std::move(reset_callback)),
      policy_(policy),
      max_size_(max_size) {
  if VUNLIKELY (max_size_ > 0 && initial_size > max_size_) {
    throw std::invalid_argument("initial_size exceeds max_size");
  }

  pool_.reserve(initial_size);
  for (size_t i = 0; i < initial_size; ++i) {
    auto obj = factory_callback_();

    if VUNLIKELY (!obj) {
      throw std::runtime_error("FactoryCallback returned nullptr during pre-fill");
    }

    if (should_reset_on_release() && reset_callback_) {
      reset_callback_(*obj);
    }

    pool_.emplace_back(std::move(obj));
  }

  total_created_ = initial_size;
  live_count_ = initial_size;
}

template <typename T>
inline std::unique_ptr<T, typename ObjectPool<T>::PoolDeleter> ObjectPool<T>::get() {
  std::unique_ptr<T> obj = acquire();

  try {
    if (should_reset_on_acquire() && reset_callback_) {
      reset_callback_(*obj);
    }
  } catch (...) {
    release(std::move(obj));
    throw;
  }

  return {obj.release(), PoolDeleter{this->weak_from_this()}};
}

template <typename T>
inline std::shared_ptr<T> ObjectPool<T>::get_shared() {
  std::unique_ptr<T> obj = acquire();

  try {
    if (should_reset_on_acquire() && reset_callback_) {
      reset_callback_(*obj);
    }
  } catch (...) {
    release(std::move(obj));
    throw;
  }

  return {obj.release(), PoolDeleter{this->weak_from_this()}};
}

template <typename T>
inline T* ObjectPool<T>::borrow() {
  std::unique_ptr<T> obj = acquire();

  try {
    if (should_reset_on_acquire() && reset_callback_) {
      reset_callback_(*obj);
    }
  } catch (...) {
    release(std::move(obj));
    throw;
  }

  return obj.release();
}

template <typename T>
inline void ObjectPool<T>::give_back(T* ptr) {
  if VUNLIKELY (!ptr) {
    return;
  }

  std::unique_ptr<T> u(ptr);
  release(std::move(u));
}

template <typename T>
inline typename ObjectPool<T>::Stats ObjectPool<T>::stats() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return {pool_.size(), borrowed_, total_created_, max_size_};
}

template <typename T>
inline size_t ObjectPool<T>::size() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return pool_.size();
}

template <typename T>
inline size_t ObjectPool<T>::borrowed() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return borrowed_;
}

template <typename T>
inline size_t ObjectPool<T>::total_created() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return total_created_;
}

template <typename T>
inline size_t ObjectPool<T>::max_size() const {
  return max_size_;
}

template <typename T>
inline typename ObjectPool<T>::FactoryCallback ObjectPool<T>::get_default_factory() {
  return [] { return std::make_unique<T>(); };
}

template <typename T>
inline std::unique_ptr<T> ObjectPool<T>::acquire() {
  std::unique_lock<std::mutex> lock(mutex_);

  if (!pool_.empty()) {
    std::unique_ptr<T> obj = std::move(pool_.back());
    pool_.pop_back();
    ++borrowed_;
    return obj;
  }

  if VUNLIKELY (max_size_ > 0 && live_count_ >= max_size_) {
    throw exhausted_error_locked();
  }

  ++live_count_;
  ++total_created_;
  ++borrowed_;

  lock.unlock();

  std::unique_ptr<T> new_obj;
  try {
    new_obj = factory_callback_();

    if VUNLIKELY (!new_obj) {
      throw std::runtime_error("FactoryCallback returned nullptr");
    }
  } catch (...) {
    lock.lock();
    --borrowed_;
    --live_count_;
    --total_created_;
    throw;
  }

  return new_obj;
}

template <typename T>
inline void ObjectPool<T>::release(std::unique_ptr<T> obj) noexcept {
  if VUNLIKELY (!obj) {
    return;
  }

  if (should_reset_on_release() && reset_callback_) {
    try {
      reset_callback_(*obj);
    } catch (...) {
      safe_dec_borrowed_and_live();
      return;
    }
  }

  {
    std::lock_guard<std::mutex> lock(mutex_);

    if (borrowed_ > 0) {
      --borrowed_;
    }

    pool_.emplace_back(std::move(obj));
  }
}

template <typename T>
inline void ObjectPool<T>::safe_dec_borrowed_and_live() noexcept {
  std::lock_guard<std::mutex> lock(mutex_);

  if (borrowed_ > 0) {
    --borrowed_;
  }

  if (live_count_ > 0) {
    --live_count_;
  }
}

template <typename T>
inline bool ObjectPool<T>::should_reset_on_acquire() const noexcept {
  return policy_ == kPolicyAcquire || policy_ == kPolicyBoth;
}

template <typename T>
inline bool ObjectPool<T>::should_reset_on_release() const noexcept {
  return policy_ == kPolicyRelease || policy_ == kPolicyBoth;
}

template <typename T>
inline std::runtime_error ObjectPool<T>::exhausted_error_locked() const {
  std::ostringstream oss;

  oss << "ObjectPool exhausted: max_size=" << max_size_ << " live_count=" << live_count_
      << " total_created=" << total_created_ << " borrowed=" << borrowed_ << " pool_size=" << pool_.size();

  return std::runtime_error(oss.str());
}

template <typename T>
inline void ObjectPool<T>::PoolDeleter::operator()(T* ptr) const noexcept {
  if VUNLIKELY (!ptr) {
    return;
  }

  if (auto sp = weak_pool.lock()) {
    std::unique_ptr<T> u(ptr);
    sp->release(std::move(u));
  } else {
    delete ptr;
  }
}

}  // namespace vlink
