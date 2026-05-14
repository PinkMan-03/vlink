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

#include "./base/coroutine.h"

#ifdef VLINK_ENABLE_COROUTINE

#include <chrono>
#include <mutex>
#include <thread>
#include <utility>
#include <vector>

#include "./base/graph_task.h"
#include "./base/logger.h"
#include "./base/memory_pool.h"
#include "./base/message_loop.h"
#include "./base/timer.h"

namespace vlink {
namespace Coroutine {  // NOLINT(readability-identifier-naming)

[[maybe_unused]] static constexpr size_t kMaxTaskSize = 5000U;

// FutureWaitLoop
class FutureWaitLoop final : public MessageLoop {
 public:
  static FutureWaitLoop& instance() {
    static FutureWaitLoop holder;
    return holder;
  }

  [[nodiscard]] size_t get_max_task_count() const override { return kMaxTaskSize; }

  static void register_poll(MoveFunction<bool()>&& poll) {
    auto& self = instance();
    bool need_post = false;

    {
      std::lock_guard lock(self.mtx_);
      self.pending_.push_back(std::move(poll));

      if (!self.running_) {
        self.running_ = true;
        need_post = true;
      }
    }

    if (need_post) {
      if VUNLIKELY (!self.post_task([]() { instance().poll_iteration(); })) {
        std::lock_guard lock(self.mtx_);
        self.running_ = false;
      }
    }
  }

 private:
  FutureWaitLoop() {
    set_name("FutureWaitLoop");
    async_run();
  }

  ~FutureWaitLoop() override {
    quit();
    wait_for_quit();
  }

  void poll_iteration() {
    std::vector<MoveFunction<bool()>> snapshot;

    {
      std::lock_guard lock(mtx_);

      if VUNLIKELY (pending_.empty()) {
        running_ = false;
        return;
      }

      snapshot.swap(pending_);
    }

    std::vector<MoveFunction<bool()>> not_done;
    not_done.reserve(snapshot.size());

    for (auto& poll : snapshot) {
      bool done = false;

      try {
        done = poll();
      } catch (const std::exception& e) {
        CLOG_E("FutureWaitLoop: poll closure threw an exception: %s.", e.what());
        done = true;
      } catch (...) {
        CLOG_E("FutureWaitLoop: poll closure threw a non-std exception.");
        done = true;
      }

      if (!done) {
        not_done.push_back(std::move(poll));
      }
    }

    bool need_post = false;

    {
      std::lock_guard lock(mtx_);

      for (auto& poll : not_done) {
        pending_.push_back(std::move(poll));
      }

      if (pending_.empty()) {
        running_ = false;
      } else {
        need_post = true;
      }
    }

    if (need_post) {
      std::this_thread::sleep_for(std::chrono::milliseconds(1));

      if VUNLIKELY (!post_task([]() { instance().poll_iteration(); })) {
        std::lock_guard lock(mtx_);
        running_ = false;
      }
    }
  }

  std::mutex mtx_;
  std::vector<MoveFunction<bool()>> pending_;
  bool running_{false};
};

void* detail::allocate_frame(size_t size) {
  void* ptr = MemoryPool::global_instance().allocate(size);

  if VLIKELY (ptr != nullptr) {
    return ptr;
  }

  throw std::bad_alloc{};
}

void detail::deallocate_frame(void* ptr, size_t size) noexcept {
  if VUNLIKELY (ptr == nullptr) {
    return;
  }

  MemoryPool::global_instance().deallocate(ptr, size);
}

detail::DetachedTask detail::DetachedTask::promise_type::get_return_object() noexcept {
  return DetachedTask{std::coroutine_handle<promise_type>::from_promise(*this)};
}

// NOLINTNEXTLINE(readability-convert-member-functions-to-static)
std::suspend_always detail::DetachedTask::promise_type::initial_suspend() noexcept { return {}; }

// NOLINTNEXTLINE(readability-convert-member-functions-to-static)
std::suspend_never detail::DetachedTask::promise_type::final_suspend() noexcept { return {}; }

void detail::DetachedTask::promise_type::return_void() noexcept {}

// NOLINTNEXTLINE(readability-convert-member-functions-to-static)
void detail::DetachedTask::promise_type::unhandled_exception() noexcept {
  try {
    auto eptr = std::current_exception();

    if VUNLIKELY (!eptr) {
      return;
    }

    std::rethrow_exception(eptr);
  } catch (const std::exception& e) {
    CLOG_E("DetachedTask: coroutine threw an exception: %s.", e.what());
  } catch (...) {
    CLOG_E("DetachedTask: coroutine threw a non-std exception.");
  }
}

detail::DetachedTask detail::co_spawn_void_impl(Task<void> task) { co_await std::move(task); }

void detail::register_future_wait(MoveFunction<bool()>&& poll) { FutureWaitLoop::register_poll(std::move(poll)); }

bool detail::post_resume(MessageLoop& loop, std::coroutine_handle<> handle, uint16_t priority) {
  if (priority != 0 && loop.get_type() == MessageLoop::kPriorityType) {
    return loop.post_task_with_priority([handle]() { handle.resume(); }, priority);
  }

  return loop.post_task([handle]() { handle.resume(); });
}

void detail::co_spawn_detached_handle(MessageLoop& loop, DetachedTask::Handle handle, uint16_t priority) {
  if VUNLIKELY (!post_resume(loop, handle, priority)) {
    handle.destroy();
  }
}

bool ScheduleAwaiter::await_ready() noexcept { return false; }

bool ScheduleAwaiter::await_suspend(std::coroutine_handle<> handle) {
  failed = !detail::post_resume(*loop, handle, priority);
  return !failed;
}

// NOLINTNEXTLINE(readability-make-member-function-const)
void ScheduleAwaiter::await_resume() {
  if VUNLIKELY (failed) {
    throw std::runtime_error("vlink::Coroutine::schedule: post_task to loop failed (loop quitting?)");
  }
}

bool YieldAwaiter::await_ready() noexcept { return false; }

bool YieldAwaiter::await_suspend(std::coroutine_handle<> handle) {
  failed = !detail::post_resume(*loop, handle, priority);
  return !failed;
}

// NOLINTNEXTLINE(readability-make-member-function-const)
void YieldAwaiter::await_resume() {
  if VUNLIKELY (failed) {
    throw std::runtime_error("vlink::Coroutine::yield: post_task to loop failed (loop quitting?)");
  }
}

bool DelayAwaiter::await_ready() noexcept { return false; }

bool DelayAwaiter::await_suspend(std::coroutine_handle<> handle) {
  failed = !Timer::call_once(loop, ms, [handle]() { handle.resume(); }, priority);
  return !failed;
}

// NOLINTNEXTLINE(readability-make-member-function-const)
void DelayAwaiter::await_resume() {
  if VUNLIKELY (failed) {
    throw std::runtime_error("vlink::Coroutine::delay_ms: Timer::call_once failed (loop quitting?)");
  }
}

ScheduleAwaiter schedule(MessageLoop& loop, uint16_t priority) noexcept { return ScheduleAwaiter{&loop, priority}; }

YieldAwaiter yield(MessageLoop& loop, uint16_t priority) noexcept { return YieldAwaiter{&loop, priority}; }

DelayAwaiter delay_ms(MessageLoop& loop, uint32_t ms, uint16_t priority) noexcept {
  return DelayAwaiter{&loop, ms, priority};
}

void co_spawn(MessageLoop& loop, Task<void>&& task) { co_spawn_with_priority(loop, std::move(task), 0); }

void co_spawn_with_priority(MessageLoop& loop, Task<void>&& task, uint16_t priority) {
  auto detached = detail::co_spawn_void_impl(std::move(task));

  auto handle = detached.handle;
  detached.handle = {};

  detail::co_spawn_detached_handle(loop, handle, priority);
}

GraphAwaiter::GraphAwaiter(MessageLoop* loop, GraphTaskPtr graph) noexcept : loop_(loop), graph_(std::move(graph)) {}

bool GraphAwaiter::await_ready() const noexcept {
  if VUNLIKELY (!graph_) {
    return true;
  }

  return graph_->get_status() == GraphTask::kStatusDone;
}

void GraphAwaiter::await_suspend(std::coroutine_handle<> handle) {
  struct State final {
    std::atomic_bool fired{false};
    std::atomic<uint32_t> id{0};
  };

  auto* loop = loop_;
  auto graph = graph_;
  auto state = std::make_shared<State>();

  uint32_t id = graph->register_status_callback(
      [loop, handle, state, graph](const std::string& /*name*/, GraphTask::Status status) {
        if (status != GraphTask::kStatusDone) {
          return;
        }

        bool expected = false;

        if (!state->fired.compare_exchange_strong(expected, true, std::memory_order_acq_rel)) {
          return;
        }

        uint32_t my_id = state->id.load(std::memory_order_acquire);

        if VLIKELY (my_id != 0) {
          if VUNLIKELY (!loop->post_task([graph, my_id]() { graph->unregister_status_callback(my_id); })) {
            CLOG_E("GraphAwaiter: unregister post failed (loop quitting?); subscription leaked.");
          }
        }

        if VUNLIKELY (!detail::post_resume(*loop, handle)) {
          CLOG_E("GraphAwaiter: post_resume failed (loop quitting?); coroutine left suspended.");
        }
      });

  state->id.store(id, std::memory_order_release);

  if VUNLIKELY (state->fired.load(std::memory_order_acquire)) {
    graph->unregister_status_callback(id);
    return;
  }

  if VUNLIKELY (graph->get_status() == GraphTask::kStatusDone) {
    bool expected = false;

    if (state->fired.compare_exchange_strong(expected, true, std::memory_order_acq_rel)) {
      graph->unregister_status_callback(id);

      if VUNLIKELY (!detail::post_resume(*loop, handle)) {
        CLOG_E("GraphAwaiter: post_resume failed (loop quitting?); coroutine left suspended.");
      }
    }
  }
}

void GraphAwaiter::await_resume() const noexcept {}

GraphAwaiter await_graph(MessageLoop& loop, GraphTaskPtr graph) {
  if VUNLIKELY (!graph) {
    throw std::invalid_argument("vlink::Coroutine::await_graph: graph must not be null");
  }

  return GraphAwaiter{&loop, std::move(graph)};
}

namespace detail {

struct WhenAllVoidState final {
  std::atomic<size_t> remaining{0};
  std::mutex exc_mtx;
  std::exception_ptr first_exc;
  std::promise<void> promise;
};

struct WhenAllVoidGuard final {
  std::shared_ptr<WhenAllVoidState> state;
  bool completed_normally{false};

  explicit WhenAllVoidGuard(std::shared_ptr<WhenAllVoidState> s) noexcept : state(std::move(s)) {}

  WhenAllVoidGuard(WhenAllVoidGuard&& other) noexcept
      : state(std::move(other.state)), completed_normally(other.completed_normally) {}

  WhenAllVoidGuard(const WhenAllVoidGuard&) = delete;
  WhenAllVoidGuard& operator=(const WhenAllVoidGuard&) = delete;
  WhenAllVoidGuard& operator=(WhenAllVoidGuard&&) = delete;

  ~WhenAllVoidGuard() {
    if (!state) {
      return;
    }

    if (!completed_normally) {
      std::lock_guard lock(state->exc_mtx);

      if (!state->first_exc) {
        state->first_exc = std::make_exception_ptr(
            std::runtime_error("vlink::Coroutine::when_all: sub-task aborted before completion"));
      }
    }

    if (state->remaining.fetch_sub(1, std::memory_order_acq_rel) == 1) {
      if (state->first_exc) {
        state->promise.set_exception(state->first_exc);
      } else {
        state->promise.set_value();
      }
    }
  }
};

static Task<void> when_all_void_runner(Task<void> task, WhenAllVoidGuard guard) {
  try {
    co_await std::move(task);
  } catch (...) {
    std::lock_guard lock(guard.state->exc_mtx);

    if (!guard.state->first_exc) {
      guard.state->first_exc = std::current_exception();
    }
  }

  guard.completed_normally = true;
}

struct WhenAnyVoidState final {
  std::atomic_bool fired{false};
  std::atomic<size_t> remaining{0};
  std::mutex exc_mtx;
  std::exception_ptr first_exc;
  size_t winner_idx{0};
  bool has_winner{false};
  std::promise<size_t> promise;
};

struct WhenAnyVoidGuard final {
  std::shared_ptr<WhenAnyVoidState> state;
  bool completed_normally{false};

  explicit WhenAnyVoidGuard(std::shared_ptr<WhenAnyVoidState> s) noexcept : state(std::move(s)) {}

  WhenAnyVoidGuard(WhenAnyVoidGuard&& other) noexcept
      : state(std::move(other.state)), completed_normally(other.completed_normally) {}

  WhenAnyVoidGuard(const WhenAnyVoidGuard&) = delete;
  WhenAnyVoidGuard& operator=(const WhenAnyVoidGuard&) = delete;
  WhenAnyVoidGuard& operator=(WhenAnyVoidGuard&&) = delete;

  ~WhenAnyVoidGuard() {
    if (!state) {
      return;
    }

    if (!completed_normally) {
      std::lock_guard lock(state->exc_mtx);

      if (!state->first_exc) {
        state->first_exc = std::make_exception_ptr(
            std::runtime_error("vlink::Coroutine::when_any: sub-task aborted before completion"));
      }
    }

    if (state->remaining.fetch_sub(1, std::memory_order_acq_rel) == 1) {
      if (state->has_winner) {
        state->promise.set_value(state->winner_idx);
      } else {
        state->promise.set_exception(state->first_exc);
      }
    }
  }
};

static Task<void> when_any_void_runner(Task<void> task, size_t i, WhenAnyVoidGuard guard) {
  try {
    co_await std::move(task);
    bool expected = false;

    if (guard.state->fired.compare_exchange_strong(expected, true, std::memory_order_acq_rel)) {
      guard.state->winner_idx = i;
      guard.state->has_winner = true;
    }
  } catch (...) {
    std::lock_guard lock(guard.state->exc_mtx);

    if (!guard.state->first_exc) {
      guard.state->first_exc = std::current_exception();
    }
  }

  guard.completed_normally = true;
}

}  // namespace detail

Task<void> when_all(MessageLoop& loop, std::vector<Task<void>> tasks) {
  if VUNLIKELY (tasks.empty()) {
    co_return;
  }

  auto state = std::make_shared<detail::WhenAllVoidState>();
  state->remaining.store(tasks.size(), std::memory_order_relaxed);
  auto fut = state->promise.get_future();

  for (auto& task : tasks) {
    co_spawn(loop, detail::when_all_void_runner(std::move(task), detail::WhenAllVoidGuard{state}));
  }

  co_await await_future(loop, std::move(fut));
  co_return;
}

Task<size_t> when_any(MessageLoop& loop, std::vector<Task<void>> tasks) {
  if VUNLIKELY (tasks.empty()) {
    throw std::invalid_argument("vlink::Coroutine::when_any: tasks must not be empty");
  }

  auto state = std::make_shared<detail::WhenAnyVoidState>();
  state->remaining.store(tasks.size(), std::memory_order_relaxed);
  auto fut = state->promise.get_future();

  for (size_t i = 0; i < tasks.size(); ++i) {
    co_spawn(loop, detail::when_any_void_runner(std::move(tasks[i]), i, detail::WhenAnyVoidGuard{state}));
  }

  co_return co_await await_future(loop, std::move(fut));
}

Task<void> sequence(MessageLoop& loop, std::vector<Task<void>> tasks) {
  for (auto& task : tasks) {
    if VUNLIKELY (!task.valid()) {
      continue;
    }

    auto state = std::make_shared<detail::WhenAllVoidState>();
    state->remaining.store(1, std::memory_order_relaxed);
    auto fut = state->promise.get_future();

    co_spawn(loop, detail::when_all_void_runner(std::move(task), detail::WhenAllVoidGuard{state}));

    co_await await_future(loop, std::move(fut));
  }

  co_return;
}

}  // namespace Coroutine
}  // namespace vlink

#endif  // VLINK_ENABLE_COROUTINE
