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

#include "./base/wheel_timer.h"

#include <atomic>
#include <limits>
#include <list>
#include <mutex>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

#include "./base/condition_variable.h"
#include "./base/logger.h"

namespace vlink {

// WheelTimer::Impl
struct WheelTimer::Impl final {  // NOLINT(clang-analyzer-optin.performance.Padding)
  // Handler
  struct Handler final {
    WheelTimer::Key key{-1};
    uint32_t remaining_rounds{0};
    WheelTimer::Callback callback;
    uint32_t repeat_interval_ms{0};

    Handler(WheelTimer::Key _key, uint32_t _rounds, WheelTimer::Callback&& _callback, uint32_t _repeat_ms = 0)
        : key(_key), remaining_rounds(_rounds), callback(std::move(_callback)), repeat_interval_ms(_repeat_ms) {}

    Handler(const Handler&) = default;

    Handler(Handler&&) = default;

    Handler& operator=(const Handler&) = default;

    Handler& operator=(Handler&&) = default;
  };

  alignas(64) std::atomic_bool stop_flag{false};
  alignas(64) std::atomic_bool paused_flag{false};
  alignas(64) std::atomic_bool is_running{false};

  std::atomic<uint32_t> catchup_limit{0};
  std::atomic<WheelTimer::Key> next_key{1};

  uint32_t slots{0};
  uint32_t interval_ms{5};
  std::vector<std::list<Handler>> wheels;
  uint32_t current_slot{0};

  std::thread worker_thread;

  std::mutex mtx;
  vlink::condition_variable cv;

  std::unordered_map<WheelTimer::Key, std::pair<uint32_t, std::list<Handler>::iterator>> timer_index;
};

// WheelTimer
WheelTimer::WheelTimer(uint32_t slots, uint32_t interval_ms) : impl_(std::make_unique<Impl>()) {
  if VUNLIKELY (slots == 0 || interval_ms == 0) {
    VLOG_F("WheelTimer: Slots and interval_ms must be greater than 0.");
    return;
  }

  impl_->slots = slots;
  impl_->interval_ms = interval_ms;
  impl_->wheels.resize(slots);
}

WheelTimer::~WheelTimer() { stop(); }

void WheelTimer::start() {
  std::unique_lock lock(impl_->mtx);

  if VUNLIKELY (impl_->is_running) {
    VLOG_W("WheelTimer: Timer is already running.");
    return;
  }

  impl_->stop_flag = false;
  impl_->is_running = true;

  lock.unlock();

  if (impl_->worker_thread.joinable()) {
    impl_->worker_thread.join();
  }

  impl_->worker_thread = std::thread([this]() { this->run(); });
}

void WheelTimer::stop() {
  std::unique_lock lock(impl_->mtx);

  impl_->stop_flag = true;

  lock.unlock();

  wakeup();

  bool called_from_worker =
      impl_->worker_thread.joinable() && impl_->worker_thread.get_id() == std::this_thread::get_id();

  if (impl_->worker_thread.joinable() && !called_from_worker) {
    impl_->worker_thread.join();
  }

  lock.lock();

  if (!called_from_worker) {
    impl_->is_running = false;
    impl_->paused_flag = false;
    impl_->current_slot = 0;
    impl_->wheels = std::vector<std::list<Impl::Handler>>(impl_->slots);
    impl_->timer_index.clear();
  }
}

void WheelTimer::pause() {
  std::lock_guard lock(impl_->mtx);
  impl_->paused_flag = true;
}

void WheelTimer::resume() {
  std::unique_lock lock(impl_->mtx);
  impl_->paused_flag = false;

  lock.unlock();

  wakeup();
}

void WheelTimer::wakeup() { impl_->cv.notify_one(); }

bool WheelTimer::is_running() const { return impl_->is_running; }

WheelTimer::Key WheelTimer::add(uint32_t timeout_ms, Callback&& callback, uint32_t repeat_ms) {
  if VUNLIKELY (timeout_ms == 0) {
    VLOG_E("WheelTimer: Timeout must be greater than 0.");
    return -1;
  }

  if VUNLIKELY (!callback) {
    VLOG_E("WheelTimer: Callback must be non-empty.");
    return -1;
  }

  std::lock_guard lock(impl_->mtx);

  uint32_t interval = impl_->interval_ms;
  uint32_t slots = impl_->slots;
  uint32_t current_slot = impl_->current_slot;

  uint64_t ticks = (static_cast<uint64_t>(timeout_ms) + interval - 1) / interval;

  if VUNLIKELY (ticks == 0) {
    ticks = 1;
  }

  uint64_t max_rounds = std::numeric_limits<uint32_t>::max();
  uint64_t rounds64 = ticks / slots;

  if VUNLIKELY (rounds64 > max_rounds) {
    VLOG_E("WheelTimer: Timeout too large (rounds overflow).");
    return -1;
  }

  auto ticks_mod = static_cast<uint32_t>(ticks % slots);
  auto rounds = static_cast<uint32_t>(rounds64);
  uint32_t slot = (current_slot + ticks_mod) % slots;

  WheelTimer::Key key = impl_->next_key++;

  if VUNLIKELY (key <= 0) {
    impl_->next_key = 1;
    key = impl_->next_key++;
  }

  int probe = 0;

  while (impl_->timer_index.find(key) != impl_->timer_index.end() && probe < 8) {
    key = impl_->next_key++;
    if (key <= 0) {
      impl_->next_key = 1;
      key = impl_->next_key++;
    }
    ++probe;
  }

  if VUNLIKELY (impl_->timer_index.find(key) != impl_->timer_index.end()) {
    VLOG_E("WheelTimer: Failed to allocate a unique key.");
    return -1;
  }

  auto& slot_list = impl_->wheels[slot];
  slot_list.emplace_back(key, rounds, std::move(callback), repeat_ms);

  auto it = std::prev(slot_list.end());
  impl_->timer_index[key] = {slot, it};

  wakeup();

  return key;
}

bool WheelTimer::remove(WheelTimer::Key key) {
  {
    std::lock_guard lock(impl_->mtx);

    auto it = impl_->timer_index.find(key);

    if VUNLIKELY (it == impl_->timer_index.end()) {
      return false;
    }

    auto& slot_list = impl_->wheels[it->second.first];
    slot_list.erase(it->second.second);
    impl_->timer_index.erase(it);
  }

  wakeup();

  return true;
}

uint32_t WheelTimer::get_remaining_time(Key key) const {
  std::lock_guard lock(impl_->mtx);

  auto it = impl_->timer_index.find(key);

  if (it == impl_->timer_index.end()) {
    return 0;
  }

  uint32_t slot = it->second.first;
  uint32_t current_slot = impl_->current_slot;
  uint32_t delta_slot = (slot + impl_->slots - current_slot) % impl_->slots;
  uint32_t rounds = it->second.second->remaining_rounds;

  return (rounds * impl_->slots + delta_slot) * impl_->interval_ms;
}

void WheelTimer::set_catchup_limit(uint32_t max_slots_to_catch_up) { impl_->catchup_limit = max_slots_to_catch_up; }

void WheelTimer::run() {
  std::vector<std::pair<Key, Callback>> callbacks_to_execute;

  auto interval = std::chrono::milliseconds(impl_->interval_ms);

  auto next_tick = std::chrono::steady_clock::now();

  for (;;) {
    std::unique_lock lock(impl_->mtx);

    if VUNLIKELY (impl_->stop_flag) {
      break;
    }

    while (impl_->paused_flag && !impl_->stop_flag) {
      impl_->cv.wait(lock);
    }

    if VUNLIKELY (impl_->stop_flag) {
      break;
    }

    auto now = std::chrono::steady_clock::now();

    if (now < next_tick) {
      impl_->cv.wait_until(lock, next_tick, [this]() -> bool { return impl_->stop_flag || impl_->paused_flag; });

      if VUNLIKELY (impl_->stop_flag) {
        break;
      }

      now = std::chrono::steady_clock::now();
    }

    uint32_t advanced = 0;
    uint32_t catchup_limit_snapshot = impl_->catchup_limit.load();

    while (now >= next_tick && !impl_->stop_flag && !impl_->paused_flag) {
      auto& timers = impl_->wheels[impl_->current_slot];

      for (auto it = timers.begin(); it != timers.end();) {
        if VLIKELY (it->remaining_rounds > 0) {
          --(it->remaining_rounds);
          ++it;
        } else {
          if (it->repeat_interval_ms > 0) {
            callbacks_to_execute.emplace_back(it->key, it->callback);

            uint64_t repeat_ticks =
                (static_cast<uint64_t>(it->repeat_interval_ms) + impl_->interval_ms - 1) / impl_->interval_ms;

            if VUNLIKELY (repeat_ticks == 0) {
              repeat_ticks = 1;
            }

            uint64_t rounds64 = repeat_ticks / impl_->slots;

            if VUNLIKELY (rounds64 > std::numeric_limits<uint32_t>::max()) {
              VLOG_E("WheelTimer: Repeat interval too large.");

              impl_->timer_index.erase(it->key);
              it = timers.erase(it);

              continue;
            }

            auto repeat_ticks_mod = static_cast<uint32_t>(repeat_ticks % impl_->slots);
            auto new_rounds = static_cast<uint32_t>(rounds64);
            auto new_slot = (impl_->current_slot + repeat_ticks_mod) % impl_->slots;

            Impl::Handler new_handler(it->key, new_rounds, std::move(it->callback), it->repeat_interval_ms);
            auto& new_list = impl_->wheels[new_slot];

            new_list.emplace_back(std::move(new_handler));

            auto new_it = std::prev(new_list.end());

            impl_->timer_index[it->key] = {new_slot, new_it};

            it = timers.erase(it);
          } else {
            callbacks_to_execute.emplace_back(it->key, std::move(it->callback));
            impl_->timer_index.erase(it->key);
            it = timers.erase(it);
          }
        }
      }

      impl_->current_slot = (impl_->current_slot + 1) % impl_->slots;

      next_tick += interval;

      if (catchup_limit_snapshot > 0) {
        if (++advanced >= catchup_limit_snapshot) {
          break;
        }
      }
    }

    // catch up
    now = std::chrono::steady_clock::now();
    if (next_tick + interval * 10 < now) {
      next_tick = now + interval;
    }
    //

    std::vector<std::pair<Key, Callback>> pending_callbacks;
    pending_callbacks.swap(callbacks_to_execute);

    lock.unlock();

    for (const auto& [key, callback] : pending_callbacks) {
      callback(key);
    }
  }

  std::lock_guard lock(impl_->mtx);
  impl_->is_running = false;
  impl_->paused_flag = false;
}

}  // namespace vlink
