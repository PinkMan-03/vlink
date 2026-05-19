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

#include <memory>
#include <mutex>
#include <utility>
#include <vector>

#include "./functional.h"
#include "./macros.h"

namespace vlink {

/**
 * @class ObjectPoolBase
 * @brief Non-template base of @c ObjectPool: owns all element-type-independent state and
 *        provides counter / policy / threading primitives.
 *
 * @details
 * Holds the mutex, the live / borrowed / total counters, the @c max_size limit, and the
 * @c Policy enum.  The element-type-dependent state (factory callback, reset callback,
 * free-list vector) lives in @c ObjectPool<T>.  Cold-path validation and error throws are
 * also implemented here so each instantiation of @c ObjectPool<T> does not duplicate the
 * @c std::ostringstream / @c std::runtime_error code.
 */
class VLINK_EXPORT ObjectPoolBase {
 public:
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
   * @brief Returns the number of objects currently held by callers.
   */
  [[nodiscard]] size_t borrowed() const;

  /**
   * @brief Returns the total number of objects ever created by this pool.
   */
  [[nodiscard]] size_t total_created() const;

  /**
   * @brief Returns the maximum total object count allowed by this pool.
   */
  [[nodiscard]] size_t max_size() const noexcept;

 protected:
  ObjectPoolBase(size_t max_size, size_t initial_size, Policy policy);

  ~ObjectPoolBase() noexcept = default;

  void safe_dec_borrowed_and_live() noexcept;

  [[nodiscard]] bool should_reset_on_acquire() const noexcept;

  [[nodiscard]] bool should_reset_on_release() const noexcept;

  [[noreturn]] static void throw_invalid_size();

  [[noreturn]] static void throw_factory_null_pre_fill();

  [[noreturn]] static void throw_factory_null();

  [[noreturn]] void throw_exhausted(size_t pool_size) const;

  Policy policy_{kPolicyNone};
  size_t max_size_{0};

  mutable std::mutex mutex_;

  size_t borrowed_{0};
  size_t live_count_{0};
  size_t total_created_{0};
};

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
class ObjectPool : public ObjectPoolBase, public std::enable_shared_from_this<ObjectPool<T>> {
 public:
  /**
   * @brief Callback type for creating a new instance of @p T.
   *
   * @details
   * Must return a non-null @c unique_ptr<T>.  Throwing or returning @c nullptr causes
   * the acquisition call to propagate the error to the caller.
   */
  using FactoryCallback = MoveFunction<std::unique_ptr<T>()>;

  /**
   * @brief Callback type for resetting an object before acquisition or after release.
   *
   * @details
   * Called with a reference to the object.  Exceptions thrown here are caught; on release,
   * the object is discarded (not returned to the pool) if the reset throws.
   */
  using ResetCallback = MoveFunction<void(T&)>;

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
   */
  [[nodiscard]] std::unique_ptr<T, typename ObjectPool<T>::PoolDeleter> get();

  /**
   * @brief Acquires an object and returns it as a @c shared_ptr with automatic pool return.
   */
  [[nodiscard]] std::shared_ptr<T> get_shared();

  /**
   * @brief Acquires an object and returns a raw pointer; caller is responsible for returning it.
   */
  [[nodiscard]] T* borrow();

  /**
   * @brief Returns a raw-pointer object previously obtained via @c borrow() to the pool.
   */
  void give_back(T* ptr);

  /**
   * @brief Returns a snapshot of all pool statistics (thread-safe).
   */
  [[nodiscard]] Stats stats() const;

  /**
   * @brief Returns the number of idle objects currently in the pool.
   */
  [[nodiscard]] size_t size() const;

 private:
  static FactoryCallback get_default_factory();

  std::unique_ptr<T> acquire();

  void release(std::unique_ptr<T> obj) noexcept;

  FactoryCallback factory_callback_;
  ResetCallback reset_callback_;
  std::vector<std::unique_ptr<T>> pool_;

  VLINK_DISALLOW_COPY_AND_ASSIGN(ObjectPool)
};

////////////////////////////////////////////////////////////////
/// Details
////////////////////////////////////////////////////////////////

template <typename T>
inline ObjectPool<T>::ObjectPool(FactoryCallback factory_callback, size_t initial_size, size_t max_size,
                                 ResetCallback reset_callback, Policy policy)
    : ObjectPoolBase(max_size, initial_size, policy),
      factory_callback_(std::move(factory_callback)),
      reset_callback_(std::move(reset_callback)) {
  pool_.reserve(initial_size);
  for (size_t i = 0; i < initial_size; ++i) {
    auto obj = factory_callback_();

    if VUNLIKELY (!obj) {
      throw_factory_null_pre_fill();
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
inline ObjectPoolBase::Stats ObjectPool<T>::stats() const {
  std::lock_guard lock(mutex_);
  return {pool_.size(), borrowed_, total_created_, max_size_};
}

template <typename T>
inline size_t ObjectPool<T>::size() const {
  std::lock_guard lock(mutex_);
  return pool_.size();
}

template <typename T>
inline typename ObjectPool<T>::FactoryCallback ObjectPool<T>::get_default_factory() {
  return [] { return std::make_unique<T>(); };
}

template <typename T>
inline std::unique_ptr<T> ObjectPool<T>::acquire() {
  std::unique_lock lock(mutex_);

  if (!pool_.empty()) {
    std::unique_ptr<T> obj = std::move(pool_.back());
    pool_.pop_back();
    ++borrowed_;
    return obj;
  }

  if VUNLIKELY (max_size_ > 0 && live_count_ >= max_size_) {
    throw_exhausted(pool_.size());
  }

  ++live_count_;
  ++total_created_;
  ++borrowed_;

  lock.unlock();

  std::unique_ptr<T> new_obj;
  try {
    new_obj = factory_callback_();

    if VUNLIKELY (!new_obj) {
      throw_factory_null();
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
    std::lock_guard lock(mutex_);

    if (borrowed_ > 0) {
      --borrowed_;
    }

    try {
      pool_.emplace_back(std::move(obj));
    } catch (...) {
      if (live_count_ > 0) {
        --live_count_;
      }
    }
  }
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
