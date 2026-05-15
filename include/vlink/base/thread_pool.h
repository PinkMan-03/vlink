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
 * @file thread_pool.h
 * @brief General-purpose thread pool for parallel task execution.
 *
 * @details
 * @c ThreadPool maintains a fixed number of worker threads that dequeue and execute
 * tasks posted via @c post_task() or @c invoke_task().  Unlike @c MessageLoop, there
 * is no timer support or loop lifecycle; the pool is started on construction and shut
 * down with @c shutdown().
 *
 * Queue types:
 *
 * | Type              | Queue implementation                                  | Notes                            |
 * | ----------------- | ----------------------------------------------------- | -------------------------------- |
 * | @c kNormalType    | Mutex-protected @c std::deque (or @c std::pmr::deque) | Default; supports droppable scan |
 * | @c kLockfreeType  | @c MpmcQueue (lock-free MPMC)                         | Lower overhead under contention  |
 *
 * Push-side back-pressure strategies for handling a full queue (same semantics as
 * @c MessageLoop::Strategy).  These only affect @c post_task / @c invoke_task when
 * the queue is at capacity; idle worker wake-up is unconditionally driven by a
 * condition variable.
 *
 * @note
 * - Tasks may execute concurrently; shared state must be protected externally.
 * - @c invoke_task() returns a @c std::future.  Blocking on the future from a thread
 *   pool worker will deadlock if all workers are busy.
 * - @c is_in_work_thread() can be used to detect reentrant calls.
 *
 * @par Example
 * @code
 * vlink::ThreadPool pool(8);
 * pool.post_task([] { heavy_work(); });
 *
 * auto fut = pool.invoke_task([]() -> int { return compute(); });
 * int result = fut.get();
 *
 * pool.shutdown();
 * @endcode
 */

#pragma once

#include <functional>
#include <future>
#include <memory>
#include <string>
#include <type_traits>
#include <utility>

#include "./functional.h"
#include "./macros.h"
#include "./memory_resource.h"
#include "./task_handle.h"

namespace vlink {

/**
 * @class ThreadPool
 * @brief Fixed-size thread pool for parallel task execution.
 *
 * @details
 * Worker threads are created on construction and destroyed on @c shutdown() or destruction.
 */
class VLINK_EXPORT ThreadPool {
 public:
  /**
   * @brief Callback type for tasks submitted to the pool.
   *
   * @details
   * Move-only (`MoveFunction<void()>`); see @c MessageLoop::Callback for
   * the rationale.
   */
  using Callback = MoveFunction<void()>;

  /**
   * @brief Queue implementation type.
   */
  enum Type : uint8_t {
    kNormalType = 0,    ///< Mutex-protected FIFO queue (default)
    kLockfreeType = 1,  ///< Lock-free MPMC queue
  };

  /**
   * @brief Push-side back-pressure strategy applied when the bounded queue is full.
   *
   * @details
   * Worker wake-up is unconditionally driven by a condition variable; this enum
   * only affects how @c post_task / @c invoke_task react when the queue has
   * reached its capacity.
   */
  enum Strategy : uint8_t {
    kOptimizationStrategy = 0,  ///< Balance: when full, retry; after 10 retries drop one eligible task and push.
    kPopStrategy = 1,           ///< When full, drop one eligible task immediately and push the new task.
    kBlockStrategy = 2,         ///< When full, retry indefinitely with 1 ms sleep between attempts.
  };

  /**
   * @brief Constructs a @c ThreadPool with @p thread_count worker threads and @c kNormalType queue.
   *
   * @param thread_count  Number of worker threads.  Default: 4.
   */
  explicit ThreadPool(size_t thread_count = 4U);

  /**
   * @brief Constructs a @c ThreadPool with the specified thread count and queue type.
   *
   * @param thread_count  Number of worker threads.
   * @param type          Queue implementation type.
   */
  explicit ThreadPool(size_t thread_count, Type type);

  /**
   * @brief Destructor.  Calls @c shutdown() and releases worker threads.
   */
  virtual ~ThreadPool();

  /**
   * @brief Sets a human-readable name for the pool (and its worker threads).
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
   * @brief Returns the queue type this pool was constructed with.
   *
   * @return Queue type.
   */
  [[nodiscard]] Type get_type() const;

  /**
   * @brief Returns the current back-pressure strategy.
   *
   * @return Current strategy.
   */
  [[nodiscard]] Strategy get_strategy() const;

  /**
   * @brief Changes the back-pressure strategy.
   *
   * @param strategy  New strategy.
   */
  void set_strategy(Strategy strategy);

  /**
   * @brief Signals all workers to finish current tasks and exit.
   *
   * @details
   * Marks the pool as quitting (rejecting further posts) and joins all worker
   * threads before returning.
   *
   * @c shutdown() may itself be called from one of this pool's worker threads
   * — for example, from a task that decides to tear the pool down.  In that
   * case the calling worker cannot join itself, so its thread handle is
   * detached instead of joined; the worker is allowed to return naturally
   * from its current task and the underlying OS thread completes on its own.
   * The shared @c Impl block is owned through a @c std::shared_ptr, so the
   * detached worker continues to see a valid pool state until it exits.
   *
   * After @c shutdown() returns, the pool can no longer accept tasks; the
   * first @c shutdown() call returns @c true and subsequent calls return
   * @c false.
   *
   * @return @c true on the first successful shutdown; @c false if the pool
   *         was already shut down.
   */
  bool shutdown();

  /**
   * @brief Posts a task to the queue for execution by a worker thread.
   *
   * @details
   * Thread-safe.  Returns @c false if the pool has been shut down (via
   * @c shutdown()) or if overflow handling cannot make room for the new task.
   * Behaviour when the queue is full depends on the configured @c Strategy:
   *
   * - @c kOptimizationStrategy: retry up to 10 times with 1 ms sleep between
   *   attempts; after that, drop one eligible task and push the new task.
   * - @c kPopStrategy: drop one eligible task immediately and push the new task.
   * - @c kBlockStrategy: retry indefinitely with 1 ms sleep until space is available.
   *
   * @note Drop-policy semantics:
   *  - @c kNormalType queues honour @c TaskDropPolicy::kProtected on a per-task
   *    basis: protected tasks are never selected as overflow drop victims, and
   *    if every queued task is protected the post fails and returns @c false.
   *  - @c kLockfreeType queues do not track per-task drop policy; overflow drop
   *    simply removes one queued task regardless of how it was submitted.
   *
   * @param callback  Task to execute.
   * @return @c true if eventually enqueued (possibly after dropping an eligible
   *         task or blocking); @c false if the pool has been shut down or no
   *         eligible task could be dropped to make room.
   */
  bool post_task(Callback&& callback);

  /**
   * @brief Posts a tracked task to the queue and returns a @c TaskHandle.
   *
   * @details
   * Tracked counterpart of @c post_task().  The returned @c TaskHandle can be
   * used to wait for completion, request cooperative cancellation through the
   * task's @c CancellationToken, and observe whether the task was eventually
   * rejected (pool shut down, no droppable victim available) or dropped
   * before execution.
   *
   * @param callback  Task to execute.
   * @param options   Optional overflow, drop and cancellation policy.  See
   *                  @c PostTaskOptions for defaults.
   * @return Handle associated with the posted task.  The handle remains valid
   *         even if the post is rejected — query its state to find out.
   */
  [[nodiscard]] TaskHandle post_task_handle(Callback&& callback, const PostTaskOptions& options = {});

  /**
   * @brief Returns the number of tasks currently in the queue.
   *
   * @return Pending task count.
   */
  [[nodiscard]] size_t get_task_count() const;

  /**
   * @brief Returns @c true if the calling thread is a worker thread of this pool.
   *
   * @return @c true if called from a pool worker.
   */
  [[nodiscard]] bool is_in_work_thread() const;

  /**
   * @brief Returns the maximum queue depth.
   *
   * @return Maximum number of tasks that can be queued simultaneously.
   */
  [[nodiscard]] virtual size_t get_max_task_count() const;

  /**
   * @brief Dispatches a callable to a worker thread and returns a @c std::future for the result.
   *
   * @details
   * Thread-safe.  The future becomes ready after the callable is executed by a worker.
   *
   * @warning Do not block on the future from a pool worker thread if all workers are busy;
   *          this will deadlock.
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

 private:
  void init();

  bool push_task(Callback&& callback, bool droppable,
                 TaskOverflowPolicy overflow_policy = TaskOverflowPolicy::kUseDispatcherStrategy,
                 const TaskHandle* submit_handle = nullptr);

  bool drop_one_normal_task();

  bool drop_one_lockfree_task(bool keep_reserved = false);

  bool reserve_lockfree_task(bool* was_empty = nullptr);

  void release_lockfree_task();

  bool push_lockfree_task(Callback&& callback);

  struct Impl;
  std::shared_ptr<Impl> impl_;

  VLINK_DISALLOW_COPY_AND_ASSIGN(ThreadPool)
};

////////////////////////////////////////////////////////////////
/// Details
////////////////////////////////////////////////////////////////

template <class FunctionT, class... ArgsT, typename ResultT>
inline std::future<ResultT> ThreadPool::invoke_task(FunctionT&& function, ArgsT&&... args) {
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
    auto task = MemoryResource::make_shared<std::packaged_task<ResultT()>>(std::move(bound));
    auto res = task->get_future();

    if VUNLIKELY (!post_task([task]() mutable { (*task)(); })) {
      task.reset();
    }

    return res;
  }
}

}  // namespace vlink
