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

// NOLINTBEGIN

#include "./base/task_handle.h"

#include <doctest/doctest.h>

#include <atomic>
#include <chrono>
#include <future>
#include <stdexcept>
#include <vector>

#include "./base/message_loop.h"
#include "./base/thread_pool.h"

//
#include "../common_test.h"

namespace {

class SmallQueueMessageLoop final : public MessageLoop {
 public:
  using MessageLoop::MessageLoop;

  [[nodiscard]] size_t get_max_task_count() const override { return 1U; }
};

class TwoTaskMessageLoop final : public MessageLoop {
 public:
  using MessageLoop::MessageLoop;

  [[nodiscard]] size_t get_max_task_count() const override { return 2U; }
};

class SmallQueueThreadPool final : public ThreadPool {
 public:
  using ThreadPool::ThreadPool;

  [[nodiscard]] size_t get_max_task_count() const override { return 1U; }
};

class TwoTaskThreadPool final : public ThreadPool {
 public:
  using ThreadPool::ThreadPool;

  [[nodiscard]] size_t get_max_task_count() const override { return 2U; }
};

template <typename PredicateT>
bool wait_until(PredicateT&& predicate, std::chrono::milliseconds timeout = std::chrono::milliseconds(1000)) {
  const auto deadline = std::chrono::steady_clock::now() + timeout;
  while (std::chrono::steady_clock::now() < deadline) {
    if (predicate()) {
      return true;
    }

    std::this_thread::yield();
  }

  return predicate();
}

}  // namespace

TEST_SUITE("base-TaskHandle") {
  TEST_CASE("default handle is invalid and wait returns false") {
    TaskHandle handle;

    CHECK_FALSE(handle.valid());
    CHECK(handle.state() == TaskExecutionState::kInvalid);
    CHECK_FALSE(handle.is_done());
    CHECK_FALSE(handle.cancel());
    CHECK_FALSE(handle.wait(0));
  }

  TEST_CASE("MessageLoop post_task_handle completes") {
    MessageLoop loop;
    loop.async_run();

    std::atomic<bool> ran{false};
    auto handle = loop.post_task_handle([&ran] { ran.store(true, std::memory_order_release); });

    CHECK(handle.wait(1000));
    CHECK(handle.state() == TaskExecutionState::kCompleted);
    CHECK(ran.load(std::memory_order_acquire));
    CHECK_FALSE(handle.cancel());

    loop.quit();
    loop.wait_for_quit();
  }

  TEST_CASE("MessageLoop tracked callback failure is observable and loop continues") {
    MessageLoop loop;
    loop.async_run();

    auto failed = loop.post_task_handle([] { throw std::runtime_error("tracked failure"); });

    CHECK(failed.wait(1000));
    CHECK(failed.state() == TaskExecutionState::kFailed);

    std::atomic<bool> ran{false};
    auto next = loop.post_task_handle([&ran] { ran.store(true, std::memory_order_release); });

    CHECK(next.wait(1000));
    CHECK(next.state() == TaskExecutionState::kCompleted);
    CHECK(ran.load(std::memory_order_acquire));

    loop.quit();
    loop.wait_for_quit();
  }

  TEST_CASE("MessageLoop queued handle can be cancelled before execution") {
    MessageLoop loop;
    std::atomic<bool> ran{false};

    auto handle = loop.post_task_handle([&ran] { ran.store(true, std::memory_order_release); });
    std::atomic<bool> callback_waited{false};
    auto registration = handle.cancellation_token().register_callback([handle, &callback_waited] {
      callback_waited.store(handle.wait(100) && handle.state() == TaskExecutionState::kCancelled,
                            std::memory_order_release);
    });
    CHECK(registration.valid());

    CHECK(handle.cancel());
    CHECK(handle.wait(100));
    CHECK(handle.state() == TaskExecutionState::kCancelled);
    CHECK(callback_waited.load(std::memory_order_acquire));

    loop.async_run();
    loop.wait_for_idle();
    CHECK_FALSE(ran.load(std::memory_order_acquire));

    loop.quit();
    loop.wait_for_quit();
  }

  TEST_CASE("MessageLoop wait times out while task is only queued") {
    MessageLoop loop;

    auto handle = loop.post_task_handle([] {});

    CHECK(handle.valid());
    CHECK(handle.state() == TaskExecutionState::kQueued);
    CHECK_FALSE(handle.wait(1));
    CHECK(handle.state() == TaskExecutionState::kQueued);

    CHECK(handle.cancel());
    CHECK(handle.wait(100));
  }

  TEST_CASE("MessageLoop rejects tracked task when explicit reject policy sees a full queue") {
    SmallQueueMessageLoop loop(MessageLoop::kNormalType);

    CHECK(loop.post_task([] {}));

    PostTaskOptions options;
    options.overflow_policy = TaskOverflowPolicy::kReject;

    std::atomic<bool> ran{false};
    auto rejected = loop.post_task_handle([&ran] { ran.store(true, std::memory_order_release); }, options);

    CHECK(rejected.wait(100));
    CHECK(rejected.state() == TaskExecutionState::kRejected);
    CHECK_FALSE(ran.load(std::memory_order_acquire));
  }

  TEST_CASE("MessageLoop dropped queued task is observable") {
    SmallQueueMessageLoop loop(MessageLoop::kNormalType);
    loop.set_strategy(MessageLoop::kPopStrategy);

    auto first = loop.post_task_handle([] {});
    auto second = loop.post_task_handle([] {});

    CHECK(first.wait(100));
    CHECK(first.state() == TaskExecutionState::kDropped);
    CHECK(second.state() == TaskExecutionState::kQueued);
  }

  TEST_CASE("MessageLoop protected task is not dropped by overflow") {
    SmallQueueMessageLoop loop(MessageLoop::kNormalType);
    loop.set_strategy(MessageLoop::kPopStrategy);

    PostTaskOptions options;
    options.drop_policy = TaskDropPolicy::kProtected;

    auto protected_task = loop.post_task_handle([] {}, options);
    CHECK(protected_task.state() == TaskExecutionState::kQueued);

    CHECK_FALSE(loop.post_task([] {}));
    CHECK(protected_task.state() == TaskExecutionState::kQueued);

    auto rejected = loop.post_task_handle([] {});
    CHECK(rejected.wait(100));
    CHECK(rejected.state() == TaskExecutionState::kRejected);
  }

  TEST_CASE("MessageLoop lockfree overflow drops queued task without drop-policy tracking") {
    TwoTaskMessageLoop loop(MessageLoop::kLockfreeType);
    loop.set_strategy(MessageLoop::kPopStrategy);

    auto first = loop.post_task_handle([] {});
    auto second = loop.post_task_handle([] {});
    auto third = loop.post_task_handle([] {});

    CHECK(first.wait(100));
    CHECK(first.state() == TaskExecutionState::kDropped);
    CHECK(second.state() == TaskExecutionState::kQueued);
    CHECK(third.state() == TaskExecutionState::kQueued);
  }

  TEST_CASE("MessageLoop lockfree overflow does not honour protected drop policy") {
    TwoTaskMessageLoop loop(MessageLoop::kLockfreeType);
    loop.set_strategy(MessageLoop::kPopStrategy);

    PostTaskOptions protected_options;
    protected_options.drop_policy = TaskDropPolicy::kProtected;

    auto protected_task = loop.post_task_handle([] {}, protected_options);
    auto second = loop.post_task_handle([] {});
    auto third = loop.post_task_handle([] {});

    CHECK(protected_task.wait(100));
    CHECK(protected_task.state() == TaskExecutionState::kDropped);
    CHECK(second.state() == TaskExecutionState::kQueued);
    CHECK(third.state() == TaskExecutionState::kQueued);
  }

  TEST_CASE("MessageLoop priority overflow drops from droppable heap without scanning protected heap") {
    TwoTaskMessageLoop loop(MessageLoop::kPriorityType);
    loop.set_strategy(MessageLoop::kPopStrategy);

    PostTaskOptions protected_options;
    protected_options.drop_policy = TaskDropPolicy::kProtected;

    auto protected_task = loop.post_task_with_priority_handle([] {}, MessageLoop::kHighestPriority, protected_options);
    auto first_droppable = loop.post_task_with_priority_handle([] {}, MessageLoop::kNormalPriority);
    auto second_droppable = loop.post_task_with_priority_handle([] {}, MessageLoop::kLowestPriority);

    CHECK(protected_task.state() == TaskExecutionState::kQueued);
    CHECK(first_droppable.wait(100));
    CHECK(first_droppable.state() == TaskExecutionState::kDropped);
    CHECK(second_droppable.state() == TaskExecutionState::kQueued);
  }

  TEST_CASE("MessageLoop priority dispatch merges protected and droppable queues by priority") {
    MessageLoop loop(MessageLoop::kPriorityType);
    std::vector<int> order;

    PostTaskOptions protected_options;
    protected_options.drop_policy = TaskDropPolicy::kProtected;

    auto low = loop.post_task_with_priority_handle([&order] { order.push_back(1); }, MessageLoop::kLowestPriority);
    auto high = loop.post_task_with_priority_handle([&order] { order.push_back(2); }, MessageLoop::kHighestPriority,
                                                    protected_options);
    auto normal = loop.post_task_with_priority_handle([&order] { order.push_back(3); }, MessageLoop::kNormalPriority);

    loop.async_run();

    CHECK(low.wait(1000));
    CHECK(high.wait(1000));
    CHECK(normal.wait(1000));
    CHECK(order == std::vector<int>{2, 3, 1});

    loop.quit();
    loop.wait_for_quit();
  }

  TEST_CASE("MessageLoop invoke_task future becomes ready when post fails") {
    SmallQueueMessageLoop loop(MessageLoop::kNormalType);
    loop.set_strategy(MessageLoop::kPopStrategy);

    PostTaskOptions options;
    options.drop_policy = TaskDropPolicy::kProtected;

    auto protected_task = loop.post_task_handle([] {}, options);
    CHECK(protected_task.state() == TaskExecutionState::kQueued);

    auto fut = loop.invoke_task([] { return 1; });
    CHECK(fut.wait_for(std::chrono::milliseconds(100)) == std::future_status::ready);
    CHECK_THROWS_AS((void)fut.get(), std::future_error);
  }

  TEST_CASE("MessageLoop already-cancelled parent token prevents enqueue") {
    MessageLoop loop;
    CancellationSource source;
    CHECK(source.request_cancel());

    PostTaskOptions options;
    options.cancellation_token = source.token();

    std::atomic<bool> ran{false};
    auto handle = loop.post_task_handle([&ran] { ran.store(true, std::memory_order_release); }, options);

    CHECK(handle.wait(100));
    CHECK(handle.state() == TaskExecutionState::kCancelled);
    CHECK(loop.get_task_count() == 0U);
    CHECK_FALSE(ran.load(std::memory_order_acquire));
  }

  TEST_CASE("parent cancellation propagates to queued handle") {
    MessageLoop loop;
    CancellationSource source;

    PostTaskOptions options;
    options.cancellation_token = source.token();

    auto handle = loop.post_task_handle([] {}, options);
    CHECK(source.request_cancel());

    CHECK(handle.wait(100));
    CHECK(handle.state() == TaskExecutionState::kCancelled);
  }

  TEST_CASE("MessageLoop blocking tracked post is interrupted by parent cancellation") {
    SmallQueueMessageLoop loop(MessageLoop::kNormalType);
    CHECK(loop.post_task([] {}));

    CancellationSource source;
    PostTaskOptions options;
    options.overflow_policy = TaskOverflowPolicy::kBlock;
    options.cancellation_token = source.token();

    auto fut =
        std::async(std::launch::async, [&loop, &options] { return loop.post_task_handle([] {}, options).state(); });

    CHECK(fut.wait_for(std::chrono::milliseconds(30)) == std::future_status::timeout);
    CHECK(source.request_cancel());
    CHECK(fut.wait_for(std::chrono::milliseconds(1000)) == std::future_status::ready);
    CHECK(fut.get() == TaskExecutionState::kCancelled);
  }

  TEST_CASE("ThreadPool post_task_handle completes") {
    ThreadPool pool(2);
    std::atomic<bool> ran{false};

    auto handle = pool.post_task_handle([&ran] { ran.store(true, std::memory_order_release); });

    CHECK(handle.wait(1000));
    CHECK(handle.state() == TaskExecutionState::kCompleted);
    CHECK(ran.load(std::memory_order_acquire));
    CHECK_FALSE(handle.cancel());

    pool.shutdown();
  }

  TEST_CASE("ThreadPool tracked callback failure is observable and worker continues") {
    ThreadPool pool(1);

    auto failed = pool.post_task_handle([] { throw std::runtime_error("tracked failure"); });

    CHECK(failed.wait(1000));
    CHECK(failed.state() == TaskExecutionState::kFailed);

    std::atomic<bool> ran{false};
    auto next = pool.post_task_handle([&ran] { ran.store(true, std::memory_order_release); });

    CHECK(next.wait(1000));
    CHECK(next.state() == TaskExecutionState::kCompleted);
    CHECK(ran.load(std::memory_order_acquire));

    pool.shutdown();
  }

  TEST_CASE("ThreadPool dropped queued task is observable") {
    SmallQueueThreadPool pool(1);
    pool.set_strategy(ThreadPool::kPopStrategy);

    std::atomic<bool> release_worker{false};
    std::atomic<bool> worker_started{false};

    CHECK(pool.post_task([&] {
      worker_started.store(true, std::memory_order_release);
      while (!release_worker.load(std::memory_order_acquire)) {
        std::this_thread::yield();
      }
    }));

    CHECK(wait_until([&worker_started] { return worker_started.load(std::memory_order_acquire); }));

    auto first = pool.post_task_handle([] {});
    auto second = pool.post_task_handle([] {});

    CHECK(first.wait(100));
    CHECK(first.state() == TaskExecutionState::kDropped);
    CHECK(second.state() == TaskExecutionState::kQueued);

    release_worker.store(true, std::memory_order_release);
    CHECK(second.wait(1000));

    pool.shutdown();
  }

  TEST_CASE("ThreadPool rejects tracked task when explicit reject policy sees a full queue") {
    SmallQueueThreadPool pool(1);

    std::atomic<bool> release_worker{false};
    std::atomic<bool> worker_started{false};

    CHECK(pool.post_task([&] {
      worker_started.store(true, std::memory_order_release);
      while (!release_worker.load(std::memory_order_acquire)) {
        std::this_thread::yield();
      }
    }));

    CHECK(wait_until([&worker_started] { return worker_started.load(std::memory_order_acquire); }));

    CHECK(pool.post_task([] {}));

    PostTaskOptions options;
    options.overflow_policy = TaskOverflowPolicy::kReject;

    std::atomic<bool> ran{false};
    auto rejected = pool.post_task_handle([&ran] { ran.store(true, std::memory_order_release); }, options);

    CHECK(rejected.wait(100));
    CHECK(rejected.state() == TaskExecutionState::kRejected);
    CHECK_FALSE(ran.load(std::memory_order_acquire));

    release_worker.store(true, std::memory_order_release);
    pool.shutdown();
  }

  TEST_CASE("ThreadPool protected queued task is not dropped by overflow") {
    SmallQueueThreadPool pool(1);
    pool.set_strategy(ThreadPool::kPopStrategy);

    std::atomic<bool> release_worker{false};
    std::atomic<bool> worker_started{false};

    CHECK(pool.post_task([&] {
      worker_started.store(true, std::memory_order_release);
      while (!release_worker.load(std::memory_order_acquire)) {
        std::this_thread::yield();
      }
    }));

    CHECK(wait_until([&worker_started] { return worker_started.load(std::memory_order_acquire); }));

    PostTaskOptions options;
    options.drop_policy = TaskDropPolicy::kProtected;

    auto protected_task = pool.post_task_handle([] {}, options);
    CHECK(protected_task.state() == TaskExecutionState::kQueued);

    CHECK_FALSE(pool.post_task([] {}));
    CHECK(protected_task.state() == TaskExecutionState::kQueued);

    auto rejected = pool.post_task_handle([] {});
    CHECK(rejected.wait(100));
    CHECK(rejected.state() == TaskExecutionState::kRejected);

    release_worker.store(true, std::memory_order_release);
    CHECK(protected_task.wait(1000));
    CHECK(protected_task.state() == TaskExecutionState::kCompleted);

    pool.shutdown();
  }

  TEST_CASE("ThreadPool lockfree overflow drops queued task without drop-policy tracking") {
    TwoTaskThreadPool pool(1, ThreadPool::kLockfreeType);
    pool.set_strategy(ThreadPool::kPopStrategy);

    std::atomic<bool> release_worker{false};
    std::atomic<bool> worker_started{false};

    CHECK(pool.post_task([&] {
      worker_started.store(true, std::memory_order_release);
      while (!release_worker.load(std::memory_order_acquire)) {
        std::this_thread::yield();
      }
    }));

    CHECK(wait_until([&worker_started] { return worker_started.load(std::memory_order_acquire); }));

    auto first = pool.post_task_handle([] {});
    auto second = pool.post_task_handle([] {});
    auto third = pool.post_task_handle([] {});

    CHECK(first.wait(100));
    CHECK(first.state() == TaskExecutionState::kDropped);
    CHECK(second.state() == TaskExecutionState::kQueued);
    CHECK(third.state() == TaskExecutionState::kQueued);

    release_worker.store(true, std::memory_order_release);
    CHECK(second.wait(1000));
    CHECK(third.wait(1000));

    pool.shutdown();
  }

  TEST_CASE("ThreadPool lockfree overflow does not honour protected drop policy") {
    TwoTaskThreadPool pool(1, ThreadPool::kLockfreeType);
    pool.set_strategy(ThreadPool::kPopStrategy);

    std::atomic<bool> release_worker{false};
    std::atomic<bool> worker_started{false};

    CHECK(pool.post_task([&] {
      worker_started.store(true, std::memory_order_release);
      while (!release_worker.load(std::memory_order_acquire)) {
        std::this_thread::yield();
      }
    }));

    CHECK(wait_until([&worker_started] { return worker_started.load(std::memory_order_acquire); }));

    PostTaskOptions protected_options;
    protected_options.drop_policy = TaskDropPolicy::kProtected;

    auto protected_task = pool.post_task_handle([] {}, protected_options);
    auto second = pool.post_task_handle([] {});
    auto third = pool.post_task_handle([] {});

    CHECK(protected_task.wait(100));
    CHECK(protected_task.state() == TaskExecutionState::kDropped);
    CHECK(second.state() == TaskExecutionState::kQueued);
    CHECK(third.state() == TaskExecutionState::kQueued);

    release_worker.store(true, std::memory_order_release);
    CHECK(second.wait(1000));
    CHECK(third.wait(1000));

    pool.shutdown();
  }

  TEST_CASE("ThreadPool invoke_task future becomes ready when post fails") {
    SmallQueueThreadPool pool(1);
    pool.set_strategy(ThreadPool::kPopStrategy);

    std::atomic<bool> release_worker{false};
    std::atomic<bool> worker_started{false};

    CHECK(pool.post_task([&] {
      worker_started.store(true, std::memory_order_release);
      while (!release_worker.load(std::memory_order_acquire)) {
        std::this_thread::yield();
      }
    }));

    CHECK(wait_until([&worker_started] { return worker_started.load(std::memory_order_acquire); }));

    PostTaskOptions options;
    options.drop_policy = TaskDropPolicy::kProtected;

    auto protected_task = pool.post_task_handle([] {}, options);
    CHECK(protected_task.state() == TaskExecutionState::kQueued);

    auto fut = pool.invoke_task([] { return 1; });
    CHECK(fut.wait_for(std::chrono::milliseconds(100)) == std::future_status::ready);
    CHECK_THROWS_AS((void)fut.get(), std::future_error);

    release_worker.store(true, std::memory_order_release);
    CHECK(protected_task.wait(1000));

    pool.shutdown();
  }

  TEST_CASE("ThreadPool blocking tracked post is interrupted by parent cancellation") {
    SmallQueueThreadPool pool(1);

    std::atomic<bool> release_worker{false};
    std::atomic<bool> worker_started{false};

    CHECK(pool.post_task([&] {
      worker_started.store(true, std::memory_order_release);
      while (!release_worker.load(std::memory_order_acquire)) {
        std::this_thread::yield();
      }
    }));

    CHECK(wait_until([&worker_started] { return worker_started.load(std::memory_order_acquire); }));
    CHECK(pool.post_task([] {}));

    CancellationSource source;
    PostTaskOptions options;
    options.overflow_policy = TaskOverflowPolicy::kBlock;
    options.cancellation_token = source.token();

    auto fut =
        std::async(std::launch::async, [&pool, &options] { return pool.post_task_handle([] {}, options).state(); });

    CHECK(fut.wait_for(std::chrono::milliseconds(30)) == std::future_status::timeout);
    CHECK(source.request_cancel());
    CHECK(fut.wait_for(std::chrono::milliseconds(1000)) == std::future_status::ready);
    CHECK(fut.get() == TaskExecutionState::kCancelled);

    release_worker.store(true, std::memory_order_release);
    pool.shutdown();
  }

  TEST_CASE("running task can observe handle cancellation token and finish cooperatively") {
    ThreadPool pool(1);
    std::atomic<bool> task_can_read_handle{false};
    std::atomic<bool> observed_cancel{false};
    std::atomic<bool> started{false};
    TaskHandle handle;

    handle = pool.post_task_handle([&] {
      started.store(true, std::memory_order_release);

      while (!task_can_read_handle.load(std::memory_order_acquire)) {
        std::this_thread::yield();
      }

      while (!handle.cancellation_token().is_cancellation_requested()) {
        std::this_thread::yield();
      }

      observed_cancel.store(true, std::memory_order_release);
    });

    CHECK(wait_until([&started] { return started.load(std::memory_order_acquire); }));

    task_can_read_handle.store(true, std::memory_order_release);

    CHECK(handle.cancel());
    CHECK(handle.wait(1000));
    CHECK(handle.state() == TaskExecutionState::kCompleted);
    CHECK(observed_cancel.load(std::memory_order_acquire));
    CHECK_FALSE(handle.cancel());

    pool.shutdown();
  }

  // ---

  TEST_CASE("default TaskHandle has no state and wait/cancel are no-ops") {
    TaskHandle handle;

    CHECK_FALSE(handle.valid());
    CHECK(handle.state() == TaskExecutionState::kInvalid);
    CHECK_FALSE(handle.is_done());
    CHECK_FALSE(handle.cancel());
    CHECK_FALSE(handle.wait(0));
    CHECK_FALSE(handle.wait(TaskHandle::kInfinite));
    CHECK_FALSE(handle.cancellation_token().valid());
  }

  // ---

  TEST_CASE("copied TaskHandle shares state and observes same lifecycle") {
    MessageLoop loop;

    auto original = loop.post_task_handle([] {});
    TaskHandle copy = original;

    CHECK(copy.valid());
    CHECK(original.valid());
    CHECK(copy.state() == original.state());
    CHECK(copy.is_done() == original.is_done());

    loop.async_run();

    CHECK(original.wait(1000));
    CHECK(copy.is_done());
    CHECK(copy.state() == TaskExecutionState::kCompleted);
    CHECK(original.state() == copy.state());

    loop.quit();
    loop.wait_for_quit();
  }

  // ---

  TEST_CASE("copy-assigned TaskHandle observes shared state") {
    MessageLoop loop;

    auto handle = loop.post_task_handle([] {});
    TaskHandle assigned;
    CHECK_FALSE(assigned.valid());

    assigned = handle;
    CHECK(assigned.valid());
    CHECK(assigned.state() == handle.state());

    loop.async_run();
    CHECK(handle.wait(1000));
    CHECK(assigned.state() == TaskExecutionState::kCompleted);

    loop.quit();
    loop.wait_for_quit();
  }

  // ---

  TEST_CASE("move-constructed TaskHandle leaves source invalid") {
    MessageLoop loop;

    auto original = loop.post_task_handle([] {});
    CHECK(original.valid());

    TaskHandle moved(std::move(original));

    CHECK(moved.valid());
    CHECK_FALSE(original.valid());
    CHECK(original.state() == TaskExecutionState::kInvalid);
    CHECK_FALSE(original.is_done());
    CHECK_FALSE(original.cancel());
    CHECK_FALSE(original.wait(0));

    loop.async_run();
    CHECK(moved.wait(1000));
    CHECK(moved.state() == TaskExecutionState::kCompleted);

    loop.quit();
    loop.wait_for_quit();
  }

  // ---

  TEST_CASE("move-assigned TaskHandle transfers state and leaves source invalid") {
    MessageLoop loop;

    auto original = loop.post_task_handle([] {});
    TaskHandle target;
    target = std::move(original);

    CHECK(target.valid());
    CHECK_FALSE(original.valid());
    CHECK(original.state() == TaskExecutionState::kInvalid);

    loop.async_run();
    CHECK(target.wait(1000));
    CHECK(target.state() == TaskExecutionState::kCompleted);

    loop.quit();
    loop.wait_for_quit();
  }

  // ---

  TEST_CASE("multiple TaskHandle copies share cancellation visibility") {
    MessageLoop loop;

    std::atomic<bool> ran{false};
    auto first = loop.post_task_handle([&ran] { ran.store(true, std::memory_order_release); });
    TaskHandle second = first;
    TaskHandle third;
    third = first;

    CHECK(second.cancel());
    CHECK(first.state() == TaskExecutionState::kCancelled);
    CHECK(third.state() == TaskExecutionState::kCancelled);
    CHECK(first.is_done());
    CHECK(second.is_done());
    CHECK(third.is_done());

    loop.async_run();
    loop.wait_for_idle();
    CHECK_FALSE(ran.load(std::memory_order_acquire));

    loop.quit();
    loop.wait_for_quit();
  }

  // ---

  TEST_CASE("wait on completed handle returns true immediately for any timeout") {
    MessageLoop loop;
    loop.async_run();

    auto handle = loop.post_task_handle([] {});
    CHECK(handle.wait(1000));
    CHECK(handle.state() == TaskExecutionState::kCompleted);

    CHECK(handle.wait(0));
    CHECK(handle.wait(1));
    CHECK(handle.wait(TaskHandle::kInfinite));
    CHECK(handle.is_done());

    loop.quit();
    loop.wait_for_quit();
  }

  // ---

  TEST_CASE("is_done is true for every terminal state and false for non-terminal") {
    MessageLoop loop;

    auto queued = loop.post_task_handle([] {});
    CHECK(queued.state() == TaskExecutionState::kQueued);
    CHECK_FALSE(queued.is_done());

    auto cancelled = loop.post_task_handle([] {});
    CHECK(cancelled.cancel());
    CHECK(cancelled.state() == TaskExecutionState::kCancelled);
    CHECK(cancelled.is_done());

    TaskHandle invalid;
    CHECK(invalid.state() == TaskExecutionState::kInvalid);
    CHECK_FALSE(invalid.is_done());

    loop.async_run();

    auto completed = loop.post_task_handle([] {});
    CHECK(completed.wait(1000));
    CHECK(completed.state() == TaskExecutionState::kCompleted);
    CHECK(completed.is_done());

    auto failed = loop.post_task_handle([] { throw std::runtime_error("boom"); });
    CHECK(failed.wait(1000));
    CHECK(failed.state() == TaskExecutionState::kFailed);
    CHECK(failed.is_done());

    loop.quit();
    loop.wait_for_quit();

    {
      SmallQueueMessageLoop drop_loop(MessageLoop::kNormalType);
      drop_loop.set_strategy(MessageLoop::kPopStrategy);
      auto first = drop_loop.post_task_handle([] {});
      auto second = drop_loop.post_task_handle([] {});
      CHECK(first.wait(100));
      CHECK(first.state() == TaskExecutionState::kDropped);
      CHECK(first.is_done());
      CHECK(second.state() == TaskExecutionState::kQueued);
      CHECK_FALSE(second.is_done());
    }

    {
      SmallQueueMessageLoop reject_loop(MessageLoop::kNormalType);
      CHECK(reject_loop.post_task([] {}));
      PostTaskOptions options;
      options.overflow_policy = TaskOverflowPolicy::kReject;
      auto rejected = reject_loop.post_task_handle([] {}, options);
      CHECK(rejected.wait(100));
      CHECK(rejected.state() == TaskExecutionState::kRejected);
      CHECK(rejected.is_done());
    }
  }

  // ---

  TEST_CASE("post_task_with_priority_handle with kNoPriority on priority loop is rejected") {
    MessageLoop loop(MessageLoop::kPriorityType);

    std::atomic<bool> ran{false};
    auto handle = loop.post_task_with_priority_handle([&ran] { ran.store(true, std::memory_order_release); },
                                                      MessageLoop::kNoPriority);

    CHECK(handle.wait(100));
    CHECK(handle.state() == TaskExecutionState::kRejected);
    CHECK_FALSE(ran.load(std::memory_order_acquire));
  }

  // ---

  TEST_CASE("post_task_with_priority_handle on non-priority loop is rejected") {
    MessageLoop loop(MessageLoop::kNormalType);

    std::atomic<bool> ran{false};
    auto handle = loop.post_task_with_priority_handle([&ran] { ran.store(true, std::memory_order_release); },
                                                      MessageLoop::kHighestPriority);

    CHECK(handle.wait(100));
    CHECK(handle.state() == TaskExecutionState::kRejected);
    CHECK_FALSE(ran.load(std::memory_order_acquire));
  }

  // ---

  TEST_CASE("post_task_with_priority_handle on lockfree loop is rejected") {
    MessageLoop loop(MessageLoop::kLockfreeType);

    std::atomic<bool> ran{false};
    auto handle = loop.post_task_with_priority_handle([&ran] { ran.store(true, std::memory_order_release); },
                                                      MessageLoop::kNormalPriority);

    CHECK(handle.wait(100));
    CHECK(handle.state() == TaskExecutionState::kRejected);
    CHECK_FALSE(ran.load(std::memory_order_acquire));
  }

  // ---

  TEST_CASE("default PostTaskOptions yield droppable kUseDispatcherStrategy behavior") {
    MessageLoop loop;
    loop.async_run();

    PostTaskOptions options;
    CHECK(options.overflow_policy == TaskOverflowPolicy::kUseDispatcherStrategy);
    CHECK(options.drop_policy == TaskDropPolicy::kDroppable);
    CHECK_FALSE(options.cancellation_token.valid());

    std::atomic<bool> ran{false};
    auto handle = loop.post_task_handle([&ran] { ran.store(true, std::memory_order_release); }, options);

    CHECK(handle.wait(1000));
    CHECK(handle.state() == TaskExecutionState::kCompleted);
    CHECK(ran.load(std::memory_order_acquire));

    loop.quit();
    loop.wait_for_quit();
  }

  // ---

  TEST_CASE("parent token cancellation while queued transitions handle to kCancelled before exec") {
    MessageLoop loop;
    CancellationSource source;

    PostTaskOptions options;
    options.cancellation_token = source.token();

    std::atomic<bool> ran{false};
    auto handle = loop.post_task_handle([&ran] { ran.store(true, std::memory_order_release); }, options);
    CHECK(handle.state() == TaskExecutionState::kQueued);

    CHECK(source.request_cancel());
    CHECK(handle.wait(100));
    CHECK(handle.state() == TaskExecutionState::kCancelled);

    loop.async_run();
    loop.wait_for_idle();
    CHECK_FALSE(ran.load(std::memory_order_acquire));

    loop.quit();
    loop.wait_for_quit();
  }

  // ---

  TEST_CASE("parent cancellation while running leaves state kRunning and signals token") {
    ThreadPool pool(1);
    CancellationSource source;
    PostTaskOptions options;
    options.cancellation_token = source.token();

    std::atomic<bool> started{false};
    std::atomic<bool> observed_cancel{false};
    std::atomic<bool> release{false};

    auto handle = pool.post_task_handle(
        [&] {
          started.store(true, std::memory_order_release);

          while (!release.load(std::memory_order_acquire)) {
            std::this_thread::yield();
          }
        },
        options);

    auto registration = handle.cancellation_token().register_callback(
        [&observed_cancel] { observed_cancel.store(true, std::memory_order_release); });
    CHECK(registration.valid());

    CHECK(wait_until([&started] { return started.load(std::memory_order_acquire); }));
    CHECK(handle.state() == TaskExecutionState::kRunning);

    CHECK(source.request_cancel());
    CHECK(wait_until([&observed_cancel] { return observed_cancel.load(std::memory_order_acquire); }));
    CHECK(handle.state() == TaskExecutionState::kRunning);
    CHECK(handle.cancellation_token().is_cancellation_requested());

    release.store(true, std::memory_order_release);
    CHECK(handle.wait(1000));
    CHECK(handle.state() == TaskExecutionState::kCompleted);

    pool.shutdown();
  }

  // ---

  TEST_CASE("ThreadPool copied TaskHandle shares state across copies") {
    ThreadPool pool(1);

    std::atomic<bool> release{false};
    auto blocker = pool.post_task_handle([&release] {
      while (!release.load(std::memory_order_acquire)) {
        std::this_thread::yield();
      }
    });

    auto original = pool.post_task_handle([] {});
    TaskHandle copy = original;
    CHECK(copy.valid());
    CHECK(copy.state() == original.state());

    release.store(true, std::memory_order_release);
    CHECK(original.wait(1000));
    CHECK(copy.is_done());
    CHECK(copy.state() == TaskExecutionState::kCompleted);
    CHECK(blocker.wait(1000));

    pool.shutdown();
  }

  // ---

  TEST_CASE("ThreadPool moved-from TaskHandle becomes invalid") {
    ThreadPool pool(1);

    auto original = pool.post_task_handle([] {});
    TaskHandle moved(std::move(original));

    CHECK(moved.valid());
    CHECK_FALSE(original.valid());
    CHECK(original.state() == TaskExecutionState::kInvalid);
    CHECK_FALSE(original.cancel());
    CHECK_FALSE(original.wait(0));

    CHECK(moved.wait(1000));
    CHECK(moved.state() == TaskExecutionState::kCompleted);

    pool.shutdown();
  }

  // ---

  TEST_CASE("ThreadPool move-assign leaves source invalid and target valid") {
    ThreadPool pool(1);

    auto original = pool.post_task_handle([] {});
    TaskHandle target;
    target = std::move(original);

    CHECK(target.valid());
    CHECK_FALSE(original.valid());
    CHECK(target.wait(1000));
    CHECK(target.state() == TaskExecutionState::kCompleted);

    pool.shutdown();
  }

  // ---

  TEST_CASE("ThreadPool multiple TaskHandle copies share cancellation visibility") {
    SmallQueueThreadPool pool(1);

    std::atomic<bool> release{false};
    std::atomic<bool> worker_started{false};
    CHECK(pool.post_task([&] {
      worker_started.store(true, std::memory_order_release);
      while (!release.load(std::memory_order_acquire)) {
        std::this_thread::yield();
      }
    }));
    CHECK(wait_until([&worker_started] { return worker_started.load(std::memory_order_acquire); }));

    std::atomic<bool> ran{false};
    auto first = pool.post_task_handle([&ran] { ran.store(true, std::memory_order_release); });
    TaskHandle second = first;
    TaskHandle third;
    third = first;

    CHECK(second.cancel());
    CHECK(first.state() == TaskExecutionState::kCancelled);
    CHECK(third.state() == TaskExecutionState::kCancelled);

    release.store(true, std::memory_order_release);
    CHECK_FALSE(ran.load(std::memory_order_acquire));

    pool.shutdown();
  }

  // ---

  TEST_CASE("ThreadPool already-cancelled parent token rejects via cancellation") {
    ThreadPool pool(1);
    CancellationSource source;
    CHECK(source.request_cancel());

    PostTaskOptions options;
    options.cancellation_token = source.token();

    std::atomic<bool> ran{false};
    auto handle = pool.post_task_handle([&ran] { ran.store(true, std::memory_order_release); }, options);

    CHECK(handle.wait(1000));
    CHECK(handle.state() == TaskExecutionState::kCancelled);
    CHECK_FALSE(ran.load(std::memory_order_acquire));

    pool.shutdown();
  }

  // ---

  TEST_CASE("ThreadPool default PostTaskOptions yield droppable kUseDispatcherStrategy behavior") {
    ThreadPool pool(1);

    PostTaskOptions options;
    CHECK(options.overflow_policy == TaskOverflowPolicy::kUseDispatcherStrategy);
    CHECK(options.drop_policy == TaskDropPolicy::kDroppable);
    CHECK_FALSE(options.cancellation_token.valid());

    std::atomic<bool> ran{false};
    auto handle = pool.post_task_handle([&ran] { ran.store(true, std::memory_order_release); }, options);

    CHECK(handle.wait(1000));
    CHECK(handle.state() == TaskExecutionState::kCompleted);
    CHECK(ran.load(std::memory_order_acquire));

    pool.shutdown();
  }

  // ---

  TEST_CASE("ThreadPool wait on completed handle returns true immediately") {
    ThreadPool pool(1);

    auto handle = pool.post_task_handle([] {});
    CHECK(handle.wait(1000));
    CHECK(handle.state() == TaskExecutionState::kCompleted);

    CHECK(handle.wait(0));
    CHECK(handle.wait(1));
    CHECK(handle.wait(TaskHandle::kInfinite));

    pool.shutdown();
  }
}

// NOLINTEND
