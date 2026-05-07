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
 * @file schedule.h
 * @brief RAII task scheduling wrapper with delay, priority, timeouts and result chaining.
 *
 * @details
 * @c Schedule is a utility namespace (implemented as a non-constructible struct) that wraps a
 * callable in a @c Config envelope and produces a @c Status or @c RetStatus RAII handle.
 * The handle lets callers register continuation callbacks in a fluent style:
 *
 * @code
 * loop.exec_task(vlink::Schedule::Config{0, 100},     // no delay, priority 100
 *                []() -> bool { return do_work(); })
 *     .on_then([] { on_success(); })
 *     .on_else([] { on_failure(); })
 *     .on_catch([](std::exception& e) { on_error(e); })
 *     .on_schedule_timeout([] { on_not_started_in_time(); })
 *     .on_execution_timeout([] { on_took_too_long(); });
 * @endcode
 *
 * Config fields:
 *
 * | Field                   | Meaning                                                        |
 * | ----------------------- | -------------------------------------------------------------- |
 * | @c delay_ms             | Delay before the task is posted (via one-shot Timer)           |
 * | @c priority             | Task dispatch priority (for @c kPriorityType loop)             |
 * | @c schedule_timeout_ms  | If the task is not started within this time, fire the timeout  |
 * | @c execution_timeout_ms | If the task runs longer than this, fire the timeout callback   |
 *
 * @note
 * - @c Status is move-only; copying is disabled.  The internal state is reference-counted
 *   via @c std::shared_ptr<Impl>, so it is safe to return by value.
 * - @c RetStatus adds @c on_then (fires when callback returns @c true) and @c on_else
 *   (fires when callback returns @c false).
 * - Callbacks are invoked from the @c MessageLoop thread.
 *
 * @par Example
 * @code
 * vlink::MessageLoop loop;
 * loop.async_run();
 *
 * // Void callback with execution timeout:
 * loop.exec_task(vlink::Schedule::Config{100},   // 100 ms delay
 *                [] { expensive_op(); })
 *     .on_execution_timeout([] { VLOG_W("slow!"); });
 *
 * // Bool callback with result chaining:
 * loop.exec_task(vlink::Schedule::Config{},
 *                []() -> bool { return try_connect(); })
 *     .on_then([] { start_session(); })
 *     .on_else([] { retry_later(); });
 * @endcode
 */

#pragma once

#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <vector>

#include "./functional.h"
#include "./macros.h"

namespace vlink {

/**
 * @struct Schedule
 * @brief Non-constructible utility struct providing task scheduling primitives.
 *
 * @details
 * Contains @c Config, @c Status, @c RetStatus, @c process() and @c process_with_ret().
 * Users interact with this struct primarily via @c MessageLoop::exec_task().
 */
struct VLINK_EXPORT Schedule final {
  /**
   * @brief Callback type for void tasks and lifecycle hooks (schedule/execution timeout, else).
   */
  using Callback = vlink::Function<void()>;

  /**
   * @brief Callback type for tasks that return a boolean result.
   */
  using RetCallback = vlink::Function<bool()>;

  /**
   * @brief Callback type invoked when an exception is caught inside the task.
   */
  using CatchCallback = vlink::Function<void(std::exception&)>;

  /**
   * @struct Config
   * @brief Scheduling parameters for a task posted via @c MessageLoop::exec_task().
   *
   * @details
   * All fields are optional; defaults result in immediate posting with no timeouts.
   */
  struct VLINK_EXPORT Config final {
    /**
     * @brief Constructs a default @c Config with all fields set to zero.
     */
    Config();

    /**
     * @brief Constructs a @c Config with all fields specified.
     *
     * @param _delay_ms              Delay before posting in milliseconds.  0 = immediate.
     * @param _priority              Task dispatch priority.  0 = default.
     * @param _schedule_timeout_ms   Max wait before the task starts (0 = no timeout).
     * @param _execution_timeout_ms  Max execution time of the task (0 = no timeout).
     */
    explicit Config(uint32_t _delay_ms, uint16_t _priority = 0, uint32_t _schedule_timeout_ms = 0,
                    uint32_t _execution_timeout_ms = 0);

    uint32_t delay_ms{0};              ///< Delay in ms before the task is posted.
    uint16_t priority{0};              ///< Dispatch priority (higher = sooner).
    uint32_t schedule_timeout_ms{0};   ///< Max ms to wait before task starts.  0 = disabled.
    uint32_t execution_timeout_ms{0};  ///< Max ms the task may execute.  0 = disabled.
  };

  /**
   * @class Status
   * @brief RAII handle returned by @c exec_task() for a void-callback task.
   *
   * @details
   * Holds a shared reference to the internal task state.  Destruction does not
   * cancel the task.  Callback registration methods return @c *this to allow chaining.
   */
  class VLINK_EXPORT Status {
   public:
    /**
     * @brief Constructs an invalid @c Status (not yet associated with a task).
     */
    Status();

    /**
     * @brief Destructor.
     */
    ~Status();

    Status(const Status&) = delete;

    Status& operator=(const Status&) = delete;

    /**
     * @brief Move constructor.
     *
     * @param status  Source status to move from.
     */
    Status(Status&& status) noexcept;

    /**
     * @brief Move assignment.
     *
     * @param status  Source status to move from.
     * @return Reference to @c *this.
     */
    Status& operator=(Status&& status) noexcept;

    /**
     * @brief Sets whether the status is valid (task was successfully posted).
     *
     * @param valid  @c true if the associated task was posted successfully.
     */
    void set_valid(bool valid);

    /**
     * @brief Returns @c true if the associated task was posted successfully.
     *
     * @return @c true if valid.
     */
    [[nodiscard]] bool is_valid() const;

    /**
     * @brief Registers a callback fired when the task does not start within @c schedule_timeout_ms.
     *
     * @param callback  Callback invoked from the loop thread on schedule timeout.
     * @return Reference to @c *this for chaining.
     */
    Status& on_schedule_timeout(Callback&& callback);

    /**
     * @brief Registers a callback fired when the task runs longer than @c execution_timeout_ms.
     *
     * @param callback  Callback invoked from the loop thread on execution timeout.
     * @return Reference to @c *this for chaining.
     */
    Status& on_execution_timeout(Callback&& callback);

    /**
     * @brief Registers a callback fired when the task throws an exception.
     *
     * @details
     * The exception is caught inside the wrapper and passed to this callback.
     * The task is considered failed after an exception.
     *
     * @param callback  Callback invoked with the caught exception.
     * @return Reference to @c *this for chaining.
     */
    Status& on_catch(CatchCallback&& callback);

   protected:
    friend Schedule;

    struct Impl final {
      std::atomic_bool is_valid{false};
      std::recursive_mutex mtx;
      Callback schedule_timeout_callback;
      Callback execution_timeout_callback;
      CatchCallback catch_callback;
      Callback else_callback;
      std::vector<RetCallback> then_callback_list;
    };

    std::shared_ptr<Impl> impl_;
  };

  /**
   * @class RetStatus
   * @brief RAII handle returned by @c exec_task() for a bool-returning callback task.
   *
   * @details
   * Extends @c Status with @c on_then (fired if callback returns @c true) and
   * @c on_else (fired if callback returns @c false).
   */
  class VLINK_EXPORT RetStatus final : public Status {
   public:
    using Status::Status;

    /**
     * @brief Registers a callback fired when the task returns @c false.
     *
     * @param callback  Invoked from the loop thread when the return value is @c false.
     * @return Reference to the base @c Status for further chaining.
     */
    Status& on_else(Callback&& callback);

    /**
     * @brief Registers a callback chain element fired when the task returns @c true.
     *
     * @details
     * Multiple @c on_then callbacks may be chained; each receives a @c bool return
     * value from the previous callback in the chain.
     *
     * @param callback  Callback taking no arguments and returning @c bool.
     * @return Reference to @c *this for further @c on_then chaining.
     */
    RetStatus& on_then(RetCallback&& callback);
  };

  Schedule() = delete;

  Schedule(const Schedule&) = delete;

  Schedule& operator=(const Schedule&) = delete;

  Schedule(Schedule&&) = delete;

  Schedule& operator=(Schedule&&) = delete;

  /**
   * @brief Wraps a void callback in a @c Config envelope and produces a wrapper task.
   *
   * @details
   * Called internally by @c MessageLoop::exec_task().  Creates the @c Status and
   * populates @p wrapper_callback with the task wrapper ready to be posted to the queue.
   *
   * @param config            Scheduling configuration.
   * @param callback          Void callable to execute.
   * @param wrapper_callback  Output: the wrapped task to be posted.
   * @return @c Status handle for chaining callbacks.
   */
  [[nodiscard]] static Status process(const Config& config, Callback&& callback, Callback& wrapper_callback);

  /**
   * @brief Wraps a bool-returning callback in a @c Config envelope and produces a wrapper task.
   *
   * @details
   * Called internally by @c MessageLoop::exec_task().  Creates the @c RetStatus and
   * populates @p wrapper_callback with the task wrapper ready to be posted to the queue.
   *
   * @param config            Scheduling configuration.
   * @param callback          Bool-returning callable to execute.
   * @param wrapper_callback  Output: the wrapped task to be posted.
   * @return @c RetStatus handle for chaining callbacks.
   */
  [[nodiscard]] static RetStatus process_with_ret(const Config& config, RetCallback&& callback,
                                                  Callback& wrapper_callback);

 private:
  static RetStatus internal_process_with_ret(const Config& config, RetCallback&& callback, Callback& wrapper_callback);
};

}  // namespace vlink
