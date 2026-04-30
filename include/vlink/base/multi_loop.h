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
 * @c MultiLoop extends @c MessageLoop by running @p thread_num worker threads that all
 * dequeue and execute tasks from the same queue.  This enables parallel task execution
 * without changing the posting API: callers still call @c post_task() and @c exec_task()
 * exactly as with a single-threaded @c MessageLoop.
 *
 * Differences from @c MessageLoop:
 * - Tasks may run concurrently on multiple threads; they are @b not serialised.
 * - @c is_in_same_thread() returns @c true if the caller is any of the worker threads.
 * - @c on_begin() and @c on_end() are called once per worker thread.
 * - @c on_task_changed() is called from the executing worker thread.
 *
 * @note
 * - Shared state accessed inside task callbacks must be protected by its own synchronisation.
 * - Timers attached to a @c MultiLoop still fire on one of the worker threads (non-deterministic).
 * - The destructor waits for all worker threads to finish.
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
 * All threads share the same task queue.  Tasks are @b not guaranteed to execute in order.
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
   * @brief Destructor.  Quits the loop and joins all worker threads.
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
   * @brief Called once on each worker thread immediately after it starts.
   *
   * @details
   * Override to perform per-thread initialisation (e.g., setting thread name or affinity).
   */
  void on_begin() override;

  /**
   * @brief Called once on each worker thread just before it exits.
   *
   * @details
   * Override to perform per-thread cleanup.
   */
  void on_end() override;

  /**
   * @brief Called on the worker thread executing the task, before the task runs.
   *
   * @param callback    The task about to be executed.
   * @param start_time  Millisecond timestamp at which the task was dequeued.
   */
  void on_task_changed(Callback&& callback, uint32_t start_time) override;

 private:
  std::unique_ptr<struct MultiLoopImpl> impl_;

  VLINK_DISALLOW_COPY_AND_ASSIGN(MultiLoop)
};

}  // namespace vlink
