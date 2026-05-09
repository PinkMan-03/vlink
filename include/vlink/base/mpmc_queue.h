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
 * @file mpmc_queue.h
 * @brief Lock-free bounded multi-producer multi-consumer queue with optional blocking behaviour.
 *
 * @details
 * @c MpmcQueue<T> is a fixed-capacity, cache-line-aligned, lock-free MPMC ring buffer
 * based on a turn-counting algorithm.  Each slot contains an atomic turn counter that
 * tracks whether the slot is empty (ready for a producer) or full (ready for a consumer).
 *
 * Concurrency model:
 * - Producers atomically increment @c head_ to claim a slot, then wait until
 *   @c chunk.turn == turn(head) * 2 (slot is empty) before constructing the value.
 * - Consumers atomically increment @c tail_ to claim a slot, then wait until
 *   @c chunk.turn == turn(tail) * 2 + 1 (slot is full) before moving the value out.
 * - All waits spin for @c kFirstSpinTimes (32) iterations before calling @c yield_cpu().
 *
 * Behaviour modes:
 *
 * | Behavior              | Effect on emplace/push              | Effect on pop                    |
 * | --------------------- | ----------------------------------- | -------------------------------- |
 * | @c kNoBehavior        | No notification                     | No blocking                      |
 * | @c kConditionBehavior | Signals @c cv_not_empty_ on push    | Signals @c cv_not_full_ on pop   |
 *
 * @c kConditionBehavior enables @c wait_not_empty() and @c wait_not_full() to wake correctly.
 * Use it with @c kBlockStrategy message loops.
 *
 * Cache-line alignment:
 * - @c head_ and @c tail_ are each aligned to 64 bytes to prevent false sharing.
 * - Each @c Chunk slot is also 64-byte aligned.
 * - The queue object itself is a multiple of 64 bytes.
 *
 * @note
 * - @c emplace() / @c pop() block indefinitely (spinning) until a slot is available.
 *   For bounded producers, use @c try_emplace() / @c try_push() instead.
 * - @c notify_to_quit() sets a quit flag and wakes all blocked @c wait_not_empty() /
 *   @c wait_not_full() calls.  After calling this, further pushes are silently dropped.
 * - Capacity must be >= 1; passing 0 throws @c std::invalid_argument.
 * - The @c VLINK_NO_INSTRUMENT attribute suppresses GCC's @c -finstrument-functions on Linux.
 *
 * @par Example
 * @code
 * vlink::MpmcQueue<int> q(1024);
 *
 * // Producer thread:
 * q.push<vlink::MpmcQueue<int>::kConditionBehavior>(42);
 *
 * // Consumer thread:
 * q.wait_not_empty();
 * int val;
 * q.pop<vlink::MpmcQueue<int>::kConditionBehavior>(val);
 * @endcode
 */

#pragma once

#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <limits>
#include <memory>
#include <mutex>
#include <new>
#include <stdexcept>
#include <utility>

#include "./condition_variable.h"
#include "./macros.h"
#include "./utils.h"

#if defined(__linux__)
#define VLINK_NO_INSTRUMENT __attribute__((no_instrument_function))
#else
#define VLINK_NO_INSTRUMENT
#endif

namespace vlink {

/**
 * @class MpmcQueue
 * @brief Fixed-capacity, lock-free, cache-line-aligned MPMC ring buffer.
 *
 * @tparam T  Element type.  Must be moveable.
 */
template <typename T>
class MpmcQueue {
 public:
  /**
   * @brief Controls whether condition-variable notifications are sent on push/pop.
   *
   * | Value               | Behaviour                                              |
   * | ------------------- | ------------------------------------------------------ |
   * | @c kNoBehavior      | No notifications; used with busy-wait or polling       |
   * | @c kConditionBehavior| Notifies @c cv_not_empty_ / @c cv_not_full_ on change |
   */
  enum Behavior : uint8_t { kNoBehavior = 0, kConditionBehavior = 1 };

  /**
   * @brief Constructs a @c MpmcQueue with the given fixed capacity.
   *
   * @param capacity  Maximum number of elements.  Must be >= 1.
   * @throws std::invalid_argument if @p capacity < 1.
   * @throws std::bad_alloc if the internal chunk array cannot be allocated.
   */
  explicit MpmcQueue(size_t capacity) VLINK_NO_INSTRUMENT;

  /**
   * @brief Destructor.  Destroys any elements still in the queue.
   */
  ~MpmcQueue() noexcept VLINK_NO_INSTRUMENT;

  /**
   * @brief In-place constructs an element and blocks until a slot is available.
   *
   * @details
   * If @c BehaviorT == @c kConditionBehavior, notifies @c wait_not_empty() waiters after push.
   * Silently drops the element if @c notify_to_quit() has been called.
   *
   * @tparam BehaviorT  Notification behaviour.  Default: @c kNoBehavior.
   * @tparam Args       Constructor argument types.
   * @param args        Arguments forwarded to @c T's constructor.
   */
  template <Behavior BehaviorT = kNoBehavior, typename... Args>
  void emplace(Args&&... args) noexcept VLINK_NO_INSTRUMENT;

  /**
   * @brief In-place constructs an element without blocking.
   *
   * @details
   * Returns @c false immediately if the queue is full.
   *
   * @tparam BehaviorT  Notification behaviour.  Default: @c kNoBehavior.
   * @tparam Args       Constructor argument types.
   * @param args        Arguments forwarded to @c T's constructor.
   * @return @c true if the element was enqueued; @c false if the queue was full.
   */
  template <Behavior BehaviorT = kNoBehavior, typename... Args>
  [[nodiscard]] bool try_emplace(Args&&... args) noexcept VLINK_NO_INSTRUMENT;

  /**
   * @brief Pushes a value (by forwarding) and blocks until a slot is available.
   *
   * @details
   * Forwards to @c emplace<BehaviorT>(std::forward<P>(v)).
   *
   * @tparam BehaviorT  Notification behaviour.  Default: @c kNoBehavior.
   * @tparam P          Value type (lvalue or rvalue reference).
   * @param v           Value to push.
   */
  template <Behavior BehaviorT = kNoBehavior, typename P>
  void push(P&& v) noexcept VLINK_NO_INSTRUMENT;

  /**
   * @brief Pushes a value without blocking; returns @c false if the queue is full.
   *
   * @tparam BehaviorT  Notification behaviour.  Default: @c kNoBehavior.
   * @tparam P          Value type.
   * @param v           Value to push.
   * @return @c true if pushed; @c false if full.
   */
  template <Behavior BehaviorT = kNoBehavior, typename P>
  [[nodiscard]] bool try_push(P&& v) noexcept VLINK_NO_INSTRUMENT;

  /**
   * @brief Pops a value by move and blocks until an element is available.
   *
   * @details
   * If @c BehaviorT == @c kConditionBehavior, notifies @c wait_not_full() waiters after pop.
   * Returns without modifying @p v if @c notify_to_quit() has been called.
   *
   * @tparam BehaviorT  Notification behaviour.  Default: @c kNoBehavior.
   * @param v           Output: receives the popped element via move.
   */
  template <Behavior BehaviorT = kNoBehavior>
  void pop(T& v) noexcept VLINK_NO_INSTRUMENT;

  /**
   * @brief Pops a value without blocking; returns @c false if the queue is empty.
   *
   * @tparam BehaviorT  Notification behaviour.  Default: @c kNoBehavior.
   * @param v           Output: receives the popped element via move.
   * @return @c true if an element was popped; @c false if empty.
   */
  template <Behavior BehaviorT = kNoBehavior>
  [[nodiscard]] bool try_pop(T& v) noexcept VLINK_NO_INSTRUMENT;

  /**
   * @brief Returns the fixed capacity of the queue.
   *
   * @return Capacity as passed to the constructor.
   */
  [[nodiscard]] size_t capacity() const noexcept VLINK_NO_INSTRUMENT;

  /**
   * @brief Returns an approximation of the current number of elements.
   *
   * @details
   * If @p real is @c false (default), returns @c head - tail (fast but may be slightly stale).
   * If @p real is @c true, retries up to 50 times until a stable snapshot is obtained.
   *
   * @param real  If @c true, use a more accurate but slower measurement.  Default: @c false.
   * @return Approximate element count.
   */
  [[nodiscard]] size_t size(bool real = false) const noexcept VLINK_NO_INSTRUMENT;

  /**
   * @brief Returns @c true if the queue appears to be empty.
   *
   * @param real  If @c true, use a more accurate measurement.  Default: @c false.
   * @return @c true if @c size(real) == 0.
   */
  [[nodiscard]] bool empty(bool real = false) const noexcept VLINK_NO_INSTRUMENT;

  /**
   * @brief Returns @c true if the queue appears to be full.
   *
   * @param real  If @c true, use a more accurate measurement.  Default: @c false.
   * @return @c true if @c size(real) >= @c capacity().
   */
  [[nodiscard]] bool is_full(bool real = false) const noexcept VLINK_NO_INSTRUMENT;

  /**
   * @brief Blocks until the queue is not empty (or a timeout elapses).
   *
   * @details
   * If @p timeout is @c std::chrono::milliseconds(0), blocks indefinitely.
   * Returns immediately if the queue already has elements.
   * Returns @c false if @c notify_to_quit() was called.
   *
   * @param timeout  Wait duration.  0 = wait indefinitely.  Default: 0.
   * @return @c true if the queue became non-empty; @c false on quit.
   */
  bool wait_not_empty(std::chrono::milliseconds timeout = std::chrono::milliseconds(0)) noexcept VLINK_NO_INSTRUMENT;

  /**
   * @brief Blocks until the queue has space (or a timeout elapses).
   *
   * @details
   * If @p timeout is @c std::chrono::milliseconds(0), blocks indefinitely.
   * Returns immediately if the queue is not full.
   * Returns @c false if @c notify_to_quit() was called.
   *
   * @param timeout  Wait duration.  0 = wait indefinitely.  Default: 0.
   * @return @c true if space is available; @c false on quit.
   */
  bool wait_not_full(std::chrono::milliseconds timeout = std::chrono::milliseconds(0)) noexcept VLINK_NO_INSTRUMENT;

  /**
   * @brief Signals all blocked @c wait_not_empty() / @c wait_not_full() callers to exit.
   *
   * @details
   * Sets the quit flag so that all subsequent @c emplace/push calls are silently dropped and
   * all blocking @c pop / wait calls return immediately.  Used during graceful shutdown.
   */
  void notify_to_quit() noexcept VLINK_NO_INSTRUMENT;

 private:
#if defined(__cpp_aligned_new)
  template <typename ChunkT>
  using AlignedAllocator = std::allocator<ChunkT>;
#else
  template <typename ChunkT>
  struct AlignedAllocator {
    using value_type = ChunkT;

    ChunkT* allocate(size_t n) {
      if (n > std::numeric_limits<size_t>::max() / sizeof(ChunkT)) {
        throw std::bad_array_new_length();
      }

#ifdef _WIN32
      auto* p = static_cast<ChunkT*>(_aligned_malloc(sizeof(ChunkT) * n, alignof(ChunkT)));

      if (p == nullptr) {
        throw std::bad_alloc();
      }
#else
      ChunkT* p;

      if (posix_memalign(reinterpret_cast<void**>(&p), alignof(ChunkT), sizeof(ChunkT) * n) != 0) {
        throw std::bad_alloc();
      }
#endif

      return p;
    }

    void deallocate(ChunkT* p, size_t) {
#ifdef _WIN32
      _aligned_free(p);
#else
      free(p);
#endif
    }
  };
#endif

  static constexpr std::memory_order kMemoryOrderAcquire = std::memory_order_acquire;
  static constexpr std::memory_order kMemoryOrderRelease = std::memory_order_release;
  static constexpr std::memory_order kMemoryOrderRelaxed = std::memory_order_relaxed;

  static constexpr size_t kInterferenceSize = 64U;
  static constexpr size_t kFirstSpinTimes = 32U;

  [[nodiscard]] constexpr size_t idx(size_t i) const noexcept { return i % capacity_; }

  [[nodiscard]] constexpr size_t turn(size_t i) const noexcept { return i / capacity_; }

  struct Chunk {
    ~Chunk() noexcept {
      if ((turn.load(kMemoryOrderAcquire) & 1U) != 0U) {
        destroy();
      }
    }

    template <typename... Args>
    void construct(Args&&... args) noexcept {
      new (storage.data()) T(std::forward<Args>(args)...);
    }

    void destroy() noexcept { reinterpret_cast<T*>(storage.data())->~T(); }

    [[nodiscard]] T&& move() noexcept { return std::move(*reinterpret_cast<T*>(storage.data())); }

    alignas(kInterferenceSize) std::atomic<size_t> turn{0U};
    alignas(alignof(T)) std::array<uint8_t, sizeof(T)> storage;
  };

  alignas(kInterferenceSize) std::atomic<size_t> head_{0U};

  Chunk* chunk_{nullptr};
  size_t capacity_{0};

  mutable std::mutex cv_mtx_;

  alignas(kInterferenceSize) std::atomic<size_t> tail_{0U};

  ConditionVariable cv_not_empty_;
  ConditionVariable cv_not_full_;

#if defined(__has_cpp_attribute) && __has_cpp_attribute(no_unique_address)
  AlignedAllocator<Chunk> allocator_ [[no_unique_address]];
#else
  AlignedAllocator<Chunk> allocator_;
#endif

  struct alignas(kInterferenceSize) QuitFlag {
    std::atomic_bool value{false};
    char padding[kInterferenceSize - sizeof(std::atomic_bool)];
  } quit_flag_;

  VLINK_DISALLOW_COPY_AND_ASSIGN(MpmcQueue)
};

////////////////////////////////////////////////////////////////
/// Details
////////////////////////////////////////////////////////////////

template <typename T>
inline MpmcQueue<T>::MpmcQueue(size_t capacity) : capacity_(capacity) {
  static_assert(alignof(Chunk) == kInterferenceSize,
                "Slot must be aligned to cache line boundary to prevent false sharing");
  static_assert(sizeof(Chunk) % kInterferenceSize == 0,
                "Slot size must be a multiple of cache line size to prevent "
                "false sharing between adjacent slots");
  static_assert(sizeof(MpmcQueue) % kInterferenceSize == 0,
                "Queue size must be a multiple of cache line size to "
                "prevent false sharing between adjacent queues");

  if VUNLIKELY (capacity_ < 1U) {
    throw std::invalid_argument("capacity < 1U");
  }

  chunk_ = allocator_.allocate(capacity_ + 1);

  if VUNLIKELY (reinterpret_cast<size_t>(chunk_) % alignof(Chunk) != 0U) {
    allocator_.deallocate(chunk_, capacity_ + 1);
    throw std::bad_alloc();
  }

  for (size_t i = 0U; i < capacity_; ++i) {
    new (&chunk_[i]) Chunk();
  }
}

template <typename T>
inline MpmcQueue<T>::~MpmcQueue() noexcept {
  for (size_t i = 0U; i < capacity_; ++i) {
    chunk_[i].~Chunk();
  }

  allocator_.deallocate(chunk_, capacity_ + 1);
}

template <typename T>
template <typename MpmcQueue<T>::Behavior BehaviorT, typename... Args>
inline void MpmcQueue<T>::emplace(Args&&... args) noexcept {
  if VUNLIKELY (quit_flag_.value.load(kMemoryOrderAcquire)) {
    return;
  }

  if constexpr (BehaviorT == kConditionBehavior) {
    wait_not_full(std::chrono::milliseconds(0));
    if VUNLIKELY (quit_flag_.value.load(kMemoryOrderAcquire)) {
      return;
    }
  }

  const auto head = head_.fetch_add(1U);
  auto& chunk = chunk_[idx(head)];

  size_t spin = 0;

  while (turn(head) * 2U != chunk.turn.load(kMemoryOrderAcquire)) {
    if VUNLIKELY (quit_flag_.value.load(kMemoryOrderAcquire)) {
      return;
    }

    if (++spin < kFirstSpinTimes) {
    } else {
      Utils::yield_cpu();
    }
  }

  chunk.construct(std::forward<Args>(args)...);
  chunk.turn.store((turn(head) * 2U) + 1U, kMemoryOrderRelease);

  if constexpr (BehaviorT == kConditionBehavior) {
    std::lock_guard lock(cv_mtx_);
    cv_not_empty_.notify_one();
  }
}

template <typename T>
template <typename MpmcQueue<T>::Behavior BehaviorT, typename... Args>
inline bool MpmcQueue<T>::try_emplace(Args&&... args) noexcept {
  if VUNLIKELY (quit_flag_.value.load(kMemoryOrderAcquire)) {
    return false;
  }

  auto head = head_.load(kMemoryOrderAcquire);

  for (;;) {
    auto& chunk = chunk_[idx(head)];

    if VLIKELY (turn(head) * 2U == chunk.turn.load(kMemoryOrderAcquire)) {
      if (head_.compare_exchange_strong(head, head + 1U)) {
        chunk.construct(std::forward<Args>(args)...);
        chunk.turn.store((turn(head) * 2U) + 1U, kMemoryOrderRelease);

        if constexpr (BehaviorT == kConditionBehavior) {
          std::lock_guard lock(cv_mtx_);
          cv_not_empty_.notify_one();
        }

        return true;
      }
    } else {
      const auto prev_head = head;
      head = head_.load(kMemoryOrderAcquire);
      if VUNLIKELY (head == prev_head) {
        return false;
      }
    }
  }
}

template <typename T>
template <typename MpmcQueue<T>::Behavior BehaviorT, typename P>
inline void MpmcQueue<T>::push(P&& v) noexcept {
  emplace<BehaviorT>(std::forward<P>(v));
}

template <typename T>
template <typename MpmcQueue<T>::Behavior BehaviorT, typename P>
inline bool MpmcQueue<T>::try_push(P&& v) noexcept {
  return try_emplace<BehaviorT>(std::forward<P>(v));
}

template <typename T>
template <typename MpmcQueue<T>::Behavior BehaviorT>
inline void MpmcQueue<T>::pop(T& v) noexcept {
  auto const tail = tail_.fetch_add(1U);
  auto& chunk = chunk_[idx(tail)];

  size_t spin = 0;

  while (turn(tail) * 2U + 1U != chunk.turn.load(kMemoryOrderAcquire)) {
    if VUNLIKELY (quit_flag_.value.load(kMemoryOrderAcquire)) {
      return;
    }

    if (++spin < kFirstSpinTimes) {
    } else {
      Utils::yield_cpu();
    }
  }

  v = chunk.move();
  chunk.destroy();
  chunk.turn.store((turn(tail) * 2U) + 2U, kMemoryOrderRelease);

  if constexpr (BehaviorT == kConditionBehavior) {
    std::lock_guard lock(cv_mtx_);
    cv_not_full_.notify_one();
  }
}

template <typename T>
template <typename MpmcQueue<T>::Behavior BehaviorT>
inline bool MpmcQueue<T>::try_pop(T& v) noexcept {
  if VUNLIKELY (quit_flag_.value.load(kMemoryOrderAcquire)) {
    return false;
  }

  auto tail = tail_.load(kMemoryOrderAcquire);

  // LCOV_EXCL_START
  // GCOVR_EXCL_START

  for (;;) {
    auto& chunk = chunk_[idx(tail)];

    if VLIKELY (turn(tail) * 2U + 1U == chunk.turn.load(kMemoryOrderAcquire)) {
      if (tail_.compare_exchange_strong(tail, tail + 1U)) {
        v = chunk.move();
        chunk.destroy();
        chunk.turn.store((turn(tail) * 2U) + 2U, kMemoryOrderRelease);

        if constexpr (BehaviorT == kConditionBehavior) {
          std::lock_guard lock(cv_mtx_);
          cv_not_full_.notify_one();
        }

        return true;
      }
    } else {
      const auto prev_tail = tail;
      tail = tail_.load(kMemoryOrderAcquire);

      if VUNLIKELY (tail == prev_tail) {
        return false;
      }
    }

    if VUNLIKELY (quit_flag_.value.load(kMemoryOrderAcquire)) {
      return false;
    }
  }

  // GCOVR_EXCL_STOP
  // LCOV_EXCL_STOP
}

template <typename T>
inline size_t MpmcQueue<T>::capacity() const noexcept {
  return capacity_;
}

template <typename T>
inline size_t MpmcQueue<T>::size(bool real) const noexcept {
  static auto safe_diff = [](size_t h, size_t t) -> size_t { return (h >= t) ? (h - t) : 0U; };

  if (real) {
    static constexpr size_t kMaxRetry = 50U;
    size_t retry_cnt = 0;

    size_t t = tail_.load(kMemoryOrderAcquire);
    size_t h = head_.load(kMemoryOrderAcquire);

    while (retry_cnt < kMaxRetry) {
      size_t t2 = tail_.load(kMemoryOrderAcquire);

      if (t == t2) {
        return safe_diff(h, t);
      }

      t = t2;
      h = head_.load(kMemoryOrderAcquire);
      retry_cnt++;

      Utils::yield_cpu();
    }

    return safe_diff(h, t);
  } else {
    auto h = head_.load(kMemoryOrderAcquire);
    auto t = tail_.load(kMemoryOrderAcquire);

    return safe_diff(h, t);
  }
}

template <typename T>
inline bool MpmcQueue<T>::empty(bool real) const noexcept {
  return size(real) == 0;
}

template <typename T>
bool MpmcQueue<T>::is_full(bool real) const noexcept {
  return size(real) >= capacity_;
}

template <typename T>
inline bool MpmcQueue<T>::wait_not_empty(std::chrono::milliseconds timeout) noexcept {
  if (!empty(true)) {
    return true;
  }

  std::unique_lock lock(cv_mtx_);

  bool ret = true;

  if (timeout == std::chrono::milliseconds(0)) {
    cv_not_empty_.wait(lock, [this]() { return !empty(true) || quit_flag_.value.load(kMemoryOrderAcquire); });
  } else {
    ret = cv_not_empty_.wait_for(lock, timeout,
                                 [this]() { return !empty(true) || quit_flag_.value.load(kMemoryOrderAcquire); });
  }

  if VUNLIKELY (quit_flag_.value.load(kMemoryOrderAcquire)) {
    return false;
  }

  return ret;
}

template <typename T>
inline bool MpmcQueue<T>::wait_not_full(std::chrono::milliseconds timeout) noexcept {
  if (!is_full(true)) {
    return true;
  }

  std::unique_lock lock(cv_mtx_);

  bool ret = true;

  if (timeout == std::chrono::milliseconds(0)) {
    cv_not_full_.wait(lock, [this]() { return !is_full(true) || quit_flag_.value.load(kMemoryOrderAcquire); });
  } else {
    ret = cv_not_full_.wait_for(lock, timeout,
                                [this]() { return !is_full(true) || quit_flag_.value.load(kMemoryOrderAcquire); });
  }

  if VUNLIKELY (quit_flag_.value.load(kMemoryOrderAcquire)) {
    return false;
  }

  return ret;
}

template <typename T>
inline void MpmcQueue<T>::notify_to_quit() noexcept {
  std::lock_guard lock(cv_mtx_);

  quit_flag_.value.store(true, kMemoryOrderRelease);

  cv_not_empty_.notify_all();
  cv_not_full_.notify_all();
}

}  // namespace vlink
