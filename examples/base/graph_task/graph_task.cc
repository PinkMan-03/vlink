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

// Example: GraphTask - DAG execution with dependencies and conditions

#include <vlink/base/graph_task.h>
#include <vlink/base/logger.h>
#include <vlink/base/multi_loop.h>

#include <thread>

#include "task_graph_builder.h"

int main() {
  // ---------------------------------------------------------------
  // 1. Simple linear DAG: A --> B --> C
  // ---------------------------------------------------------------
  {
    VLOG_I("=== Section 1: Linear DAG (A -> B -> C) ===");
    vlink::MultiLoop engine(4);
    engine.async_run();

    auto root = task_graph_builder::build_linear_dag();
    MLOG_I("  Has cycle: {}", root->has_cycle());

    root->execute(&engine);
    engine.wait_for_idle();

    engine.quit();
    engine.wait_for_quit();
  }

  // ---------------------------------------------------------------
  // 2. Diamond DAG: A --> B, A --> C, B --> D, C --> D
  // ---------------------------------------------------------------
  {
    VLOG_I("=== Section 2: Diamond DAG ===");
    vlink::MultiLoop engine(4);
    engine.async_run();

    auto root = task_graph_builder::build_diamond_dag();

    root->execute(&engine);
    engine.wait_for_idle();

    engine.quit();
    engine.wait_for_quit();
  }

  // ---------------------------------------------------------------
  // 3. Condition task - branch based on runtime result.
  // ---------------------------------------------------------------
  {
    VLOG_I("=== Section 3: Condition branching ===");
    vlink::MultiLoop engine(4);
    engine.async_run();

    auto root = task_graph_builder::build_conditional_dag(1);

    root->execute(&engine);
    engine.wait_for_idle();

    engine.quit();
    engine.wait_for_quit();
  }

  // ---------------------------------------------------------------
  // 4. Status callback - monitor task state transitions.
  // ---------------------------------------------------------------
  {
    VLOG_I("=== Section 4: Status callbacks ===");
    vlink::MultiLoop engine(2);
    engine.async_run();

    auto task = vlink::GraphTask::create("Monitored", []() {
      VLOG_I("  Monitored task executing");
      std::this_thread::sleep_for(std::chrono::milliseconds(30));
    });

    task->register_status_callback([](const std::string& name, vlink::GraphTask::Status status) {
      const char* status_str = "unknown";
      switch (status) {
        case vlink::GraphTask::kStatusInActive:
          status_str = "InActive";
          break;
        case vlink::GraphTask::kStatusPending:
          status_str = "Pending";
          break;
        case vlink::GraphTask::kStatusRunning:
          status_str = "Running";
          break;
        case vlink::GraphTask::kStatusDone:
          status_str = "Done";
          break;
      }
      MLOG_I("  [StatusCallback] {} -> {}", name, status_str);
    });

    task->execute(&engine);
    engine.wait_for_idle();

    engine.quit();
    engine.wait_for_quit();
  }

  // ---------------------------------------------------------------
  // 5. DOT export for graph visualization.
  // ---------------------------------------------------------------
  {
    VLOG_I("=== Section 5: DOT export ===");

    auto root = task_graph_builder::build_pipeline_dag();
    std::string dot = root->export_to_dot();
    MLOG_I("  DOT output:\n{}", dot);
  }

  VLOG_I("GraphTask example finished.");
  return 0;
}
