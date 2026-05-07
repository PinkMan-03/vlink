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
 * @file message_loop.h
 * @brief Single-threaded event loop with three queue types, timer management and task scheduling.
 *
 * @details
 * @c MessageLoop is the primary task dispatcher in VLink.  It owns a task queue and an
 * associated timer registry.  Tasks posted via @c post_task() are executed serially on
 * the loop's thread.  Timers registered with @c Timer::attach() fire their callbacks as
 * regular queue tasks.
 *
 * Queue types:
 *
 * | Type              | Queue implementation           | Max tasks        | Notes                          |
 * | ----------------- | ------------------------------ | ---------------- | ------------------------------ |
 * | @c kNormalType    | Mutex-protected std::queue     | 10000            | Default; no priority support   |
 * | @c kLockfreeType  | MpmcQueue (lock-free MPMC)     | 10000            | Fastest single-producer path   |
 * | @c kPriorityType  | Priority queue                 | 10000            | Supports task priority levels  |
 *
 * Dispatch strategies (control behaviour when the task queue is FULL on push;
 * idle dispatch is always cv-based, independent of strategy):
 *
 * | Strategy                | Behaviour when push hits the cap                                        |
 * | ----------------------- | ----------------------------------------------------------------------- |
 * | @c kOptimizationStrategy | Retry up to 10 times with 1 ms sleep each; then drop oldest and push  |
 * | @c kPopStrategy         | Drop the oldest entry immediately and push the new task                 |
 * | @c kBlockStrategy       | Retry indefinitely with 1 ms sleep until space is available             |
 *
 * Run modes:
 * - @c run() -- blocks the calling thread until @c quit() is called.
 * - @c async_run() -- launches a new background thread and returns immediately.
 * - @c spin() -- calls @c spin_once() in a loop; suitable for use in an existing event loop.
 * - @c spin_once() -- processes one batch of pending tasks (optionally blocking).
 *
 * Task execution with scheduling:
 * @c exec_task() wraps a callback in a @c Schedule::Config (delay, priority, timeouts) and
 * posts it.  It returns a @c Schedule::Status or @c Schedule::RetStatus that can be chained
 * with @c on_then / @c on_else / @c on_catch / @c on_schedule_timeout callbacks.
 *
 * @note
 * - Maximum task queue depth is 10000 (@c kMaxTaskSize); posts beyond this fail silently.
 * - Maximum active timer count is 100 (@c kMaxTimerSize).
 * - @c invoke_task() dispatches a callable and returns a @c std::future for the result.
 *   Blocking on the future from the same thread as the loop will deadlock.
 *
 * @par Example
 * @code
 * vlink::MessageLoop loop;
 * loop.async_run();
 *
 * loop.post_task([] { do_work(); });
 *
 * // Blocking invoke from another thread:
 * auto fut = loop.invoke_task([]() -> int { return compute(); });
 * int result = fut.get();  // waits for the loop to process the task
 *
 * loop.quit();
 * loop.wait_for_quit();
 * @endcode
 */

#pragma once

#include <future>
#include <limits>
#include <memory>
#include <string>
#include <type_traits>
#include <utility>

#include "./functional.h"
#include "./schedule.h"
#include "./timer.h"

namespace vlink {

/**
 * @class MessageLoop
 * @brief Single-threaded serial task dispatcher with integrated timer support.
 *
 * @details
 * All tasks and timer callbacks run on a single thread.  Thread-safe posting of
 * tasks is allowed from any thread via @c post_task().
 */
class VLINK_EXPORT MessageLoop {
 public:
  /**
   * @brief Callback type for tasks and event handlers.
   *
   * @details
   * Move-only (`vlink::MoveFunction<void()>`).  Posting and storage paths only
   * require move semantics; using a move-only wrapper allows targets such as
   * @c std::packaged_task and lambdas with @c std::unique_ptr captures to be
   * scheduled directly without a @c std::shared_ptr trampoline.
   */
  using Callback = vlink::MoveFunction<void()>;

  /**
   * @brief Queue implementation type.
   *
   * @details
   * Selects the internal task queue algorithm.  See class documentation for a comparison table.
   */
  enum Type : uint8_t {
    kNormalType = 0,    ///< Mutex-protected FIFO queue (default)
    kLockfreeType = 1,  ///< Lock-free MPMC queue
    kPriorityType = 2,  ///< Priority-ordered queue
  };

  /**
   * @brief Idle strategy controlling CPU and latency trade-offs.
   *
   * @details
   * See class documentation for a comparison table.
   */
  enum Strategy : uint8_t {
    kOptimizationStrategy = 0,  ///< Balance: when full, retry; after 10 retries drop oldest and push.
    kPopStrategy = 1,           ///< When full, drop oldest immediately and push the new task.
    kBlockStrategy = 2,         ///< When full, retry indefinitely with 1 ms sleep between attempts.
  };

  /**
   * @brief Pre-defined task priority levels for @c kPriorityType loops.
   *
   * @details
   * Higher numeric values are dispatched first.  Custom priority values may be used
   * between @c kLowestPriority and @c kHighestPriority.
   */
  enum Priority : uint16_t {
    kNoPriority = 0,                                         ///< No priority (FIFO order)
    kLowestPriority = 1,                                     ///< Lowest real priority
    kTimerPriority = 50,                                     ///< Used internally for timer callbacks
    kNormalPriority = 100,                                   ///< Default task priority
    kHighestPriority = std::numeric_limits<uint16_t>::max()  ///< Highest available priority
  };

  /**
   * @brief Constructs a @c MessageLoop with @c kNormalType queue.
   */
  MessageLoop();

  /**
   * @brief Constructs a @c MessageLoop with the specified queue type.
   *
   * @param type  Queue implementation to use.
   */
  explicit MessageLoop(Type type);

  /**
   * @brief Destructor.  Calls @c quit(true) and waits for the background thread (if any).
   */
  virtual ~MessageLoop();

  /**
   * @brief Sets a human-readable name for this loop (visible in profiling tools).
   *
   * @param name  Name string.
   */
  void set_name(const std::string& name);

  /**
   * @brief Returns the name set via @c set_name().
   *
   * @return Reference to the name string.
   */
  [[nodiscard]] const std::string& get_name() const;

  /**
   * @brief Returns the queue type this loop was constructed with.
   *
   * @return Queue type.
   */
  [[nodiscard]] Type get_type() const;

  /**
   * @brief Returns the current idle dispatch strategy.
   *
   * @return Current strategy.
   */
  [[nodiscard]] Strategy get_strategy() const;

  /**
   * @brief Changes the idle dispatch strategy.
   *
   * @param strategy  New strategy.  Takes effect on the next idle cycle.
   */
  void set_strategy(Strategy strategy);

  /**
   * @brief Registers a callback invoked once when the loop thread starts.
   *
   * @param callback  Called from the loop thread before the first task is processed.
   */
  void register_begin_handler(Callback&& callback);

  /**
   * @brief Registers a callback invoked once when the loop thread exits.
   *
   * @param callback  Called from the loop thread after the last task has been processed.
   */
  void register_end_handler(Callback&& callback);

  /**
   * @brief Registers a callback invoked each time the task queue becomes empty.
   *
   * @param callback  Called from the loop thread on each idle cycle.
   */
  void register_idle_handler(Callback&& callback);

  /**
   * @brief Runs the message loop on the calling thread (blocking).
   *
   * @details
   * Processes tasks and fires timers until @c quit() is called.
   *
   * @return @c true if the loop ran and exited normally; @c false if already running.
   */
  bool run();

  /**
   * @brief Starts the message loop on a new background thread (non-blocking).
   *
   * @details
   * Returns immediately.  The background thread runs until @c quit() is called.
   *
   * @return @c true if the thread was started; @c false if already running.
   */
  bool async_run();

  /**
   * @brief Runs the loop continuously in a spin mode (blocking; no background thread).
   *
   * @details
   * Calls @c spin_once() repeatedly until @c quit() is called.
   *
   * @return @c true on normal exit.
   */
  bool spin();

  /**
   * @brief Processes one batch of pending tasks and timers.
   *
   * @details
   * Intended for integration into an existing event loop.
   *
   * @param block  If @c true and the queue is empty, blocks until a task arrives.
   *               If @c false, returns immediately if the queue is empty.  Default: @c true.
   * @return @c true if at least one task was processed; @c false if the loop should quit.
   */
  bool spin_once(bool block = true);

  /**
   * @brief Requests the loop to exit cleanly.
   *
   * @details
   * Signals the loop to stop after finishing the current task.  If @p force is @c true,
   * remaining queued tasks are discarded.
   *
   * @param force  If @c true, discard pending tasks.  Default: @c false.
   * @return @c true on success.
   */
  bool quit(bool force = false);

  /**
   * @brief Waits until the task queue is drained.
   *
   * @param ms     Maximum wait time in milliseconds.  @c Timer::kInfinite for unlimited.  Default: @c kInfinite.
   * @param check  If @c true, also verify the loop is in the idle state.  Default: @c true.
   * @return @c true if the queue drained within the timeout.
   */
  bool wait_for_idle(int ms = Timer::kInfinite, bool check = true);

  /**
   * @brief Waits until the loop has fully exited (after @c quit() was called).
   *
   * @param ms     Maximum wait time in milliseconds.  @c Timer::kInfinite for unlimited.  Default: @c kInfinite.
   * @param check  If @c true, also verify the loop thread has joined.  Default: @c true.
   * @return @c true if the loop exited within the timeout.
   */
  bool wait_for_quit(int ms = Timer::kInfinite, bool check = true);

  /**
   * @brief Posts a task to the queue for execution on the loop thread.
   *
   * @details
   * Thread-safe.  Returns @c false only if the loop has been signalled to quit.
   * When the queue is at the cap (@c kMaxTaskSize), behaviour follows the configured
   * @c Strategy (drop oldest immediately, retry-then-drop, or retry indefinitely);
   * none of those paths returns @c false because of fullness.
   *
   * @param callback  Task to execute.
   * @return @c true if the task was eventually enqueued (possibly after dropping an
   *         older task or blocking).  @c false if the loop is quitting.
   */
  bool post_task(Callback&& callback);

  /**
   * @brief Posts a task with an explicit priority (requires @c kPriorityType loop).
   *
   * @details
   * Tasks with higher priority values are dispatched before lower-priority tasks.
   * For @c kNormalType and @c kLockfreeType loops, priority is ignored and the task is
   * enqueued in FIFO order.
   *
   * @param callback  Task to execute.
   * @param priority  Dispatch priority.
   * @return @c true if enqueued successfully.
   */
  bool post_task_with_priority(Callback&& callback, uint16_t priority);

  /**
   * @brief Posts a scheduled task and returns a @c Schedule::Status for chaining callbacks.
   *
   * @details
   * This overload is for callbacks returning @c void.
   * The @c Schedule::Config can specify a delay, priority, schedule timeout and execution timeout.
   * Chain @c on_schedule_timeout, @c on_execution_timeout or @c on_catch on the returned status.
   *
   * @tparam CallbackT  Callable type returning @c void.
   * @param config    Scheduling configuration.
   * @param callback  Callable to execute.
   * @return @c Schedule::Status for chaining.
   */
  // NOLINTNEXTLINE(modernize-use-constraints)
  template <typename CallbackT, typename = std::enable_if_t<!std::is_convertible_v<CallbackT, Schedule::RetCallback>>>
  Schedule::Status exec_task(const Schedule::Config& config, CallbackT&& callback);

  /**
   * @brief Posts a scheduled task and returns a @c Schedule::RetStatus for chaining callbacks.
   *
   * @details
   * This overload is for callbacks returning @c bool.  Chain @c on_then (fires if @c true)
   * and @c on_else (fires if @c false) on the returned status.
   *
   * @tparam CallbackT  Callable type returning @c bool.
   * @param config    Scheduling configuration.
   * @param callback  Callable to execute.
   * @return @c Schedule::RetStatus for chaining.
   */
  // NOLINTNEXTLINE(modernize-use-constraints)
  template <typename CallbackT, typename = std::enable_if_t<std::is_convertible_v<CallbackT, Schedule::RetCallback>>>
  Schedule::RetStatus exec_task(const Schedule::Config& config, CallbackT&& callback);

  /**
   * @brief Wakes the loop thread if it is sleeping (e.g., in @c kBlockStrategy).
   *
   * @return @c true if the wakeup signal was sent.
   */
  bool wakeup();

  /**
   * @brief Resets the lock-free queue to its initial capacity.
   *
   * @details
   * Only applicable to @c kLockfreeType loops.  Useful after a large burst of tasks
   * to reclaim internal capacity bookkeeping.
   */
  void reset_lockfree_capacity();

  /**
   * @brief Returns @c true if the loop is currently running (started and not quit).
   *
   * @return @c true if the loop is active.
   */
  [[nodiscard]] bool is_running() const;

  /**
   * @brief Returns @c true if @c quit() has been called and the loop is winding down.
   *
   * @return @c true if the loop is in the process of quitting.
   */
  [[nodiscard]] bool is_ready_to_quit() const;

  /**
   * @brief Returns @c true if the loop is currently executing a task.
   *
   * @return @c true if a task callback is in progress on the loop thread.
   */
  [[nodiscard]] bool is_busy() const;

  /**
   * @brief Returns the number of tasks currently in the queue.
   *
   * @return Pending task count.
   */
  [[nodiscard]] size_t get_task_count() const;

  /**
   * @brief Returns the maximum queue depth.
   *
   * @return @c kMaxTaskSize (10000) by default.
   */
  [[nodiscard]] virtual size_t get_max_task_count() const;

  /**
   * @brief Returns the maximum number of timers that can be attached to this loop.
   *
   * @return @c kMaxTimerSize (100) by default.
   */
  [[nodiscard]] virtual size_t get_max_timer_count() const;

  /**
   * @brief Returns the maximum allowed task execution time in milliseconds.
   *
   * @details
   * When a task exceeds this duration, @c on_task_timeout() is called.
   * Returns 0 to disable timeout checking.
   *
   * @return Maximum execution time in ms.
   */
  [[nodiscard]] virtual uint32_t get_max_elapsed_time() const;

  /**
   * @brief Returns @c true if the calling thread is the same as the loop thread.
   *
   * @details
   * Used internally to detect if a task is calling back into the loop synchronously.
   * For @c MultiLoop, returns @c true if the caller is any of the worker threads.
   *
   * @return @c true if called from the loop's own thread.
   */
  [[nodiscard]] virtual bool is_in_same_thread() const;

  /**
   * @brief Dispatches a callable to the loop thread and returns a @c std::future for the result.
   *
   * @details
   * Thread-safe.  The future becomes ready after the callable is executed on the loop thread.
   *
   * @warning Do not call @c .get() on the future from the same thread as the loop; doing so will
   *          deadlock.
   *
   * @tparam FunctionT  Callable type.
   * @tparam ArgsT      Argument types.
   * @tparam ResultT    Return type (deduced).
   * @param function  Callable to dispatch.
   * @param args      Arguments forwarded to the callable.
   * @return @c std::future<ResultT> that becomes ready when the task completes.
   */
  template <class FunctionT, class... ArgsT, typename ResultT = std::invoke_result_t<FunctionT, ArgsT...>>
  [[nodiscard]] std::future<ResultT> invoke_task(FunctionT&& function, ArgsT&&... args);

  /**
   * @brief Dispatches a callable with an explicit priority and returns a @c std::future.
   *
   * @details
   * Same as @c invoke_task() but the task is enqueued at @p priority level.
   * Requires a @c kPriorityType loop for priority to take effect.
   *
   * @tparam FunctionT  Callable type.
   * @tparam ArgsT      Argument types.
   * @tparam ResultT    Return type (deduced).
   * @param function  Callable to dispatch.
   * @param priority  Dispatch priority.
   * @param args      Arguments forwarded to the callable.
   * @return @c std::future<ResultT>.
   */
  template <class FunctionT, class... ArgsT, typename ResultT = std::invoke_result_t<FunctionT, ArgsT...>>
  [[nodiscard]] std::future<ResultT> invoke_task_with_priority(FunctionT&& function, uint16_t priority,
                                                               ArgsT&&... args);

 protected:
  /**
   * @brief Called from the loop thread just before the first task is processed.
   *
   * @details
   * Override in subclasses to perform per-thread initialisation.
   */
  virtual void on_begin();

  /**
   * @brief Called from the loop thread just after the last task has been processed.
   *
   * @details
   * Override in subclasses to perform per-thread cleanup.
   */
  virtual void on_end();

  /**
   * @brief Called from the loop thread each time the queue becomes empty.
   *
   * @details
   * Override in subclasses to perform idle work (e.g., statistics updates).
   */
  virtual void on_idle();

  /**
   * @brief Called before each task is executed.
   *
   * @details
   * Provides the task callback and the monotonic start timestamp (in milliseconds).
   * Override to implement per-task tracing or accounting.
   *
   * @param callback    The task about to be executed.
   * @param start_time  Millisecond timestamp at which the task was dequeued.
   */
  virtual void on_task_changed(Callback&& callback, uint32_t start_time);

  /**
   * @brief Called when a task's execution time exceeds @c get_max_elapsed_time().
   *
   * @details
   * Override to log or handle slow tasks.
   *
   * @param callback      The task that timed out.
   * @param elapsed_time  Actual execution time in milliseconds.
   */
  virtual void on_task_timeout(Callback&& callback, uint32_t elapsed_time);

 private:
  static uint64_t get_current_nano_time();

  bool add_timer(Timer* timer);

  bool remove_timer(Timer* timer);

  bool push_task(Callback&& callback, uint16_t priority);

  void push_normal_task(Callback&& callback);

  bool push_lockfree_task(Callback&& callback);

  void push_priority_task(Callback&& callback, uint16_t priority);

  void do_consume();

  bool process_normal_task(bool block);

  bool process_lockfree_task(bool block);

  bool process_priority_task(bool block);

  bool process_timer_task(int64_t& next_sleep_time);

  friend Timer;
  struct Impl;
  std::unique_ptr<Impl> impl_;

  VLINK_DISALLOW_COPY_AND_ASSIGN(MessageLoop)
};

////////////////////////////////////////////////////////////////
/// Details
////////////////////////////////////////////////////////////////

template <typename CallbackT, typename>
Schedule::Status MessageLoop::exec_task(const Schedule::Config& config, CallbackT&& callback) {
  Schedule::Callback wrapper_callback;

  // NOLINTNEXTLINE(bugprone-move-forwarding-reference)
  auto status = Schedule::process(config, std::move(callback), wrapper_callback);

  bool post_ret = false;

  if (config.delay_ms > 0) {
    post_ret = Timer::call_once(this, config.delay_ms, std::move(wrapper_callback), config.priority);
  } else {
    if (get_type() == kPriorityType && config.priority != kNoPriority) {
      post_ret = post_task_with_priority(std::move(wrapper_callback), config.priority);
    } else {
      post_ret = post_task(std::move(wrapper_callback));
    }
  }

  if VUNLIKELY (!post_ret) {
    status.set_valid(false);
  }

  return status;
}

template <typename CallbackT, typename>
Schedule::RetStatus MessageLoop::exec_task(const Schedule::Config& config, CallbackT&& callback) {
  Schedule::Callback wrapper_callback;

  // NOLINTNEXTLINE(bugprone-move-forwarding-reference)
  auto status = Schedule::process_with_ret(config, std::move(callback), wrapper_callback);

  bool post_ret = false;

  if (config.delay_ms > 0) {
    post_ret = Timer::call_once(this, config.delay_ms, std::move(wrapper_callback), config.priority);
  } else {
    if (get_type() == kPriorityType && config.priority != kNoPriority) {
      post_ret = post_task_with_priority(std::move(wrapper_callback), config.priority);
    } else {
      post_ret = post_task(std::move(wrapper_callback));
    }
  }

  if VUNLIKELY (!post_ret) {
    status.set_valid(false);
  }

  return status;
}

template <class FunctionT, class... ArgsT, typename ResultT>
inline std::future<ResultT> MessageLoop::invoke_task(FunctionT&& function, ArgsT&&... args) {
  std::packaged_task<ResultT()> task(
      std::bind(std::forward<FunctionT>(function), std::forward<ArgsT>(args)...));  // NOLINT(modernize-avoid-bind)

  std::future<ResultT> res = task.get_future();

  post_task([t = std::move(task)]() mutable { t(); });

  return res;
}

template <class FunctionT, class... ArgsT, typename ResultT>
inline std::future<ResultT> MessageLoop::invoke_task_with_priority(FunctionT&& function, uint16_t priority,
                                                                   ArgsT&&... args) {
  std::packaged_task<ResultT()> task(
      std::bind(std::forward<FunctionT>(function), std::forward<ArgsT>(args)...));  // NOLINT(modernize-avoid-bind)

  std::future<ResultT> res = task.get_future();

  post_task_with_priority([t = std::move(task)]() mutable { t(); }, priority);

  return res;
}

}  // namespace vlink
