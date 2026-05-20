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
 * @file wheel_timer.h
 * @brief Hash-wheel timer for managing large numbers of concurrent timeouts efficiently.
 *
 * @details
 * @c WheelTimer implements a hashed timing wheel -- a classic data structure for O(1) timer
 * insertion, removal and expiry checking.  It is appropriate when hundreds to hundreds of
 * thousands of independent timeouts must be tracked simultaneously, such as in a session
 * manager or a connection-pool keep-alive system.
 *
 * Algorithm:
 * - The wheel contains @c slots evenly-spaced time buckets.
 * - Each tick advances the wheel by one slot; the advance period is @c interval_ms.
 * - Timeouts longer than @c slots * @c interval_ms are stored with a round counter and
 *   skipped until the counter reaches zero.
 * - Adding a timer is O(1).  Removal is O(1) via the internal key-to-slot index.
 * - Expiry detection per tick is O(k) where k is the number of handlers in the current slot.
 *
 * Lifecycle:
 * -# Construct with @c WheelTimer(slots, interval_ms).
 * -# Call @c start() to launch the background worker thread.
 * -# Add timers with @c add(); store the returned @c Key for later removal.
 * -# Call @c stop() to terminate the background thread.
 *
 * @note
 * - Expired callbacks are invoked from the internal worker thread.  Use thread-safe
 *   structures or post results to a @c MessageLoop inside the callback.
 * - @c set_catchup_limit() controls how many missed slots are processed in one tick
 *   to prevent a long stall from causing a burst of expired callbacks.
 *
 * @par Example
 * @code
 * // 256 slots, 10 ms per slot -> max 2.56 s without wrapping; higher durations use rounds.
 * vlink::WheelTimer wheel(256, 10);
 * wheel.start();
 *
 * auto key = wheel.add(1000, [](vlink::WheelTimer::Key k) {
 *   // fired after ~1000 ms
 * });
 *
 * // Repeating every 500 ms:
 * auto repeat_key = wheel.add(500, [](vlink::WheelTimer::Key k) {}, 500);
 *
 * wheel.remove(key);  // cancel before expiry
 * wheel.stop();
 * @endcode
 */

#pragma once

#include <cstdint>
#include <memory>

#include "./functional.h"
#include "./macros.h"

namespace vlink {

/**
 * @class WheelTimer
 * @brief O(1) hash-wheel timer backed by a fixed-size circular slot array.
 *
 * @details
 * Runs its own internal background thread to advance the wheel on each @c interval_ms tick.
 */
class VLINK_EXPORT WheelTimer {
 public:
  /**
   * @brief Opaque handle returned by @c add() and used to @c remove() a timer.
   */
  using Key = int64_t;

  /**
   * @brief Callback invoked when a timer expires.
   *
   * @details
   * The @c Key parameter allows a single lambda to manage multiple timers.
   */
  using Callback = Function<void(Key)>;

  /**
   * @brief Constructs the wheel timer.
   *
   * @details
   * Creates the internal slot array.  Both @p slots and @p interval_ms must be
   * greater than zero; invalid values log a fatal error and throw.  Call
   * @c start() to begin advancing the wheel.
   *
   * @param slots        Number of time buckets in the wheel.  Higher values reduce the
   *                     number of round counters needed for long timeouts.
   * @param interval_ms  Duration of one slot in milliseconds.  Resolution of all timers.
   */
  explicit WheelTimer(uint32_t slots, uint32_t interval_ms);

  /**
   * @brief Destructor.  Calls @c stop() if the wheel is still running.
   */
  ~WheelTimer();

  /**
   * @brief Starts the internal background thread and begins advancing the wheel.
   */
  void start();

  /**
   * @brief Stops the wheel and joins the background thread.
   *
   * @details
   * Pending timers are not fired after @c stop() returns.
   */
  void stop();

  /**
   * @brief Temporarily suspends the wheel without terminating the background thread.
   *
   * @details
   * Timers will not fire while paused.  The background thread remains alive.
   * Call @c resume() to continue normal operation.
   */
  void pause();

  /**
   * @brief Resumes a paused wheel.
   *
   * @details
   * If the wheel is not paused, this is a no-op.
   */
  void resume();

  /**
   * @brief Wakes the internal worker thread if it is sleeping between ticks.
   *
   * @details
   * Useful for triggering an immediate tick after adding a very short timeout.
   */
  void wakeup();

  /**
   * @brief Returns @c true if the wheel is currently running (started and not stopped).
   *
   * @return @c true if running.
   */
  [[nodiscard]] bool is_running() const;

  /**
   * @brief Adds a new timer to the wheel.
   *
   * @details
   * The callback will be invoked from the wheel's internal thread after @p timeout_ms
   * milliseconds.  If @p repeat_ms > 0, the timer is automatically re-added with the
   * @p repeat_ms interval each time it fires.
   *
   * @param timeout_ms  Initial delay in milliseconds (rounded up to the nearest slot).
   * @param callback    Function to call on expiry.  Receives the timer's @c Key as argument.
   * @param repeat_ms   Re-fire interval in milliseconds.  0 = one-shot.  Default: 0.
   * @return A unique @c Key identifying this timer entry, or -1 on invalid input
   *         or key allocation failure.
   */
  Key add(uint32_t timeout_ms, Callback&& callback, uint32_t repeat_ms = 0);

  /**
   * @brief Removes a timer before it fires.
   *
   * @param key  Key returned by @c add().
   * @return @c true if the timer was found and removed; @c false if already expired or invalid.
   */
  bool remove(Key key);

  /**
   * @brief Returns the estimated remaining time for a timer.
   *
   * @details
   * The returned value is approximate due to discretisation to slot boundaries.
   *
   * @param key  Key returned by @c add().
   * @return Estimated remaining time in milliseconds, or 0 if the key is not found.
   */
  [[nodiscard]] uint32_t get_remaining_time(Key key) const;

  /**
   * @brief Sets the maximum number of missed slots processed in a single tick.
   *
   * @details
   * If the wheel falls behind (e.g., due to system sleep), it may have many slots to
   * catch up.  This limit prevents a single tick from blocking for too long by capping
   * the number of catch-up slots processed per iteration.  Default is unlimited.
   *
   * @param max_slots_to_catch_up  Maximum slots to process per tick cycle.
   */
  void set_catchup_limit(uint32_t max_slots_to_catch_up);

 private:
  struct Impl;
  std::shared_ptr<Impl> impl_;

  VLINK_DISALLOW_COPY_AND_ASSIGN(WheelTimer)
};

}  // namespace vlink
