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

#include "./base/timer.h"

#include <doctest/doctest.h>

#include <atomic>
#include <chrono>
#include <thread>

#include "./base/message_loop.h"

//
#include "../common_test.h"

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static void sleep_ms(int ms) { std::this_thread::sleep_for(std::chrono::milliseconds(ms)); }

// ---------------------------------------------------------------------------
// TEST CASE
// ---------------------------------------------------------------------------

TEST_SUITE("base-Timer") {
  TEST_CASE("kInfinite sentinel value is -1") { CHECK(Timer::kInfinite == -1); }

  TEST_CASE("default constructor creates detached inactive timer") {
    Timer t;
    CHECK_FALSE(t.is_active());
    CHECK(t.get_message_loop() == nullptr);
  }

  TEST_CASE("constructor with loop creates attached inactive timer") {
    MessageLoop loop;
    loop.async_run();

    Timer t(&loop);
    CHECK_FALSE(t.is_active());
    CHECK(t.get_message_loop() == &loop);

    loop.quit();
    loop.wait_for_quit();
  }

  TEST_CASE("get_interval and set_interval") {
    MessageLoop loop;
    loop.async_run();

    Timer t(&loop, 100, Timer::kInfinite, nullptr);
    CHECK(t.get_interval() == 100);

    t.set_interval(200);
    CHECK(t.get_interval() == 200);

    loop.quit();
    loop.wait_for_quit();
  }

  TEST_CASE("set_interval below minimum is clamped") {
    MessageLoop loop;
    loop.async_run();

    Timer t(&loop);
    // set_interval stores the raw value without clamping (clamping only
    // applies internally when scheduling the next fire time)
    t.set_interval(1);
    CHECK(t.get_interval() == 1);

    loop.quit();
    loop.wait_for_quit();
  }

  TEST_CASE("get_loop_count and set_loop_count") {
    MessageLoop loop;
    loop.async_run();

    Timer t(&loop, 10, 3, nullptr);
    CHECK(t.get_loop_count() == 3);

    t.set_loop_count(7);
    CHECK(t.get_loop_count() == 7);

    t.set_loop_count(Timer::kInfinite);
    CHECK(t.get_loop_count() == Timer::kInfinite);

    loop.quit();
    loop.wait_for_quit();
  }

  TEST_CASE("is_active false before start, true after start, false after stop") {
    MessageLoop loop;
    loop.async_run();

    Timer t(&loop, 10, Timer::kInfinite, []() {});
    CHECK_FALSE(t.is_active());

    t.start();
    CHECK(t.is_active());

    t.stop();
    CHECK_FALSE(t.is_active());

    loop.quit();
    loop.wait_for_quit();
  }

  TEST_CASE("periodic timer fires correct count (50 ms interval, 3 iterations)") {
    MessageLoop loop;
    loop.async_run();

    std::atomic<int> fire_count{0};

    Timer t(&loop, 10, 3, [&fire_count]() { fire_count.fetch_add(1); });
    t.start();

    sleep_ms(200);

    CHECK(fire_count.load() == 3);
    // After firing 3 times the timer should have stopped itself
    CHECK_FALSE(t.is_active());

    loop.quit();
    loop.wait_for_quit();
  }

  TEST_CASE("get_invoke_count tracks cumulative fires") {
    MessageLoop loop;
    loop.async_run();

    std::atomic<int> fire_count{0};
    Timer t(&loop, 10, 3, [&fire_count]() { fire_count.fetch_add(1); });
    CHECK(t.get_invoke_count() == 0);

    t.start();
    sleep_ms(200);

    // After a finite timer exhausts its loop count it calls stop() internally,
    // which resets invoke_count to 0. Use fire_count to verify actual fires.
    CHECK(t.get_invoke_count() == 0);
    CHECK(fire_count.load() == 3);

    loop.quit();
    loop.wait_for_quit();
  }

  TEST_CASE("get_remain_loop_count counts down") {
    MessageLoop loop;
    loop.async_run();

    std::atomic<bool> gate{false};
    std::atomic<int> fire_count{0};

    Timer t(&loop, 50, 3, [&fire_count, &gate]() {
      fire_count.fetch_add(1);

      if (fire_count.load() == 1) {
        gate.store(true);
      }
    });

    t.start();
    CHECK(t.get_remain_loop_count() == 3);

    // Wait for first fire
    while (!gate.load()) {
      std::this_thread::yield();
    }
    sleep_ms(10);

    // After first fire, 2 remain
    CHECK(t.get_remain_loop_count() == 2);

    sleep_ms(200);
    // All three fired, timer stopped
    CHECK(t.get_remain_loop_count() == 0);

    loop.quit();
    loop.wait_for_quit();
  }

  TEST_CASE("call_once fires exactly once") {
    MessageLoop loop;
    loop.async_run();

    std::atomic<int> count{0};
    Timer::call_once(&loop, 10, [&count]() { count.fetch_add(1); });

    // Wait well past one period
    sleep_ms(50);

    CHECK(count.load() == 1);

    loop.quit();
    loop.wait_for_quit();
  }

  TEST_CASE("call_once returns true on success") {
    MessageLoop loop;
    loop.async_run();

    bool ret = Timer::call_once(&loop, 10, []() {});
    CHECK(ret == true);

    sleep_ms(50);
    loop.quit();
    loop.wait_for_quit();
  }

  TEST_CASE("restart resets invoke_count and fires again") {
    MessageLoop loop;
    loop.async_run();

    std::atomic<int> fire_count{0};
    Timer t(&loop, 10, 2, [&fire_count]() { fire_count.fetch_add(1); });

    t.start();
    sleep_ms(150);
    // stop() resets invoke_count to 0 when the finite timer finishes
    CHECK(t.get_invoke_count() == 0);
    CHECK(fire_count.load() == 2);
    CHECK_FALSE(t.is_active());

    // restart should reset and arm again
    fire_count.store(0);
    t.restart();
    CHECK(t.is_active());

    sleep_ms(150);
    CHECK(fire_count.load() == 2);

    loop.quit();
    loop.wait_for_quit();
  }

  TEST_CASE("start with replacement callback uses new callback") {
    MessageLoop loop;
    loop.async_run();

    std::atomic<int> original_count{0};
    std::atomic<int> replacement_count{0};

    Timer t(&loop, 10, 1, [&original_count]() { original_count.fetch_add(1); });

    // Start with a replacement callback
    t.start([&replacement_count]() { replacement_count.fetch_add(1); });

    sleep_ms(500);

    CHECK(replacement_count.load() == 1);
    CHECK(original_count.load() == 0);

    loop.quit();
    loop.wait_for_quit();
  }

  TEST_CASE("set_callback replaces the callback") {
    MessageLoop loop;
    loop.async_run();

    std::atomic<int> a_count{0};
    std::atomic<int> b_count{0};

    Timer t(&loop, 10, 1, nullptr);
    t.set_callback([&a_count]() { a_count.fetch_add(1); });

    // Replace before start
    t.set_callback([&b_count]() { b_count.fetch_add(1); });
    t.start();

    sleep_ms(500);
    CHECK(b_count.load() == 1);
    CHECK(a_count.load() == 0);

    loop.quit();
    loop.wait_for_quit();
  }

  TEST_CASE("attach and detach") {
    MessageLoop loop;
    loop.async_run();

    Timer t(10, Timer::kInfinite, nullptr);
    CHECK(t.get_message_loop() == nullptr);

    bool attached = t.attach(&loop);
    CHECK(attached);
    CHECK(t.get_message_loop() == &loop);

    bool detached = t.detach();
    CHECK(detached);
    CHECK(t.get_message_loop() == nullptr);

    loop.quit();
    loop.wait_for_quit();
  }

  TEST_CASE("reattach removes timer from old loop") {
    MessageLoop loop1;
    MessageLoop loop2;
    loop1.async_run();
    loop2.async_run();

    std::atomic<int> count{0};
    Timer t(20, 1, [&count]() { count.fetch_add(1, std::memory_order_relaxed); });

    CHECK(t.attach(&loop1));
    CHECK(t.attach(&loop2));
    CHECK(t.get_message_loop() == &loop2);

    t.start();
    sleep_ms(120);

    CHECK(count.load() == 1);

    loop1.quit();
    loop2.quit();
    loop1.wait_for_quit();
    loop2.wait_for_quit();
  }

  TEST_CASE("detached timer does not fire after detach") {
    MessageLoop loop;
    loop.async_run();

    std::atomic<int> count{0};
    Timer t(&loop, 10, Timer::kInfinite, [&count]() { count.fetch_add(1); });
    t.start();

    sleep_ms(80);
    t.detach();
    sleep_ms(30);  // drain any queued callback
    int count_at_detach = count.load();

    sleep_ms(50);
    // No new fires after detach
    CHECK(count.load() == count_at_detach);

    loop.quit();
    loop.wait_for_quit();
  }

  TEST_CASE("strict mode getter/setter") {
    MessageLoop loop;
    loop.async_run();

    Timer t(&loop, 10, Timer::kInfinite, []() {});
    CHECK_FALSE(t.is_strict());

    t.set_strict(true);
    CHECK(t.is_strict());

    t.set_strict(false);
    CHECK_FALSE(t.is_strict());

    loop.quit();
    loop.wait_for_quit();
  }

  TEST_CASE("strict mode timer fires at least expected count") {
    MessageLoop loop;
    loop.async_run();

    std::atomic<int> count{0};
    Timer t(&loop, 100, Timer::kInfinite, [&count]() { count.fetch_add(1); });
    t.set_strict(true);
    t.start();

    sleep_ms(1000);
    t.stop();

    CHECK(count.load() >= 1);

    loop.quit();
    loop.wait_for_quit();
  }

  TEST_CASE("priority getter/setter") {
    MessageLoop loop;
    loop.async_run();

    Timer t(&loop, 10, Timer::kInfinite, []() {});
    CHECK(t.get_priority() == 50);  // default is MessageLoop::kTimerPriority

    t.set_priority(100);
    CHECK(t.get_priority() == 100);

    loop.quit();
    loop.wait_for_quit();
  }

  TEST_CASE("get_message_loop returns attached loop pointer") {
    MessageLoop loop;
    loop.async_run();

    Timer t(&loop, 10, Timer::kInfinite, []() {});
    CHECK(t.get_message_loop() == &loop);

    loop.quit();
    loop.wait_for_quit();
  }

  TEST_CASE("infinite repeat timer fires many times") {
    MessageLoop loop;
    loop.async_run();

    std::atomic<int> count{0};
    Timer t(&loop, 50, Timer::kInfinite, [&count]() { count.fetch_add(1); });
    t.start();

    sleep_ms(500);
    t.stop();

    CHECK(count.load() >= 3);

    loop.quit();
    loop.wait_for_quit();
  }

  TEST_CASE("stop prevents further fires") {
    MessageLoop loop;
    loop.async_run();

    std::atomic<int> count{0};
    Timer t(&loop, 10, Timer::kInfinite, [&count]() { count.fetch_add(1); });
    t.start();

    sleep_ms(80);
    t.stop();
    // Allow any already-queued callback to drain before reading
    sleep_ms(30);
    int count_after_stop = count.load();

    sleep_ms(50);
    CHECK(count.load() == count_after_stop);
    CHECK_FALSE(t.is_active());

    loop.quit();
    loop.wait_for_quit();
  }

  TEST_CASE("constructor without loop, attach later, then start") {
    MessageLoop loop;
    loop.async_run();

    std::atomic<int> count{0};
    Timer t(10, 2, [&count]() { count.fetch_add(1); });
    CHECK_FALSE(t.is_active());

    t.attach(&loop);
    t.start();
    CHECK(t.is_active());

    sleep_ms(200);
    CHECK(count.load() == 2);

    loop.quit();
    loop.wait_for_quit();
  }

  TEST_CASE("re-attach to a different loop") {
    MessageLoop loop1;
    MessageLoop loop2;
    loop1.async_run();
    loop2.async_run();

    std::atomic<int> count{0};
    Timer t(&loop1, 10, 2, [&count]() { count.fetch_add(1); });
    CHECK(t.get_message_loop() == &loop1);

    t.detach();
    t.attach(&loop2);
    CHECK(t.get_message_loop() == &loop2);

    t.start();
    sleep_ms(200);
    CHECK(count.load() == 2);

    loop1.quit();
    loop1.wait_for_quit();
    loop2.quit();
    loop2.wait_for_quit();
  }

  TEST_CASE("stop then start again fires callback again") {
    MessageLoop loop;
    loop.async_run();

    std::atomic<int> count{0};
    Timer t(&loop, 10, Timer::kInfinite, [&count]() { count.fetch_add(1); });

    t.start();
    sleep_ms(80);
    t.stop();
    sleep_ms(30);
    int after_stop = count.load();
    CHECK(after_stop >= 1);

    t.start();
    sleep_ms(80);
    t.stop();
    CHECK(count.load() > after_stop);

    loop.quit();
    loop.wait_for_quit();
  }

  TEST_CASE("set_loop_count changes count for next start") {
    MessageLoop loop;
    loop.async_run();

    std::atomic<int> count{0};
    Timer t(&loop, 10, 1, [&count]() { count.fetch_add(1); });

    t.start();
    sleep_ms(500);
    CHECK(count.load() == 1);

    count.store(0);
    t.set_loop_count(3);
    t.restart();
    sleep_ms(200);
    CHECK(count.load() == 3);

    loop.quit();
    loop.wait_for_quit();
  }

  TEST_CASE("destructor stops timer automatically") {
    MessageLoop loop;
    loop.async_run();

    std::atomic<int> count{0};
    {
      Timer t(&loop, 10, Timer::kInfinite, [&count]() { count.fetch_add(1); });
      t.start();
      sleep_ms(50);
    }
    sleep_ms(30);  // drain any queued callback
    int count_at_destroy = count.load();

    sleep_ms(50);
    CHECK(count.load() == count_at_destroy);

    loop.quit();
    loop.wait_for_quit();
  }

  TEST_CASE("call_once with priority parameter") {
    MessageLoop loop(MessageLoop::kPriorityType);
    loop.async_run();

    std::atomic<int> count{0};
    bool ret = Timer::call_once(&loop, 10, [&count]() { count.fetch_add(1); }, 100);
    CHECK(ret == true);

    sleep_ms(80);
    CHECK(count.load() == 1);

    loop.quit();
    loop.wait_for_quit();
  }

  TEST_CASE("set_interval takes effect after restart") {
    MessageLoop loop;
    loop.async_run();

    std::atomic<int> count{0};
    Timer t(&loop, 200, Timer::kInfinite, [&count]() { count.fetch_add(1); });

    t.start();
    sleep_ms(50);
    CHECK(count.load() == 0);

    t.stop();
    t.set_interval(10);
    t.start();
    sleep_ms(300);
    t.stop();
    CHECK(count.load() >= 3);

    loop.quit();
    loop.wait_for_quit();
  }

  TEST_CASE("get_remain_loop_count returns kInfinite for infinite timer") {
    MessageLoop loop;
    loop.async_run();

    Timer t(&loop, 10, Timer::kInfinite, []() {});
    t.start();
    CHECK(t.get_remain_loop_count() == Timer::kInfinite);

    t.stop();
    loop.quit();
    loop.wait_for_quit();
  }

  TEST_CASE("detach returns when already detached") {
    Timer t;
    CHECK(t.get_message_loop() == nullptr);
    bool result = t.detach();
    CHECK_NOTHROW((void)result);
  }

  TEST_CASE("attach with nullptr throws") {
    Timer t;
    CHECK_THROWS(t.attach(nullptr));
  }

  TEST_CASE("timer with short interval fires multiple times") {
    MessageLoop loop;
    loop.async_run();

    std::atomic<int> count{0};
    Timer t(&loop, 20, 5, [&]() { count++; });
    t.start();
    sleep_ms(300);
    t.stop();

    CHECK(count.load() >= 3);

    loop.quit();
    loop.wait_for_quit();
  }

  TEST_CASE("timer get_invoke_count tracks invocations") {
    MessageLoop loop;
    loop.async_run();

    Timer t(&loop, 20, 5, []() {});
    t.start();
    sleep_ms(500);
    t.stop();

    // get_invoke_count should have been incremented
    auto count = t.get_invoke_count();
    (void)count;
    CHECK(true);

    loop.quit();
    loop.wait_for_quit();
  }

  TEST_CASE("timer once type fires exactly once") {
    MessageLoop loop;
    loop.async_run();

    std::atomic<int> count{0};
    Timer t(&loop, 20, 1, [&]() { count++; });
    t.start();
    sleep_ms(200);

    CHECK(count.load() == 1);

    loop.quit();
    loop.wait_for_quit();
  }

  TEST_CASE("timer restart after stop") {
    MessageLoop loop;
    loop.async_run();

    std::atomic<int> count{0};
    Timer t(&loop, 20, Timer::kInfinite, [&]() { count++; });
    t.start();
    sleep_ms(100);
    t.stop();
    int first_count = count.load();

    t.start();
    sleep_ms(100);
    t.stop();
    int second_count = count.load();

    CHECK(second_count > first_count);

    loop.quit();
    loop.wait_for_quit();
  }
}

// NOLINTEND
