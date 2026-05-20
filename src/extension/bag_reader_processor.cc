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

#include "./extension/bag_reader_processor.h"

#include <algorithm>
#include <atomic>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <utility>

#include "./base/condition_variable.h"
#include "./base/elapsed_timer.h"
#include "./base/logger.h"

namespace vlink {

// BagData
struct BagData {
  int64_t timestamp{0};
  int64_t push_call_time{0};
  int64_t push_enqueue_time{0};
  std::string url;
  ActionType action_type{ActionType::kUnknownAction};
  Bytes bytes;
  bool processed{false};
};

// BagReaderProcessor::Impl
struct BagReaderProcessor::Impl final {
  std::atomic_bool quit_flag{false};

  BagReaderProcessor::Config config;
  std::shared_ptr<BagReaderProcessor::OutputCallback> output_callback;

  std::deque<BagData> data_queue;

  int64_t current_size{0};

  std::mutex mtx;
  ConditionVariable cv;
  ConditionVariable wait_cv;
  std::thread thread;
};

// BagReaderProcessor
BagReaderProcessor::BagReaderProcessor(const Config& config) : impl_(std::make_unique<Impl>()) {
  impl_->config = config;

  impl_->thread = std::thread(&BagReaderProcessor::on_run, this);
}

BagReaderProcessor::~BagReaderProcessor() {
  impl_->quit_flag = true;

  impl_->cv.notify_all();
  impl_->wait_cv.notify_all();

  if VLIKELY (impl_->thread.joinable()) {
    impl_->thread.join();
  }
}

void BagReaderProcessor::register_output_callback(OutputCallback&& output_callback) {
  std::lock_guard lock(impl_->mtx);
  impl_->output_callback = std::make_shared<OutputCallback>(std::move(output_callback));
}

void BagReaderProcessor::push(int64_t timestamp, const std::string& url, ActionType action_type, const Bytes& data) {
  int64_t push_call_time = ElapsedTimer::get_sys_timestamp(ElapsedTimer::kMicro);

  std::unique_lock lock(impl_->mtx);

  if VUNLIKELY (impl_->current_size >= impl_->config.max_cache_size) {
    VLOG_W("BagReaderProcessor: Cache size is full, waiting to consume.");
  }

  impl_->cv.wait(lock, [this]() -> bool {
    if (impl_->data_queue.empty()) {
      return true;
    }

    return impl_->current_size < impl_->config.max_cache_size || impl_->quit_flag.load();
  });

  if VUNLIKELY (impl_->quit_flag) {
    return;
  }

  int64_t push_enqueue_time = ElapsedTimer::get_sys_timestamp(ElapsedTimer::kMicro);

  impl_->current_size += data.size();

  BagData bag_data{timestamp, push_call_time, push_enqueue_time, url, action_type, data, false};

  auto iter = std::upper_bound(impl_->data_queue.begin(), impl_->data_queue.end(), bag_data.timestamp,
                               [](int64_t timestamp, const BagData& target) { return timestamp < target.timestamp; });
  impl_->data_queue.emplace(iter, std::move(bag_data));

  impl_->cv.notify_one();
}

bool BagReaderProcessor::on_check() {
  if (impl_->data_queue.empty()) {
    return false;
  }

  if (impl_->current_size >= impl_->config.max_cache_size) {
    return true;
  }

  const int64_t min_cache_time = impl_->config.min_cache_time * 1000;

  if (impl_->data_queue.back().timestamp - impl_->data_queue.front().timestamp >= min_cache_time) {
    return true;
  }

  const int64_t cache_elapsed = static_cast<int64_t>(ElapsedTimer::get_sys_timestamp(ElapsedTimer::kMicro)) -
                                impl_->data_queue.front().push_enqueue_time;

  return cache_elapsed >= min_cache_time;
}

void BagReaderProcessor::on_output(std::unique_lock<std::mutex>& lock, bool at_end) {
  if VUNLIKELY (!impl_->output_callback || !*impl_->output_callback) {
    impl_->data_queue.clear();
    impl_->current_size = 0;
    return;
  }

  if (impl_->data_queue.empty()) {
    return;
  }

  do {
    const int64_t min_cache_time = impl_->config.min_cache_time * 1000;
    const bool flush_all = at_end || impl_->current_size >= impl_->config.max_cache_size;
    bool should_output = flush_all;

    if (!should_output) {
      const int64_t timestamp_span = impl_->data_queue.back().timestamp - impl_->data_queue.front().timestamp;

      if (timestamp_span >= min_cache_time) {
        should_output = impl_->data_queue.front().timestamp <= impl_->data_queue.back().timestamp - min_cache_time;
      } else {
        const int64_t cache_elapsed = static_cast<int64_t>(ElapsedTimer::get_sys_timestamp(ElapsedTimer::kMicro)) -
                                      impl_->data_queue.front().push_enqueue_time;

        should_output = cache_elapsed >= min_cache_time;
      }

      if (!should_output) {
        return;
      }
    }

    auto output_callback = impl_->output_callback;
    auto bag_data = std::move(impl_->data_queue.front());
    impl_->data_queue.pop_front();

    impl_->current_size -= bag_data.bytes.size();

    lock.unlock();

    (*output_callback)(bag_data.timestamp, bag_data.url, bag_data.action_type, bag_data.bytes);

    lock.lock();
  } while (at_end && !impl_->data_queue.empty());
}

void BagReaderProcessor::on_run() {
  while (!impl_->quit_flag) {
    on_exec(false);
  }

  on_exec(true);
}

void BagReaderProcessor::on_exec(bool at_end) {
  std::unique_lock lock(impl_->mtx);

  if VLIKELY (!at_end) {
    impl_->cv.wait(lock, [this]() -> bool { return !impl_->data_queue.empty() || impl_->quit_flag; });

    if VUNLIKELY (impl_->quit_flag) {
      return;
    }
  }

  if VLIKELY (!at_end) {
    if (!on_check()) {
      const int64_t min_cache_time = impl_->config.min_cache_time * 1000;
      const int64_t cache_elapsed = static_cast<int64_t>(ElapsedTimer::get_sys_timestamp(ElapsedTimer::kMicro)) -
                                    impl_->data_queue.front().push_enqueue_time;
      const int64_t wait_time = min_cache_time - cache_elapsed;

      if (wait_time > 0) {
        impl_->cv.wait_for(lock, std::chrono::microseconds(wait_time),
                           [this]() -> bool { return impl_->quit_flag.load() || on_check(); });
      }

      if VUNLIKELY (impl_->quit_flag) {
        return;
      }

      if (!on_check()) {
        return;
      }
    }
  }

  on_output(lock, at_end);

  if VLIKELY (!at_end) {
    impl_->cv.notify_all();
  }
}

}  // namespace vlink
