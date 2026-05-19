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
 * @file deadline_timer.h
 * @brief An absolute-deadline timer for lightweight, lock-free timeout tracking.
 *
 * @details
 * @c DeadlineTimer stores an absolute expiry timestamp (rather than a countdown
 * duration) in an atomic 64-bit word, making it safe to read from multiple threads
 * without a mutex.  It is used inside VLink connection and request handling to
 * detect expired operations.
 *
 * The deadline can be set in two ways:
 * - @c set_deadline(interval)  -- sets the deadline @p interval time units from now.
 * - @c set_deadline_abs(abs)   -- sets an explicit absolute timestamp.
 *
 * The same @c Accuracy enum from @c ElapsedTimer is reused to select
 * millisecond, microsecond, or nanosecond granularity.
 *
 * @note
 * - A default-constructed @c DeadlineTimer has @c deadline() == 0 and
 *   @c is_valid() == false.  Always call @c set_deadline() before
 *   checking @c has_expired().
 * - @c remaining_time() returns 0 once the deadline has already passed, or when
 *   no deadline is set.
 * - The internal deadline is stored as a 64-bit atomic, so concurrent reads are
 *   safe.  Concurrent writes from multiple threads are technically racy at the
 *   application level, but the store itself is atomic.
 * - The timer is aligned to 64 bytes to prevent false sharing in structures
 *   that embed multiple timers.
 *
 * @par Example
 * @code
 * // Set a 200 ms deadline from now:
 * vlink::DeadlineTimer t(200);
 * while (!t.has_expired()) {
 *   process_events();
 * }
 * // Returns remaining ms (0 once expired):
 * int64_t left = t.remaining_time();
 *
 * // Set an absolute deadline:
 * uint64_t abs_ms = vlink::ElapsedTimer::get_cpu_timestamp();
 * t.set_deadline_abs(abs_ms + 500);
 * @endcode
 */

#pragma once

#include <atomic>
#include <cstdint>

#include "./elapsed_timer.h"
#include "./macros.h"

namespace vlink {

/**
 * @class DeadlineTimer
 * @brief Atomic absolute-deadline timer for lock-free timeout detection.
 *
 * @details
 * Stores an absolute expiry time (monotonic CPU timestamp) in a 64-bit atomic.
 * Concurrent @c has_expired() / @c remaining_time() reads from multiple threads
 * are safe without external locking.
 */
class VLINK_EXPORT DeadlineTimer final {
 public:
  /**
   * @brief Time precision.  Aliases @c ElapsedTimer::Accuracy for convenience.
   */
  using Accuracy = ElapsedTimer::Accuracy;

  /**
   * @brief Default constructor.  Creates an invalid (unset) deadline.
   *
   * @details
   * @c is_valid() returns @c false and @c has_expired() returns @c false
   * until @c set_deadline() or @c set_deadline_abs() is called.
   */
  DeadlineTimer() noexcept;

  /**
   * @brief Constructs a @c DeadlineTimer that expires @p interval time units from now.
   *
   * @details
   * The absolute deadline is computed as the current monotonic timestamp plus
   * @p interval. If @p interval <= 0 the timer is reset to the invalid state
   * (equivalent to @c reset()).
   *
   * @param interval  Duration until expiry, in @p accuracy units.
   * @param accuracy  Time precision (default: @c ElapsedTimer::kMilli).
   */
  explicit DeadlineTimer(int64_t interval, Accuracy accuracy = ElapsedTimer::kMilli) noexcept;

  /**
   * @brief Copy constructor.
   *
   * @param other  The source timer.
   */
  DeadlineTimer(const DeadlineTimer& other) noexcept;

  /**
   * @brief Move constructor.
   *
   * @param other  The source timer (moved from).
   */
  DeadlineTimer(DeadlineTimer&& other) noexcept;

  /**
   * @brief Destructor.
   */
  ~DeadlineTimer() noexcept;

  /**
   * @brief Copy-assignment operator.
   *
   * @param other  The source timer.
   * @return A reference to @c *this.
   */
  DeadlineTimer& operator=(const DeadlineTimer& other) noexcept;

  /**
   * @brief Move-assignment operator.
   *
   * @param other  The source timer.
   * @return A reference to @c *this.
   */
  DeadlineTimer& operator=(DeadlineTimer&& other) noexcept;

  /**
   * @brief Sets the deadline to @p interval time units from now.
   *
   * @details
   * Reads the current monotonic timestamp and adds @p interval to produce the
   * absolute deadline. A value of 0 or less clears the timer and makes it
   * invalid.
   *
   * @param interval  Duration until expiry in the configured accuracy units.
   */
  void set_deadline(int64_t interval) noexcept;

  /**
   * @brief Sets an explicit absolute deadline timestamp.
   *
   * @details
   * The value must be in the same unit and time base as the @c Accuracy
   * configured for this timer. Typically obtain it via
   * @c ElapsedTimer::get_cpu_timestamp(accuracy).
   *
   * @param abs_deadline  Absolute monotonic timestamp of the deadline.
   */
  void set_deadline_abs(uint64_t abs_deadline) noexcept;

  /**
   * @brief Resets the deadline to 0, making the timer invalid again.
   *
   * @details
   * After @c reset(), @c is_valid() returns @c false.
   */
  void reset() noexcept;

  /**
   * @brief Returns the stored absolute deadline timestamp.
   *
   * @details
   * A value of 0 means the timer has not been set (see @c is_valid()).
   *
   * @return Absolute deadline in the configured accuracy units.
   */
  [[nodiscard]] uint64_t deadline() const noexcept;

  /**
   * @brief Returns the time remaining until the deadline.
   *
   * @details
   * Computed as @c (deadline - current_cpu_timestamp). Returns 0 when the
   * timer is invalid or already expired.
   *
   * @return Remaining time in the configured accuracy units.
   *         A return value of 0 means invalid or expired.
   */
  [[nodiscard]] int64_t remaining_time() const noexcept;

  /**
   * @brief Returns @c true if the current time is past the stored deadline.
   *
   * @details
   * Equivalent to @c remaining_time() <= 0 when @c is_valid() is @c true.
   * Always returns @c false for an unset (invalid) timer.
   *
   * @return @c true if expired, @c false otherwise.
   */
  [[nodiscard]] bool has_expired() const noexcept;

  /**
   * @brief Returns @c true if the deadline has been explicitly set (non-zero).
   *
   * @return @c true if the timer is valid (deadline != 0).
   */
  [[nodiscard]] bool is_valid() const noexcept;

  /**
   * @brief Returns the time precision configured for this timer.
   *
   * @return The @c Accuracy value set at construction.
   */
  [[nodiscard]] Accuracy get_accuracy() const noexcept;

 private:
  alignas(64) std::atomic<uint64_t> deadline_{0};
  Accuracy accuracy_{ElapsedTimer::kMilli};
};

}  // namespace vlink
