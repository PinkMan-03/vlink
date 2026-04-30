# pub_sub_fbs -- VLink DDS-C + FlatBuffers 发布订阅示例

## 概述

本示例演示如何使用 VLink 的事件模型（`Publisher` / `Subscriber`）配合 FlatBuffers 序列化发送和接收一条较复杂的嵌套消息。

示例使用 `ddsc://` 传输协议，schema 直接复用目录内现成的 [fbs/helloworld.fbs](fbs/helloworld.fbs)。消息根类型是 `User`，内部包含：

- `Profile` / `Address`
- `Order`
- `Item`
- `Vec2`

这个示例的重点不是传输原语种类，而是展示 FlatBuffers 在 VLink 里的典型发布订阅写法：

- 发布端使用 `UserT`（Object API，可变对象）构造消息
- 订阅端使用 `User*`（零拷贝只读指针）直接读取消息

## 文件说明

| 文件 | 说明 |
|------|------|
| `pub_sub_fbs.cc` | 主程序，演示 FlatBuffers 发布和订阅 |
| `fbs/helloworld.fbs` | FlatBuffers schema，定义 `User`、`Profile`、`Order` 等结构 |
| `CMakeLists.txt` | 构建配置，生成 FlatBuffers 代码并链接 `vlink::ddsc` |

## 演示内容

### 1. `sub` 模式：常驻监听 FlatBuffers 指针

订阅端直接监听 `const hw::User*`：

```cpp
Subscriber<hw::User*> sub("ddsc://samples/pub_sub_fbs/user");
sub.listen([](const hw::User* user) {
  VLOG_I(user->user_id());
});
```

这种写法会走 FlatBuffers 指针类型（零拷贝）路径，适合高频或大消息读取场景。

注意：`User*` 只在回调执行期间有效，不应在回调外长期保存。

### 2. `pub` 模式：使用 `MessageLoop + Timer` 周期发布

示例里 `make_user()` 会构造一条完整的用户消息，包括昵称、地址、地理位置、订单和商品列表：

```cpp
Publisher<hw::UserT> pub("ddsc://samples/pub_sub_fbs/user");

Timer timer;
timer.attach(&message_loop);
timer.set_interval(500);
timer.set_loop_count(Timer::kInfinite);
```

`UserT` 属于 FlatBuffers 的 Object API，适合在 C++ 里像普通对象一样填充字段，VLink 会在 `publish()` 时自动完成序列化。发布端通过 `MessageLoop + Timer` 每 500ms 发送一条数据，并持续运行直到 Ctrl+C。

### 3. 发布端启动后立即发送

发布端不会等待订阅端上线。只要执行 `sample_pub_sub_fbs pub`，定时器就会立刻开始工作，按 500ms 周期持续发送消息。

### 4. 根据启动参数决定进程角色

程序支持两种启动模式：

```bash
sample_pub_sub_fbs sub
sample_pub_sub_fbs pub
```

- `sub`：启动订阅进程，持续接收消息，直到 Ctrl+C
- `pub`：启动发布进程，每 500ms 发送一条消息，直到 Ctrl+C

### 5. 每次发送的数据都会变化

发布端内部维护递增序号 `seq`，每个周期都会基于这个序号重新构造一条新的 `UserT`：

- `user_id`、`name`、`email` 会递增变化
- `profile.nickname`、`profile.age`、`profile.home.street` 会变化
- `order.order_id`、`order.status` 也会变化

所以订阅端看到的不是重复包，而是一串持续变化的示例数据。

## FlatBuffers 类型约定

| 类型 | 含义 | 典型用途 |
|------|------|----------|
| `hw::UserT` | Object API / NativeTable | 发送前构造、修改消息 |
| `hw::User*` | 只读 Table 指针 | 回调中零拷贝读取消息 |

其中 `hw` 是示例里对 `Helloworld::fbs` 命名空间的别名。

## 依赖

- VLink 库（`vlink::ddsc` 组件）
- FlatBuffers
- CycloneDDS / DDS-C 运行环境

## 构建与运行

```bash
# 从仓库根目录
cd /work/vlink

cmake -S . -B build
cmake --build build --target sample_pub_sub_fbs -j$(nproc)

./build/output/bin/sample_pub_sub_fbs
```

推荐使用两个终端分别运行：

```bash
# 终端 1：启动订阅端
./build/output/bin/sample_pub_sub_fbs sub

# 终端 2：启动发布端
./build/output/bin/sample_pub_sub_fbs pub
```

正常运行时，你会看到：

- `pub` 进程每 500ms 连续发布一条 `User` 消息
- `sub` 进程收到每条消息后打印用户、地址和订单摘要

## 可继续扩展的方向

- 把 `ddsc://` 替换成 `dds://`，验证同一份 FlatBuffers schema 在不同传输后端上的用法
- 把订阅端类型改成 `hw::UserT`，对比“零拷贝读取”和“解包到对象”的差异
- 扩展 schema，添加更多订单字段，观察生成代码和使用方式
