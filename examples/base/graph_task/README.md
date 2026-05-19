# VLink GraphTask 示例 -- 深入解析

## 1. 概述

`GraphTask` 是 VLink 的有向无环图（DAG）任务调度器。开发者以声明式方式定义任务间的依赖关系，由执行引擎自动调度就绪任务。无依赖的任务可以在 `MultiLoop` 或 `ThreadPool` 上并行执行。

本示例深入演示线性依赖、菱形并行、条件分支、状态监控以及 DOT 图形导出。

## 2. 文件说明

| 文件 | 说明 |
|------|------|
| `graph_task.cc` | 主程序入口，构建各种 DAG 并执行 |
| `task_graph_builder.h` | DAG 构建辅助函数：linear、diamond、conditional、pipeline |
| `CMakeLists.txt` | 构建配置，链接 `vlink::all` |

## 3. 构建与运行

```bash
cmake -B build -S . -DCMAKE_PREFIX_PATH=/path/to/vlink/install
cmake --build build --target example_graph_task
./build/output/bin/example_graph_task
```

## 4. DAG 概念

![GraphTask DAG](images/graph-task-dag.png)

### 4.1 什么是 DAG

DAG（Directed Acyclic Graph，有向无环图）是一种图结构，其中：
- **有向**：边有方向，表示"必须在...之前完成"
- **无环**：没有循环依赖（A->B->C->A 是非法的）

在任务调度中，DAG 表示任务间的依赖关系：
- **节点**：任务
- **边**：依赖关系（A->B 表示 A 必须在 B 之前完成）
- **入度为 0 的节点**：可以立即执行的任务
- **出度为 0 的节点**：最终输出任务

### 4.2 为什么使用 DAG 调度

1. **自动并行化**：无依赖的任务自动并行执行
2. **声明式编程**：只需声明"什么依赖什么"，无需手动管理执行顺序
3. **可视化**：DAG 可以导出为 DOT 格式，用 Graphviz 可视化
4. **环路检测**：构建时检测循环依赖，防止死锁

## 5. 依赖模式

### 5.1 模式 1：线性链 (A -> B -> C)

```cpp
auto a = GraphTask::create("A", []() { load_data(); });
auto b = GraphTask::create("B", []() { process_data(); });
auto c = GraphTask::create("C", []() { save_results(); });

// 操作符语法（A 先跑、B 次之、C 最后）
a-- > b-- > c;

// 等效的 API 调用
a->precede(b);
b->precede(c);
```

执行顺序：A -> B -> C，严格串行。

### 5.2 模式 2：菱形 (Diamond)

```cpp
auto a = GraphTask::create("A", init_task);
auto b = GraphTask::create("B", path1_task);
auto c = GraphTask::create("C", path2_task);
auto d = GraphTask::create("D", merge_task);

a->precede(b);  // A -> B（A 先跑，B 后跑）
a->precede(c);  // A -> C
b->precede(d);  // B -> D
c->precede(d);  // C -> D
```

执行顺序：A 先执行，然后 B 和 C **并行**执行，最后 D 等待 B 和 C 都完成后执行。

这是最常用的并行模式：**分叉-合并**。

### 5.3 模式 3：条件分支

```cpp
auto branch = GraphTask::create_condition("Branch", [&condition]() -> int {
    return condition;  // 返回值选择后继分支
});

branch->precede(path_0);  // branch 返回 0 时跑 path_0
branch->precede(path_1);  // branch 返回 1 时跑 path_1
```

条件任务返回一个整数索引，选择激活哪个后继分支。未被选中的分支被跳过。

### 5.4 模式 4：Fan-out

```cpp
auto source = GraphTask::create("Source", generate_data);
for (int i = 0; i < 8; ++i) {
    auto worker = GraphTask::create("Worker" + std::to_string(i), process_chunk);
    source->precede(worker);   // source 先跑，worker 后跑
}
```

一个源任务后接多个并行工作任务。

## 6. 关键代码分析

### 6.1 任务创建

```cpp
// 普通任务（void 回调）
auto task = GraphTask::create("name", []() { work(); });

// 条件任务（int 回调，返回值选择分支）
auto cond = GraphTask::create_condition("name", []() -> int { return 0; });
```

任务通过 `shared_ptr<GraphTask>` 管理。`GraphTaskPtr` 是其类型别名。

### 6.2 依赖声明

**API 含义以代码为准**：

| 调用              | 含义                                                          |
| ----------------- | ------------------------------------------------------------- |
| `X->precede(Y)`   | Y 是 X 的后继（X 先跑，Y 后跑）— Y 进入 X.succeed_task_list   |
| `X->succeed(Y)`   | Y 是 X 的前驱（Y 先跑，X 后跑）— Y 进入 X.precede_task_list   |
| `X -- > Y`        | 等价于 `X->precede(Y)`                                        |
| `X -- < Y`        | 等价于 `X->succeed(Y)`                                        |

```cpp
// "A 先跑，B 后跑" 的三种等价写法：
a->precede(b);
b->succeed(a);
a-- > b;
```

> `execute()` 必须从根节点（无前驱节点）发起；从子节点 `execute()` 只会运行该子节点和它的后继子图。

### 6.3 执行策略

| 策略 | 说明 |
|------|------|
| `kPolicyOnce` | 每次 execute() 只运行一次（默认） |
| `kPolicyMultiple` | 允许一次 execute() 中多次运行 |
| `kPolicyWaitAll` | 等待**所有**前驱完成后运行 |

`kPolicyWaitAll` 是菱形模式合并节点的关键：

```cpp
d->set_policy(GraphTask::kPolicyWaitAll);
// D 必须等待 B 和 C 都完成才能执行
```

### 6.4 执行引擎

```cpp
task->execute(&engine);
```

`execute()` 接受任何提供 `post_task()` 方法的引擎：

| 引擎 | 行为 |
|------|------|
| `MessageLoop` | 所有任务串行执行（单线程） |
| `MultiLoop` | 无依赖的任务并行执行（多线程） |
| `ThreadPool` | 无依赖的任务并行执行（线程池） |

### 6.5 状态回调

```cpp
// 支持多订阅者；返回 id 用于注销
uint32_t id = task->register_status_callback(
    [](const std::string& name, GraphTask::Status status) {
        // status: kStatusInActive -> kStatusPending -> kStatusRunning -> kStatusDone
    });

task->unregister_status_callback(id);   // 按 id 注销
task->clear_status_callbacks();         // 清空全部
```

任务状态转换：
1. **InActive**：初始状态，未加入执行
2. **Pending**：已加入执行，等待前驱完成
3. **Running**：正在执行回调
4. **Done**：执行完成

> **回调限制**（内部锁非递归）：回调内**不可**对同一任务调 `register_status_callback`/
> `unregister_status_callback`/`clear_status_callbacks`/`cancel()`；可安全调 `set_name`/`get_name`/
> `get_status` 或操作其它任务。回调抛出的异常会被捕获并打日志，**不影响其它订阅者继续触发**。

### 6.6 环路检测

```cpp
bool has_cycle = task->has_cycle();
```

使用深度优先搜索（DFS）配合三色标记法检测环路。

**自动检测**：`precede()` / `succeed()` 在每次加边后会自动在受影响子图上跑一次环检测；
若新边导致成环则被静默回滚并记录错误日志（best-effort，并发构图时应当作单写阶段处理）。
`has_cycle()` 仍可用于显式校验。

### 6.7 DOT 导出

```cpp
std::string dot = task->export_to_dot();
```

导出 Graphviz DOT 字符串，用于可视化：

```bash
echo "$DOT_STRING" | dot -Tpng -o graph.png
```

## 7. 车载软件中的典型 DAG

### 7.1 感知流水线

```
Camera ──> Detect ──> Track ──> Fusion ──> Publish
Lidar  ──> Detect ──> Track ──┘
Radar  ──> Detect ──────────┘
```

三个传感器的检测可以并行执行，Track 等待各自的 Detect 完成，Fusion 等待所有 Track 完成。

### 7.2 系统启动序列

```
LoadConfig ──> InitNetwork ──┐
             InitHardware ──┤──> StartApplication
             InitLogger   ──┘
```

配置加载后，网络、硬件、日志三个初始化可以并行进行。

## 8. 常见错误

### 8.1 错误 1：循环依赖

```cpp
// 建链：a -> b -> c
a->precede(b);
b->precede(c);

// c->precede(a) 会形成环路 a->b->c->a；
// precede/succeed 内部已自动检测，这条调用会被静默回滚并记日志，无需手动处理。
c->precede(a);

// 仍可显式校验
assert(!a->has_cycle());
```

### 8.2 错误 2：忘记设置 kPolicyWaitAll

```cpp
// 表达 a, b 完成后再跑 d：
a->precede(d);   // a 先跑，d 后跑（d 进 a.succeed_list）
b->precede(d);   // b 先跑，d 后跑

// 默认策略下 d 在 a 或 b 任一完成后就开始执行；如需等待全部前驱：
d->set_policy(GraphTask::kPolicyWaitAll);
```

### 8.3 错误 3：在并行任务中共享状态

```cpp
int shared = 0;
auto b = GraphTask::create("B", [&shared]() { shared++; });
auto c = GraphTask::create("C", [&shared]() { shared++; });
a->precede(b);
a->precede(c);
// B 和 C 并行执行，shared 有数据竞争！
```

### 8.4 错误 4：条件任务返回越界索引

```cpp
auto branch = GraphTask::create_condition("Branch", []() -> int {
    return 5;  // 只有 2 个后继 (0 和 1)
});
branch->precede(path_0);
branch->precede(path_1);
// 返回 5 时，所有后继分支被跳过
```

## 9. 相关示例

- [thread_pool](../thread_pool/) -- GraphTask 的并行执行引擎
- [multi_loop](../multi_loop/) -- GraphTask 的另一种并行引擎
- [message_loop_basic](../message_loop_basic/) -- GraphTask 的串行执行引擎
- [schedule](../schedule/) -- Schedule::Config 高级任务调度
