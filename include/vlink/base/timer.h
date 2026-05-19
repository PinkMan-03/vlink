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
 * @file timer.h
 * @brief Event-loop-driven periodic/one-shot timer with configurable priority.
 *
 * @details
 * @c Timer integrates with a @c MessageLoop to deliver callbacks on its thread.
 * Unlike platform timers, the callback runs serially with all other tasks posted to
 * the same loop, so no extra synchronisation is needed inside the callback.
 *
 * Key features:
 * - Repeating or one-shot mode (@c kInfinite for loop_count means repeat forever).
 * - Optional strict mode: if the loop is busy and the next tick is missed, strict mode
 *   fires the missed callbacks immediately to maintain the schedule.
 * - Priority support for @c kPriorityType message loops.
 * - When @c interval_ms is zero, the internal tick interval falls back to @c kMinInterval (10000 ns = 10 us)
 *   to prevent busy-wait overhead from a zero interval.
 * - A detached @c Timer does not fire until attached to a loop via @c attach().
 *
 * Lifecycle:
 * -# Construct a @c Timer (with or without a @c MessageLoop).
 * -# Optionally call @c attach() to bind to a loop.
 * -# Call @c start() to arm the timer.
 * -# Call @c stop() to disarm; @c restart() to reset the countdown.
 * -# Destruction automatically calls @c stop() and @c detach().
 *
 * @note
 * - When its @c MessageLoop is destroyed, a timer is detached/cleared so it
 *   can no longer fire on that loop; this does not call @c stop() on the timer.
 * - @c call_once() is a convenience factory for fire-and-forget one-shot timers.
 *
 * @par Example
 * @code
 * vlink::MessageLoop loop;
 * vlink::Timer timer(&loop, 500, vlink::Timer::kInfinite, []() {
 *   // called every 500 ms on the loop thread
 * });
 * timer.start();
 * loop.run();
 * @endcode
 */

#pragma once

#include <atomic>
#include <cstdint>
#include <memory>

#include "./functional.h"
#include "./macros.h"

namespace vlink {

/**
 * @class Timer
 * @brief Event-loop-driven repeating or one-shot timer.
 *
 * @details
 * Callbacks are dispatched on the thread of the associated @c MessageLoop.
 */
class VLINK_EXPORT Timer final {
 public:
  /**
   * @brief Callback type invoked on each timer tick.
   */
  using Callback = MoveFunction<void()>;

  /**
   * @brief Sentinel loop count meaning repeat indefinitely.
   */
  static constexpr int kInfinite{-1};

  /**
   * @brief Constructs a detached timer with the default interval and no callback.
   *
   * @details
   * Must be configured with @c attach() and @c set_callback() before @c start()
   * can fire.  The initial interval is 1000 ms.
   */
  explicit Timer();

  /**
   * @brief Constructs a timer attached to @p message_loop with the default interval and no callback.
   *
   * @param message_loop  The @c MessageLoop to deliver callbacks on.
   */
  explicit Timer(class MessageLoop* message_loop);

  /**
   * @brief Constructs and fully configures a timer attached to a loop.
   *
   * @param message_loop  The @c MessageLoop to deliver callbacks on.
   * @param interval_ms   Tick interval in milliseconds.  When zero, the internal tick interval falls back
   *                      to @c kMinInterval (10000 ns = 10 us).
   * @param loop_count    Number of times to fire (@c kInfinite for indefinite repeat).  Default: @c kInfinite.
   * @param callback      Callback to invoke on each tick.  Default: nullptr.
   */
  explicit Timer(class MessageLoop* message_loop, uint32_t interval_ms, int32_t loop_count = kInfinite,
                 Callback&& callback = nullptr);

  /**
   * @brief Constructs a timer without attaching it to a loop.
   *
   * @details
   * Call @c attach() before @c start().
   *
   * @param interval_ms  Tick interval in milliseconds.
   * @param loop_count   Number of times to fire.  Default: @c kInfinite.
   * @param callback     Callback to invoke on each tick.  Default: nullptr.
   */
  explicit Timer(uint32_t interval_ms, int32_t loop_count = kInfinite, Callback&& callback = nullptr);

  /**
   * @brief Destructor.  Stops the timer and detaches it from the loop.
   */
  ~Timer();

  /**
   * @brief Posts a one-shot timer to a @c MessageLoop without creating a @c Timer object.
   *
   * @details
   * The timer fires exactly once after @p interval_ms milliseconds and is then destroyed.
   * Useful for simple delayed tasks where lifetime management of a @c Timer object is inconvenient.
   *
   * @param message_loop  The @c MessageLoop to deliver the callback on.
   * @param interval_ms   Delay in milliseconds.
   * @param callback      Callback to invoke once.
   * @param priority      Priority of the timer task.  0 keeps the default timer
   *                      priority; positive values override it.
   * @return @c true if the timer was successfully posted.
   */
  static bool call_once(class MessageLoop* message_loop, uint32_t interval_ms, Callback&& callback,
                        uint16_t priority = 0);

  /**
   * @brief Returns @c true if the timer is currently running (armed and has remaining ticks).
   *
   * @return @c true if the timer is active.
   */
  [[nodiscard]] bool is_active() const;

  /**
   * @brief Returns @c true if strict mode is enabled.
   *
   * @details
   * In strict mode, missed ticks (due to a busy loop) are fired immediately on the
   * next loop iteration to compensate for schedule drift.
   *
   * @return @c true if strict mode is on.
   */
  [[nodiscard]] bool is_strict() const;

  /**
   * @brief Returns the current tick interval in milliseconds.
   *
   * @return Interval in milliseconds.
   */
  [[nodiscard]] uint32_t get_interval() const;

  /**
   * @brief Returns the configured total loop count.
   *
   * @return Loop count, or @c kInfinite (-1) for indefinite repeat.
   */
  [[nodiscard]] int32_t get_loop_count() const;

  /**
   * @brief Returns the number of remaining ticks before the timer stops automatically.
   *
   * @return Remaining tick count, or @c kInfinite if repeating indefinitely.
   */
  [[nodiscard]] int32_t get_remain_loop_count() const;

  /**
   * @brief Returns the number of timer ticks scheduled since the last start/restart.
   *
   * @return Processed tick count, reset by @c stop() and finite-timer completion.
   */
  [[nodiscard]] uint64_t get_invoke_count() const;

  /**
   * @brief Returns the task dispatch priority.
   *
   * @details
   * Relevant only when the timer is attached to a @c kPriorityType @c MessageLoop.
   *
   * @return Priority value (higher = dispatched sooner).
   */
  [[nodiscard]] uint16_t get_priority() const;

  /**
   * @brief Returns the @c MessageLoop this timer is attached to.
   *
   * @return Pointer to the associated loop, or @c nullptr if detached.
   */
  [[nodiscard]] class MessageLoop* get_message_loop() const;

  /**
   * @brief Attaches the timer to a @c MessageLoop.
   *
   * @details
   * Must be called before @c start() if the timer was constructed without a loop.
   * If the timer is already attached to a different loop it is detached first.
   *
   * @param message_loop  Loop to attach to.
   * @return @c true on success.
   */
  bool attach(class MessageLoop* message_loop);

  /**
   * @brief Stops the timer and detaches it from its @c MessageLoop.
   *
   * @details
   * After detaching the timer is inactive and can be re-attached to a different loop.
   * This explicit @c detach() path calls @c stop() before removing the timer.
   *
   * @return @c true on success.
   */
  bool detach();

  /**
   * @brief Arms and starts the timer.
   *
   * @details
   * If @p callback is provided it replaces the previously set callback.
   * The first tick fires after one full interval.
   *
   * @param callback  Optional replacement callback.  Default: nullptr (keep existing).
   */
  void start(Callback&& callback = nullptr);

  /**
   * @brief Resets the countdown to zero and continues firing.
   *
   * @details
   * Resets the remaining loop count to the configured @c loop_count, clears
   * @c get_invoke_count() to zero, and re-arms timing as if newly started.
   */
  void restart();

  /**
   * @brief Disarms the timer without destroying it.
   *
   * @details
   * After @c stop(), @c is_active() returns @c false.  Call @c start() to re-arm.
   */
  void stop();

  /**
   * @brief Enables or disables strict (catch-up) firing mode.
   *
   * @details
   * When @c true, if the loop was busy and one or more ticks were missed, they are
   * fired immediately on the next loop iteration.  Default is @c false.
   *
   * @param strict  @c true to enable catch-up firing.
   */
  void set_strict(bool strict);

  /**
   * @brief Changes the tick interval.
   *
   * @details
   * Takes effect immediately for an active timer: the processed tick count is
   * recalculated against the existing start time and the loop is woken.
   * When set to zero, the internal tick interval falls back to @c kMinInterval (10000 ns = 10 us).
   *
   * @param interval_ms  New interval in milliseconds.
   */
  void set_interval(uint32_t interval_ms);

  /**
   * @brief Changes the total number of ticks.
   *
   * @param loop_count  New loop count.  Use @c kInfinite for indefinite repeat.
   */
  void set_loop_count(int32_t loop_count);

  /**
   * @brief Replaces the callback invoked on each tick.
   *
   * @param callback  New callback.
   */
  void set_callback(Callback&& callback);

  /**
   * @brief Sets the dispatch priority for the timer task.
   *
   * @details
   * Only effective when the associated loop is of type @c kPriorityType.
   *
   * @param priority  Priority value (higher = earlier dispatch).
   */
  void set_priority(uint16_t priority);

 private:
  void run_callback();

  void begin_in_flight();

  void end_in_flight();

  void wait_for_idle();

  void clear();

  void force_to_start();

  void set_remain_loop_count(int32_t loop_count) const;

  void sub_remain_loop_count() const;

  void set_invoke_count(uint64_t invoke_count) const;

  uint64_t get_start_time() const;

  bool is_once_type() const;

  bool has_callback() const;

  Callback take_callback();

  std::shared_ptr<std::atomic_bool> get_alive_flag() const;

  friend class MessageLoop;
  struct Impl;
  std::unique_ptr<Impl> impl_;
  static constexpr uint32_t kMinInterval{10'000U};

  VLINK_DISALLOW_COPY_AND_ASSIGN(Timer)
};

}  // namespace vlink
