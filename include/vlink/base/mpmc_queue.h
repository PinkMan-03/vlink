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
 * @class MpmcQueueBase
 * @brief Non-template base of @c MpmcQueue: owns all element-type-independent state and
 *        provides capacity / size / wait / quit operations.
 *
 * @details
 * Holds the cache-line-aligned @c head_ / @c tail_ cursors, the condition-variable
 * pair used by @c kConditionBehavior, the quit flag, the shared capacity, and a raw
 * @c void* slot for the @c Chunk array storage.  The element type @c T -- the
 * @c Chunk struct definition, the per-slot turn counter, and the @c emplace / @c pop
 * value-moving operations -- lives in @c MpmcQueue<T>, which accesses the slots
 * by typed cast of the @c chunk_storage_ pointer.
 *
 * Field order and alignment exactly mirror the historical single-class layout so that
 * @c sizeof(MpmcQueue<T>) and the offsets of @c head_, @c chunk_, @c tail_, and
 * @c quit_flag_ are preserved byte-for-byte.
 */
class VLINK_EXPORT MpmcQueueBase {
 public:
  /**
   * @brief Controls whether condition-variable notifications are sent on push/pop.
   *
   * @details
   * Selected via the @c BehaviorT template argument to @c emplace / @c push / @c pop /
   * @c try_*.  Use @c kConditionBehavior when consumers / producers may block on
   * @c wait_not_empty() / @c wait_not_full(); otherwise the cv_not_*_ notify calls
   * are pure overhead and @c kNoBehavior is preferred.
   *
   * Effects:
   * - @c kNoBehavior: push and pop perform no condition-variable notification.
   * - @c kConditionBehavior: push acquires @c cv_mtx_ and notifies @c cv_not_empty_; pop acquires
   *   @c cv_mtx_ and notifies @c cv_not_full_.
   */
  enum Behavior : uint8_t { kNoBehavior = 0, kConditionBehavior = 1 };

  /**
   * @brief Returns the fixed capacity of the queue.
   *
   * @details
   * The capacity is set at construction time and never changes.  Safe to call
   * without holding any lock; it reads the immutable @c capacity_ field.
   *
   * @return Number of slots reserved at construction (>= 1).
   */
  [[nodiscard]] size_t capacity() const noexcept VLINK_NO_INSTRUMENT;

  /**
   * @brief Returns an approximation of the current number of elements.
   *
   * @details
   * Computes @c head_ - @c tail_ using acquire loads of both cursors.  Because
   * each cursor is updated atomically by a different set of threads (head_ by
   * producers, tail_ by consumers), the two snapshots may not be coherent, so
   * the returned value is generally a near-current approximation.
   *
   * When @p real is @c true, this function retries up to 50 times (yielding the
   * CPU between attempts) until two consecutive @c tail_ reads match, giving a
   * more stable snapshot at the cost of additional latency.  When @p real is
   * @c false (the default), a single non-retrying read pair is used -- cheaper
   * but more racy.
   *
   * In both modes the result is clamped at zero (@c head_ < @c tail_ is briefly
   * observable under concurrent updates and is reported as 0 rather than a
   * huge unsigned wraparound).
   *
   * @param real  If @c true, retries up to 50 times for stability.  Default: @c false.
   * @return Approximate number of enqueued elements.
   */
  [[nodiscard]] size_t size(bool real = false) const noexcept VLINK_NO_INSTRUMENT;

  /**
   * @brief Returns @c true if the queue appears to be empty.
   *
   * @details
   * Defined as @c size(real) == 0.  Subject to the same coherence caveats as
   * @c size().  Useful for fast polling loops with @c kNoBehavior; for
   * blocking semantics prefer @c wait_not_empty().
   *
   * @param real  If @c true, uses the retrying @c size() variant.  Default: @c false.
   * @return @c true if no elements are currently enqueued (approximately).
   */
  [[nodiscard]] bool empty(bool real = false) const noexcept VLINK_NO_INSTRUMENT;

  /**
   * @brief Returns @c true if the queue appears to be full.
   *
   * @details
   * Defined as @c size(real) >= @c capacity().  Subject to the same coherence
   * caveats as @c size().  For blocking semantics on the producer side prefer
   * @c wait_not_full().
   *
   * @param real  If @c true, uses the retrying @c size() variant.  Default: @c false.
   * @return @c true if the number of enqueued elements is at least @c capacity().
   */
  [[nodiscard]] bool is_full(bool real = false) const noexcept VLINK_NO_INSTRUMENT;

  /**
   * @brief Blocks until the queue is not empty (or a timeout elapses).
   *
   * @details
   * Fast path: if @c empty(true) is @c false, returns @c true immediately.
   * Slow path: acquires @c cv_mtx_ and waits on @c cv_not_empty_, woken by
   * producers that push under @c kConditionBehavior or by @c notify_to_quit().
   *
   * If @p timeout is @c std::chrono::milliseconds(0) the wait is unbounded;
   * any positive value imposes a maximum wait.  Returns @c false when the
   * timeout expires without the queue becoming non-empty, or when
   * @c notify_to_quit() has been called.
   *
   * @param timeout  Maximum wait duration; 0 means wait forever.  Default: 0.
   * @return @c true if the queue became non-empty, @c false on timeout or quit.
   */
  bool wait_not_empty(std::chrono::milliseconds timeout = std::chrono::milliseconds(0)) noexcept VLINK_NO_INSTRUMENT;

  /**
   * @brief Blocks until the queue has space (or a timeout elapses).
   *
   * @details
   * Mirror of @c wait_not_empty() for producers: fast-returns @c true if
   * @c is_full(true) is @c false, otherwise waits on @c cv_not_full_ until a
   * consumer pops under @c kConditionBehavior or @c notify_to_quit() is
   * called.
   *
   * @param timeout  Maximum wait duration; 0 means wait forever.  Default: 0.
   * @return @c true if space became available, @c false on timeout or quit.
   */
  bool wait_not_full(std::chrono::milliseconds timeout = std::chrono::milliseconds(0)) noexcept VLINK_NO_INSTRUMENT;

  /**
   * @brief Signals all blocked @c wait_not_empty() / @c wait_not_full() callers to exit.
   *
   * @details
   * Sets the internal quit flag (release order) and broadcasts both condition
   * variables.  After this call:
   * - All currently blocked @c wait_not_empty / @c wait_not_full calls return @c false.
   * - All subsequent @c emplace / @c push calls are silently dropped (no slot is claimed).
   * - @c try_pop returns @c false without modifying its output.  @c pop returns
   *   without modifying its output if it observes the quit flag while waiting for
   *   a claimed empty slot; if a claimed slot is already full, it may still consume it.
   * - @c try_emplace / @c try_push return @c false.
   *
   * Intended for graceful shutdown of producer / consumer loops.  Safe to call
   * multiple times; subsequent calls are no-ops aside from re-broadcasting.
   */
  void notify_to_quit() noexcept VLINK_NO_INSTRUMENT;

 protected:
  static constexpr std::memory_order kMemoryOrderAcquire = std::memory_order_acquire;
  static constexpr std::memory_order kMemoryOrderRelease = std::memory_order_release;
  static constexpr std::memory_order kMemoryOrderRelaxed = std::memory_order_relaxed;

  static constexpr size_t kInterferenceSize = 64U;
  static constexpr size_t kFirstSpinTimes = 32U;

  explicit MpmcQueueBase(size_t capacity);

  ~MpmcQueueBase() noexcept = default;

  [[noreturn]] static void throw_mpmc_invalid_capacity();

  [[noreturn]] static void throw_mpmc_alignment_failure();

  [[nodiscard]] constexpr size_t idx(size_t i) const noexcept { return i % capacity_; }

  [[nodiscard]] constexpr size_t turn(size_t i) const noexcept { return i / capacity_; }

  alignas(kInterferenceSize) std::atomic<size_t> head_{0U};

  void* chunk_storage_{nullptr};
  size_t capacity_{0};

  mutable std::mutex cv_mtx_;

  alignas(kInterferenceSize) std::atomic<size_t> tail_{0U};

  ConditionVariable cv_not_empty_;
  ConditionVariable cv_not_full_;

  struct alignas(kInterferenceSize) QuitFlag {
    std::atomic_bool value{false};
    char padding[kInterferenceSize - sizeof(std::atomic_bool)];
  } quit_flag_;
};

/**
 * @class MpmcQueue
 * @brief Fixed-capacity, lock-free, cache-line-aligned MPMC ring buffer.
 *
 * @tparam T  Element type.  Must be moveable.
 */
template <typename T>
class MpmcQueue : public MpmcQueueBase {
 public:
  /**
   * @brief Constructs a @c MpmcQueue with the given fixed capacity.
   *
   * @details
   * Allocates @c capacity + 1 cache-line-aligned @c Chunk slots from the
   * platform's aligned allocator (@c std::allocator<Chunk> when @c __cpp_aligned_new
   * is available, otherwise a @c posix_memalign / @c _aligned_malloc fallback).
   * The extra trailing slot acts as a guard so producers / consumers can
   * compute @c idx() without wrapping mid-call.
   *
   * Validates that the allocated pointer is itself aligned to
   * @c kInterferenceSize (64 bytes); on mis-alignment the allocation is
   * released and @c std::bad_alloc is thrown.  Each chunk's turn counter is
   * default-initialised to 0.
   *
   * @param capacity  Maximum number of elements.  Must be >= 1.
   * @throws std::invalid_argument if @p capacity < 1.
   * @throws std::bad_alloc        if the chunk array allocation failed or returned a misaligned pointer.
   */
  explicit MpmcQueue(size_t capacity) VLINK_NO_INSTRUMENT;

  /**
   * @brief Destructor.  Destroys any elements still in the queue.
   *
   * @details
   * Walks the chunk array and calls @c Chunk::~Chunk() on each entry; the
   * @c Chunk destructor in turn destroys its stored @p T iff the slot's turn
   * counter has its low bit set (i.e. the slot is "full").  Then the chunk
   * array is returned to the allocator.
   *
   * Not thread-safe with respect to concurrent producers / consumers; the
   * caller must ensure no other thread is using the queue at destruction.
   */
  ~MpmcQueue() noexcept VLINK_NO_INSTRUMENT;

  /**
   * @brief In-place constructs an element and blocks until a slot is available.
   *
   * @details
   * Claims the next slot by atomically incrementing @c head_, then spins on
   * the slot's turn counter until it reaches @c turn(head) * 2 (i.e. the
   * previous consumer of this index has finished).  Spinning runs for up to
   * @c kFirstSpinTimes (32) iterations before yielding the CPU between
   * checks.
   *
   * Once the slot is ready, constructs @c T in place via @c chunk.construct(args...)
   * and bumps the turn counter to @c turn(head) * 2 + 1 (slot full).
   *
   * If @c BehaviorT == @c kConditionBehavior, additionally:
   * - Before claiming the slot, calls @c wait_not_full(0) so producers block
   *   until a consumer makes space rather than busy-spinning on full slots.
   * - After publishing the element, takes @c cv_mtx_ and notifies one
   *   @c wait_not_empty waiter.
   *
   * Silently returns (dropping the would-be element and leaving the claimed
   * slot in its previous state) if @c notify_to_quit() has been called,
   * either before the claim or during the spin.
   *
   * @tparam BehaviorT  Notification behaviour.  Default: @c kNoBehavior.
   * @tparam Args       Argument types forwarded to @c T's constructor.
   * @param args        Arguments forwarded to construct the @c T instance.
   */
  template <Behavior BehaviorT = kNoBehavior, typename... Args>
  void emplace(Args&&... args) noexcept VLINK_NO_INSTRUMENT;

  /**
   * @brief In-place constructs an element without blocking.
   *
   * @details
   * Reads @c head_ (acquire) and inspects the candidate slot's turn counter.
   * If the slot is ready (turn == @c turn(head) * 2) attempts to commit the
   * claim via @c head_.compare_exchange_strong; on success constructs the
   * element and publishes (turn = @c turn(head) * 2 + 1).  If the CAS fails,
   * re-reads @c head_ and retries; if @c head_ has not advanced and the slot
   * is not ready, the queue is full and @c false is returned.
   *
   * If @c BehaviorT == @c kConditionBehavior, on successful publish takes
   * @c cv_mtx_ and notifies one @c wait_not_empty waiter.
   *
   * Returns @c false immediately if @c notify_to_quit() has been called.
   *
   * @tparam BehaviorT  Notification behaviour.  Default: @c kNoBehavior.
   * @tparam Args       Argument types forwarded to @c T's constructor.
   * @param args        Arguments forwarded to construct the @c T instance.
   * @return @c true if the element was enqueued; @c false if the queue was full
   *         or @c notify_to_quit() was called.
   */
  template <Behavior BehaviorT = kNoBehavior, typename... Args>
  [[nodiscard]] bool try_emplace(Args&&... args) noexcept VLINK_NO_INSTRUMENT;

  /**
   * @brief Pushes a value (by perfect forwarding) and blocks until a slot is available.
   *
   * @details
   * Convenience wrapper that forwards to @c emplace<BehaviorT>(std::forward<P>(v)).
   * Behaviour, blocking semantics, and quit handling are identical to @c emplace.
   *
   * @tparam BehaviorT  Notification behaviour.  Default: @c kNoBehavior.
   * @tparam P          Value type (lvalue or rvalue reference).
   * @param v           Value to push; perfect-forwarded to @c T's constructor.
   */
  template <Behavior BehaviorT = kNoBehavior, typename P>
  void push(P&& v) noexcept VLINK_NO_INSTRUMENT;

  /**
   * @brief Pushes a value without blocking; returns @c false if the queue is full.
   *
   * @details
   * Convenience wrapper that forwards to @c try_emplace<BehaviorT>(std::forward<P>(v)).
   *
   * @tparam BehaviorT  Notification behaviour.  Default: @c kNoBehavior.
   * @tparam P          Value type (lvalue or rvalue reference).
   * @param v           Value to push; perfect-forwarded to @c T's constructor.
   * @return @c true if pushed; @c false if the queue was full or quit was signalled.
   */
  template <Behavior BehaviorT = kNoBehavior, typename P>
  [[nodiscard]] bool try_push(P&& v) noexcept VLINK_NO_INSTRUMENT;

  /**
   * @brief Pops a value by move and blocks until an element is available.
   *
   * @details
   * Mirror of @c emplace: claims the next slot by incrementing @c tail_, spins
   * until the slot's turn counter reaches @c turn(tail) * 2 + 1 (slot full),
   * then moves the stored @c T into @p v, calls the chunk's @c destroy() to
   * destroy the slot's @c T, and bumps the turn counter to @c turn(tail) * 2 + 2
   * (slot empty, ready for the next round).
   *
   * If @c BehaviorT == @c kConditionBehavior, after consuming, takes
   * @c cv_mtx_ and notifies one @c wait_not_full waiter.
   *
   * If @c notify_to_quit() has been called during the spin, returns without
   * modifying @p v; the claimed slot is left for cleanup by the queue's
   * destructor.
   *
   * @tparam BehaviorT  Notification behaviour.  Default: @c kNoBehavior.
   * @param v           Output reference assigned via @c std::move from the slot.
   */
  template <Behavior BehaviorT = kNoBehavior>
  void pop(T& v) noexcept VLINK_NO_INSTRUMENT;

  /**
   * @brief Pops a value without blocking; returns @c false if the queue is empty.
   *
   * @details
   * Reads @c tail_ (acquire) and inspects the candidate slot's turn counter.
   * If the slot is full attempts to commit the claim via CAS; on success moves
   * the @c T into @p v, destroys the slot's contents, and advances the turn
   * counter.  If the CAS fails, re-reads @c tail_ and retries; if @c tail_
   * has not advanced and the slot is not full, the queue is empty and
   * @c false is returned.
   *
   * Returns @c false immediately if @c notify_to_quit() has been called.
   *
   * @tparam BehaviorT  Notification behaviour.  Default: @c kNoBehavior.
   * @param v           Output reference assigned via @c std::move on success;
   *                    untouched on failure.
   * @return @c true if an element was popped; @c false if empty or quit signalled.
   */
  template <Behavior BehaviorT = kNoBehavior>
  [[nodiscard]] bool try_pop(T& v) noexcept VLINK_NO_INSTRUMENT;

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

      if VUNLIKELY (p == nullptr) {
        throw std::bad_alloc();
      }
#else
      ChunkT* p;

      if VUNLIKELY (posix_memalign(reinterpret_cast<void**>(&p), alignof(ChunkT), sizeof(ChunkT) * n) != 0) {
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

    void destroy() noexcept { std::launder(reinterpret_cast<T*>(storage.data()))->~T(); }

    [[nodiscard]] T&& move() noexcept { return std::move(*std::launder(reinterpret_cast<T*>(storage.data()))); }

    alignas(kInterferenceSize) std::atomic<size_t> turn{0U};
    alignas(alignof(T)) std::array<uint8_t, sizeof(T)> storage;
  };

  [[nodiscard]] Chunk* chunks() noexcept { return static_cast<Chunk*>(chunk_storage_); }

  [[nodiscard]] const Chunk* chunks() const noexcept { return static_cast<const Chunk*>(chunk_storage_); }

  VLINK_DISALLOW_COPY_AND_ASSIGN(MpmcQueue)
};

////////////////////////////////////////////////////////////////
/// Details
////////////////////////////////////////////////////////////////

template <typename T>
inline MpmcQueue<T>::MpmcQueue(size_t capacity) : MpmcQueueBase(capacity) {
  static_assert(alignof(Chunk) == kInterferenceSize,
                "Slot must be aligned to cache line boundary to prevent false sharing");
  static_assert(sizeof(Chunk) % kInterferenceSize == 0,
                "Slot size must be a multiple of cache line size to prevent "
                "false sharing between adjacent slots");
  static_assert(sizeof(MpmcQueue) % kInterferenceSize == 0,
                "Queue size must be a multiple of cache line size to "
                "prevent false sharing between adjacent queues");

  AlignedAllocator<Chunk> allocator;
  Chunk* raw = allocator.allocate(capacity_ + 1);

  if VUNLIKELY (reinterpret_cast<size_t>(raw) % alignof(Chunk) != 0U) {
    allocator.deallocate(raw, capacity_ + 1);
    throw_mpmc_alignment_failure();
  }

  chunk_storage_ = raw;

  for (size_t i = 0U; i < capacity_; ++i) {
    new (&raw[i]) Chunk();
  }
}

template <typename T>
inline MpmcQueue<T>::~MpmcQueue() noexcept {
  Chunk* raw = chunks();

  for (size_t i = 0U; i < capacity_; ++i) {
    raw[i].~Chunk();
  }

  AlignedAllocator<Chunk> allocator;
  allocator.deallocate(raw, capacity_ + 1);
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
  auto& chunk = chunks()[idx(head)];

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
    auto& chunk = chunks()[idx(head)];

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
  auto& chunk = chunks()[idx(tail)];

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
    auto& chunk = chunks()[idx(tail)];

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

}  // namespace vlink
