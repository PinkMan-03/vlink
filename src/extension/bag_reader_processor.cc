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
  BagReaderProcessor::OutputCallback output_callback;

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

  if (impl_->thread.joinable()) {
    impl_->thread.join();
  }
}

void BagReaderProcessor::register_output_callback(OutputCallback&& output_callback) {
  impl_->output_callback = std::move(output_callback);
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

  impl_->data_queue.emplace_back(std::move(bag_data));

  impl_->cv.notify_one();
}

bool BagReaderProcessor::on_check() {
  if (!impl_->data_queue.empty()) {
    return impl_->data_queue.back().timestamp - impl_->data_queue.front().timestamp >=
           impl_->config.min_cache_time * 1000;
  }

  return false;
}

void BagReaderProcessor::on_output(std::unique_lock<std::mutex>& lock) {
  if VUNLIKELY (!impl_->output_callback) {
    return;
  }

  int64_t output_start_time = ElapsedTimer::get_sys_timestamp(ElapsedTimer::kMicro);
  int64_t first_push_time = 0;

  bool is_first = true;

  while (!impl_->data_queue.empty()) {
    auto bag_data = std::move(impl_->data_queue.front());
    impl_->data_queue.pop_front();

    impl_->current_size -= bag_data.bytes.size();

    if (is_first) {
      first_push_time = bag_data.push_call_time;
      is_first = false;
    } else {
      int64_t expected_output_time = output_start_time + (bag_data.push_call_time - first_push_time);

      int64_t current_time = ElapsedTimer::get_sys_timestamp(ElapsedTimer::kMicro);
      int64_t wait_time = expected_output_time - current_time;

      if (wait_time > 0) {
        impl_->wait_cv.wait_for(lock, std::chrono::microseconds(wait_time),
                                [this]() -> bool { return impl_->quit_flag.load(); });

        if VUNLIKELY (impl_->quit_flag) {
          return;
        }
      }
    }

    impl_->output_callback(bag_data.timestamp, bag_data.url, bag_data.action_type, bag_data.bytes);

    if VUNLIKELY (impl_->quit_flag) {
      return;
    }
  }
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
      return;
    }
  }

  on_output(lock);

  if VLIKELY (!at_end) {
    impl_->cv.notify_all();
  }
}

}  // namespace vlink
