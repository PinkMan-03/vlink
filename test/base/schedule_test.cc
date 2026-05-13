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

#include "./base/schedule.h"

#include <doctest/doctest.h>

#include <atomic>
#include <chrono>
#include <stdexcept>
#include <thread>

#include "./base/message_loop.h"

//
#include "../common_test.h"

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static void sleep_ms(int ms) { std::this_thread::sleep_for(std::chrono::milliseconds(ms)); }

// ---------------------------------------------------------------------------
// TEST SUITE
// ---------------------------------------------------------------------------

TEST_SUITE("base-Schedule") {
  // -------------------------------------------------------------------------
  // Config tests
  // -------------------------------------------------------------------------

  TEST_CASE("Config default constructor sets all fields to zero") {
    Schedule::Config cfg;
    CHECK(cfg.delay_ms == 0);
    CHECK(cfg.priority == 0);
    CHECK(cfg.schedule_timeout_ms == 0);
    CHECK(cfg.execution_timeout_ms == 0);
  }

  TEST_CASE("Config explicit constructor sets fields") {
    Schedule::Config cfg(100, 50, 200, 300);
    CHECK(cfg.delay_ms == 100);
    CHECK(cfg.priority == 50);
    CHECK(cfg.schedule_timeout_ms == 200);
    CHECK(cfg.execution_timeout_ms == 300);
  }

  TEST_CASE("Config explicit constructor with defaults") {
    Schedule::Config cfg(50);
    CHECK(cfg.delay_ms == 50);
    CHECK(cfg.priority == 0);
    CHECK(cfg.schedule_timeout_ms == 0);
    CHECK(cfg.execution_timeout_ms == 0);
  }

  TEST_CASE("Config explicit constructor partial args") {
    Schedule::Config cfg(10, 5);
    CHECK(cfg.delay_ms == 10);
    CHECK(cfg.priority == 5);
    CHECK(cfg.schedule_timeout_ms == 0);
    CHECK(cfg.execution_timeout_ms == 0);
  }

  // -------------------------------------------------------------------------
  // Status tests (via Schedule::process directly)
  // -------------------------------------------------------------------------

  TEST_CASE("Status default constructed is valid") {
    // The default constructor initialises impl_ with is_valid = true.
    Schedule::Status s;
    CHECK(s.is_valid());
  }

  TEST_CASE("Status set_valid and is_valid") {
    Schedule::Status s;
    s.set_valid(true);
    CHECK(s.is_valid());

    s.set_valid(false);
    CHECK_FALSE(s.is_valid());
  }

  TEST_CASE("Status is move constructible") {
    Schedule::Status s;
    s.set_valid(true);

    Schedule::Status s2(std::move(s));
    CHECK(s2.is_valid());
  }

  TEST_CASE("Status is move assignable") {
    Schedule::Status s;
    s.set_valid(true);

    Schedule::Status s2;
    s2 = std::move(s);
    CHECK(s2.is_valid());
  }

  // -------------------------------------------------------------------------
  // Schedule::process — void callback
  // -------------------------------------------------------------------------

  TEST_CASE("process with void callback — wrapper fires the callback") {
    Schedule::Config cfg;
    std::atomic<bool> ran{false};

    Schedule::Callback wrapper;
    auto status = Schedule::process(cfg, [&ran]() { ran.store(true); }, wrapper);

    CHECK(status.is_valid());
    REQUIRE(wrapper != nullptr);

    // Calling the wrapper executes the underlying callback
    wrapper();
    CHECK(ran.load());
  }

  TEST_CASE("process void callback — multiple wrappers are independent") {
    std::atomic<int> count{0};

    Schedule::Callback w1;
    Schedule::Callback w2;

    (void)Schedule::process(Schedule::Config{}, [&count]() { count.fetch_add(1); }, w1);
    (void)Schedule::process(Schedule::Config{}, [&count]() { count.fetch_add(10); }, w2);

    w1();
    w2();

    CHECK(count.load() == 11);
  }

  // -------------------------------------------------------------------------
  // Schedule::process — via MessageLoop::exec_task
  // -------------------------------------------------------------------------

  TEST_CASE("exec_task void callback runs and status is valid") {
    MessageLoop loop;
    loop.async_run();

    std::atomic<bool> ran{false};
    auto status = loop.exec_task(Schedule::Config{}, [&ran]() { ran.store(true); });

    CHECK(status.is_valid());
    loop.wait_for_idle();
    CHECK(ran.load());

    loop.quit();
    loop.wait_for_quit();
  }

  TEST_CASE("exec_task void callback with delay posts via timer") {
    MessageLoop loop;
    loop.async_run();

    std::atomic<bool> ran{false};
    auto status = loop.exec_task(Schedule::Config{50}, [&ran]() { ran.store(true); });

    CHECK(status.is_valid());

    // Should not have run yet
    CHECK_FALSE(ran.load());

    // Wait past the delay
    sleep_ms(80);
    CHECK(ran.load());

    loop.quit();
    loop.wait_for_quit();
  }

  // -------------------------------------------------------------------------
  // on_execution_timeout
  // -------------------------------------------------------------------------

  TEST_CASE("on_execution_timeout fires when task exceeds limit") {
    MessageLoop loop;
    loop.async_run();

    std::atomic<bool> timeout_fired{false};

    // Use delay_ms=100 so the callback chain completes before the task starts.
    // The wrapper locks impl->mtx for the full task duration; registering
    // on_execution_timeout requires the same lock.  Without a delay the task
    // may acquire the lock before on_execution_timeout() registers the callback.
    loop.exec_task(Schedule::Config{50, 0, 0, 30}, []() { std::this_thread::sleep_for(100ms); })
        .on_execution_timeout([&timeout_fired]() { timeout_fired.store(true); });

    // 50ms delay + 100ms execution + 30ms timeout + buffer
    sleep_ms(220);
    CHECK(timeout_fired.load());

    loop.quit();
    loop.wait_for_quit();
  }

  TEST_CASE("on_execution_timeout does not fire when task is fast") {
    MessageLoop loop;
    loop.async_run();

    std::atomic<bool> timeout_fired{false};

    // execution_timeout_ms = 200 ms; task finishes immediately
    loop.exec_task(Schedule::Config{0, 0, 0, 200}, []() {}).on_execution_timeout([&timeout_fired]() {
      timeout_fired.store(true);
    });

    loop.wait_for_idle();
    sleep_ms(50);
    CHECK_FALSE(timeout_fired.load());

    loop.quit();
    loop.wait_for_quit();
  }

  // -------------------------------------------------------------------------
  // on_schedule_timeout
  // -------------------------------------------------------------------------

  TEST_CASE("on_schedule_timeout fires when task waits too long in queue") {
    MessageLoop loop;
    loop.async_run();

    std::atomic<bool> sched_timeout{false};
    std::atomic<bool> gate{false};

    // Block the loop so the next task cannot start
    loop.post_task([&gate]() {
      while (!gate.load()) {
        std::this_thread::yield();
      }
    });

    sleep_ms(30);

    // This task will not start for at least 200 ms because the loop is blocked
    loop.exec_task(Schedule::Config{0, 0, 50, 0}, []() {}).on_schedule_timeout([&sched_timeout]() {
      sched_timeout.store(true);
    });

    sleep_ms(120);

    // Unblock and allow loop to process
    gate.store(true);
    loop.wait_for_idle();

    CHECK(sched_timeout.load());

    loop.quit();
    loop.wait_for_quit();
  }

  // -------------------------------------------------------------------------
  // on_catch
  // -------------------------------------------------------------------------

  TEST_CASE("on_catch fires when callback throws std::exception") {
    MessageLoop loop;
    loop.async_run();

    std::atomic<bool> caught{false};
    std::string caught_msg;
    std::mutex msg_mtx;

    // Use delay_ms=100 so on_catch() is registered before the task starts.
    // The wrapper holds impl->mtx for the entire task body; registering
    // on_catch() requires the same lock, so without a delay the exception
    // handler may not be set when the task throws.
    loop.exec_task(Schedule::Config{100}, []() { throw std::runtime_error("test error"); })
        .on_catch([&caught, &caught_msg, &msg_mtx](std::exception& e) {
          std::lock_guard lk(msg_mtx);
          caught.store(true);
          caught_msg = e.what();
        });

    // 100ms delay + execution + buffer
    sleep_ms(150);

    CHECK(caught.load());
    {
      std::lock_guard lk(msg_mtx);
      CHECK(caught_msg == "test error");
    }

    loop.quit();
    loop.wait_for_quit();
  }

  TEST_CASE("on_catch does not fire when no exception is thrown") {
    MessageLoop loop;
    loop.async_run();

    std::atomic<bool> caught{false};

    loop.exec_task(Schedule::Config{}, []() { /* no throw */ }).on_catch([&caught](std::exception&) {
      caught.store(true);
    });

    loop.wait_for_idle();
    sleep_ms(50);
    CHECK_FALSE(caught.load());

    loop.quit();
    loop.wait_for_quit();
  }

  // -------------------------------------------------------------------------
  // process_with_ret / RetStatus
  // -------------------------------------------------------------------------

  TEST_CASE("process_with_ret on_then fires when callback returns true") {
    Schedule::Config cfg;
    std::atomic<bool> then_ran{false};
    std::atomic<bool> else_ran{false};

    Schedule::Callback wrapper;
    auto ret_status = Schedule::process_with_ret(cfg, []() -> bool { return true; }, wrapper);

    ret_status
        .on_then([&then_ran]() -> bool {
          then_ran.store(true);
          return true;
        })
        .on_else([&else_ran]() { else_ran.store(true); });

    REQUIRE(wrapper != nullptr);
    wrapper();

    CHECK(then_ran.load());
    CHECK_FALSE(else_ran.load());
  }

  TEST_CASE("process_with_ret on_else fires when callback returns false") {
    Schedule::Config cfg;
    std::atomic<bool> then_ran{false};
    std::atomic<bool> else_ran{false};

    Schedule::Callback wrapper;
    auto ret_status = Schedule::process_with_ret(cfg, []() -> bool { return false; }, wrapper);

    ret_status
        .on_then([&then_ran]() -> bool {
          then_ran.store(true);
          return true;
        })
        .on_else([&else_ran]() { else_ran.store(true); });

    REQUIRE(wrapper != nullptr);
    wrapper();

    CHECK_FALSE(then_ran.load());
    CHECK(else_ran.load());
  }

  TEST_CASE("exec_task bool callback on_then and on_else via loop") {
    MessageLoop loop;
    loop.async_run();

    // --- case: returns true ---
    {
      std::atomic<bool> then_ran{false};
      std::atomic<bool> else_ran{false};

      loop.exec_task(Schedule::Config{}, []() -> bool { return true; })
          .on_then([&then_ran]() -> bool {
            then_ran.store(true);
            return true;
          })
          .on_else([&else_ran]() { else_ran.store(true); });

      loop.wait_for_idle();
      CHECK(then_ran.load());
      CHECK_FALSE(else_ran.load());
    }

    loop.quit();
    loop.wait_for_quit();
  }

  // Moved to a standalone TEST_CASE below to avoid SUBCASE re-entry issues.

  TEST_CASE("RetStatus on_catch fires on exception in bool callback") {
    MessageLoop loop;
    loop.async_run();

    std::atomic<bool> caught{false};
    std::atomic<bool> then_ran{false};

    loop.exec_task(Schedule::Config{100},
                   []() -> bool {
                     throw std::logic_error("boom");
                     return true;
                   })
        .on_then([&then_ran]() -> bool {
          then_ran.store(true);
          return true;
        })
        .on_catch([&caught](std::exception&) { caught.store(true); });

    loop.wait_for_idle();
    // Wait for on_catch to complete
    for (int i = 0; i < 100 && !caught.load(); ++i) {
      sleep_ms(20);
    }

    CHECK(caught.load());
    CHECK_FALSE(then_ran.load());

    loop.quit();
    loop.wait_for_quit();
  }

  // -------------------------------------------------------------------------
  // delay_ms > 0
  // -------------------------------------------------------------------------

  TEST_CASE("delay_ms defers execution") {
    MessageLoop loop;
    loop.async_run();

    std::atomic<bool> ran{false};

    auto status = loop.exec_task(Schedule::Config{80}, [&ran]() { ran.store(true); });
    CHECK(status.is_valid());

    // Must not have run yet (use 40ms margin for Windows ~15ms timer granularity)
    sleep_ms(40);
    CHECK_FALSE(ran.load());

    // Should have run by now
    sleep_ms(80);
    CHECK(ran.load());

    loop.quit();
    loop.wait_for_quit();
  }

  TEST_CASE("delay_ms zero posts immediately") {
    MessageLoop loop;
    loop.async_run();

    std::atomic<bool> ran{false};
    loop.exec_task(Schedule::Config{0}, [&ran]() { ran.store(true); });

    loop.wait_for_idle();
    CHECK(ran.load());

    loop.quit();
    loop.wait_for_quit();
  }

  // -------------------------------------------------------------------------
  // Priority via Config on kPriorityType loop
  // -------------------------------------------------------------------------

  TEST_CASE("Config priority used by kPriorityType loop for ordering") {
    MessageLoop loop(MessageLoop::kPriorityType);
    loop.async_run();

    std::atomic<bool> gate{false};
    std::vector<int> order;
    std::mutex mtx;

    // Block the loop
    loop.post_task([&gate]() {
      while (!gate.load()) {
        std::this_thread::yield();
      }
    });
    sleep_ms(20);

    loop.exec_task(Schedule::Config{0, MessageLoop::kLowestPriority}, [&order, &mtx]() {
      std::lock_guard lk(mtx);
      order.push_back(1);
    });

    loop.exec_task(Schedule::Config{0, MessageLoop::kHighestPriority}, [&order, &mtx]() {
      std::lock_guard lk(mtx);
      order.push_back(3);
    });

    loop.exec_task(Schedule::Config{0, MessageLoop::kNormalPriority}, [&order, &mtx]() {
      std::lock_guard lk(mtx);
      order.push_back(2);
    });

    gate.store(true);
    loop.wait_for_idle();

    REQUIRE(order.size() == 3);
    CHECK(order[0] == 3);
    CHECK(order[1] == 2);
    CHECK(order[2] == 1);

    loop.quit();
    loop.wait_for_quit();
  }

  TEST_CASE("Config with schedule_timeout_ms and execution_timeout_ms set") {
    Schedule::Config cfg(0, 0, 100, 200);
    CHECK(cfg.schedule_timeout_ms == 100);
    CHECK(cfg.execution_timeout_ms == 200);
  }

  TEST_CASE("Status on_schedule_timeout chaining returns self reference") {
    Schedule::Callback wrapper;
    auto status = Schedule::process(Schedule::Config{}, []() {}, wrapper);

    auto& ref = status.on_schedule_timeout([]() {});
    CHECK(&ref == &status);
  }

  TEST_CASE("Status on_execution_timeout chaining returns self reference") {
    Schedule::Callback wrapper;
    auto status = Schedule::process(Schedule::Config{}, []() {}, wrapper);

    auto& ref = status.on_execution_timeout([]() {});
    CHECK(&ref == &status);
  }

  TEST_CASE("Status on_catch chaining returns self reference") {
    Schedule::Callback wrapper;
    auto status = Schedule::process(Schedule::Config{}, []() {}, wrapper);

    auto& ref = status.on_catch([](std::exception&) {});
    CHECK(&ref == &status);
  }

  TEST_CASE("RetStatus on_then chaining returns RetStatus reference") {
    Schedule::Callback wrapper;
    auto ret_status = Schedule::process_with_ret(Schedule::Config{}, []() -> bool { return true; }, wrapper);

    auto& ref = ret_status.on_then([]() -> bool { return true; });
    CHECK(&ref == &ret_status);
  }

  TEST_CASE("chained on_then stops when one returns false") {
    Schedule::Callback wrapper;
    std::atomic<int> count{0};

    auto ret_status = Schedule::process_with_ret(Schedule::Config{}, []() -> bool { return true; }, wrapper);

    ret_status
        .on_then([&count]() -> bool {
          count.fetch_add(1);
          return false;
        })
        .on_then([&count]() -> bool {
          count.fetch_add(1);
          return true;
        });

    REQUIRE(wrapper != nullptr);
    wrapper();

    CHECK(count.load() == 1);
  }

  TEST_CASE("exec_task with delay and execution timeout both working") {
    MessageLoop loop;
    loop.async_run();

    std::atomic<bool> ran{false};
    std::atomic<bool> timeout_fired{false};

    loop.exec_task(Schedule::Config{50, 0, 0, 200}, [&ran]() { ran.store(true); })
        .on_execution_timeout([&timeout_fired]() { timeout_fired.store(true); });

    sleep_ms(80);

    CHECK(ran.load());
    CHECK_FALSE(timeout_fired.load());

    loop.quit();
    loop.wait_for_quit();
  }

  TEST_CASE("RetStatus on_catch fires and on_then/on_else do not on exception") {
    Schedule::Callback wrapper;
    std::atomic<bool> caught{false};
    std::atomic<bool> then_ran{false};
    std::atomic<bool> else_ran{false};

    auto ret_status =
        Schedule::process_with_ret(Schedule::Config{}, []() -> bool { throw std::runtime_error("fail"); }, wrapper);

    ret_status
        .on_then([&then_ran]() -> bool {
          then_ran.store(true);
          return true;
        })
        .on_else([&else_ran]() { else_ran.store(true); })
        .on_catch([&caught](std::exception&) { caught.store(true); });

    REQUIRE(wrapper != nullptr);
    wrapper();

    CHECK(caught.load());
    CHECK_FALSE(then_ran.load());
    CHECK_FALSE(else_ran.load());
  }

  TEST_CASE("on_schedule_timeout does not fire when task starts promptly") {
    MessageLoop loop;
    loop.async_run();

    std::atomic<bool> sched_timeout{false};

    loop.exec_task(Schedule::Config{0, 0, 200, 0}, []() {}).on_schedule_timeout([&sched_timeout]() {
      sched_timeout.store(true);
    });

    loop.wait_for_idle();
    sleep_ms(50);
    CHECK_FALSE(sched_timeout.load());

    loop.quit();
    loop.wait_for_quit();
  }

  TEST_CASE("multiple exec_task void callbacks all run") {
    MessageLoop loop;
    loop.async_run();

    std::atomic<int> count{0};

    for (int i = 0; i < 5; ++i) {
      loop.exec_task(Schedule::Config{}, [&count]() { count.fetch_add(1); });
    }

    loop.wait_for_idle();
    CHECK(count.load() == 5);

    loop.quit();
    loop.wait_for_quit();
  }

  TEST_CASE("chained on_then callbacks") {
    MessageLoop loop;
    loop.async_run();

    std::atomic<int> chain_count{0};

    loop.exec_task(Schedule::Config{50}, []() -> bool { return true; })
        .on_then([&chain_count]() -> bool {
          chain_count.fetch_add(1);
          return true;
        })
        .on_then([&chain_count]() -> bool {
          chain_count.fetch_add(1);
          return true;
        })
        .on_then([&chain_count]() -> bool {
          chain_count.fetch_add(1);
          return true;
        });

    for (int attempt = 0; attempt < 300 && chain_count.load() < 3; ++attempt) {
      loop.wait_for_idle();
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    CHECK(chain_count.load() == 3);

    loop.quit();
    loop.wait_for_quit();
  }

  TEST_CASE("on_then registered after dispatch is rejected (not silently accepted)") {
    MessageLoop loop;
    loop.async_run();

    std::atomic<int> first_then_count{0};
    std::atomic<int> late_then_count{0};

    auto status = loop.exec_task(Schedule::Config{}, []() -> bool { return true; });

    status.on_then([&first_then_count]() -> bool {
      first_then_count.fetch_add(1);
      return true;
    });

    for (int attempt = 0; attempt < 300 && first_then_count.load() == 0; ++attempt) {
      loop.wait_for_idle();
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    REQUIRE(first_then_count.load() == 1);

    status.on_then([&late_then_count]() -> bool {
      late_then_count.fetch_add(1);
      return true;
    });

    CHECK(late_then_count.load() == 0);

    loop.wait_for_idle();

    CHECK(late_then_count.load() == 0);

    loop.quit();
    loop.wait_for_quit();
  }
}

// NOLINTEND
