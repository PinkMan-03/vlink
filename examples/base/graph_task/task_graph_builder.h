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

#pragma once

/**
 * @file task_graph_builder.h
 * @brief Helper functions for building common DAG patterns with GraphTask.
 *
 * Provides factory functions that construct frequently-used graph topologies:
 *   - Linear chain:  A -> B -> C
 *   - Diamond:       A -> {B, C} -> D
 *   - Conditional:   Start -> Branch -> {Path0, Path1} -> Finish
 */

#include <vlink/base/graph_task.h>
#include <vlink/base/logger.h>

#include <string>
#include <thread>

namespace task_graph_builder {

// Build and return the root of a linear DAG: A -> B -> C.
inline vlink::GraphTaskPtr build_linear_dag() {
  auto a = vlink::GraphTask::create("A", []() { VLOG_I("  Task A: loading data"); });
  auto b = vlink::GraphTask::create("B", []() { VLOG_I("  Task B: processing data"); });
  auto c = vlink::GraphTask::create("C", []() { VLOG_I("  Task C: saving results"); });

  a-- > b-- > c;
  return a;
}

// Build and return the root of a diamond DAG: A -> {B, C} -> D.
// B and C can run in parallel after A completes.
inline vlink::GraphTaskPtr build_diamond_dag() {
  auto a = vlink::GraphTask::create("A", []() { VLOG_I("  Task A: init"); });
  auto b = vlink::GraphTask::create("B", []() {
    VLOG_I("  Task B: path 1 (parallel)");
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
  });
  auto c = vlink::GraphTask::create("C", []() {
    VLOG_I("  Task C: path 2 (parallel)");
    std::this_thread::sleep_for(std::chrono::milliseconds(30));
  });
  auto d = vlink::GraphTask::create("D", []() { VLOG_I("  Task D: merge (waits for B and C)"); });

  a->precede(b);
  a->precede(c);
  b->precede(d);
  c->precede(d);

  d->set_policy(vlink::GraphTask::kPolicyWaitAll);
  return a;
}

// Build and return the root of a conditional DAG with branching.
// The condition_value parameter selects which branch (0 or 1) to take.
inline vlink::GraphTaskPtr build_conditional_dag(int condition_value) {
  auto start = vlink::GraphTask::create("Start", []() { VLOG_I("  Start: evaluating condition"); });

  auto branch = vlink::GraphTask::create_condition("Branch", [condition_value]() -> int {
    MLOG_I("  Branch: selecting path {}", condition_value);
    return condition_value;
  });

  auto path_0 = vlink::GraphTask::create("Path0", []() { VLOG_I("  Path 0: chosen when branch returns 0"); });
  auto path_1 = vlink::GraphTask::create("Path1", []() { VLOG_I("  Path 1: chosen when branch returns 1"); });
  auto finish = vlink::GraphTask::create("Finish", []() { VLOG_I("  Finish: done"); });

  start->precede(branch);
  branch->precede(path_0);
  branch->precede(path_1);
  path_0->precede(finish);
  path_1->precede(finish);

  return start;
}

// Build a DOT-exportable pipeline graph for visualization demos.
inline vlink::GraphTaskPtr build_pipeline_dag() {
  auto a = vlink::GraphTask::create("Load", []() {});
  auto b = vlink::GraphTask::create("Parse", []() {});
  auto c = vlink::GraphTask::create("Validate", []() {});
  auto d = vlink::GraphTask::create("Transform", []() {});
  auto e = vlink::GraphTask::create("Save", []() {});

  a-- > b-- > c;
  b-- > d;
  c-- > e;
  d-- > e;

  return a;
}

}  // namespace task_graph_builder
