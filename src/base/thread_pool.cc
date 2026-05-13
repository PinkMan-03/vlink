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

#include "./base/thread_pool.h"

#include <algorithm>
#include <deque>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "./base/condition_variable.h"
#include "./base/logger.h"
#include "./base/memory_resource.h"
#include "./base/mpmc_queue.h"

namespace vlink {

static constexpr size_t kMaxTaskSize = 10000U;

// ThreadPoolGlobal
struct ThreadPoolGlobal final {
  std::atomic<int> instance_index{0};

  static ThreadPoolGlobal& get() {
    static ThreadPoolGlobal instance;

    return instance;
  }

 private:
  ThreadPoolGlobal() = default;
};

// ThreadPool::Impl
struct ThreadPool::Impl final {  // NOLINT(clang-analyzer-optin.performance.Padding)
#ifdef VLINK_ENABLE_BASE_MEMORY_RESOURCE
  using NormalQueue = std::pmr::deque<ThreadPool::Callback>;
  using LockfreeQueue = MpmcQueue<ThreadPool::Callback>;
#else
  using NormalQueue = std::deque<ThreadPool::Callback>;
  using LockfreeQueue = MpmcQueue<ThreadPool::Callback>;
#endif

  alignas(64) std::atomic_bool quit_flag{false};

  std::string name;
  size_t thread_count{0};
  ThreadPool::Type type{ThreadPool::kNormalType};
  ThreadPool::Strategy strategy{ThreadPool::kOptimizationStrategy};
  std::vector<std::thread> threads;

  std::optional<NormalQueue> normal_queue;
  std::optional<LockfreeQueue> lockfree_queue;

  ConditionVariable cv;
  std::mutex mtx;
};

// ThreadPool
ThreadPool::ThreadPool(size_t thread_count) : impl_(std::make_unique<Impl>()) {
  impl_->name = "ThreadPool_" + std::to_string(ThreadPoolGlobal::get().instance_index++);
  impl_->thread_count = thread_count;

  MemoryPool::global_instance();

  init();
}

ThreadPool::ThreadPool(size_t thread_count, Type type) : impl_(std::make_unique<Impl>()) {
  impl_->name = "ThreadPool_" + std::to_string(ThreadPoolGlobal::get().instance_index++);
  impl_->thread_count = thread_count;
  impl_->type = type;

  MemoryPool::global_instance();

  init();
}

ThreadPool::~ThreadPool() { shutdown(); }

void ThreadPool::set_name(const std::string& name) { impl_->name = name; }

const std::string& ThreadPool::get_name() const { return impl_->name; }

ThreadPool::Type ThreadPool::get_type() const { return impl_->type; }

ThreadPool::Strategy ThreadPool::get_strategy() const { return impl_->strategy; }

void ThreadPool::set_strategy(Strategy strategy) { impl_->strategy = strategy; }

bool ThreadPool::post_task(Callback&& callback) {
  if VUNLIKELY (impl_->quit_flag) {
    return false;
  }

  bool is_full = false;
  int retry_cnt = 0;

  if (impl_->type == kNormalType) {
    do {
      {
        std::lock_guard lock(impl_->mtx);
        if VUNLIKELY (impl_->quit_flag) {
          return false;
        }

        is_full = impl_->normal_queue->size() >= get_max_task_count();

        if VLIKELY (!is_full) {
          impl_->normal_queue->emplace_back(std::move(callback));
        } else if (impl_->strategy == kPopStrategy) {
          impl_->normal_queue->pop_front();
          impl_->normal_queue->emplace_back(std::move(callback));
          is_full = false;
          break;
        }
      }

      if VUNLIKELY (is_full) {
        if (impl_->strategy == kOptimizationStrategy) {
          if (++retry_cnt > 10) {
            {
              std::lock_guard lock(impl_->mtx);
              if VUNLIKELY (impl_->quit_flag) {
                return false;
              }

              if VLIKELY (!impl_->normal_queue->empty()) {
                impl_->normal_queue->pop_front();
              }

              impl_->normal_queue->emplace_back(std::move(callback));
              is_full = false;
            }

            CLOG_W("ThreadPool: Task is full, removed top data (%s).", impl_->name.c_str());
            break;
          }
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(1));
      }
    } while (is_full);
  } else if (impl_->type == kLockfreeType) {
    do {
      {
        std::lock_guard lock(impl_->mtx);
        if VUNLIKELY (impl_->quit_flag) {
          return false;
        }

        is_full = impl_->lockfree_queue->size() >= get_max_task_count();

        if VLIKELY (!is_full) {
          impl_->lockfree_queue->emplace(std::move(callback));
        } else if (impl_->strategy == kPopStrategy) {
          Callback temp_task;
          bool ret = impl_->lockfree_queue->try_pop(temp_task);
          (void)temp_task;
          (void)ret;
          impl_->lockfree_queue->emplace(std::move(callback));
          is_full = false;
          break;
        }
      }

      if VUNLIKELY (is_full) {
        if (impl_->strategy == kOptimizationStrategy) {
          if (++retry_cnt > 10) {
            {
              std::lock_guard lock(impl_->mtx);
              if VUNLIKELY (impl_->quit_flag) {
                return false;
              }

              Callback temp_task;
              bool ret = impl_->lockfree_queue->try_pop(temp_task);
              (void)temp_task;
              (void)ret;
              impl_->lockfree_queue->emplace(std::move(callback));
              is_full = false;
            }

            CLOG_W("ThreadPool: Task is full, removed top data (%s).", impl_->name.c_str());
            break;
          }
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(1));
      }
    } while (is_full);
  }

  impl_->cv.notify_one();

  return !is_full;
}

size_t ThreadPool::get_task_count() const {
  if (impl_->type == kNormalType) {
    std::lock_guard lock(impl_->mtx);
    return impl_->normal_queue->size();
  } else if (impl_->type == kLockfreeType) {
    return impl_->lockfree_queue->size();
  } else {
    return 0U;
  }
}

bool ThreadPool::is_in_work_thread() const {
  return std::any_of(impl_->threads.begin(), impl_->threads.end(),
                     [](const std::thread& thread) -> bool { return thread.get_id() == std::this_thread::get_id(); });
}

size_t ThreadPool::get_max_task_count() const { return kMaxTaskSize; }

bool ThreadPool::shutdown() {
  {
    std::lock_guard lock(impl_->mtx);
    if VUNLIKELY (impl_->quit_flag) {
      return false;
    }

    impl_->quit_flag = true;
  }

  impl_->cv.notify_all();

  const auto self_id = std::this_thread::get_id();

  for (auto& thread : impl_->threads) {
    if (thread.joinable()) {
      if (thread.get_id() == self_id) {
        thread.detach();
      } else {
        thread.join();
      }
    }
  }

  return true;
}

void ThreadPool::init() {
  if (impl_->type == kNormalType) {
    impl_->normal_queue.emplace();
  } else if (impl_->type == kLockfreeType) {
    impl_->lockfree_queue.emplace(get_max_task_count());  // NOLINT(clang-analyzer-optin.cplusplus.VirtualCall)
  }

  if (impl_->thread_count == 0) {
    VLOG_E("ThreadPool: Thread count is zero.");
    impl_->quit_flag = true;
    return;
  }

  impl_->threads.reserve(impl_->thread_count);

  for (size_t i = 0; i < impl_->thread_count; ++i) {
    if (impl_->type == kNormalType) {
      std::thread thread([this] {
        for (;;) {
          Callback task;

          {
            std::unique_lock lock(impl_->mtx);
            impl_->cv.wait(lock, [this] { return !impl_->normal_queue->empty() || impl_->quit_flag; });

            if VUNLIKELY (impl_->normal_queue->empty() && impl_->quit_flag) {
              break;
            }

            task = std::move(impl_->normal_queue->front());

            impl_->normal_queue->pop_front();
          }

          if VLIKELY (task) {
            task();
          }
        }
      });

      impl_->threads.emplace_back(std::move(thread));
    } else if (impl_->type == kLockfreeType) {
      std::thread thread([this] {
        for (;;) {
          Callback task;

          if (impl_->lockfree_queue->try_pop(task)) {
            if VLIKELY (task) {
              task();
            }

            continue;
          }

          std::unique_lock lock(impl_->mtx);
          impl_->cv.wait(lock, [this] { return !impl_->lockfree_queue->empty() || impl_->quit_flag; });

          if VUNLIKELY (impl_->lockfree_queue->empty() && impl_->quit_flag) {
            break;
          }
        }
      });

      impl_->threads.emplace_back(std::move(thread));
    }
  }
}

}  // namespace vlink
