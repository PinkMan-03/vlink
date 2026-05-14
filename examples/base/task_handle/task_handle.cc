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

#include <vlink/base/cancellation.h>
#include <vlink/base/logger.h>
#include <vlink/base/message_loop.h>
#include <vlink/base/task_handle.h>
#include <vlink/base/thread_pool.h>

#include <atomic>
#include <chrono>
#include <stdexcept>
#include <thread>
#include <vector>

namespace {

const char* state_name(vlink::TaskExecutionState s) {
  switch (s) {
    case vlink::TaskExecutionState::kInvalid:
      return "kInvalid";
    case vlink::TaskExecutionState::kQueued:
      return "kQueued";
    case vlink::TaskExecutionState::kRunning:
      return "kRunning";
    case vlink::TaskExecutionState::kCompleted:
      return "kCompleted";
    case vlink::TaskExecutionState::kCancelled:
      return "kCancelled";
    case vlink::TaskExecutionState::kDropped:
      return "kDropped";
    case vlink::TaskExecutionState::kRejected:
      return "kRejected";
    case vlink::TaskExecutionState::kFailed:
      return "kFailed";
  }
  return "?";
}

}  // namespace

int main() {
  vlink::Logger::init("example_task_handle");

  // ---------------------------------------------------------------
  // 1. Basic post_task_handle + wait.
  // ---------------------------------------------------------------
  {
    VLOG_I("=== Section 1: post_task_handle + wait ===");

    vlink::MessageLoop loop;
    loop.async_run();

    std::atomic_int counter{0};
    auto h = loop.post_task_handle([&counter]() {
      std::this_thread::sleep_for(std::chrono::milliseconds(30));
      counter.fetch_add(1);
    });

    h.wait();
    VLOG_I("handle.state=", state_name(h.state()), " counter=", counter.load());

    loop.quit();
    loop.wait_for_quit();
  }

  // ---------------------------------------------------------------
  // 2. PostTaskOptions::cancellation_token (parent token).
  // ---------------------------------------------------------------
  {
    VLOG_I("=== Section 2: parent cancellation_token ===");

    vlink::MessageLoop loop;
    loop.async_run();

    vlink::CancellationSource parent;
    vlink::PostTaskOptions opts;
    opts.cancellation_token = parent.token();

    auto h = loop.post_task_handle(
        [token = opts.cancellation_token]() {
          while (!token.is_cancellation_requested()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
          }
          VLOG_I("worker observed cancellation via parent token");
        },
        opts);

    std::this_thread::sleep_for(std::chrono::milliseconds(30));
    parent.request_cancel();
    h.wait();
    VLOG_I("handle.state=", state_name(h.state()));

    loop.quit();
    loop.wait_for_quit();
  }

  // ---------------------------------------------------------------
  // 3. cancel() before run -> kCancelled.
  // ---------------------------------------------------------------
  {
    VLOG_I("=== Section 3: cancel while still queued ===");

    vlink::MessageLoop loop;  // do NOT start; task stays queued
    auto h = loop.post_task_handle([]() { VLOG_E("should not run"); });
    VLOG_I("before cancel state=", state_name(h.state()));
    h.cancel();
    VLOG_I("after  cancel state=", state_name(h.state()));
  }

  // ---------------------------------------------------------------
  // 4. cancel() during execution: running task observes via own token.
  // ---------------------------------------------------------------
  {
    VLOG_I("=== Section 4: cancel during execution (own token polling) ===");

    vlink::MessageLoop loop;
    loop.async_run();

    vlink::TaskHandle h;
    h = loop.post_task_handle([&h]() {
      auto self_token = h.cancellation_token();
      uint32_t iter = 0;
      while (!self_token.is_cancellation_requested()) {
        ++iter;
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
      }
      VLOG_I("running task observed self.cancel() after ", iter, " iterations");
    });

    std::this_thread::sleep_for(std::chrono::milliseconds(40));
    h.cancel();  // flips cancellation_source; running task observes via token
    h.wait();
    // Once execution has started, cancel() only flips the source; the running
    // task returns normally and the final state is kCompleted (not kCancelled).
    VLOG_I("h.state=", state_name(h.state()), " (kCompleted: task returned)");

    loop.quit();
    loop.wait_for_quit();
  }

  // ---------------------------------------------------------------
  // 5. Callback throws -> kFailed.
  // ---------------------------------------------------------------
  {
    VLOG_I("=== Section 5: callback throws -> kFailed ===");

    vlink::MessageLoop loop;
    loop.async_run();

    auto h = loop.post_task_handle([]() { throw std::runtime_error("nope"); });
    h.wait();
    VLOG_I("handle.state=", state_name(h.state()));

    loop.quit();
    loop.wait_for_quit();
  }

  // ---------------------------------------------------------------
  // 6. Priority + post_task_with_priority_handle.
  // ---------------------------------------------------------------
  {
    VLOG_I("=== Section 6: priority post_task_with_priority_handle ===");

    vlink::MessageLoop loop(vlink::MessageLoop::kPriorityType);
    loop.async_run();

    std::vector<vlink::TaskHandle> handles;
    handles.reserve(3);
    handles.push_back(
        loop.post_task_with_priority_handle([]() { VLOG_I("low ran"); }, vlink::MessageLoop::kLowestPriority));
    handles.push_back(
        loop.post_task_with_priority_handle([]() { VLOG_I("normal ran"); }, vlink::MessageLoop::kNormalPriority));
    handles.push_back(
        loop.post_task_with_priority_handle([]() { VLOG_I("high ran"); }, vlink::MessageLoop::kHighestPriority));

    for (auto& h : handles) h.wait();
    for (size_t i = 0; i < handles.size(); ++i) {
      VLOG_I("handle[", i, "].state=", state_name(handles[i].state()));
    }

    loop.quit();
    loop.wait_for_quit();
  }

  // ---------------------------------------------------------------
  // 7. overflow_policy=kReject after quit -> kRejected.
  // ---------------------------------------------------------------
  {
    VLOG_I("=== Section 7: kReject after quit ===");

    vlink::MessageLoop loop;
    loop.async_run();
    loop.quit();
    loop.wait_for_quit();

    vlink::PostTaskOptions opts;
    opts.overflow_policy = vlink::TaskOverflowPolicy::kReject;

    auto h = loop.post_task_handle([]() { VLOG_E("should not run"); }, opts);
    VLOG_I("post after quit, state=", state_name(h.state()));
  }

  // ---------------------------------------------------------------
  // 8. drop_policy=kProtected vs kDroppable (API surface).
  // ---------------------------------------------------------------
  {
    VLOG_I("=== Section 8: kProtected vs kDroppable on normal queue ===");

    vlink::MessageLoop loop;
    loop.set_strategy(vlink::MessageLoop::kPopStrategy);

    vlink::PostTaskOptions protect_opts;
    protect_opts.drop_policy = vlink::TaskDropPolicy::kProtected;
    vlink::PostTaskOptions drop_opts;
    drop_opts.drop_policy = vlink::TaskDropPolicy::kDroppable;

    auto protected_handle = loop.post_task_handle([]() {}, protect_opts);
    auto droppable_handle = loop.post_task_handle([]() {}, drop_opts);

    VLOG_I("after submission: protected.state=", state_name(protected_handle.state()),
           " droppable.state=", state_name(droppable_handle.state()));
    protected_handle.cancel();
    droppable_handle.cancel();
  }

  // ---------------------------------------------------------------
  // 9. ThreadPool: tracked task + group cancellation.
  // ---------------------------------------------------------------
  {
    VLOG_I("=== Section 9: ThreadPool group cancellation ===");

    vlink::ThreadPool pool(4);

    vlink::CancellationSource batch;
    vlink::PostTaskOptions opts;
    opts.cancellation_token = batch.token();

    std::vector<vlink::TaskHandle> handles;
    handles.reserve(8);
    for (int i = 0; i < 8; ++i) {
      handles.emplace_back(pool.post_task_handle(
          [token = opts.cancellation_token, i]() {
            while (!token.is_cancellation_requested()) {
              std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
            VLOG_I("worker ", i, " saw cancel");
          },
          opts));
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(40));
    batch.request_cancel();
    for (auto& h : handles) h.wait();

    int terminal = 0;
    for (auto& h : handles) {
      if (h.is_done()) ++terminal;
    }
    VLOG_I("all 8 workers reached terminal state=", terminal);

    pool.shutdown();
  }

  // ---------------------------------------------------------------
  // 10. wait(timeout_ms) timeout vs completion.
  // ---------------------------------------------------------------
  {
    VLOG_I("=== Section 10: wait with timeout ===");

    vlink::MessageLoop loop;
    loop.async_run();

    auto h = loop.post_task_handle([]() { std::this_thread::sleep_for(std::chrono::milliseconds(200)); });

    const bool finished_in_50ms = h.wait(/*timeout_ms=*/50);
    VLOG_I("finished in 50ms? ", finished_in_50ms, " state=", state_name(h.state()));
    const bool finished_in_500ms = h.wait(/*timeout_ms=*/500);
    VLOG_I("finished in 500ms? ", finished_in_500ms, " state=", state_name(h.state()));

    loop.quit();
    loop.wait_for_quit();
  }

  // ---------------------------------------------------------------
  // 11. Handle destruction does NOT cancel; dispatcher keeps the task alive.
  // ---------------------------------------------------------------
  {
    VLOG_I("=== Section 11: handle destruction does not cancel ===");

    vlink::MessageLoop loop;
    loop.async_run();

    std::atomic_bool ran{false};
    {
      auto h = loop.post_task_handle([&ran]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(40));
        ran.store(true);
      });
    }  // handle out of scope here -- task still runs

    loop.wait_for_idle();
    VLOG_I("task still ran=", ran.load());

    loop.quit();
    loop.wait_for_quit();
  }

  // ---------------------------------------------------------------
  // 12. Handle copies share state.
  // ---------------------------------------------------------------
  {
    VLOG_I("=== Section 12: handle copies share state ===");

    vlink::MessageLoop loop;
    loop.async_run();

    vlink::TaskHandle h;
    h = loop.post_task_handle([&h]() {
      auto self_token = h.cancellation_token();
      while (!self_token.is_cancellation_requested()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
      }
    });

    vlink::TaskHandle another_view = h;  // copy shares state
    std::this_thread::sleep_for(std::chrono::milliseconds(30));
    another_view.cancel();  // request via copy
    h.wait();
    VLOG_I("h.state=", state_name(h.state()), " another_view.state=", state_name(another_view.state()),
           " (same shared state)");

    loop.quit();
    loop.wait_for_quit();
  }

  // ---------------------------------------------------------------
  // 13. Default-constructed (invalid) handle semantics.
  // ---------------------------------------------------------------
  {
    VLOG_I("=== Section 13: invalid handle semantics ===");

    vlink::TaskHandle invalid;
    VLOG_I("valid=", invalid.valid(), " state=", state_name(invalid.state()), " is_done=", invalid.is_done());
    VLOG_I("wait(10ms) returns=", invalid.wait(/*timeout_ms=*/10), " cancel() returns=", invalid.cancel(),
           " cancellation_token().valid=", invalid.cancellation_token().valid());
  }

  VLOG_I("example_task_handle done");
  return 0;
}
