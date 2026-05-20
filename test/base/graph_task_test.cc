/*
 * Copyright (C) 2026 by Thun Lu. All rights reserved.
 * Author: Thun Lu <thun.lu@zohomail.cn>
 * Repo:   https://github.com/thun-res/vlink
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

// NOLINTBEGIN

#include "./base/graph_task.h"

#include <doctest/doctest.h>

#include <atomic>
#include <chrono>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "./base/thread_pool.h"

//
#include "../common_test.h"

// ---------------------------------------------------------------------------
// Shared engine — heap-allocated, intentionally never freed.
//
// Per-test ThreadPool creation/destruction causes races because shutdown()
// joins workers that may still be blocked in GraphTask::wait().  A persistent
// pool avoids this entirely.
// ---------------------------------------------------------------------------

static ThreadPool& get_shared_pool() {
  static auto* pool = new ThreadPool(1);
  return *pool;
}

static void run_graph(const GraphTaskPtr& entry, int timeout_ms = 30) {
  entry->execute(&get_shared_pool());

  std::this_thread::sleep_for(std::chrono::milliseconds(timeout_ms));
}

struct RejectGraphEngine {
  bool post_task(GraphTask::Callback&&) { return false; }
};

// ---------------------------------------------------------------------------
// Property tests (no threading) — safe to use TEST_CASE inside TEST_SUITE
// ---------------------------------------------------------------------------

TEST_SUITE("base-GraphTask") {
  TEST_CASE("get_name and set_name") {
    auto task = GraphTask::create("my_task", [] {});

    CHECK(task->get_name() == "my_task");

    task->set_name("renamed");
    CHECK(task->get_name() == "renamed");
  }

  TEST_CASE("unnamed task gets auto-generated name") {
    auto task = GraphTask::create([] {});
    CHECK(!task->get_name().empty());
  }

  TEST_CASE("get_status starts at kStatusInActive") {
    auto task = GraphTask::create("t", [] {});
    CHECK(task->get_status() == GraphTask::kStatusInActive);
  }

  TEST_CASE("get_policy defaults to kPolicyOnce and set_policy works") {
    auto task = GraphTask::create([] {});
    CHECK(task->get_policy() == GraphTask::kPolicyOnce);

    task->set_policy(GraphTask::kPolicyMultiple);
    CHECK(task->get_policy() == GraphTask::kPolicyMultiple);

    task->set_policy(GraphTask::kPolicyWaitAll);
    CHECK(task->get_policy() == GraphTask::kPolicyWaitAll);
  }

  TEST_CASE("is_condition_task returns false for regular task") {
    auto task = GraphTask::create([] {});
    CHECK(!task->is_condition_task());
  }

  TEST_CASE("is_condition_task returns true for condition task") {
    auto task = GraphTask::create_condition([] { return 0; });
    CHECK(task->is_condition_task());
  }

  TEST_CASE("has_cycle returns false for single node") {
    auto task = GraphTask::create("solo", [] {});
    CHECK(!task->has_cycle());
  }

  TEST_CASE("has_cycle returns false for linear chain A->B->C") {
    auto a = GraphTask::create("A", [] {});
    auto b = GraphTask::create("B", [] {});
    auto c = GraphTask::create("C", [] {});

    a->succeed(b);
    b->succeed(c);

    CHECK(!a->has_cycle());
  }

  TEST_CASE("back-edge that would create a cycle is auto-rejected by precede/succeed") {
    auto a = GraphTask::create("A", [] {});
    auto b = GraphTask::create("B", [] {});
    auto c = GraphTask::create("C", [] {});

    a->precede(b);
    b->precede(c);

    REQUIRE(!a->has_cycle());

    c->precede(a);

    CHECK(!a->has_cycle());
  }

  TEST_CASE("has_cycle returns false for diamond DAG") {
    auto a = GraphTask::create("A", [] {});
    auto b = GraphTask::create("B", [] {});
    auto c = GraphTask::create("C", [] {});
    auto d = GraphTask::create("D", [] {});

    a->succeed(b);
    a->succeed(c);
    b->succeed(d);
    c->succeed(d);

    CHECK(!a->has_cycle());
  }

  TEST_CASE("export_to_dot contains node names") {
    auto a = GraphTask::create("node_alpha", [] {});
    auto b = GraphTask::create("node_beta", [] {});

    a->succeed(b);

    std::string dot = b->export_to_dot();

    CHECK(!dot.empty());
    CHECK(dot.find("node_alpha") != std::string::npos);
    CHECK(dot.find("node_beta") != std::string::npos);
  }

  TEST_CASE("export_to_dot for single node is non-empty") {
    auto task = GraphTask::create("standalone", [] {});
    std::string dot = task->export_to_dot();
    CHECK(!dot.empty());
  }

  TEST_CASE("self-loop precede throws via VLOG_F and leaves graph untouched") {
    auto a = GraphTask::create("A", [] {});
    CHECK_THROWS(a->precede(a));
    CHECK(a->get_succeed_task_list().empty());
    CHECK(a->get_precede_task_list().empty());
  }

  TEST_CASE("get_condition_number and set_condition_number") {
    auto task = GraphTask::create("c", [] {}, 3);
    CHECK(task->get_condition_number() == 3);

    task->set_condition_number(5);
    CHECK(task->get_condition_number() == 5);
  }

  TEST_CASE("get_priority and set_priority") {
    auto task = GraphTask::create([] {});
    task->set_priority(42U);
    CHECK(task->get_priority() == 42U);
  }

  TEST_CASE("set_max_recursion_depth and get_max_recursion_depth") {
    auto task = GraphTask::create([] {});
    task->set_max_recursion_depth(128U);
    CHECK(task->get_max_recursion_depth() == 128U);
  }

  TEST_CASE("set_group_name and get_group_name") {
    auto task = GraphTask::create("t", [] {});
    task->set_group_name("my_group");
    CHECK(task->get_group_name() == "my_group");
  }

  TEST_CASE("named condition task factory sets is_condition_task and name") {
    auto task = GraphTask::create_condition("my_cond", [] { return 0; }, 2);
    CHECK(task->is_condition_task());
    CHECK(task->get_name() == "my_cond");
    CHECK(task->get_condition_number() == 2);
  }

  TEST_CASE("unnamed condition task has auto name") {
    auto task = GraphTask::create_condition([] { return 0; }, 3);
    CHECK(task->is_condition_task());
    CHECK(task->get_condition_number() == 3);
    CHECK(!task->get_name().empty());
  }

  TEST_CASE("precede and succeed are symmetric") {
    auto a = GraphTask::create("A", [] {});
    auto b = GraphTask::create("B", [] {});
    auto c = GraphTask::create("C", [] {});

    a->succeed(b);
    c->precede(a);

    auto succ_list = a->get_precede_task_list();
    CHECK(succ_list.size() == 2U);
  }

  TEST_CASE("remove_succeed_task shrinks task's succeed_task_list-side dependency") {
    auto a = GraphTask::create("A", [] {});
    auto b = GraphTask::create("B", [] {});

    a->succeed(b);
    CHECK(b->get_succeed_task_list().size() == 1U);

    a->remove_succeed_task(b);
    CHECK(b->get_succeed_task_list().empty());
  }

  TEST_CASE("remove_succeed_task shrinks self's precede_task_list-side dependency") {
    auto a = GraphTask::create("A", [] {});
    auto b = GraphTask::create("B", [] {});

    a->succeed(b);
    CHECK(a->get_precede_task_list().size() == 1U);

    a->remove_succeed_task(b);
    CHECK(a->get_precede_task_list().empty());
  }

  TEST_CASE("remove_precede_task shrinks self's succeed_task_list-side dependency") {
    auto a = GraphTask::create("A", [] {});
    auto b = GraphTask::create("B", [] {});

    b->precede(a);
    CHECK(b->get_succeed_task_list().size() == 1U);

    b->remove_precede_task(a);
    CHECK(b->get_succeed_task_list().empty());
  }

  TEST_CASE("cancel sets task to kStatusInActive and it does not execute") {
    std::atomic<int> executed{0};

    auto task = GraphTask::create("cancelable", [&] { executed.store(1); });

    task->cancel();
    CHECK(task->get_status() == GraphTask::kStatusInActive);
    CHECK(executed.load() == 0);
  }

  TEST_CASE("cancel sets status to kStatusInActive") {
    auto a = GraphTask::create("A", [] {});
    auto b = GraphTask::create("B", [] {});

    a->precede(b);
    b->cancel();
    CHECK(b->get_status() == GraphTask::kStatusInActive);
  }

  TEST_CASE("export_to_dot contains digraph keyword") {
    auto a = GraphTask::create("src", [] {});
    auto b = GraphTask::create("dst", [] {});

    a->succeed(b);

    std::string dot = b->export_to_dot();
    CHECK(!dot.empty());
    CHECK(dot.find("digraph") != std::string::npos);
  }

  TEST_CASE("set_group_name produces valid dot output") {
    auto a = GraphTask::create("node_a", [] {});
    a->set_group_name("my_group");

    std::string dot = a->export_to_dot();
    CHECK(!dot.empty());
  }

  TEST_CASE("get_precede_task_list and get_succeed_task_list sizes") {
    auto a = GraphTask::create("A", [] {});
    auto b = GraphTask::create("B", [] {});
    auto c = GraphTask::create("C", [] {});

    a->succeed(b);
    a->succeed(c);

    CHECK(a->get_precede_task_list().size() == 2U);
    CHECK(b->get_succeed_task_list().size() == 1U);
    CHECK(c->get_succeed_task_list().size() == 1U);
  }
}

// ---------------------------------------------------------------------------
// Execution tests — each uses run_graph() with retry to work around the
// framework's lost-wakeup race in notify().
//
// The build_graph lambda creates fresh tasks and wires up the graph.
// Assertions capture results via shared_ptr<atomic> so they survive retries.
// ---------------------------------------------------------------------------

TEST_SUITE("base-GraphTask - execution") {
  TEST_CASE("single task completes") {
    auto ran = std::make_shared<std::atomic<int>>(0);
    auto task = GraphTask::create("solo", [ran] { ran->store(1); });

    run_graph(task);

    CHECK(ran->load() == 1);
  }

  TEST_CASE("register_status_callback fires on transitions") {
    auto has_done = std::make_shared<std::atomic<bool>>(false);

    auto task = GraphTask::create("tracked", [] { std::this_thread::sleep_for(std::chrono::milliseconds(5)); });
    task->register_status_callback([has_done](const std::string&, GraphTask::Status s) {
      if (s == GraphTask::kStatusDone) {
        has_done->store(true);
      }
    });

    run_graph(task);

    CHECK(has_done->load());
  }

  TEST_CASE("linear chain A->B->C executes in order") {
    auto order = std::make_shared<std::vector<int>>();
    auto mtx = std::make_shared<std::mutex>();

    auto ta = GraphTask::create("A", [order, mtx] {
      std::lock_guard l(*mtx);
      order->push_back(0);
    });
    auto tb = GraphTask::create("B", [order, mtx] {
      std::lock_guard l(*mtx);
      order->push_back(1);
    });
    auto tc = GraphTask::create("C", [order, mtx] {
      std::lock_guard l(*mtx);
      order->push_back(2);
    });

    ta->precede(tb);
    tb->precede(tc);

    run_graph(ta);

    REQUIRE(order->size() == 3U);
    CHECK((*order)[0] == 0);
    CHECK((*order)[1] == 1);
    CHECK((*order)[2] == 2);
  }

  TEST_CASE("fan-out A->B and A->C") {
    auto a_done = std::make_shared<std::atomic<int>>(0);
    auto b_done = std::make_shared<std::atomic<int>>(0);
    auto c_done = std::make_shared<std::atomic<int>>(0);
    auto b_saw_a = std::make_shared<std::atomic<bool>>(false);
    auto c_saw_a = std::make_shared<std::atomic<bool>>(false);

    auto ta = GraphTask::create("A", [a_done] { a_done->store(1); });
    auto tb = GraphTask::create("B", [a_done, b_done, b_saw_a] {
      b_saw_a->store(a_done->load() == 1);
      b_done->store(1);
    });
    auto tc = GraphTask::create("C", [a_done, c_done, c_saw_a] {
      c_saw_a->store(a_done->load() == 1);
      c_done->store(1);
    });

    ta->precede(tb);
    ta->precede(tc);

    run_graph(ta);

    CHECK(a_done->load() == 1);
    CHECK(b_done->load() == 1);
    CHECK(c_done->load() == 1);
    CHECK(b_saw_a->load());
    CHECK(c_saw_a->load());
  }

  TEST_CASE("fan-in D runs after B and C") {
    auto b_done = std::make_shared<std::atomic<int>>(0);
    auto c_done = std::make_shared<std::atomic<int>>(0);
    auto d_done = std::make_shared<std::atomic<int>>(0);
    auto d_saw_b = std::make_shared<std::atomic<bool>>(false);
    auto d_saw_c = std::make_shared<std::atomic<bool>>(false);

    auto ta = GraphTask::create("A", [] {});
    auto tb = GraphTask::create("B", [b_done] { b_done->store(1); });
    auto tc = GraphTask::create("C", [c_done] { c_done->store(1); });
    auto td = GraphTask::create("D", [b_done, c_done, d_done, d_saw_b, d_saw_c] {
      d_saw_b->store(b_done->load() == 1);
      d_saw_c->store(c_done->load() == 1);
      d_done->store(1);
    });

    ta->precede(tb);
    ta->precede(tc);
    tb->precede(td);
    tc->precede(td);
    td->set_policy(GraphTask::kPolicyWaitAll);

    run_graph(ta);

    CHECK(d_done->load() == 1);
    CHECK(d_saw_b->load());
    CHECK(d_saw_c->load());
  }

  TEST_CASE("diamond DAG executes all four tasks") {
    auto a_ran = std::make_shared<std::atomic<int>>(0);
    auto b_ran = std::make_shared<std::atomic<int>>(0);
    auto c_ran = std::make_shared<std::atomic<int>>(0);
    auto d_ran = std::make_shared<std::atomic<int>>(0);

    auto ta = GraphTask::create("A", [a_ran] { a_ran->store(1); });
    auto tb = GraphTask::create("B", [b_ran] { b_ran->store(1); });
    auto tc = GraphTask::create("C", [c_ran] { c_ran->store(1); });
    auto td = GraphTask::create("D", [d_ran] { d_ran->store(1); });

    ta->precede(tb);
    ta->precede(tc);
    tb->precede(td);
    tc->precede(td);
    td->set_policy(GraphTask::kPolicyWaitAll);

    run_graph(ta);

    CHECK(a_ran->load() == 1);
    CHECK(b_ran->load() == 1);
    CHECK(c_ran->load() == 1);
    CHECK(d_ran->load() == 1);
  }

  TEST_CASE("diamond DAG ordering D runs after predecessors") {
    auto b_ran = std::make_shared<std::atomic<int>>(0);
    auto c_ran = std::make_shared<std::atomic<int>>(0);
    auto d_ran = std::make_shared<std::atomic<int>>(0);

    auto ta = GraphTask::create("A", [] {});
    auto tb = GraphTask::create("B", [b_ran] { b_ran->store(1); });
    auto tc = GraphTask::create("C", [c_ran] { c_ran->store(1); });
    auto td = GraphTask::create("D", [d_ran] { d_ran->store(1); });

    ta->precede(tb);
    ta->precede(tc);
    tb->precede(td);
    tc->precede(td);
    td->set_policy(GraphTask::kPolicyWaitAll);

    run_graph(ta);

    CHECK(b_ran->load() == 1);
    CHECK(c_ran->load() == 1);
    CHECK(d_ran->load() == 1);
  }

  TEST_CASE("condition return 0 activates branch_0") {
    auto branch0 = std::make_shared<std::atomic<int>>(0);
    auto branch1 = std::make_shared<std::atomic<int>>(0);

    auto cond = GraphTask::create_condition("cond", [] { return 0; }, 2);
    auto b0 = GraphTask::create("branch_0", [branch0] { branch0->store(1); });
    auto b1 = GraphTask::create("branch_1", [branch1] { branch1->store(1); });

    b1->set_condition_number(1);
    cond->precede(b0);
    cond->precede(b1);

    run_graph(cond);

    CHECK(branch0->load() == 1);
    CHECK(branch1->load() == 0);
  }

  TEST_CASE("condition return 1 activates branch_1") {
    auto branch0 = std::make_shared<std::atomic<int>>(0);
    auto branch1 = std::make_shared<std::atomic<int>>(0);

    auto cond = GraphTask::create_condition("cond", [] { return 1; }, 2);
    auto b0 = GraphTask::create("branch_0", [branch0] { branch0->store(1); });
    auto b1 = GraphTask::create("branch_1", [branch1] { branch1->store(1); });

    b1->set_condition_number(1);
    cond->precede(b0);
    cond->precede(b1);

    run_graph(cond);

    CHECK(branch0->load() == 0);
    CHECK(branch1->load() == 1);
  }

  TEST_CASE("condition out-of-range skips all branches") {
    auto branch0 = std::make_shared<std::atomic<int>>(0);
    auto branch1 = std::make_shared<std::atomic<int>>(0);

    auto cond = GraphTask::create_condition("cond", [] { return 99; }, 2);
    auto b0 = GraphTask::create("branch_0", [branch0] { branch0->store(1); });
    auto b1 = GraphTask::create("branch_1", [branch1] { branch1->store(1); });

    b1->set_condition_number(1);
    cond->precede(b0);
    cond->precede(b1);

    run_graph(cond);

    CHECK(branch0->load() == 0);
    CHECK(branch1->load() == 0);
  }

  TEST_CASE("operator --> builds chain") {
    auto order = std::make_shared<std::vector<int>>();
    auto mtx = std::make_shared<std::mutex>();

    auto ta = GraphTask::create("A", [order, mtx] {
      std::lock_guard l(*mtx);
      order->push_back(0);
    });
    auto tb = GraphTask::create("B", [order, mtx] {
      std::lock_guard l(*mtx);
      order->push_back(1);
    });
    auto tc = GraphTask::create("C", [order, mtx] {
      std::lock_guard l(*mtx);
      order->push_back(2);
    });

    ta-- > tb-- > tc;  // NOLINT(bugprone-chained-comparison)

    run_graph(ta);

    REQUIRE(order->size() == 3U);
    CHECK((*order)[0] == 0);
    CHECK((*order)[1] == 1);
    CHECK((*order)[2] == 2);
  }

  TEST_CASE("status transitions Pending->Running->Done") {
    auto statuses = std::make_shared<std::vector<GraphTask::Status>>();
    auto mtx = std::make_shared<std::mutex>();
    auto inside = std::make_shared<std::atomic<bool>>(false);
    auto release = std::make_shared<std::atomic<bool>>(false);

    auto task = GraphTask::create("status_track", [inside, release] {
      inside->store(true);
      while (!release->load()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
      }
    });

    task->register_status_callback([statuses, mtx](const std::string&, GraphTask::Status s) {
      std::lock_guard lock(*mtx);
      statuses->push_back(s);
    });

    task->execute(&get_shared_pool());

    while (!inside->load()) {
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    GraphTask::Status mid_status = task->get_status();
    release->store(true);

    auto dl = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (task->get_status() != GraphTask::kStatusDone && std::chrono::steady_clock::now() < dl) {
      std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }

    CHECK(mid_status == GraphTask::kStatusRunning);
    CHECK(task->get_status() == GraphTask::kStatusDone);

    std::lock_guard lock(*mtx);
    bool has_running = false;
    bool has_done = false;
    for (auto s : *statuses) {
      if (s == GraphTask::kStatusRunning) {
        has_running = true;
      }

      if (s == GraphTask::kStatusDone) {
        has_done = true;
      }
    }
    CHECK(has_running);
    CHECK(has_done);
  }

  TEST_CASE("large fan-out") {
    constexpr int kFanOut = 5;
    auto count = std::make_shared<std::atomic<int>>(0);

    auto root = GraphTask::create("root", [count] { count->fetch_add(1); });
    std::vector<GraphTaskPtr> leaves;
    for (int i = 0; i < kFanOut; ++i) {
      auto leaf = GraphTask::create("leaf_" + std::to_string(i), [count] { count->fetch_add(1); });
      root->precede(leaf);
      leaves.push_back(leaf);
    }

    run_graph(root);

    CHECK(count->load() >= kFanOut + 1);
  }

  TEST_CASE("condition task with downstream chain") {
    auto branch0_count = std::make_shared<std::atomic<int>>(0);
    auto after_branch0 = std::make_shared<std::atomic<int>>(0);

    auto cond = GraphTask::create_condition("cond", [] { return 0; }, 2);
    auto b0 = GraphTask::create("b0", [branch0_count] { branch0_count->store(1); });
    auto b1 = GraphTask::create("b1", [] {});
    auto after = GraphTask::create("after", [after_branch0] { after_branch0->store(1); });

    b1->set_condition_number(1);
    cond->precede(b0);
    cond->precede(b1);
    b0->precede(after);

    run_graph(cond);

    CHECK(branch0_count->load() == 1);
    CHECK(after_branch0->load() == 1);
  }

  TEST_CASE("condition skipped branch still satisfies wait-all join") {
    auto b0_count = std::make_shared<std::atomic<int>>(0);
    auto b1_count = std::make_shared<std::atomic<int>>(0);
    auto join_count = std::make_shared<std::atomic<int>>(0);

    auto cond = GraphTask::create_condition("cond_join", [] { return 0; }, 2);
    auto b0 = GraphTask::create("b0_join", [b0_count] { b0_count->fetch_add(1); });
    auto b1 = GraphTask::create("b1_join", [b1_count] { b1_count->fetch_add(1); });
    auto join = GraphTask::create("join", [join_count] { join_count->fetch_add(1); });

    b1->set_condition_number(1);
    join->set_policy(GraphTask::kPolicyWaitAll);

    cond->precede(b0);
    cond->precede(b1);
    b0->precede(join);
    b1->precede(join);

    run_graph(cond, 100);

    CHECK(b0_count->load() == 1);
    CHECK(b1_count->load() == 0);
    CHECK(join_count->load() == 1);
  }

  TEST_CASE("pending status callback may inspect successors") {
    auto inspected = std::make_shared<std::atomic<bool>>(false);

    auto root = GraphTask::create("pending_root", [] {});
    auto child = GraphTask::create("pending_child", [] {});
    root->precede(child);

    std::weak_ptr<GraphTask> weak_root = root;
    root->register_status_callback([weak_root, inspected](const std::string&, GraphTask::Status status) {
      if (status == GraphTask::kStatusPending) {
        auto root = weak_root.lock();

        if VUNLIKELY (!root) {
          return;
        }

        auto list = root->get_succeed_task_list();
        inspected->store(!list.empty(), std::memory_order_release);
      }
    });

    run_graph(root, 50);

    CHECK(inspected->load(std::memory_order_acquire));
  }

  TEST_CASE("execute cancels task when engine rejects post") {
    auto task = GraphTask::create("reject_post", [] {});
    RejectGraphEngine engine;

    task->execute(&engine);

    CHECK(task->get_status() == GraphTask::kStatusInActive);
  }

  TEST_CASE("throwing callback is caught; task transitions to kStatusDone and downstream runs") {
    auto downstream_ran = std::make_shared<std::atomic<int>>(0);

    auto a = GraphTask::create("A", [] { throw std::runtime_error("boom"); });
    auto b = GraphTask::create("B", [downstream_ran] { downstream_ran->store(1); });

    a->precede(b);

    run_graph(a);

    CHECK(a->get_status() == GraphTask::kStatusDone);
    CHECK(downstream_ran->load() == 1);
  }

  TEST_CASE("register_status_callback supports multiple subscribers; both fire") {
    auto subA_calls = std::make_shared<std::atomic<int>>(0);
    auto subB_calls = std::make_shared<std::atomic<int>>(0);

    auto task = GraphTask::create("multi", [] {});

    uint32_t id_a = task->register_status_callback(
        [subA_calls](const std::string&, GraphTask::Status) { subA_calls->fetch_add(1); });
    uint32_t id_b = task->register_status_callback(
        [subB_calls](const std::string&, GraphTask::Status) { subB_calls->fetch_add(1); });

    CHECK(id_a != 0);
    CHECK(id_b != 0);
    CHECK(id_a != id_b);

    run_graph(task);

    // Both subscribers see Pending->Running->Done transitions
    CHECK(subA_calls->load() >= 1);
    CHECK(subB_calls->load() >= 1);
    CHECK(subA_calls->load() == subB_calls->load());
  }

  TEST_CASE("unregister_status_callback removes a specific subscription") {
    auto subA_calls = std::make_shared<std::atomic<int>>(0);
    auto subB_calls = std::make_shared<std::atomic<int>>(0);

    auto task = GraphTask::create("unreg", [] {});

    uint32_t id_a = task->register_status_callback(
        [subA_calls](const std::string&, GraphTask::Status) { subA_calls->fetch_add(1); });
    task->register_status_callback([subB_calls](const std::string&, GraphTask::Status) { subB_calls->fetch_add(1); });

    CHECK(task->unregister_status_callback(id_a) == true);
    CHECK(task->unregister_status_callback(id_a) == false);  // second unregister is a no-op

    run_graph(task);

    CHECK(subA_calls->load() == 0);
    CHECK(subB_calls->load() >= 1);
  }

  TEST_CASE("register_status_callback returns 0 for empty callback") {
    auto task = GraphTask::create("empty_cb", [] {});

    GraphTask::StatusCallback empty;
    uint32_t id = task->register_status_callback(std::move(empty));

    CHECK(id == 0);
  }

  TEST_CASE("throwing condition_callback gracefully disables all branches (no deadlock)") {
    auto b0_ran = std::make_shared<std::atomic<int>>(0);
    auto b1_ran = std::make_shared<std::atomic<int>>(0);

    auto cond = GraphTask::create_condition("cond", []() -> int { throw std::runtime_error("boom"); }, 2);
    auto b0 = GraphTask::create("b0", [b0_ran] { b0_ran->store(1); });
    auto b1 = GraphTask::create("b1", [b1_ran] { b1_ran->store(1); });

    b1->set_condition_number(1);
    cond->precede(b0);
    cond->precede(b1);

    run_graph(cond);

    CHECK(cond->get_status() == GraphTask::kStatusDone);
    CHECK(b0_ran->load() == 0);
    CHECK(b1_ran->load() == 0);
  }

  TEST_CASE("succeed that would create a transitive cycle is rejected with rollback") {
    auto a = GraphTask::create("a", [] {});
    auto b = GraphTask::create("b", [] {});
    auto c = GraphTask::create("c", [] {});

    a->precede(b);
    b->precede(c);

    REQUIRE(!a->has_cycle());

    // a.succeed(c) means c precedes a → adds edge c→a, closing cycle a→b→c→a
    a->succeed(c);

    CHECK(!a->has_cycle());
    CHECK(a->get_precede_task_list().empty());

    auto c_succ = c->get_succeed_task_list();
    bool c_has_a_edge = false;

    for (const auto& w : c_succ) {
      auto p = w.lock();

      if (p && p.get() == a.get()) {
        c_has_a_edge = true;
      }
    }

    CHECK(!c_has_a_edge);
  }

  TEST_CASE("precede that would create a transitive cycle is rejected with rollback") {
    auto a = GraphTask::create("a", [] {});
    auto b = GraphTask::create("b", [] {});
    auto c = GraphTask::create("c", [] {});

    a->precede(b);
    b->precede(c);

    REQUIRE(!c->has_cycle());

    // c.precede(a) means c before a → adds edge c→a, closing cycle a→b→c→a
    c->precede(a);

    CHECK(!c->has_cycle());
    CHECK(a->get_precede_task_list().empty());

    auto c_succ = c->get_succeed_task_list();
    bool c_has_a_edge = false;

    for (const auto& w : c_succ) {
      auto p = w.lock();

      if (p && p.get() == a.get()) {
        c_has_a_edge = true;
      }
    }

    CHECK(!c_has_a_edge);
  }

  TEST_CASE("throwing status_callback is caught; remaining subscribers still fire") {
    auto throwing_calls = std::make_shared<std::atomic<int>>(0);
    auto safe_calls = std::make_shared<std::atomic<int>>(0);

    auto task = GraphTask::create("throw_sub", [] {});

    task->register_status_callback([throwing_calls](const std::string&, GraphTask::Status) {
      throwing_calls->fetch_add(1);
      throw std::runtime_error("boom_from_status_callback");
    });

    task->register_status_callback([safe_calls](const std::string&, GraphTask::Status) { safe_calls->fetch_add(1); });

    run_graph(task);

    CHECK(task->get_status() == GraphTask::kStatusDone);
    CHECK(throwing_calls->load() >= 1);
    CHECK(safe_calls->load() >= 1);
    CHECK(safe_calls->load() == throwing_calls->load());
  }

  TEST_CASE("status callback may call set_name and get_name without deadlock") {
    auto task = GraphTask::create("original", [] {});
    auto saw_name_in_done = std::make_shared<std::string>();
    std::weak_ptr<GraphTask> weak = task;

    task->register_status_callback([weak, saw_name_in_done](const std::string&, GraphTask::Status status) {
      auto t = weak.lock();

      if (!t) {
        return;
      }

      if (status == GraphTask::kStatusRunning) {
        t->set_name("renamed_in_callback");
      }

      if (status == GraphTask::kStatusDone) {
        *saw_name_in_done = t->get_name();
      }
    });

    run_graph(task);

    CHECK(task->get_status() == GraphTask::kStatusDone);
    CHECK(task->get_name() == "renamed_in_callback");
    CHECK(*saw_name_in_done == "renamed_in_callback");
  }

  TEST_CASE("kPolicyWaitAll fires exactly once after all predecessors complete") {
    auto fired = std::make_shared<std::atomic<int>>(0);

    auto root = GraphTask::create("root", [] {});
    auto p1 = GraphTask::create("p1", [] {});
    auto p2 = GraphTask::create("p2", [] {});
    auto p3 = GraphTask::create("p3", [] {});
    auto sink = GraphTask::create("sink", [fired] { fired->fetch_add(1); });

    sink->set_policy(GraphTask::kPolicyWaitAll);

    root->precede(p1);
    root->precede(p2);
    root->precede(p3);

    p1->precede(sink);
    p2->precede(sink);
    p3->precede(sink);

    run_graph(root, 100);

    CHECK(sink->get_status() == GraphTask::kStatusDone);
    CHECK(fired->load() == 1);
  }

  TEST_CASE("precede cycle pre-check never mutates lists (no transient invalid edge)") {
    auto a = GraphTask::create("a", [] {});
    auto b = GraphTask::create("b", [] {});
    auto c = GraphTask::create("c", [] {});

    a->precede(b);
    b->precede(c);

    REQUIRE(a->get_succeed_task_list().size() == 1U);
    REQUIRE(b->get_succeed_task_list().size() == 1U);
    REQUIRE(c->get_succeed_task_list().empty());

    c->precede(a);

    CHECK(a->get_succeed_task_list().size() == 1U);
    CHECK(b->get_succeed_task_list().size() == 1U);
    CHECK(c->get_succeed_task_list().empty());
    CHECK(a->get_precede_task_list().empty());
    CHECK(c->get_precede_task_list().size() == 1U);
  }

  TEST_CASE("succeed cycle pre-check never mutates lists (no transient invalid edge)") {
    auto a = GraphTask::create("a", [] {});
    auto b = GraphTask::create("b", [] {});
    auto c = GraphTask::create("c", [] {});

    a->precede(b);
    b->precede(c);

    a->succeed(c);

    CHECK(a->get_succeed_task_list().size() == 1U);
    CHECK(b->get_succeed_task_list().size() == 1U);
    CHECK(c->get_succeed_task_list().empty());
    CHECK(a->get_precede_task_list().empty());
  }

  TEST_CASE("register_status_callback assigns unique non-zero ids across many registrations") {
    auto task = GraphTask::create("uniq", [] {});

    std::unordered_set<uint32_t> ids;

    for (int i = 0; i < 256; ++i) {
      uint32_t id = task->register_status_callback([](const std::string&, GraphTask::Status) {});
      REQUIRE(id != 0U);
      CHECK(ids.insert(id).second);
    }

    for (uint32_t id : ids) {
      CHECK(task->unregister_status_callback(id));
    }
  }

  TEST_CASE("cancel cascades to reachable successors") {
    auto a = GraphTask::create("a", [] {});
    auto b = GraphTask::create("b", [] {});
    auto c = GraphTask::create("c", [] {});

    a->precede(b);
    b->precede(c);

    a->cancel();

    CHECK(a->get_status() == GraphTask::kStatusInActive);
    CHECK(b->get_status() == GraphTask::kStatusInActive);
    CHECK(c->get_status() == GraphTask::kStatusInActive);
  }
}

// NOLINTEND
