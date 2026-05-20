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

// MessageLoop Binding Example
// Demonstrates attach/detach, callback thread control, multi-loop patterns.

#include <vlink/base/logger.h>
#include <vlink/vlink.h>

#include <atomic>
#include <iostream>
#include <thread>

using namespace std::chrono_literals;  // NOLINT(build/namespaces, google-build-using-namespace)

int main() {
  // ======== Section 1: Basic Attach ========
  // attach() binds a node's callback to a specific MessageLoop thread.
  // Without attach, callbacks run on the transport's internal thread.
  {
    std::cout << "\n[1] Basic Attach" << std::endl;

    vlink::MessageLoop loop;
    loop.set_name("my_loop");
    loop.async_run();

    std::cout << "  Loop is_running: " << loop.is_running() << std::endl;

    bool callback_on_loop_thread = false;

    vlink::Subscriber<std::string> sub("dds://loop_binding/basic");
    sub.attach(&loop);  // Callbacks will run on loop's thread
    sub.listen([&](const std::string& msg) {
      // is_in_same_thread() returns true if called from the loop's thread
      callback_on_loop_thread = loop.is_in_same_thread();
      VLOG_I("[Sub] Received on loop thread:", msg);
    });

    vlink::Publisher<std::string> pub("dds://loop_binding/basic");
    pub.wait_for_subscribers();

    pub.publish("test_message");
    loop.wait_for_idle(1000);

    std::cout << "  Callback ran on loop thread: " << std::boolalpha << callback_on_loop_thread << std::endl;

    loop.quit();
    loop.wait_for_quit();
  }

  // ======== Section 2: Multiple Nodes on One Loop ========
  // Attaching multiple subscribers to the same loop serialises their callbacks,
  // avoiding the need for user-side mutex locking.
  {
    std::cout << "\n[2] Multiple Nodes on One Loop (serialised callbacks)" << std::endl;

    vlink::MessageLoop loop;
    loop.set_name("shared_loop");
    loop.async_run();

    std::atomic<int> total_received{0};

    vlink::Subscriber<std::string> sub1("dds://loop_binding/multi");
    sub1.attach(&loop);
    sub1.listen([&total_received](const std::string& msg) {
      total_received++;
      VLOG_I("[Sub1]", msg);
    });

    vlink::Subscriber<std::string> sub2("dds://loop_binding/multi");
    sub2.attach(&loop);
    sub2.listen([&total_received](const std::string& msg) {
      total_received++;
      VLOG_I("[Sub2]", msg);
    });

    vlink::Publisher<std::string> pub("dds://loop_binding/multi");
    pub.wait_for_subscribers();

    pub.publish("shared_loop_message");
    loop.wait_for_idle(1000);

    std::cout << "  Total received (2 subs on same loop): " << total_received.load() << std::endl;
    std::cout << "  Callbacks are serialised -- no mutex needed" << std::endl;

    loop.quit();
    loop.wait_for_quit();
  }

  // ======== Section 3: Separate Loops for Isolation ========
  {
    std::cout << "\n[3] Separate Loops for Isolation" << std::endl;

    vlink::MessageLoop loop_a;
    loop_a.set_name("loop_a");
    loop_a.async_run();

    vlink::MessageLoop loop_b;
    loop_b.set_name("loop_b");
    loop_b.async_run();

    std::thread::id tid_a;
    std::thread::id tid_b;

    vlink::Subscriber<std::string> sub_a("dds://loop_binding/isolated");
    sub_a.attach(&loop_a);
    sub_a.listen([&tid_a](const std::string& msg) {
      (void)msg;
      tid_a = std::this_thread::get_id();
    });

    vlink::Subscriber<std::string> sub_b("dds://loop_binding/isolated");
    sub_b.attach(&loop_b);
    sub_b.listen([&tid_b](const std::string& msg) {
      (void)msg;
      tid_b = std::this_thread::get_id();
    });

    vlink::Publisher<std::string> pub("dds://loop_binding/isolated");
    pub.wait_for_subscribers();

    pub.publish("isolation_test");

    loop_a.wait_for_idle(1000);
    loop_b.wait_for_idle(1000);

    std::cout << "  Sub A thread: " << tid_a << std::endl;
    std::cout << "  Sub B thread: " << tid_b << std::endl;
    std::cout << "  Different threads: " << std::boolalpha << (tid_a != tid_b) << std::endl;

    loop_a.quit();
    loop_b.quit();
    loop_a.wait_for_quit();
    loop_b.wait_for_quit();
  }

  // ======== Section 4: Attaching Publisher to a Loop ========
  {
    std::cout << "\n[4] Attaching Publisher to a Loop" << std::endl;
    std::cout << "  Publishers can also be attached to a loop." << std::endl;
    std::cout << "  This controls the thread for connect/disconnect callbacks." << std::endl;

    vlink::MessageLoop loop;
    loop.set_name("pub_loop");
    loop.async_run();

    vlink::Publisher<std::string> pub("dds://loop_binding/pub_attach");
    pub.attach(&loop);

    std::cout << "  Publisher attached to loop" << std::endl;

    loop.quit();
    loop.wait_for_quit();
  }

  // ======== Section 5: Server/Client with Loop ========
  {
    std::cout << "\n[5] Server/Client with Loop Binding" << std::endl;

    vlink::MessageLoop server_loop;
    server_loop.set_name("server_loop");
    server_loop.async_run();

    vlink::Server<int, int> server("dds://loop_binding/rpc");
    server.attach(&server_loop);  // Server callback runs on server_loop thread
    server.listen([](const int& req, int& resp) { resp = req * 2; });

    vlink::Client<int, int> client("dds://loop_binding/rpc");
    std::this_thread::sleep_for(50ms);

    auto result = client.invoke(21);

    if (result.has_value()) {
      std::cout << "  21 * 2 = " << result.value() << std::endl;
    }

    server_loop.quit();
    server_loop.wait_for_quit();
  }

  // ======== Section 6: Summary ========
  {
    std::cout << "\n[6] Summary" << std::endl;
    std::cout << "  +----------------------------+------------------------------------------+" << std::endl;
    std::cout << "  | Pattern                    | Description                              |" << std::endl;
    std::cout << "  +----------------------------+------------------------------------------+" << std::endl;
    std::cout << "  | No attach                  | Callback on transport's internal thread   |" << std::endl;
    std::cout << "  | attach(&loop)              | Callback on loop's dedicated thread       |" << std::endl;
    std::cout << "  | Multiple nodes, one loop   | Serialised (no locks needed)              |" << std::endl;
    std::cout << "  | Multiple loops             | Parallel (need sync for shared state)     |" << std::endl;
    std::cout << "  +----------------------------+------------------------------------------+" << std::endl;
  }

  VLOG_I("MessageLoop binding example complete.");
  return 0;
}
