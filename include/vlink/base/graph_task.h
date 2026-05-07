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

/**
 * @file graph_task.h
 * @brief DAG-based task graph with condition branching, cycle detection, and DOT export.
 *
 * @details
 * @c GraphTask implements a directed acyclic graph (DAG) of work units.  Tasks define
 * their dependencies by calling @c precede() (this task must finish before the target)
 * or @c succeed() (the target must finish before this task).  The entire graph is then
 * submitted to any @c MessageLoop, @c MultiLoop or @c ThreadPool via @c execute().
 *
 * Task types:
 *
 * | Factory                        | Callback signature       | Use case                            |
 * | ------------------------------ | ------------------------ | ----------------------------------- |
 * | @c create(callback)            | @c void()                | Regular work task                   |
 * | @c create_condition(callback)  | @c int()                 | Branch selector (returns branch id) |
 *
 * Condition task:
 * A condition task returns an integer selecting which successor sub-graph to activate.
 * All other successors are skipped.  This enables if/switch style DAG branching.
 *
 * Execution policies:
 *
 * | Policy            | Behaviour                                                         |
 * | ----------------- | ----------------------------------------------------------------- |
 * | @c kPolicyOnce    | Task runs exactly once per @c execute() call (default)            |
 * | @c kPolicyMultiple| Task may run multiple times in one @c execute() pass              |
 * | @c kPolicyWaitAll | Task waits for all predecessors before running (default DAG rule) |
 *
 * Operator syntax for building graphs:
 * @code
 * auto A = vlink::GraphTask::create("A", []{ step_a(); });
 * auto B = vlink::GraphTask::create("B", []{ step_b(); });
 * auto C = vlink::GraphTask::create("C", []{ step_c(); });
 *
 * A-- > B-- > C;  // A runs before B, B runs before C (execution order: A, B, C)
 * @endcode
 *
 * @note
 * - @c execute() traverses the reachable sub-graph and dispatches all ready tasks to the engine.
 * - @c has_cycle() detects cycles using DFS; cycles cause undefined behaviour at runtime.
 * - @c export_to_dot() produces a Graphviz DOT string for visualisation.
 *
 * @par Example
 * @code
 * vlink::MultiLoop engine(4);
 * engine.async_run();
 *
 * auto load  = vlink::GraphTask::create("load",  [] { load_data(); });
 * auto proc  = vlink::GraphTask::create("proc",  [] { process();   });
 * auto save  = vlink::GraphTask::create("save",  [] { save_data(); });
 *
 * // A-- > B calls A->succeed(B), meaning A must complete before B.
 * // So save-- > proc-- > load produces execution order: save, proc, load.
 * save-- > proc-- > load;
 *
 * assert(!save->has_cycle());
 * save->execute(&engine);
 * @endcode
 */

#pragma once

#include <memory>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

#include "./functional.h"
#include "./macros.h"
#include "./traits.h"

namespace vlink {

/**
 * @class GraphTask
 * @brief Node in a directed acyclic task graph supporting condition branching and parallel execution.
 *
 * @details
 * Must be created via one of the static factory methods (@c create / @c create_condition).
 * Inherits from @c std::enable_shared_from_this to safely pass @c shared_ptr to internal callbacks.
 */
class VLINK_EXPORT GraphTask final : public std::enable_shared_from_this<GraphTask> {
 public:
  /**
   * @brief Execution state of the task within a single @c execute() pass.
   */
  enum Status : uint8_t {
    kStatusInActive = 0,  ///< Not yet submitted or cancelled
    kStatusPending = 1,   ///< Waiting for predecessors to complete
    kStatusRunning = 2,   ///< Currently executing
    kStatusDone = 3,      ///< Execution completed
  };

  /**
   * @brief Execution policy controlling how many times and when the task runs.
   */
  enum Policy : uint8_t {
    kPolicyOnce = 0,      ///< Run exactly once per execute() call (default)
    kPolicyMultiple = 1,  ///< Allow multiple invocations in a single pass
    kPolicyWaitAll = 2,   ///< Wait for ALL predecessors before running
  };

  /**
   * @brief Callback type for regular (void-returning) tasks.
   */
  using Callback = vlink::Function<void()>;

  /**
   * @brief Callback type for condition tasks; returns the branch index to activate.
   */
  using ConditionCallback = vlink::Function<int()>;

  /**
   * @brief Callback for status change notifications.
   *
   * @details
   * Called whenever the task's @c Status changes.
   * First argument is the task name; second is the new status.
   */
  using StatusCallback = vlink::Function<void(const std::string&, Status)>;

  /**
   * @brief Creates a regular (void) task node.
   *
   * @param callback          Work function to execute.
   * @param condition_number  Number of successor branches available.  0 = no branches.
   * @return Shared pointer to the new task.
   */
  [[nodiscard]] static std::shared_ptr<GraphTask> create(Callback&& callback, int condition_number = 0);

  /**
   * @brief Creates a named regular (void) task node.
   *
   * @param name              Task name (used in DOT export and status callbacks).
   * @param callback          Work function to execute.
   * @param condition_number  Number of successor branches available.  0 = no branches.
   * @return Shared pointer to the new task.
   */
  [[nodiscard]] static std::shared_ptr<GraphTask> create(const std::string& name, Callback&& callback,
                                                         int condition_number = 0);

  /**
   * @brief Creates a condition task that returns a branch index.
   *
   * @details
   * The integer returned by @p callback selects which successor sub-graph to activate.
   * Return values outside [0, condition_number) skip all successors.
   *
   * @param callback          Condition function returning the branch index.
   * @param condition_number  Number of possible branches.
   * @return Shared pointer to the new condition task.
   */
  [[nodiscard]] static std::shared_ptr<GraphTask> create_condition(ConditionCallback&& callback,
                                                                   int condition_number = 0);

  /**
   * @brief Creates a named condition task.
   *
   * @param name              Task name.
   * @param callback          Condition function returning the branch index.
   * @param condition_number  Number of possible branches.
   * @return Shared pointer to the new condition task.
   */
  [[nodiscard]] static std::shared_ptr<GraphTask> create_condition(const std::string& name,
                                                                   ConditionCallback&& callback,
                                                                   int condition_number = 0);

  /**
   * @brief Submits this task (and all reachable successors) to a graph execution engine.
   *
   * @details
   * Traverses the reachable sub-graph, identifies tasks whose predecessors have all
   * completed, and posts them to @p graph_engine using either @c post_task() or
   * @c post_task_with_priority() (if available).
   *
   * @tparam GraphEngineT  Any type providing @c post_task(Callback) and optionally
   *                       @c post_task_with_priority(Callback, uint16_t).
   *                       @c MessageLoop, @c MultiLoop and @c ThreadPool all satisfy this.
   * @param graph_engine   Pointer to the execution engine.
   */
  template <class GraphEngineT>
  void execute(GraphEngineT* graph_engine);

  /**
   * @brief Cancels this task; sets its status to @c kStatusInActive.
   *
   * @details
   * A cancelled task will not be executed even if all its predecessors complete.
   */
  void cancel();

  /**
   * @brief Declares that @p task must complete before this task starts.
   *
   * @details
   * Equivalent to @c task->succeed(this_task).
   *
   * @param task  Predecessor task that must run before @c *this.
   */
  void precede(const std::shared_ptr<GraphTask>& task);

  /**
   * @brief Declares that this task must complete before @p task starts.
   *
   * @details
   * Equivalent to @c task->precede(this_task).
   *
   * @param task  Successor task that runs after @c *this.
   */
  void succeed(const std::shared_ptr<GraphTask>& task);

  /**
   * @brief Registers a callback invoked whenever this task's status changes.
   *
   * @param callback  Called with (name, new_status) on every status transition.
   */
  void register_status_callback(StatusCallback&& callback);

  /**
   * @brief Sets the task name used in DOT export and status callbacks.
   *
   * @param name  Task name string.
   */
  void set_name(const std::string& name);

  /**
   * @brief Sets a group name for visual grouping in DOT export.
   *
   * @param name  Group name string.
   */
  void set_group_name(const std::string& name);

  /**
   * @brief Sets the number of condition branches this task can select.
   *
   * @param condition_number  Branch count (used for condition tasks).
   */
  void set_condition_number(int condition_number);

  /**
   * @brief Sets the task dispatch priority (used by priority-aware engines).
   *
   * @param priority  Priority value.
   */
  void set_priority(uint16_t priority);

  /**
   * @brief Sets the maximum recursion depth to guard against infinitely deep graphs.
   *
   * @details
   * If a task graph exceeds this depth during execution, a @c kFatal log is emitted.
   * The default value is 10000.
   *
   * @param depth  Maximum depth.
   */
  void set_max_recursion_depth(uint32_t depth);

  /**
   * @brief Sets the execution policy.
   *
   * @param policy  See @c Policy enum.
   */
  void set_policy(Policy policy);

  /**
   * @brief Returns the task name.
   *
   * @return Task name string.
   */
  [[nodiscard]] std::string get_name() const;

  /**
   * @brief Returns the group name.
   *
   * @return Group name string.
   */
  [[nodiscard]] std::string get_group_name() const;

  /**
   * @brief Returns the number of condition branches.
   *
   * @return Condition number.
   */
  [[nodiscard]] int get_condition_number() const;

  /**
   * @brief Returns the dispatch priority.
   *
   * @return Priority value.
   */
  [[nodiscard]] uint16_t get_priority() const;

  /**
   * @brief Returns the maximum recursion depth.
   *
   * @return Max recursion depth.  Default is 10000.
   */
  [[nodiscard]] uint32_t get_max_recursion_depth() const;

  /**
   * @brief Returns the execution policy.
   *
   * @return Policy value.
   */
  [[nodiscard]] Policy get_policy() const;

  /**
   * @brief Returns the current execution status of this task.
   *
   * @return Status value.
   */
  [[nodiscard]] Status get_status() const;

  /**
   * @brief Removes a previously added predecessor dependency.
   *
   * @param task  The predecessor to remove.
   */
  void remove_precede_task(const std::shared_ptr<GraphTask>& task);

  /**
   * @brief Removes a previously added successor dependency.
   *
   * @param task  The successor to remove.
   */
  void remove_succeed_task(const std::shared_ptr<GraphTask>& task);

  /**
   * @brief Returns the list of predecessor tasks (this task depends on them).
   *
   * @return Vector of weak pointers to predecessor tasks.
   */
  [[nodiscard]] std::vector<std::weak_ptr<GraphTask>> get_precede_task_list() const;

  /**
   * @brief Returns the list of successor tasks (they depend on this task).
   *
   * @return Vector of weak pointers to successor tasks.
   */
  [[nodiscard]] std::vector<std::weak_ptr<GraphTask>> get_succeed_task_list() const;

  /**
   * @brief Returns @c true if this task was created with @c create_condition().
   *
   * @return @c true for condition tasks.
   */
  [[nodiscard]] bool is_condition_task() const;

  /**
   * @brief Detects whether the reachable sub-graph contains any cycles.
   *
   * @details
   * Uses DFS with a recursion stack to find back edges.  A cycle-free graph is required
   * for correct @c execute() behaviour.
   *
   * @return @c true if a cycle is detected.
   */
  [[nodiscard]] bool has_cycle() const;

  /**
   * @brief Exports the reachable sub-graph as a Graphviz DOT string.
   *
   * @details
   * The DOT output can be passed to @c dot -Tpng to produce a dependency diagram.
   *
   * @return DOT language string representing the task graph.
   */
  [[nodiscard]] std::string export_to_dot() const;

 protected:
  using FindTaskCallback = vlink::Function<void(const std::shared_ptr<GraphTask>&)>;

  explicit GraphTask(Callback&& callback, int condition_number);

  explicit GraphTask(const std::string& name, Callback&& callback, int condition_number);

  explicit GraphTask(ConditionCallback&& callback, int condition_number);

  explicit GraphTask(const std::string& name, ConditionCallback&& callback, int condition_number);

  ~GraphTask();

  void process_and_traverse(const FindTaskCallback& callback);

 private:
  int invoke(bool once);

  void wait();

  void notify(int condition_number);

  void update_status(Status status);

  bool detect_cycle(const GraphTask* task, std::unordered_set<const GraphTask*>& visited,
                    std::unordered_set<const GraphTask*>& recursion_stack) const;

  static void clear_invalid_task(const std::shared_ptr<GraphTask>& task);

  struct Impl;
  std::unique_ptr<Impl> impl_;

  VLINK_DISALLOW_COPY_AND_ASSIGN(GraphTask)
};

using GraphTaskPtr = std::shared_ptr<GraphTask>;

////////////////////////////////////////////////////////////////
/// Details
////////////////////////////////////////////////////////////////

template <class GraphEngineT>
inline void GraphTask::execute(GraphEngineT* graph_engine) {
  auto self = shared_from_this();

  process_and_traverse([self, graph_engine](const std::shared_ptr<GraphTask>& task) {
    constexpr bool kHaspriority = VLINK_HAS_MEMBER(GraphEngineT, post_task_with_priority);
    [[maybe_unused]] constexpr uint8_t kPriorityType = 2;

    auto task_func = [self, task]() {
      if (task.get() != self.get()) {
        task->wait();
      }

      int ret = task->invoke(true);

      if (ret >= 0) {
        task->notify(ret);
      }
    };

    if constexpr (kHaspriority) {
      if constexpr (VLINK_HAS_MEMBER(GraphEngineT, get_type)) {
        if (graph_engine->get_type() == kPriorityType) {
          graph_engine->post_task_with_priority(std::move(task_func), task->get_priority());
        } else {
          graph_engine->post_task(std::move(task_func));
        }
      } else {
        graph_engine->post_task(std::move(task_func));
      }
    } else {
      graph_engine->post_task(std::move(task_func));
    }
  });
}

[[maybe_unused]] static inline GraphTaskPtr& operator--(GraphTaskPtr& task, int) { return task; }

[[maybe_unused]] static inline GraphTaskPtr& operator>(GraphTaskPtr& task, GraphTaskPtr& target_task) {
  task->succeed(target_task);
  return target_task;
}

[[maybe_unused]] static inline GraphTaskPtr& operator<(GraphTaskPtr& task, GraphTaskPtr& target_task) {
  task->precede(target_task);
  return target_task;
}

[[maybe_unused]] static inline GraphTaskPtr& operator--(GraphTaskPtr& task) { return task; }

}  // namespace vlink
