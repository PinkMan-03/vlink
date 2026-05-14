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

#include <vlink/base/cancellation.h>
#include <vlink/base/exception.h>
#include <vlink/base/logger.h>
#include <vlink/base/message_loop.h>
#include <vlink/base/task_handle.h>

#include <atomic>
#include <chrono>
#include <stdexcept>
#include <thread>
#include <vector>

int main() {
  vlink::Logger::init("example_cancellation");

  // ---------------------------------------------------------------
  // 1. Polling style cancellation.
  // ---------------------------------------------------------------
  {
    VLOG_I("=== Section 1: polling cancellation ===");

    vlink::CancellationSource source;
    auto token = source.token();

    std::thread worker([token]() {
      uint32_t iter = 0;
      while (!token.is_cancellation_requested()) {
        ++iter;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
      }
      VLOG_I("polling worker exited after ", iter, " iterations");
    });

    std::this_thread::sleep_for(std::chrono::milliseconds(80));
    const bool first = source.request_cancel();
    const bool second = source.request_cancel();
    VLOG_I("request_cancel: first=", first, " second=", second);
    worker.join();
  }

  // ---------------------------------------------------------------
  // 2. Structured cancellation via throw + RAII unwind.
  // ---------------------------------------------------------------
  {
    VLOG_I("=== Section 2: structured cancellation (OperationCancelled) ===");

    vlink::CancellationSource source;
    auto token = source.token();

    std::thread worker([token]() {
      struct RaiiUnwind {
        ~RaiiUnwind() { VLOG_I("RAII destructor ran during stack unwind"); }
      };

      try {
        RaiiUnwind guard;
        for (int i = 0; i < 1000; ++i) {
          token.throw_if_cancellation_requested();
          std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
      } catch (const vlink::Exception::OperationCancelled& ex) {
        VLOG_I("worker caught: ", ex.what());
      }
    });

    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    source.request_cancel();
    worker.join();
  }

  // ---------------------------------------------------------------
  // 3. register_callback: async one-shot notification.
  // ---------------------------------------------------------------
  {
    VLOG_I("=== Section 3: register_callback async fire ===");

    vlink::CancellationSource source;
    auto token = source.token();

    std::atomic_int fired{0};
    auto reg = token.register_callback([&fired]() { fired.fetch_add(1); });

    VLOG_I("registration valid before cancel: ", reg.valid());
    source.request_cancel();
    VLOG_I("fired counter after cancel: ", fired.load());
    VLOG_I("registration valid after cancel: ", reg.valid());
  }

  // ---------------------------------------------------------------
  // 4. register_callback after cancel -> synchronous fire on caller thread.
  // ---------------------------------------------------------------
  {
    VLOG_I("=== Section 4: register_callback after cancel (sync fire) ===");

    vlink::CancellationSource source;
    auto token = source.token();
    source.request_cancel();

    const auto reg_tid = std::this_thread::get_id();
    std::atomic<std::thread::id> fire_tid{};
    auto reg = token.register_callback([&fire_tid]() { fire_tid.store(std::this_thread::get_id()); });

    VLOG_I("registration valid: ", reg.valid(), " (expect false)");
    VLOG_I("fired synchronously on caller thread: ", (reg_tid == fire_tid.load()));
  }

  // ---------------------------------------------------------------
  // 5. CancellationRegistration::reset() unregisters before fire.
  // ---------------------------------------------------------------
  {
    VLOG_I("=== Section 5: reset before fire ===");

    vlink::CancellationSource source;
    auto token = source.token();

    std::atomic_int fired{0};
    auto reg = token.register_callback([&fired]() { fired.fetch_add(1); });
    reg.reset();
    source.request_cancel();
    VLOG_I("fired after reset: ", fired.load(), " (expect 0)");
  }

  // ---------------------------------------------------------------
  // 6. Multiple tokens observe the same source (fan-out).
  // ---------------------------------------------------------------
  {
    VLOG_I("=== Section 6: fan-out observers ===");

    vlink::CancellationSource source;
    static const int kObservers = 6;
    std::atomic_int joined{0};
    std::vector<std::thread> workers;
    workers.reserve(kObservers);

    for (int i = 0; i < kObservers; ++i) {
      workers.emplace_back([token = source.token(), i, &joined]() {
        while (!token.is_cancellation_requested()) {
          std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        joined.fetch_add(1);
        VLOG_I("observer ", i, " saw cancellation");
      });
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(40));
    source.request_cancel();
    for (auto& t : workers) t.join();
    VLOG_I("all ", kObservers, " observers joined=", joined.load());
  }

  // ---------------------------------------------------------------
  // 7. Hierarchical cancellation: parent -> child cascade.
  // ---------------------------------------------------------------
  {
    VLOG_I("=== Section 7: parent/child cascade ===");

    vlink::CancellationSource root;
    vlink::CancellationSource middle;
    vlink::CancellationSource leaf;

    auto reg_mid = root.token().register_callback([&middle]() {
      VLOG_I("root fired -> cancelling middle");
      middle.request_cancel();
    });
    auto reg_leaf = middle.token().register_callback([&leaf]() {
      VLOG_I("middle fired -> cancelling leaf");
      leaf.request_cancel();
    });

    auto leaf_token = leaf.token();
    root.request_cancel();
    VLOG_I("leaf.is_cancellation_requested=", leaf_token.is_cancellation_requested());
  }

  // ---------------------------------------------------------------
  // 8. Sibling sources cancellation inside a callback is deadlock-free.
  // ---------------------------------------------------------------
  {
    VLOG_I("=== Section 8: sibling cancel inside callback (no self-deadlock) ===");

    vlink::CancellationSource a;
    vlink::CancellationSource b;

    auto reg = a.token().register_callback([token_a = a.token(), &b]() {
      // The internal mutex of a is already released before this callback runs,
      // so we can freely query a's token and trigger b without deadlock.
      VLOG_I("inside a's callback, a.is_cancellation_requested=", token_a.is_cancellation_requested());
      b.request_cancel();
    });

    a.request_cancel();
    VLOG_I("b.is_cancellation_requested=", b.token().is_cancellation_requested());
  }

  // ---------------------------------------------------------------
  // 9. Callback exceptions are swallowed and other callbacks still fire.
  // ---------------------------------------------------------------
  {
    VLOG_I("=== Section 9: callback exception isolation ===");

    vlink::CancellationSource source;
    auto token = source.token();

    auto reg1 = token.register_callback([]() { throw std::runtime_error("boom"); });
    std::atomic_bool reached{false};
    auto reg2 = token.register_callback([&reached]() { reached.store(true); });

    source.request_cancel();
    VLOG_I("second callback still fired=", reached.load());
  }

  // ---------------------------------------------------------------
  // 10. Default-constructed token never reports cancellation.
  // ---------------------------------------------------------------
  {
    VLOG_I("=== Section 10: empty token semantics ===");

    vlink::CancellationToken empty_token;
    VLOG_I("empty.valid=", empty_token.valid(), " requested=", empty_token.is_cancellation_requested());

    // Registering on an empty token returns an empty registration without
    // observing or invoking the callback.
    std::atomic_int never_fired{0};
    auto reg = empty_token.register_callback([&never_fired]() { never_fired.fetch_add(1); });
    VLOG_I("registration on empty token valid=", reg.valid(), " fired=", never_fired.load());
  }

  // ---------------------------------------------------------------
  // 11. Race: cancel between register_callback() and stash of returned reg.
  // ---------------------------------------------------------------
  {
    VLOG_I("=== Section 11: concurrent producers racing one source ===");

    vlink::CancellationSource source;
    auto token = source.token();

    std::atomic_int registered{0};
    std::atomic_int fired{0};

    // Many threads register concurrently while another thread cancels.
    std::vector<std::thread> registrars;
    registrars.reserve(8);
    for (int i = 0; i < 8; ++i) {
      registrars.emplace_back([&]() {
        auto reg = token.register_callback([&fired]() { fired.fetch_add(1); });
        registered.fetch_add(1);
        // Some threads release immediately, some keep the registration alive.
        if ((registered.load() & 1U) != 0U) {
          reg.reset();
        }
      });
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(2));
    source.request_cancel();

    for (auto& t : registrars) t.join();
    VLOG_I("registered=", registered.load(), " fired=", fired.load(),
           " (fired <= registered; exact value depends on race)");
  }

  // ---------------------------------------------------------------
  // 12. Integration with TaskHandle: parent token cascades into queued tasks.
  // ---------------------------------------------------------------
  {
    VLOG_I("=== Section 12: integration with TaskHandle ===");

    vlink::MessageLoop loop;
    loop.async_run();

    vlink::CancellationSource batch;
    vlink::PostTaskOptions opts;
    opts.cancellation_token = batch.token();

    auto h = loop.post_task_handle(
        [token = opts.cancellation_token]() {
          while (!token.is_cancellation_requested()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
          }
          VLOG_I("task observed cancellation via inherited token");
        },
        opts);

    std::this_thread::sleep_for(std::chrono::milliseconds(30));
    batch.request_cancel();
    h.wait();

    loop.quit();
    loop.wait_for_quit();
  }

  VLOG_I("example_cancellation done");
  return 0;
}
