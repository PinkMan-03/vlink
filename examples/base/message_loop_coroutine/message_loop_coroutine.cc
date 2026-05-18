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

// Example: MessageLoop + Coroutine - co_spawn, awaiters, bridges, orchestration, priority.
//
// IMPORTANT: every coroutine body in this example is defined as a free function
// taking its state via parameters.  Using a coroutine lambda as a temporary
// (e.g. [&](){...}()) is undefined behaviour - the lambda is destroyed at the
// end of the full-expression while the coroutine body runs later on the loop
// thread, leaving any captured reference dangling.

#include <vlink/base/coroutine.h>
#include <vlink/base/graph_task.h>
#include <vlink/base/logger.h>
#include <vlink/base/message_loop.h>
#include <vlink/base/schedule.h>

#include <atomic>
#include <chrono>
#include <future>
#include <thread>
#include <utility>
#include <vector>

#ifndef VLINK_ENABLE_COROUTINE

int main() {
  VLOG_W("This example requires C++20 coroutines (ENABLE_CXX_STD_20=ON).");
  return 0;
}

#else

using vlink::GraphTask;
using vlink::GraphTaskPtr;
using vlink::MessageLoop;
using vlink::Schedule;

// ---------------------------------------------------------------------------
// Coroutine bodies (must be free functions to avoid the temporary-lambda UB)
// ---------------------------------------------------------------------------

namespace {

vlink::Co::Task<> body_hello(std::promise<void>* done) {
  VLOG_I("  [body] hello from coroutine, tid={}", std::hash<std::thread::id>{}(std::this_thread::get_id()));
  done->set_value();
  co_return;
}

vlink::Co::Task<int> body_answer() { co_return 42; }

vlink::Co::Task<int> body_square_after_delay(MessageLoop* loop, int key) {
  // NOLINTNEXTLINE(readability-static-accessed-through-instance)
  co_await vlink::Co::delay_ms(*loop, 30);
  co_return key* key;
}

vlink::Co::Task<> body_compose_squares(MessageLoop* loop, std::promise<int>* done) {
  int a = co_await body_square_after_delay(loop, 6);
  int b = co_await body_square_after_delay(loop, 7);
  done->set_value(a + b);
  co_return;
}

vlink::Co::Task<> body_measure_delay(MessageLoop* loop, std::chrono::steady_clock::time_point start,
                                     std::promise<int64_t>* done) {
  // NOLINTNEXTLINE(readability-static-accessed-through-instance)
  co_await vlink::Co::delay_ms(*loop, 100);
  auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start);
  done->set_value(elapsed.count());
  co_return;
}

vlink::Co::Task<> body_stamp_then_yield(MessageLoop* loop, std::vector<int>* trace, int tag, int rounds,
                                        std::promise<void>* done) {
  for (int i = 0; i < rounds; ++i) {
    trace->push_back(tag);
    // NOLINTNEXTLINE(readability-static-accessed-through-instance)
    co_await vlink::Co::yield(*loop);
  }
  done->set_value();
  co_return;
}

vlink::Co::Task<> body_consumer(MessageLoop* loop, std::future<int> input, std::promise<int>* output) {
  int value = co_await vlink::Co::await_future(*loop, std::move(input));
  // NOLINTNEXTLINE(readability-static-accessed-through-instance)
  co_await vlink::Co::schedule(*loop);
  output->set_value(value * 2);
  co_return;
}

vlink::Co::Task<> body_run_under_exec(MessageLoop* loop, std::promise<void>* done) {
  Schedule::Config cfg(/*delay_ms=*/50, MessageLoop::kNormalPriority, /*schedule_timeout_ms=*/1000,
                       /*execution_timeout_ms=*/500);
  co_await vlink::Co::exec(*loop, cfg, [] { VLOG_I("  exec body ran inside Schedule envelope"); });
  done->set_value();
  co_return;
}

vlink::Co::Task<> body_launch_and_wait_graph(MessageLoop* loop, GraphTaskPtr root, GraphTaskPtr leaf,
                                             std::atomic<int>* step, std::promise<int>* done) {
  root->execute(loop);
  co_await vlink::Co::await_graph(*loop, leaf);
  done->set_value(step->load(std::memory_order_acquire));
  co_return;
}

vlink::Co::Task<int> body_tagged_delay(MessageLoop* loop, uint32_t ms, int tag) {
  // NOLINTNEXTLINE(readability-static-accessed-through-instance)
  co_await vlink::Co::delay_ms(*loop, ms);
  co_return tag;
}

vlink::Co::Task<> body_orchestration(MessageLoop* loop, std::promise<void>* done) {
  std::vector<vlink::Co::Task<int> > branches;
  branches.emplace_back(body_tagged_delay(loop, 30, 100));
  branches.emplace_back(body_tagged_delay(loop, 10, 200));
  branches.emplace_back(body_tagged_delay(loop, 20, 300));

  auto winner = co_await vlink::Co::when_any<int>(*loop, std::move(branches));
  VLOG_I("  when_any winner: idx={}, value={}", winner.first, winner.second);

  std::vector<vlink::Co::Task<int> > all_tasks;
  all_tasks.emplace_back(body_tagged_delay(loop, 5, 1));
  all_tasks.emplace_back(body_tagged_delay(loop, 10, 2));
  all_tasks.emplace_back(body_tagged_delay(loop, 15, 3));

  auto results = co_await vlink::Co::when_all<int>(*loop, std::move(all_tasks));
  VLOG_I("  when_all results: [{}, {}, {}]", results[0], results[1], results[2]);

  done->set_value();
  co_return;
}

vlink::Co::Task<> body_primed_then_delay(MessageLoop* loop, std::promise<void>* primed) {
  primed->set_value();
  // NOLINTNEXTLINE(readability-static-accessed-through-instance)
  co_await vlink::Co::delay_ms(*loop, 50);
  co_return;
}

vlink::Co::Task<> body_tag_then_signal(std::vector<int>* trace, int tag, std::promise<void>* done) {
  trace->push_back(tag);
  done->set_value();
  co_return;
}

}  // namespace

int main() {
  // ---------------------------------------------------------------
  // 1. Basic Task<void> spawn -- fire-and-forget.
  // ---------------------------------------------------------------
  {
    VLOG_I("=== Section 1: basic Task<void> spawn ===");
    MessageLoop loop;
    loop.set_name("coro_basic_loop");
    loop.async_run();

    std::promise<void> done;
    auto fut = done.get_future();
    vlink::Co::co_spawn(loop, body_hello(&done));
    fut.get();

    loop.quit();
    loop.wait_for_quit();
  }

  // ---------------------------------------------------------------
  // 2. Task<int> with completion callback.
  // ---------------------------------------------------------------
  {
    VLOG_I("=== Section 2: Task<int> with completion callback ===");
    MessageLoop loop;
    loop.async_run();

    std::promise<int> done;
    auto fut = done.get_future();
    vlink::Co::co_spawn(loop, body_answer(), [done_ptr = &done](int v) { done_ptr->set_value(v); });

    VLOG_I("  result = {}", fut.get());

    loop.quit();
    loop.wait_for_quit();
  }

  // ---------------------------------------------------------------
  // 3. Nested co_await -- one Task awaits another.
  // ---------------------------------------------------------------
  {
    VLOG_I("=== Section 3: nested co_await ===");
    MessageLoop loop;
    loop.async_run();

    std::promise<int> done;
    auto fut = done.get_future();
    vlink::Co::co_spawn(loop, body_compose_squares(&loop, &done));

    VLOG_I("  6^2 + 7^2 = {}", fut.get());

    loop.quit();
    loop.wait_for_quit();
  }

  // ---------------------------------------------------------------
  // 4. delay_ms -- non-blocking sleep via Timer.
  // ---------------------------------------------------------------
  {
    VLOG_I("=== Section 4: delay_ms ===");
    MessageLoop loop;
    loop.async_run();

    std::promise<int64_t> done;
    auto fut = done.get_future();
    vlink::Co::co_spawn(loop, body_measure_delay(&loop, std::chrono::steady_clock::now(), &done));

    VLOG_I("  slept for {} ms (target 100ms)", fut.get());

    loop.quit();
    loop.wait_for_quit();
  }

  // ---------------------------------------------------------------
  // 5. yield -- cooperative interleaving of multiple coroutines.
  // ---------------------------------------------------------------
  {
    VLOG_I("=== Section 5: yield interleaving ===");
    MessageLoop loop;
    loop.async_run();

    std::vector<int> trace;
    trace.reserve(20);
    std::promise<void> done_a;
    std::promise<void> done_b;

    vlink::Co::co_spawn(loop, body_stamp_then_yield(&loop, &trace, 0, 5, &done_a));
    vlink::Co::co_spawn(loop, body_stamp_then_yield(&loop, &trace, 1, 5, &done_b));

    done_a.get_future().get();
    done_b.get_future().get();

    std::string line;
    for (int v : trace) {
      line.push_back(v == 0 ? 'A' : 'B');
    }
    VLOG_I("  interleaving = {}", line);

    loop.quit();
    loop.wait_for_quit();
  }

  // ---------------------------------------------------------------
  // 6 & 7. schedule + await_future -- bridge std::future into a coroutine.
  // ---------------------------------------------------------------
  {
    VLOG_I("=== Sections 6 & 7: schedule + await_future ===");
    MessageLoop loop;
    loop.async_run();

    std::promise<int> input;
    std::promise<int> output;
    auto output_fut = output.get_future();

    vlink::Co::co_spawn(loop, body_consumer(&loop, input.get_future(), &output));

    std::thread producer([&input]() {
      std::this_thread::sleep_for(std::chrono::milliseconds(50));
      input.set_value(21);
    });

    VLOG_I("  consumer received 21 -> doubled = {}", output_fut.get());
    producer.join();

    loop.quit();
    loop.wait_for_quit();
  }

  // ---------------------------------------------------------------
  // 8. exec -- Schedule::Config envelope (delay, priority, timeouts).
  // ---------------------------------------------------------------
  {
    VLOG_I("=== Section 8: Schedule::exec inside coroutine ===");
    MessageLoop loop;
    loop.async_run();

    std::promise<void> done;
    auto fut = done.get_future();
    vlink::Co::co_spawn(loop, body_run_under_exec(&loop, &done));
    fut.get();

    loop.quit();
    loop.wait_for_quit();
  }

  // ---------------------------------------------------------------
  // 9. await_graph -- await an entire GraphTask DAG to finish.
  // ---------------------------------------------------------------
  {
    VLOG_I("=== Section 9: await_graph ===");
    MessageLoop loop;
    loop.async_run();

    std::atomic<int> step{0};
    auto a = GraphTask::create("A", [step_ptr = &step]() { step_ptr->fetch_add(1, std::memory_order_release); });
    auto b = GraphTask::create("B", [step_ptr = &step]() { step_ptr->fetch_add(10, std::memory_order_release); });
    auto c = GraphTask::create("C", [step_ptr = &step]() { step_ptr->fetch_add(100, std::memory_order_release); });

    // A -> B -> C: a runs first, then b, then c.
    // x.precede(y) registers y as a successor of x (y runs after x).
    a->precede(b);
    b->precede(c);

    std::promise<int> done;
    auto fut = done.get_future();
    vlink::Co::co_spawn(loop, body_launch_and_wait_graph(&loop, a, c, &step, &done));

    VLOG_I("  graph result = {} (expect 111)", fut.get());

    loop.quit();
    loop.wait_for_quit();
  }

  // ---------------------------------------------------------------
  // 10. Orchestration -- when_all / when_any.
  // ---------------------------------------------------------------
  {
    VLOG_I("=== Section 10: when_all / when_any orchestration ===");
    MessageLoop loop;
    loop.async_run();

    std::promise<void> done;
    auto fut = done.get_future();
    vlink::Co::co_spawn(loop, body_orchestration(&loop, &done));
    fut.get();

    loop.quit();
    loop.wait_for_quit();
  }

  // ---------------------------------------------------------------
  // 11. Priority-aware spawn on a kPriorityType loop.
  // ---------------------------------------------------------------
  {
    VLOG_I("=== Section 11: priority scheduling on kPriorityType loop ===");
    MessageLoop loop(MessageLoop::kPriorityType);
    loop.set_name("coro_priority_loop");
    loop.async_run();

    std::promise<void> primed;
    vlink::Co::co_spawn(loop, body_primed_then_delay(&loop, &primed));
    primed.get_future().get();

    std::vector<int> trace;
    std::promise<void> done_low;
    std::promise<void> done_high;

    vlink::Co::co_spawn_with_priority(loop, body_tag_then_signal(&trace, 0, &done_low), MessageLoop::kLowestPriority);
    vlink::Co::co_spawn_with_priority(loop, body_tag_then_signal(&trace, 1, &done_high), MessageLoop::kHighestPriority);

    done_high.get_future().get();
    done_low.get_future().get();

    VLOG_I("  trace = [{}, {}]  (HIGH=1 first, LOW=0 second)", trace[0], trace[1]);

    loop.quit();
    loop.wait_for_quit();
  }

  VLOG_I("MessageLoop + Coroutine example finished.");
  return 0;
}

#endif  // VLINK_ENABLE_COROUTINE
