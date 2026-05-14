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
 * @file task_handle.h
 * @brief Observable handle returned by tracked task-posting APIs of @c MessageLoop and @c ThreadPool.
 *
 * @details
 * @c TaskHandle is a small shared-state value type that lets callers observe and
 * influence a task they previously submitted to a base dispatcher.  Each handle
 * carries a strong reference to an internal state block; copying a handle is
 * cheap and all copies refer to the same task.  Destroying every handle does
 * not cancel the underlying task -- the dispatcher retains its own reference
 * for the duration of the task lifecycle.
 *
 * The submission options and lifecycle types defined here are:
 *
 * | Type                  | Role                                                                  |
 * | --------------------- | --------------------------------------------------------------------- |
 * | TaskExecutionState    | Lifecycle stage of a tracked task (queued/running/terminal).          |
 * | TaskOverflowPolicy    | Per-post override for bounded-queue overflow handling.                |
 * | TaskDropPolicy        | Whether an accepted task may be discarded by drop-oldest paths.       |
 * | PostTaskOptions       | Aggregate of optional knobs passed to tracked @c post_task overloads. |
 * | TaskHandle            | The observer object returned to the caller.                           |
 *
 * @par Lock ordering
 * @c TaskHandle's internal state mutex is the innermost mutex in the dispatching
 * layer.  The expected lock order is
 * @c MessageLoopAliveState::mtx -> @c MessageLoop::Impl::mtx -> @c TaskHandle::State::mtx.
 * @c TaskHandle::cancel() additionally acquires the underlying cancellation source's
 * internal state mutex, but only AFTER releasing @c TaskHandle::State::mtx, so the
 * cancellation source mutex is never nested inside any dispatching-layer mutex.
 * Callbacks registered via @c TaskHandle::cancellation_token() are fired outside
 * @c TaskHandle::State::mtx and so may safely call @c cancel() / @c wait() on
 * the same or related handles.
 *
 * @par Example
 * @code
 * vlink::PostTaskOptions opts;
 * opts.cancellation_token = parent.token();
 * auto handle = loop.post_task_handle([]{ work(); }, opts);
 * if (giving_up) {
 *   handle.cancel();
 * }
 * handle.wait();
 * if (handle.state() == vlink::TaskExecutionState::kFailed) {
 *   // task threw; inspect logs
 * }
 * @endcode
 *
 * @note Destroying a @c TaskHandle does @b not cancel the associated task; the
 * task continues to run to completion under the dispatcher's ownership.
 */

#pragma once

#include <cstdint>
#include <memory>

#include "./cancellation.h"
#include "./functional.h"
#include "./macros.h"

namespace vlink {

class MessageLoop;
class ThreadPool;

namespace detail {
class TrackedTask;
}  // namespace detail

/**
 * @enum TaskExecutionState
 * @brief Observable lifecycle stage of a task submitted through a tracked posting API.
 *
 * @details
 * States divide into non-terminal stages (@c kInvalid, @c kQueued, @c kRunning)
 * and terminal stages (@c kCompleted, @c kCancelled, @c kDropped, @c kRejected,
 * @c kFailed).  Once a task reaches a terminal stage its state never changes
 * again, and @c TaskHandle::wait() unblocks all waiters.
 */
enum class TaskExecutionState : uint8_t {
  kInvalid = 0,  ///< Non-terminal: empty handle or not yet associated with a queued task.
  kQueued,       ///< Non-terminal: task has been accepted by the dispatcher but has not started running.
  kRunning,      ///< Non-terminal: task callback is currently executing on a worker thread.
  kCompleted,    ///< Terminal: task callback returned normally.
  kCancelled,    ///< Terminal: task was cancelled before its callback started executing.
  kDropped,      ///< Terminal: task was removed from a bounded queue before execution (drop-oldest path).
  kRejected,     ///< Terminal: dispatcher refused the task, usually because it is quitting or its queue is full.
  kFailed,       ///< Terminal: task callback threw an exception while executing.
};

/**
 * @enum TaskOverflowPolicy
 * @brief Per-post override for how a bounded dispatcher queue handles a full-queue condition.
 *
 * @details
 * The default value @c kUseDispatcherStrategy inherits whatever overflow
 * behaviour the owning @c MessageLoop or @c ThreadPool was configured with.
 * The other values let an individual caller force @c kReject or @c kBlock
 * semantics for one submission.
 */
enum class TaskOverflowPolicy : uint8_t {
  kUseDispatcherStrategy = 0,  ///< Follow the owning dispatcher's configured strategy.
  kReject,                     ///< Return a rejected handle immediately if the queue is full.
  kBlock,                      ///< Wait until queue capacity is available or the dispatcher quits.
};

/**
 * @enum TaskDropPolicy
 * @brief Controls whether an accepted task may be discarded by overflow handling.
 *
 * @details
 * Even after a task has been admitted to the queue, drop-oldest overflow paths
 * may select older queued tasks for discard when new work arrives.  Marking a
 * task @c kProtected opts it out of those drop paths.
 */
enum class TaskDropPolicy : uint8_t {
  kDroppable = 0,  ///< The task may be selected for drop when an older task must be discarded.
  kProtected,      ///< The task must not be selected for drop after it has been queued.  (Lock-free dispatchers do not
                   ///< honour this; see @c MessageLoop::post_task documentation.)
};

/**
 * @struct PostTaskOptions
 * @brief Optional controls passed to tracked task-submission APIs.
 *
 * @details
 * All fields have safe defaults so a default-constructed @c PostTaskOptions
 * matches the dispatcher's default behaviour for an unprotected, non-cancellable
 * task.
 */
struct VLINK_EXPORT PostTaskOptions final {
  /**
   * @brief Parent cancellation token that may abort the submitted task.
   *
   * @details
   * The token may be empty (the default), in which case only the handle's own
   * @c cancellation_token() can request cancellation.  If a non-empty parent
   * token is already cancelled at submission time, the returned handle is
   * immediately marked @c TaskExecutionState::kCancelled.  If the parent token
   * is cancelled later while the task is still queued, the queued task is
   * skipped when dequeued.
   */
  CancellationToken cancellation_token;

  /**
   * @brief Queue-full behaviour for this single post.
   *
   * @details
   * Defaults to @c TaskOverflowPolicy::kUseDispatcherStrategy so the dispatcher's
   * configured policy applies.
   */
  TaskOverflowPolicy overflow_policy{TaskOverflowPolicy::kUseDispatcherStrategy};

  /**
   * @brief Whether this task may be discarded by overflow drop paths.
   *
   * @details
   * Defaults to @c TaskDropPolicy::kDroppable.  See @c TaskDropPolicy for the
   * caveat about lock-free dispatchers.
   */
  TaskDropPolicy drop_policy{TaskDropPolicy::kDroppable};
};

/**
 * @class TaskHandle
 * @brief Shared observable handle for a task posted to @c MessageLoop or @c ThreadPool.
 *
 * @details
 * @c TaskHandle is a lightweight value type backed by a @c std::shared_ptr to
 * an internal state block; copies and moves all refer to the same task.  The
 * handle exposes cooperative cancellation, terminal-state waiting, and a task-
 * local @c CancellationToken that running callbacks can poll.  Destroying every
 * handle does @b not cancel the task -- the dispatcher owns the work for the
 * duration of its lifecycle.
 */
class VLINK_EXPORT TaskHandle final {
 public:
  /**
   * @brief Sentinel timeout value for @c wait() meaning "wait indefinitely".
   */
  static constexpr int kInfinite{-1};

  /**
   * @brief Constructs an invalid handle that does not reference any task.
   *
   * @details
   * @c valid() returns @c false and @c state() returns
   * @c TaskExecutionState::kInvalid on the resulting object.
   */
  TaskHandle() noexcept;

  /**
   * @brief Destroys this handle reference.
   *
   * @note Destruction does not cancel the associated task.  The dispatcher
   * retains its own reference to the underlying state.
   */
  ~TaskHandle();

  /**
   * @brief Copy-constructs a handle that shares state with @p other.
   */
  TaskHandle(const TaskHandle&) noexcept;

  /**
   * @brief Copy-assigns from another handle; both handles then share state.
   *
   * @return Reference to @c *this.
   */
  TaskHandle& operator=(const TaskHandle&) noexcept;

  /**
   * @brief Move-constructs a handle, transferring the shared state from the source.
   */
  TaskHandle(TaskHandle&&) noexcept;

  /**
   * @brief Move-assigns from another handle, transferring its shared state.
   *
   * @return Reference to @c *this.
   */
  TaskHandle& operator=(TaskHandle&&) noexcept;

  /**
   * @brief Reports whether this handle references a live task state.
   *
   * @return @c true if the handle is associated with an internal state block;
   *         @c false if it was default-constructed or moved-from.
   */
  [[nodiscard]] bool valid() const noexcept;

  /**
   * @brief Returns the current lifecycle state of the task.
   *
   * @return The current @c TaskExecutionState; @c TaskExecutionState::kInvalid
   *         if the handle is not valid.
   */
  [[nodiscard]] TaskExecutionState state() const noexcept;

  /**
   * @brief Reports whether the task has reached a terminal state.
   *
   * @return @c true once @c state() is one of @c kCompleted, @c kCancelled,
   *         @c kDropped, @c kRejected or @c kFailed.
   */
  [[nodiscard]] bool is_done() const noexcept;

  /**
   * @brief Returns the task-local cancellation token associated with this handle.
   *
   * @details
   * Task callbacks can poll this token if they need cooperative cancellation
   * after execution has already started.  Callbacks added via this token fire
   * on the thread that requests cancellation, outside any @c TaskHandle mutex,
   * so they may safely call back into @c TaskHandle methods.
   *
   * @return The associated @c CancellationToken; an empty token if the handle
   *         is not valid.
   */
  [[nodiscard]] CancellationToken cancellation_token() const noexcept;

  /**
   * @brief Requests cooperative cancellation of the task.
   *
   * @details
   * If the handle is invalid or already in a terminal state, the call is a
   * no-op and returns @c false.  Otherwise, when the task has not yet started
   * running (state @c kInvalid or @c kQueued), this call transitions the state
   * to @c TaskExecutionState::kCancelled; in all non-terminal cases it also
   * flips the underlying cancellation source so that already-running callbacks
   * polling @c cancellation_token() observe the request.
   *
   * @return @c true if this call either transitioned the state or flipped the
   *         underlying cancellation source; @c false if the handle is invalid
   *         or was already in a terminal state at the start of the call.
   */
  bool cancel() const;

  /**
   * @brief Blocks until the task reaches a terminal state or the timeout elapses.
   *
   * @details
   * @c wait() never throws on cancellation; the caller should inspect
   * @c state() after the call to distinguish @c kCompleted from @c kCancelled,
   * @c kDropped, @c kRejected or @c kFailed.
   *
   * @param timeout_ms  Maximum time to wait in milliseconds, or @c kInfinite to
   *                    wait without a deadline.
   * @return @c true if a terminal state was reached before the timeout;
   *         @c false if the timeout expired first or if the handle is invalid.
   */
  bool wait(int timeout_ms = kInfinite) const;

 private:
  struct State;

  explicit TaskHandle(std::shared_ptr<State> state) noexcept;

  static TaskHandle make_task_handle(const CancellationToken& parent_token = {});

  static MoveFunction<void()> make_tracked_task(TaskHandle handle, MoveFunction<void()>&& callback);

  static void mark_task_queued(const TaskHandle& handle);

  static void mark_task_rejected(const TaskHandle& handle);

  static bool begin_task_execution(const TaskHandle& handle);

  static void complete_task_execution(const TaskHandle& handle);

  static void fail_task_execution(const TaskHandle& handle);

  static void drop_task_if_queued(const TaskHandle& handle);

  static bool request_cancel(std::shared_ptr<State> state);

  std::shared_ptr<State> state_;

  friend class MessageLoop;
  friend class ThreadPool;
  friend class detail::TrackedTask;
};

}  // namespace vlink
