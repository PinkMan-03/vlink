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

#include "./base/graph_task.h"

#include <algorithm>
#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <sstream>
#include <stack>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "./base/condition_variable.h"
#include "./base/helpers.h"
#include "./base/logger.h"

namespace vlink {

[[maybe_unused]] static std::atomic<uint32_t> global_graph_task_count = 0;

// GraphTask::Impl
struct GraphTask::Impl final {  // NOLINT(clang-analyzer-optin.performance.Padding)
  alignas(64) std::atomic<size_t> pending_index{0};
  alignas(64) std::atomic<bool> is_ready{false};
  alignas(64) std::atomic<bool> is_enable{false};
  alignas(64) std::atomic<GraphTask::Status> status{GraphTask::kStatusInActive};

  std::atomic<uint32_t> max_recursion_depth{10'000};
  std::atomic<GraphTask::Policy> policy{GraphTask::kPolicyOnce};
  std::atomic<uint16_t> priority{100};
  std::atomic<int> condition_number{0};

  std::vector<std::weak_ptr<GraphTask>> precede_task_list;
  std::vector<std::weak_ptr<GraphTask>> succeed_task_list;

  mutable std::mutex mtx;
  vlink::condition_variable cv;

  std::shared_mutex shared_mtx;

  std::string name;
  std::string group_name;

  GraphTask::Callback callback;
  GraphTask::ConditionCallback condition_callback;
  GraphTask::StatusCallback status_callback;

  bool is_condition_task{false};
};

// GraphTask
std::shared_ptr<GraphTask> GraphTask::create(Callback&& callback, int condition_number) {
  return std::shared_ptr<GraphTask>(new GraphTask(std::move(callback), condition_number),
                                    [](GraphTask* task) { delete task; });
}

std::shared_ptr<GraphTask> GraphTask::create(const std::string& name, Callback&& callback, int condition_number) {
  return std::shared_ptr<GraphTask>(new GraphTask(name, std::move(callback), condition_number),
                                    [](GraphTask* task) { delete task; });
}

std::shared_ptr<GraphTask> GraphTask::create_condition(ConditionCallback&& callback, int condition_number) {
  return std::shared_ptr<GraphTask>(new GraphTask(std::move(callback), condition_number),
                                    [](GraphTask* task) { delete task; });
}

std::shared_ptr<GraphTask> GraphTask::create_condition(const std::string& name, ConditionCallback&& callback,
                                                       int condition_number) {
  return std::shared_ptr<GraphTask>(new GraphTask(name, std::move(callback), condition_number),
                                    [](GraphTask* task) { delete task; });
}

void GraphTask::cancel() {
  std::stack<std::shared_ptr<GraphTask>> task_stack;
  task_stack.emplace(shared_from_this());

  while (!task_stack.empty()) {
    auto current_task = task_stack.top();
    task_stack.pop();

    std::lock_guard lock(current_task->impl_->mtx);

    if (current_task->impl_->status == kStatusInActive) {
      continue;
    }

    current_task->update_status(kStatusInActive);
    current_task->impl_->cv.notify_all();

    for (const auto& weak_task : current_task->impl_->succeed_task_list) {
      if (auto task_ptr = weak_task.lock()) {
        task_stack.emplace(task_ptr);
      }
    }
  }
}

void GraphTask::precede(const std::shared_ptr<GraphTask>& task) {
  if VUNLIKELY (!task || task.get() == this) {
    VLOG_F("GraphTask: Invalid task for precede.");
    return;
  }

  std::unique_lock lock1(this->impl_->mtx, std::defer_lock);
  std::unique_lock lock2(task->impl_->mtx, std::defer_lock);
  std::lock(lock1, lock2);

  if (std::find_if(impl_->precede_task_list.begin(), impl_->precede_task_list.end(), [&task](const auto& weak_task) {
        return weak_task.lock() == task;
      }) != impl_->precede_task_list.end()) {
    VLOG_F("GraphTask: Task already added.");

    return;
  }

  impl_->precede_task_list.emplace_back(task);

  task->impl_->succeed_task_list.emplace_back(shared_from_this());
}

void GraphTask::succeed(const std::shared_ptr<GraphTask>& task) {
  if VUNLIKELY (!task || task.get() == this) {
    VLOG_F("GraphTask: Invalid task for succeed.");
    return;
  }

  std::unique_lock lock1(this->impl_->mtx, std::defer_lock);
  std::unique_lock lock2(task->impl_->mtx, std::defer_lock);
  std::lock(lock1, lock2);

  if (std::find_if(impl_->succeed_task_list.begin(), impl_->succeed_task_list.end(), [&task](const auto& weak_task) {
        return weak_task.lock() == task;
      }) != impl_->succeed_task_list.end()) {
    VLOG_F("GraphTask: Task already added.");

    return;
  }

  impl_->succeed_task_list.emplace_back(task);

  task->impl_->precede_task_list.emplace_back(shared_from_this());
}

void GraphTask::register_status_callback(StatusCallback&& callback) {
  std::lock_guard lock(impl_->shared_mtx);
  impl_->status_callback = std::move(callback);
}

void GraphTask::set_name(const std::string& name) {
  std::lock_guard lock(impl_->shared_mtx);
  impl_->name = name;
}

void GraphTask::set_group_name(const std::string& name) {
  std::lock_guard lock(impl_->shared_mtx);
  impl_->group_name = name;
}

void GraphTask::set_condition_number(int condition_number) { impl_->condition_number = condition_number; }

void GraphTask::set_priority(uint16_t priority) { impl_->priority = priority; }

void GraphTask::set_max_recursion_depth(uint32_t depth) { impl_->max_recursion_depth = depth; }

void GraphTask::set_policy(Policy policy) { impl_->policy = policy; }

std::string GraphTask::get_name() const {
  std::shared_lock lock(impl_->shared_mtx);
  return impl_->name;
}

std::string GraphTask::get_group_name() const {
  std::shared_lock lock(impl_->shared_mtx);
  return impl_->group_name;
}

int GraphTask::get_condition_number() const { return impl_->condition_number; }

uint16_t GraphTask::get_priority() const { return impl_->priority; }

uint32_t GraphTask::get_max_recursion_depth() const { return impl_->max_recursion_depth; }

GraphTask::Policy GraphTask::get_policy() const { return impl_->policy; }

GraphTask::Status GraphTask::get_status() const { return impl_->status; }

void GraphTask::remove_precede_task(const std::shared_ptr<GraphTask>& task) {
  if VUNLIKELY (!task) {
    VLOG_F("GraphTask: Invalid task provided to remove_preceding_task.");
    return;
  }

  std::unique_lock lock1(this->impl_->mtx, std::defer_lock);
  std::unique_lock lock2(task->impl_->mtx, std::defer_lock);
  std::lock(lock1, lock2);

  auto iter_precede = std::remove_if(impl_->precede_task_list.begin(), impl_->precede_task_list.end(),
                                     [&task](const auto& weak_task) { return weak_task.lock() == task; });
  if (iter_precede != impl_->precede_task_list.end()) {
    impl_->precede_task_list.erase(iter_precede, impl_->precede_task_list.end());
  }

  auto iter_succeed = std::remove_if(task->impl_->succeed_task_list.begin(), task->impl_->succeed_task_list.end(),
                                     [this](const auto& weak_task) { return weak_task.lock().get() == this; });
  if (iter_succeed != task->impl_->succeed_task_list.end()) {
    task->impl_->succeed_task_list.erase(iter_succeed, task->impl_->succeed_task_list.end());
  }
}

void GraphTask::remove_succeed_task(const std::shared_ptr<GraphTask>& task) {
  if VUNLIKELY (!task) {
    VLOG_F("GraphTask: Invalid task provided to remove_succeeding_task.");
    return;
  }

  std::unique_lock lock1(this->impl_->mtx, std::defer_lock);
  std::unique_lock lock2(task->impl_->mtx, std::defer_lock);
  std::lock(lock1, lock2);

  auto iter_precede = std::remove_if(impl_->succeed_task_list.begin(), impl_->succeed_task_list.end(),
                                     [&task](const std::weak_ptr<GraphTask>& weak_task) {
                                       auto locked_task = weak_task.lock();
                                       return locked_task == task;
                                     });

  if VLIKELY (iter_precede != impl_->succeed_task_list.end()) {
    impl_->succeed_task_list.erase(iter_precede, impl_->succeed_task_list.end());
  } else {
    VLOG_F("GraphTask: Task not found in succeeding list.");
  }

  auto iter_succeed = std::remove_if(task->impl_->precede_task_list.begin(), task->impl_->precede_task_list.end(),
                                     [this](const std::weak_ptr<GraphTask>& weak_task) {
                                       auto locked_task = weak_task.lock();
                                       return locked_task.get() == this;
                                     });

  if VLIKELY (iter_succeed != task->impl_->precede_task_list.end()) {
    task->impl_->precede_task_list.erase(iter_succeed, task->impl_->precede_task_list.end());
  } else {
    VLOG_F("GraphTask: Current task not found in task's preceding list.");
  }
}

std::vector<std::weak_ptr<GraphTask>> GraphTask::get_precede_task_list() const {
  std::lock_guard lock(impl_->mtx);
  return impl_->precede_task_list;
}

std::vector<std::weak_ptr<GraphTask>> GraphTask::get_succeed_task_list() const {
  std::lock_guard lock(impl_->mtx);
  return impl_->succeed_task_list;
}

bool GraphTask::is_condition_task() const { return impl_->is_condition_task; }

GraphTask::GraphTask(Callback&& callback, int condition_number) : impl_(std::make_unique<Impl>()) {
  impl_->name = "Task_" + std::to_string(global_graph_task_count.fetch_add(1));
  impl_->condition_number = condition_number;
  impl_->is_condition_task = false;
  impl_->callback = std::move(callback);
}

GraphTask::GraphTask(const std::string& name, Callback&& callback, int condition_number)
    : impl_(std::make_unique<Impl>()) {
  impl_->name = name;
  impl_->condition_number = condition_number;
  impl_->is_condition_task = false;
  impl_->callback = std::move(callback);
}

GraphTask::GraphTask(ConditionCallback&& callback, int condition_number) : impl_(std::make_unique<Impl>()) {
  impl_->name = "Task_" + std::to_string(global_graph_task_count.fetch_add(1));
  impl_->condition_number = condition_number;
  impl_->is_condition_task = true;
  impl_->condition_callback = std::move(callback);
}

GraphTask::GraphTask(const std::string& name, ConditionCallback&& callback, int condition_number)
    : impl_(std::make_unique<Impl>()) {
  impl_->name = name;
  impl_->condition_number = condition_number;
  impl_->is_condition_task = true;
  impl_->condition_callback = std::move(callback);
}

GraphTask::~GraphTask() = default;

void GraphTask::process_and_traverse(const FindTaskCallback& callback) {
  uint32_t recursion_count = 0;

  std::stack<std::shared_ptr<GraphTask>> task_stack;

  task_stack.emplace(shared_from_this());

  std::unordered_map<GraphTask*, int> pending_count_map;
  std::unordered_set<GraphTask*> processed;

  std::vector<std::shared_ptr<GraphTask>> top_task_list;

  while (!task_stack.empty()) {
    auto current_task = task_stack.top();
    task_stack.pop();

    if (!processed.insert(current_task.get()).second) {
      continue;
    }

    {
      std::lock_guard lock(current_task->impl_->mtx);

      clear_invalid_task(current_task);

      if (recursion_count == 0) {
        current_task->impl_->is_ready = true;
        current_task->impl_->is_enable = true;

        current_task->update_status(kStatusPending);

        auto& sub_pending_count = pending_count_map[current_task.get()];
        current_task->impl_->pending_index = ++sub_pending_count;

        top_task_list.emplace_back(current_task);
      }

      for (const auto& task : current_task->impl_->succeed_task_list) {
        auto task_ptr = task.lock();

        if VUNLIKELY (!task_ptr) {
          continue;
        }

        bool first_seen = (pending_count_map.find(task_ptr.get()) == pending_count_map.end());

        auto& sub_pending_count = pending_count_map[task_ptr.get()];
        task_ptr->impl_->pending_index = ++sub_pending_count;

        if (first_seen) {
          task_ptr->impl_->is_ready = false;
          task_ptr->impl_->is_enable = false;

          task_ptr->update_status(kStatusPending);

          top_task_list.emplace_back(task_ptr);
          task_stack.emplace(task_ptr);
        }

        if VUNLIKELY (recursion_count++ >= impl_->max_recursion_depth) {
          CLOG_F("GraphTask: Recursion detection exceeds the upper limit (%d).", impl_->max_recursion_depth.load());
          return;
        }
      }
    }
  }

  for (const auto& top_task : top_task_list) {
    callback(top_task);
  }
}

bool GraphTask::has_cycle() const {
  std::unordered_set<const GraphTask*> visited;
  std::unordered_set<const GraphTask*> recursion_stack;

  return detect_cycle(this, visited, recursion_stack);
}

std::string GraphTask::export_to_dot() const {
  std::ostringstream dot_stream;

  dot_stream << "digraph TaskGraph {\n";

  dot_stream << "  node [fontname=\"Arial\"];\n";

  std::unordered_map<std::string, std::vector<const GraphTask*>> groups;

  std::unordered_set<const GraphTask*> visited;

  vlink::Function<void(const GraphTask*)> traverse = [&](const GraphTask* task) {
    if (visited.count(task)) {
      return;
    }

    visited.insert(task);

    {
      groups[task->get_group_name()].emplace_back(task);
    }

    {
      std::lock_guard lock(task->impl_->mtx);

      for (const auto& succeed_task_weak : task->impl_->succeed_task_list) {
        auto succeed_task = succeed_task_weak.lock();

        if VUNLIKELY (!succeed_task) {
          continue;
        }

        if (task->is_condition_task()) {
          dot_stream << "  \"" << task->impl_->name << "\" -> \"" << succeed_task->impl_->name
                     << "\" [style=dashed, arrowhead=vee];\n";
        } else {
          dot_stream << "  \"" << task->impl_->name << "\" -> \"" << succeed_task->impl_->name << "\";\n";
        }

        traverse(succeed_task.get());
      }
    }
  };

  traverse(this);

  std::string title_label;
  std::string extra_label;
  std::string color_label;

  for (const auto& [group_name, tasks] : groups) {
    if (!group_name.empty()) {
      dot_stream << "  subgraph cluster_" << group_name << " {\n";
      dot_stream << "    label = \"" << group_name << "\";\n";
      dot_stream << "    style = filled;\n";
      dot_stream << "    color = lightgray;\n";
    }

    for (const auto* task : tasks) {
      const std::string& name = task->get_name();

      if (task->impl_->policy == kPolicyOnce) {
        extra_label.clear();
        color_label = "lightgray";
      } else if (task->impl_->policy == kPolicyMultiple) {
        extra_label = "\\n[Multiple]";
        color_label = "lightgreen";
      } else if (task->impl_->policy == kPolicyWaitAll) {
        extra_label = "\\n[Waitfor]";
        color_label = "lightblue";
      } else {
        extra_label.clear();
        color_label = "lightgray";
      }

      if (task->is_condition_task()) {
        title_label = "style=dashed, shape=diamond, style=filled";
        Helpers::replace_string(color_label, "light", "");
      } else {
        title_label = "style=ellipse, style=filled";
      }

      dot_stream << "    \"";
      dot_stream << name;
      dot_stream << "\" [";
      dot_stream << title_label;
      dot_stream << ", color=";
      dot_stream << color_label;
      dot_stream << ", label=\"";
      dot_stream << name;
      dot_stream << extra_label;
      dot_stream << "\"];\n";
    }

    if (!group_name.empty()) {
      dot_stream << "  }\n";
    }
  }

  dot_stream << "}\n";

  return dot_stream.str();
}

int GraphTask::invoke(bool once) {
  if (once) {
    if (!impl_->is_enable || impl_->status != kStatusPending) {
      return -1;
    }
  }

  update_status(kStatusRunning);

  if (!impl_->is_condition_task && impl_->callback) {
    impl_->callback();

    update_status(kStatusDone);

    return 0;
  }

  if (impl_->is_condition_task && impl_->condition_callback) {
    int ret = impl_->condition_callback();

    update_status(kStatusDone);

    return ret;
  }

  update_status(kStatusDone);

  return -1;
}

void GraphTask::wait() {
  std::unique_lock lock(impl_->mtx);

  impl_->cv.wait(lock, [this]() -> bool { return impl_->is_ready.load() || impl_->status == kStatusInActive; });
}

void GraphTask::notify(int condition_number) {
  std::vector<std::weak_ptr<GraphTask>> invoke_list;

  {
    std::lock_guard lock(impl_->mtx);

    for (const auto& task : impl_->succeed_task_list) {
      auto task_ptr = task.lock();

      if VUNLIKELY (!task_ptr) {
        continue;
      }

      if (condition_number != task_ptr->impl_->condition_number) {
        if (impl_->is_condition_task) {
          task_ptr->impl_->is_ready = true;
          task_ptr->impl_->is_enable = false;
          task_ptr->impl_->cv.notify_all();
        }

        continue;
      }

      if (task_ptr->impl_->policy == kPolicyOnce) {
        task_ptr->impl_->is_ready = true;
        task_ptr->impl_->is_enable = true;
        task_ptr->impl_->cv.notify_all();
      } else if (task_ptr->impl_->policy == kPolicyMultiple) {
        task_ptr->impl_->is_ready = true;
        task_ptr->impl_->is_enable = false;
        task_ptr->impl_->cv.notify_all();

        invoke_list.emplace_back(task);
      } else if (task_ptr->impl_->policy == kPolicyWaitAll) {
        size_t old_val = task_ptr->impl_->pending_index.fetch_sub(1);

        if (old_val == 0) {
          task_ptr->impl_->pending_index.fetch_add(1);
          continue;
        }

        if (old_val != 1) {
          continue;
        }

        task_ptr->impl_->is_ready = true;
        task_ptr->impl_->is_enable = true;
        task_ptr->impl_->cv.notify_all();
      }
    }
  }

  for (const auto& task : invoke_list) {
    auto task_ptr = task.lock();

    if VUNLIKELY (!task_ptr) {
      continue;
    }

    task_ptr->invoke(false);
  }
}

void GraphTask::update_status(Status status) {
  if (impl_->status != status) {
    impl_->status = status;

    std::shared_lock lock(impl_->shared_mtx);
    if (impl_->status_callback) {
      impl_->status_callback(impl_->name, status);
    }
  }
}

bool GraphTask::detect_cycle(const GraphTask* task, std::unordered_set<const GraphTask*>& visited,
                             std::unordered_set<const GraphTask*>& recursion_stack) const {
  if VUNLIKELY (recursion_stack.count(task)) {
    return true;
  }

  if (visited.count(task)) {
    return false;
  }

  visited.insert(task);
  recursion_stack.insert(task);

  std::vector<std::weak_ptr<GraphTask>> succ_copy;
  {
    std::lock_guard lock(task->impl_->mtx);
    succ_copy = task->impl_->succeed_task_list;
  }

  for (const auto& weak_task : succ_copy) {
    auto next_task = weak_task.lock();

    if (!next_task) {
      continue;
    }

    if VUNLIKELY (detect_cycle(next_task.get(), visited, recursion_stack)) {
      return true;
    }
  }

  recursion_stack.erase(task);

  return false;
}

void GraphTask::clear_invalid_task(const std::shared_ptr<GraphTask>& task) {
  {
    task->impl_->precede_task_list.erase(
        std::remove_if(task->impl_->precede_task_list.begin(), task->impl_->precede_task_list.end(),
                       [](const std::weak_ptr<GraphTask>& weak_task) { return weak_task.expired(); }),
        task->impl_->precede_task_list.end());

    task->impl_->succeed_task_list.erase(
        std::remove_if(task->impl_->succeed_task_list.begin(), task->impl_->succeed_task_list.end(),
                       [](const std::weak_ptr<GraphTask>& weak_task) { return weak_task.expired(); }),
        task->impl_->succeed_task_list.end());
  }
}

}  // namespace vlink
