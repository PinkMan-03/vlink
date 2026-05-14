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
 * | Type             | Backend                       | Cap   | Notes                       |
 * | ---------------- | ----------------------------- | ----- | --------------------------- |
 * | @c kNormalType   | mutex + std::deque (or pmr)   | 10000 | default; FIFO               |
 * | @c kLockfreeType | MpmcQueue (lock-free MPMC)    | 10000 | lowest contention overhead  |
 * | @c kPriorityType | two pmr/std priority_queue    | 10000 | split droppable / protected |
 *
 * Dispatch strategies (control behaviour when the task queue is FULL on push;
 * idle dispatch is always cv-based, independent of strategy):
 *
 * | Strategy                | Behaviour when push hits the cap                                        |
 * | ------------------------ | ---------------------------------------------------------------------- |
 * | @c kOptimizationStrategy | Retry up to 10 times with 1 ms sleep each; then drop one eligible task |
 * | @c kPopStrategy          | Drop one eligible task immediately and push the new task               |
 * | @c kBlockStrategy        | Retry indefinitely with 1 ms sleep until space is available            |
 *
 * Run modes:
 * - @c run() -- blocks the calling thread until @c quit() is called.
 * - @c async_run() -- launches a new background thread and returns immediately.
 * - @c spin() -- alias for @c run() (blocks the calling thread until @c quit() is called).
 * - @c spin_once() -- processes one batch of pending tasks on the calling thread (optionally blocking);
 *   suitable for integration into an existing event loop.
 *
 * Task execution with scheduling:
 * @c exec_task() wraps a callback in a @c Schedule::Config (delay, priority, timeouts) and
 * posts it.  It returns a @c Schedule::Status or @c Schedule::RetStatus that can be chained
 * with @c on_then / @c on_else / @c on_catch / @c on_schedule_timeout callbacks.
 *
 * @note
 * - Maximum task queue depth is 10000 (@c kMaxTaskSize).  When the queue is at
 *   capacity, the configured @c Strategy decides between drop, retry-then-drop
 *   or block-forever; only @c post_task callers using @c TaskOverflowPolicy::kReject
 *   (via @c post_task_handle) get an immediate failure, in which case the @c bool
 *   return is @c false and the returned @c TaskHandle reports @c kRejected.
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

#include <atomic>
#include <functional>
#include <future>
#include <limits>
#include <memory>
#include <mutex>
#include <string>
#include <type_traits>
#include <utility>

#include "./functional.h"
#include "./schedule.h"
#include "./task_handle.h"
#include "./timer.h"

namespace vlink {

namespace detail {

/**
 * @struct MessageLoopAliveState
 * @brief Lifetime flag shared between a @c MessageLoop and external observers.
 *
 * @details
 * @c MessageLoopAliveState is a tiny refcounted block published via
 * @c MessageLoop::get_alive_state() and used by coroutine adapters (and similar
 * cross-thread bridges) that need to check whether the loop they are about to
 * resume on is still alive without keeping the @c MessageLoop itself pinned.
 *
 * The contract is:
 *  - @c alive starts at @c true on construction.
 *  - @c MessageLoop sets @c alive to @c false under @c mtx as the very first
 *    step of its destructor, before any other teardown.
 *  - Observers acquire @c mtx, re-check @c alive, and only touch the loop
 *    (e.g. @c post_task) while still holding the lock.
 *
 * Holding @c mtx therefore guarantees the @c MessageLoop has not yet entered
 * destruction; releasing @c mtx without observing @c alive @c == @c false also
 * guarantees the next destructor step is still pending.
 *
 * @note Internal type.  Not intended for direct user code; obtain it via
 *       @c MessageLoop::get_alive_state().
 */
struct MessageLoopAliveState final {
  std::mutex mtx;                ///< Serialises destruction against observers.
  std::atomic_bool alive{true};  ///< @c false once the owning loop has begun destruction.
};

}  // namespace detail

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
   * Move-only (`MoveFunction<void()>`).  Posting and storage paths only
   * require move semantics; using a move-only wrapper allows targets such as
   * @c std::packaged_task and lambdas with @c std::unique_ptr captures to be
   * scheduled directly without a @c std::shared_ptr trampoline.
   */
  using Callback = MoveFunction<void()>;

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
   * @brief Push-side back-pressure strategy applied when the bounded queue is full.
   *
   * @details
   * Idle dispatch is unconditionally driven by a condition variable; this enum only
   * affects how @c post_task / @c invoke_task react when the queue has reached its
   * capacity.  See the class documentation for a comparison table.
   */
  enum Strategy : uint8_t {
    kOptimizationStrategy = 0,  ///< Balance: when full, retry; after 10 retries drop one eligible task and push.
    kPopStrategy = 1,           ///< When full, drop one eligible task immediately and push the new task.
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
   * @brief Destructor.  Requests quit and waits for the background thread if needed.
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
   * @brief Alias for @c run().  Runs the loop on the calling thread (blocking).
   *
   * @details
   * Equivalent to calling @c run().  Blocks the calling thread until @c quit() is called.
   *
   * @return Same return value as @c run().
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
   * @return @c true after a normal processing cycle.
   *         @c false if the loop has been signalled to quit, or if the call was made from
   *         a thread other than the loop thread (when one has already been bound by @c run/async_run).
   */
  bool spin_once(bool block = true);

  /**
   * @brief Requests the loop to exit.
   *
   * @details
   * Sets an internal quit flag that the dispatcher checks between iterations.
   * Behaviour is **per-batch**, not per-task:
   *  - With @p force @c == @c false (default), the loop finishes the current
   *    batch of already-dequeued tasks (the snapshot it is currently iterating)
   *    and then exits.  Tasks posted after this call are rejected.
   *  - With @p force @c == @c true, the dispatcher additionally aborts the
   *    in-flight batch and drops the remaining tasks in that batch.
   *
   * If a graceful drain of *everything currently queued* is required, prefer
   * @c wait_for_idle() before @c quit().  Returns @c false if the loop is not
   * running or @c quit() has already been called (only when @p force is
   * @c false).
   *
   * @param force  If @c true, also discard the in-flight batch.  Default: @c false.
   * @return @c true if the quit signal was accepted.
   */
  bool quit(bool force = false);

  /**
   * @brief Waits until the task queue is drained.
   *
   * @details
   * "Idle" means: the loop is not currently executing a task and the task
   * queue is empty.  If the loop has not yet been started — i.e. neither
   * @c run() nor @c async_run() has been called (or the loop has already
   * exited) — only the empty-queue condition is required; tasks queued
   * before the first start still keep the loop non-idle and cause this
   * function to wait (or time out).
   *
   * @param ms     Maximum wait time in milliseconds.  @c Timer::kInfinite for unlimited.  Default: @c kInfinite.
   * @param check  If @c true, also reject calls made from the loop's own
   *               thread (which would deadlock).  Default: @c true.
   * @return @c true if the loop reached the idle state within @p ms;
   *         @c false on timeout or if @p check is @c true and the call was
   *         made from the loop thread.
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
   * Thread-safe.  Returns @c false if the loop has been signalled to quit, or if
   * the configured overflow handling cannot make room for the new task.  When the
   * queue is at the cap (@c kMaxTaskSize) the behaviour follows the configured
   * @c Strategy (drop an eligible task immediately, retry-then-drop, or retry
   * indefinitely).
   *
   * On @c kPriorityType loops, tasks posted without an explicit priority use
   * @c kNormalPriority.
   *
   * @note Drop-policy semantics:
   *  - @c kNormalType and @c kPriorityType queues honour @c TaskDropPolicy::kProtected
   *    on a per-task basis: protected tasks are never selected as overflow drop
   *    victims, and if every task currently in the queue is protected the post
   *    fails and returns @c false.
   *  - @c kLockfreeType queues do not track per-task drop policy; overflow drop
   *    simply removes one queued task regardless of how it was submitted.
   *
   * @param callback  Task to execute.
   * @return @c true if the task was eventually enqueued (possibly after dropping
   *         an eligible task or blocking); @c false if the loop is quitting or
   *         no eligible task could be dropped to make room.
   */
  bool post_task(Callback&& callback);

  /**
   * @brief Posts a tracked task to the queue and returns a @c TaskHandle.
   *
   * @details
   * Tracked counterpart of @c post_task().  The returned @c TaskHandle can be
   * used to wait for completion, request cooperative cancellation through the
   * task's @c CancellationToken, and observe whether the task was eventually
   * rejected (queue closed, no droppable victim available) or dropped before
   * execution.
   *
   * @param callback  Task to execute.
   * @param options   Optional overflow, drop and cancellation policy.  See
   *                  @c PostTaskOptions for defaults.
   * @return Handle associated with the posted task.  The handle remains valid
   *         even if the post is rejected — query its state to find out.
   */
  [[nodiscard]] TaskHandle post_task_handle(Callback&& callback, const PostTaskOptions& options = {});

  /**
   * @brief Posts a task with an explicit priority (requires @c kPriorityType loop).
   *
   * @details
   * Tasks with higher priority values are dispatched before lower-priority tasks.
   * Only @c kPriorityType loops honour @p priority; calling this on a
   * @c kNormalType or @c kLockfreeType loop is rejected and returns @c false.
   * Overflow behaviour and drop-policy semantics are the same as @c post_task().
   *
   * @param callback  Task to execute.
   * @param priority  Dispatch priority; higher runs first.  Use one of the
   *                  @c Priority enumerators or any value in
   *                  @c [kLowestPriority, kHighestPriority].
   * @return @c true if enqueued successfully; @c false on a non-priority loop,
   *         if the loop is quitting, or if no eligible task could be dropped.
   */
  bool post_task_with_priority(Callback&& callback, uint16_t priority);

  /**
   * @brief Posts a tracked task with an explicit priority and returns a @c TaskHandle.
   *
   * @details
   * Tracked counterpart of @c post_task_with_priority().  Requires a
   * @c kPriorityType loop; on other loop types the post is rejected and the
   * returned handle reports rejection.  See @c post_task_handle() for the
   * general semantics of the returned handle.
   *
   * @param callback  Task to execute.
   * @param priority  Dispatch priority; higher values run first.
   * @param options   Optional overflow, drop and cancellation policy.  See
   *                  @c PostTaskOptions for defaults.
   * @return Handle associated with the posted task.
   */
  [[nodiscard]] TaskHandle post_task_with_priority_handle(Callback&& callback, uint16_t priority,
                                                          const PostTaskOptions& options = {});

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
   * @brief Wakes the loop thread if it is blocked in its idle @c cv.wait.
   *
   * @details
   * Coalesces concurrent calls: only the first call after the loop last cleared
   * its wakeup-pending flag actually grabs the wait mutex and notifies.  Useful
   * when external state (other than queue insertion) has changed and the loop
   * should re-evaluate its idle predicate.
   *
   * @return @c true if a wakeup signal is pending for the loop (either sent by
   *         this call or already pending from a prior call); @c false if the
   *         loop is not running.
   */
  bool wakeup();

  /**
   * @brief Re-creates the lock-free queue, clearing all queued tasks and bookkeeping.
   *
   * @details
   * Only applicable to @c kLockfreeType loops.  Re-emplaces the underlying
   * @c MpmcQueue (so the @c head_ / @c tail_ cursors and the
   * @c lockfree_task_count counter are reset to zero) and clears the deferred
   * @c lockfree_needs_reset flag.
   *
   * @note The loop **must be stopped** (i.e. @c is_running() returning @c false)
   *       before calling this function; calls made while the loop is running
   *       are logged at error level and silently skipped.  On non-lockfree
   *       loops the call is a no-op.
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
   * @brief Returns the shared lifetime flag used by coroutine adapters.
   *
   * @details
   * The returned @c MessageLoopAliveState outlives this @c MessageLoop: its
   * @c alive member is flipped to @c false under @c mtx as the very first step
   * of the destructor (see @c MessageLoopAliveState).  Adapters that need to
   * post a continuation back to this loop from another thread should:
   *
   *  1. Capture the @c shared_ptr returned here (cheap, refcounted).
   *  2. On the resumer thread, lock @c MessageLoopAliveState::mtx, re-check
   *     @c alive, and only call back into the loop while still holding the
   *     lock.
   *
   * @return Shared handle to this loop's lifetime flag; never null while the
   *         loop object itself is alive.
   * @note Internal API for coroutine adapters and similar cross-thread bridges.
   *       Application code should not need to call this directly.
   */
  [[nodiscard]] std::shared_ptr<detail::MessageLoopAliveState> get_alive_state() const;

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
   * @brief Dispatches a single ready task on the loop thread.
   *
   * @details
   * The default implementation invokes @p callback directly (after optionally checking
   * elapsed-time and forwarding overruns to @c on_task_timeout).  Subclasses override
   * this to wrap or redirect dispatch -- e.g. @c MultiLoop posts the callback to an
   * internal @c ThreadPool instead of running it inline.  An override that does not
   * forward to the base implementation must arrange for @p callback to be invoked
   * itself, or the task will be silently dropped.
   *
   * @param callback    The task to dispatch.
   * @param start_time  Millisecond steady_clock timestamp captured when the task was
   *                    enqueued (0 if elapsed-time tracking is disabled).
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

  bool push_task(Callback&& callback, uint16_t priority, bool droppable = true,
                 TaskOverflowPolicy overflow_policy = TaskOverflowPolicy::kUseDispatcherStrategy,
                 const TaskHandle* submit_handle = nullptr);

  bool drop_one_normal_task();

  bool drop_one_lockfree_task(bool keep_reserved = false);

  bool drop_one_priority_task();

  bool reserve_lockfree_task();

  void release_lockfree_task();

  void push_normal_task(Callback&& callback, bool droppable = true);

  bool push_lockfree_task(Callback&& callback);

  void push_priority_task(Callback&& callback, uint16_t priority, bool droppable = true);

  void do_consume();

  bool process_normal_task(bool block);

  bool process_lockfree_task(bool block);

  bool process_priority_task(bool block);

  bool process_timer_task(int64_t& next_sleep_time);

  void drop_pending_tasks();

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
  auto bound =
      std::bind(std::forward<FunctionT>(function), std::forward<ArgsT>(args)...);  // NOLINT(modernize-avoid-bind)

  if constexpr (kIsSupportMoveFunction) {
    std::packaged_task<ResultT()> task(std::move(bound));
    auto res = task.get_future();

    if VUNLIKELY (!post_task([task = std::move(task)]() mutable { task(); })) {
      // Destroying the unposted packaged_task makes the returned future ready with broken_promise.
    }

    return res;
  } else {
    auto task = std::make_shared<std::packaged_task<ResultT()>>(std::move(bound));
    auto res = task->get_future();

    if VUNLIKELY (!post_task([task]() mutable { (*task)(); })) {
      task.reset();
    }

    return res;
  }
}

template <class FunctionT, class... ArgsT, typename ResultT>
inline std::future<ResultT> MessageLoop::invoke_task_with_priority(FunctionT&& function, uint16_t priority,
                                                                   ArgsT&&... args) {
  auto bound =
      std::bind(std::forward<FunctionT>(function), std::forward<ArgsT>(args)...);  // NOLINT(modernize-avoid-bind)

  if constexpr (kIsSupportMoveFunction) {
    std::packaged_task<ResultT()> task(std::move(bound));
    auto res = task.get_future();

    if VUNLIKELY (!post_task_with_priority([task = std::move(task)]() mutable { task(); }, priority)) {
      // Destroying the unposted packaged_task makes the returned future ready with broken_promise.
    }

    return res;
  } else {
    auto task = std::make_shared<std::packaged_task<ResultT()>>(std::move(bound));
    auto res = task->get_future();

    if VUNLIKELY (!post_task_with_priority([task]() mutable { (*task)(); }, priority)) {
      task.reset();
    }

    return res;
  }
}

}  // namespace vlink
