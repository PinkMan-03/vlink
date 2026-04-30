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

#include "./base/multi_loop.h"

#include <optional>
#include <utility>

#include "./base/logger.h"
#include "./base/thread_pool.h"

namespace vlink {

// MultiLoopGlobal
struct MultiLoopGlobal final {
  std::atomic<int> instance_index{0};

  static MultiLoopGlobal& get() {
    static MultiLoopGlobal instance;

    return instance;
  }

 private:
  MultiLoopGlobal() = default;
};

// MultiLoopImpl
struct MultiLoopImpl final {
  std::optional<ThreadPool> thread_pool;
  size_t thread_num{0};
};

// MultiLoop
MultiLoop::MultiLoop(size_t thread_num) : impl_(std::make_unique<MultiLoopImpl>()) {
  impl_->thread_num = thread_num;

  set_name("MultiLoop_" + std::to_string(MultiLoopGlobal::get().instance_index++));
}

MultiLoop::MultiLoop(size_t thread_num, Type type) : MessageLoop(type), impl_(std::make_unique<MultiLoopImpl>()) {
  impl_->thread_num = thread_num;

  set_name("MultiLoop_" + std::to_string(MultiLoopGlobal::get().instance_index++));

  // if VUNLIKELY (type == MultiLoop::kPriorityType) {
  //   CLOG_F("MultiLoop not support priority type(%s).", get_name().c_str());
  // }
}

MultiLoop::~MultiLoop() = default;

bool MultiLoop::is_in_same_thread() const {
  if VUNLIKELY (!impl_->thread_pool) {
    return false;
  }

  return impl_->thread_pool->is_in_work_thread();
}

void MultiLoop::on_begin() {
  if (get_type() == MultiLoop::kNormalType) {
    impl_->thread_pool.emplace(impl_->thread_num, ThreadPool::kNormalType);
  } else if (get_type() == MultiLoop::kLockfreeType) {
    impl_->thread_pool.emplace(impl_->thread_num, ThreadPool::kLockfreeType);
  } else if (get_type() == MultiLoop::kPriorityType) {
    impl_->thread_pool.emplace(impl_->thread_num, ThreadPool::kNormalType);
  }

  MessageLoop::on_begin();
}

void MultiLoop::on_end() {
  impl_->thread_pool->shutdown();
  impl_->thread_pool.reset();

  MessageLoop::on_end();
}

void MultiLoop::on_task_changed(Callback&& callback, uint32_t start_time) {
  impl_->thread_pool->post_task([this, tmp_callback = std::move(callback), start_time]() mutable {
    MessageLoop::on_task_changed(std::move(tmp_callback), start_time);
  });
}

}  // namespace vlink
