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
 * | Type              | Queue implementation           | Notes                           |
 * | ----------------- | ------------------------------ | ------------------------------- |
 * | @c kNormalType    | Mutex-protected std::queue     | Default                         |
 * | @c kLockfreeType  | MpmcQueue (lock-free MPMC)     | Lower overhead under contention |
 *
 * Idle strategies (same semantics as @c MessageLoop::Strategy).
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

#include <future>
#include <memory>
#include <string>
#include <utility>

#include "./functional.h"
#include "./macros.h"

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
   */
  using Callback = vlink::Function<void()>;

  /**
   * @brief Queue implementation type.
   */
  enum Type : uint8_t {
    kNormalType = 0,    ///< Mutex-protected FIFO queue (default)
    kLockfreeType = 1,  ///< Lock-free MPMC queue
  };

  /**
   * @brief Idle strategy controlling CPU and wake-up latency trade-offs.
   */
  enum Strategy : uint8_t {
    kOptimizationStrategy = 0,  ///< Balance: when full, retry; after 10 retries drop oldest and push.
    kPopStrategy = 1,           ///< When full, drop oldest immediately and push the new task.
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
   * @brief Destructor.  Calls @c shutdown() and joins all worker threads.
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
   * @brief Returns the current idle strategy.
   *
   * @return Current strategy.
   */
  [[nodiscard]] Strategy get_strategy() const;

  /**
   * @brief Changes the idle strategy.
   *
   * @param strategy  New strategy.
   */
  void set_strategy(Strategy strategy);

  /**
   * @brief Signals all workers to finish current tasks and exit.
   *
   * @details
   * Waits for all worker threads to join before returning.
   * After @c shutdown(), the pool can no longer accept tasks.
   *
   * @return @c true on success.
   */
  bool shutdown();

  /**
   * @brief Posts a task to the queue for execution by a worker thread.
   *
   * @details
   * Thread-safe.  Returns @c false only if the pool has been shut down (@c shutdown()
   * called).  Behaviour when the queue is full depends on the configured @c Strategy:
   *
   * - @c kOptimizationStrategy: retry up to 10 times with 1 ms sleep between attempts;
   *   after that, drop the oldest entry and push the new task (still returns @c true).
   * - @c kPopStrategy: drop the oldest entry immediately and push the new task.
   * - @c kBlockStrategy: retry indefinitely with 1 ms sleep until space is available.
   *
   * In none of these paths does the function return @c false because the queue is full.
   *
   * @param callback  Task to execute.
   * @return @c true if enqueued (possibly after dropping an older task or blocking).
   *         @c false if the pool has been shut down.
   */
  bool post_task(Callback&& callback);

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

  struct Impl;
  std::unique_ptr<Impl> impl_;

  VLINK_DISALLOW_COPY_AND_ASSIGN(ThreadPool)
};

////////////////////////////////////////////////////////////////
/// Details
////////////////////////////////////////////////////////////////

template <class FunctionT, class... ArgsT, typename ResultT>
inline std::future<ResultT> ThreadPool::invoke_task(FunctionT&& function, ArgsT&&... args) {
  auto task = std::make_shared<std::packaged_task<ResultT()>>(
      std::bind(std::forward<FunctionT>(function), std::forward<ArgsT>(args)...));  // NOLINT(modernize-avoid-bind)

  std::future<ResultT> res = task->get_future();

  std::invoke(&ThreadPool::post_task, this, [task]() { (*task.get())(); });

  return res;
}

}  // namespace vlink
