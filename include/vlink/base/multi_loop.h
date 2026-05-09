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
 * @file multi_loop.h
 * @brief Multi-threaded event loop backed by a pool of worker threads sharing one task queue.
 *
 * @details
 * @c MultiLoop extends @c MessageLoop with an internal @c ThreadPool of @p thread_num workers.
 * The base @c MessageLoop dispatcher thread continues to dequeue tasks from the loop's queue,
 * but instead of running each task inline, it forwards the task to the internal @c ThreadPool
 * for execution on one of the worker threads.  This enables parallel task execution without
 * changing the posting API: callers still call @c post_task() and @c exec_task() exactly as
 * with a single-threaded @c MessageLoop.
 *
 * Differences from @c MessageLoop:
 * - Tasks may run concurrently on multiple worker threads; they are @b not serialised.
 * - @c is_in_same_thread() returns @c true if the caller is one of the internal pool's worker
 *   threads (not the dispatcher thread).
 * - @c on_begin() / @c on_end() (overrides) are still invoked once on the dispatcher thread;
 *   they are used to construct and tear down the internal @c ThreadPool.
 * - The @c on_task_changed() override runs on the dispatcher thread and posts the task to
 *   the pool; the base @c MessageLoop::on_task_changed() then runs on the executing worker.
 *
 * @note
 * - Shared state accessed inside task callbacks must be protected by its own synchronisation.
 * - Timers attached to a @c MultiLoop fire as queue tasks on the dispatcher and execute on a
 *   pool worker (non-deterministic which worker).
 * - The destructor is defaulted. Stop the loop before destruction with @c quit()
 *   and @c wait_for_quit(), or destroy it only after the dispatcher has already
 *   exited and @c on_end() has shut down the internal pool.
 *
 * @par Example
 * @code
 * vlink::MultiLoop loop(4);  // 4 worker threads
 * loop.async_run();
 *
 * for (int i = 0; i < 100; ++i) {
 *   loop.post_task([i]() { process(i); });
 * }
 *
 * loop.wait_for_idle();
 * loop.quit();
 * loop.wait_for_quit();
 * @endcode
 */

#pragma once

#include <memory>

#include "./message_loop.h"

namespace vlink {

/**
 * @class MultiLoop
 * @brief Multi-threaded variant of @c MessageLoop running tasks on a worker-thread pool.
 *
 * @details
 * Tasks posted to the @c MultiLoop go through the inherited @c MessageLoop queue and are
 * forwarded to an internal @c ThreadPool for execution.  Because pool workers run concurrently,
 * task @b execution order is @b not guaranteed even though dequeueing is FIFO.
 */
class VLINK_EXPORT MultiLoop : public MessageLoop {
 public:
  /**
   * @brief Constructs a @c MultiLoop with the default @c kNormalType queue and @p thread_num workers.
   *
   * @param thread_num  Number of worker threads.  Default: 4.
   */
  explicit MultiLoop(size_t thread_num = 4U);

  /**
   * @brief Constructs a @c MultiLoop with a specific queue type.
   *
   * @param thread_num  Number of worker threads.
   * @param type        Queue implementation type (see @c MessageLoop::Type).
   */
  explicit MultiLoop(size_t thread_num, Type type);

  /**
   * @brief Destructor.  Defaulted; callers must stop the loop before destruction.
   *
   * @warning
   * @c MessageLoop's destructor can request an emergency quit and join the
   * dispatcher thread, but it runs after the @c MultiLoop sub-object and its
   * members have been destroyed.  Do not rely on that path to call
   * @c MultiLoop::on_end() or to tear down the internal @c ThreadPool.
   * Always call @c quit() and @c wait_for_quit() before destruction, or destroy
   * the loop only after it has exited on its own.
   */
  ~MultiLoop() override;

  /**
   * @brief Returns @c true if the calling thread is one of the worker threads.
   *
   * @return @c true if called from any thread owned by this @c MultiLoop.
   */
  [[nodiscard]] bool is_in_same_thread() const override;

 protected:
  /**
   * @brief Called once on the dispatcher thread when the loop starts; constructs the
   *        internal @c ThreadPool.
   */
  void on_begin() override;

  /**
   * @brief Called once on the dispatcher thread when the loop exits; shuts down the
   *        internal @c ThreadPool and joins all worker threads.
   */
  void on_end() override;

  /**
   * @brief Called on the dispatcher thread before a task is forwarded to the worker pool.
   *
   * @details
   * Forwards @p callback (and @p start_time) onto the internal @c ThreadPool, where
   * the base @c MessageLoop::on_task_changed() will subsequently run on the executing worker.
   *
   * @param callback    The task about to be executed.
   * @param start_time  Millisecond steady_clock timestamp captured when the task was
   *                    enqueued (0 if elapsed-time tracking is disabled).
   */
  void on_task_changed(Callback&& callback, uint32_t start_time) override;

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;

  VLINK_DISALLOW_COPY_AND_ASSIGN(MultiLoop)
};

}  // namespace vlink
