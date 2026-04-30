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

// Node Lifecycle Example
// Demonstrates kWithoutInit, manual init/deinit, interrupt, has_inited.

#include <vlink/base/logger.h>
#include <vlink/vlink.h>

#include <iostream>
#include <thread>

using namespace std::chrono_literals;  // NOLINT(build/namespaces, google-build-using-namespace)

int main() {
  // ======== Section 1: Default Construction (auto init) ========
  {
    std::cout << "\n[1] Default Construction (auto init)" << std::endl;

    vlink::Publisher<std::string> pub("dds://lifecycle/auto");
    std::cout << "  has_inited: " << std::boolalpha << pub.has_inited() << std::endl;
    std::cout << "  (Default constructor initialises the node automatically)" << std::endl;
  }

  // ======== Section 2: Deferred Initialisation with kWithoutInit ========
  {
    std::cout << "\n[2] Deferred Initialisation (kWithoutInit)" << std::endl;

    // Create the node but do NOT initialise the transport yet
    vlink::Publisher<std::string> pub("dds://lifecycle/deferred", vlink::InitType::kWithoutInit);
    std::cout << "  After construction: has_inited = " << std::boolalpha << pub.has_inited() << std::endl;

    // Set properties before initialisation
    pub.set_property("qos.reliability.kind", "1");
    pub.set_property("qos.history.depth", "10");
    std::cout << "  Properties set before init()" << std::endl;

    // Now manually initialise the node
    pub.init();
    std::cout << "  After init():      has_inited = " << std::boolalpha << pub.has_inited() << std::endl;

    // Publish a message to verify the node is active
    bool ok = pub.publish("Hello from deferred node");
    std::cout << "  publish() returned: " << std::boolalpha << ok << std::endl;
  }

  // ======== Section 3: Manual deinit ========
  {
    std::cout << "\n[3] Manual deinit()" << std::endl;

    vlink::Publisher<std::string> pub("dds://lifecycle/deinit_demo");
    std::cout << "  Before deinit: has_inited = " << std::boolalpha << pub.has_inited() << std::endl;

    // Deinitialise the node -- releases transport resources
    pub.deinit();
    std::cout << "  After deinit:  has_inited = " << std::boolalpha << pub.has_inited() << std::endl;

    // publish() on a deinited node returns false
    bool ok = pub.publish("This should fail");
    std::cout << "  publish() after deinit: " << std::boolalpha << ok << std::endl;

    // Re-initialise the node
    pub.init();
    std::cout << "  After re-init: has_inited = " << std::boolalpha << pub.has_inited() << std::endl;
  }

  // ======== Section 4: Interrupt ========
  {
    std::cout << "\n[4] Interrupt" << std::endl;

    vlink::MessageLoop loop;
    loop.set_name("lifecycle_loop");
    loop.async_run();

    vlink::Subscriber<std::string> sub("dds://lifecycle/interrupt_demo");
    sub.attach(&loop);

    int received = 0;
    sub.listen([&received](const std::string& msg) {
      received++;
      VLOG_I("[Sub] Received:", msg);
    });

    vlink::Publisher<std::string> pub("dds://lifecycle/interrupt_demo");
    pub.wait_for_subscribers();

    pub.publish("Before interrupt");
    std::this_thread::sleep_for(50ms);
    std::cout << "  Received before interrupt: " << received << std::endl;

    // Interrupt the subscriber node -- pauses message delivery
    sub.interrupt();
    std::cout << "  Subscriber interrupted" << std::endl;

    pub.publish("During interrupt (should not be received)");
    std::this_thread::sleep_for(50ms);
    std::cout << "  Received after interrupt:  " << received << std::endl;

    loop.quit();
    loop.wait_for_quit();
  }

  // ======== Section 5: Lifecycle Pattern for Configuration ========
  // Demonstrates the recommended lifecycle pattern using real API calls.
  {
    std::cout << "\n[5] Recommended Lifecycle Pattern" << std::endl;

    // Step 1: Create with kWithoutInit to defer transport creation
    vlink::Subscriber<std::string> sub("dds://lifecycle/pattern", vlink::InitType::kWithoutInit);
    VLOG_I("[Pattern] Step 1: Created subscriber with kWithoutInit, has_inited=", sub.has_inited());

    // Step 2: Configure properties before transport is created
    sub.set_property("qos.reliability.kind", "1");
    sub.set_property("qos.history.depth", "50");
    VLOG_I("[Pattern] Step 2: Properties configured before init()");

    // Step 3: Initialise -- transport and discovery begin here
    sub.init();
    VLOG_I("[Pattern] Step 3: init() called, has_inited=", sub.has_inited());

    // Step 4: Register callback
    sub.listen([](const std::string& msg) { VLOG_I("[Pattern] Received:", msg); });
    VLOG_I("[Pattern] Step 4: listen() callback registered");

    // Step 5: At shutdown, deinit releases transport resources
    sub.deinit();
    VLOG_I("[Pattern] Step 5: deinit() called, has_inited=", sub.has_inited());
  }

  // ======== Section 6: All 6 Node Types ========
  {
    std::cout << "[6] kWithoutInit works on all 6 primitives" << std::endl;

    vlink::Publisher<std::string> pub("dds://lifecycle/all", vlink::InitType::kWithoutInit);
    vlink::Subscriber<std::string> sub("dds://lifecycle/all", vlink::InitType::kWithoutInit);
    vlink::Setter<int> setter("dds://lifecycle/all_field", vlink::InitType::kWithoutInit);
    vlink::Getter<int> getter("dds://lifecycle/all_field", vlink::InitType::kWithoutInit);
    vlink::Server<int, int> server("dds://lifecycle/all_rpc", vlink::InitType::kWithoutInit);
    vlink::Client<int, int> client("dds://lifecycle/all_rpc", vlink::InitType::kWithoutInit);

    std::cout << "  pub:    " << std::boolalpha << pub.has_inited() << std::endl;
    std::cout << "  sub:    " << std::boolalpha << sub.has_inited() << std::endl;
    std::cout << "  setter: " << std::boolalpha << setter.has_inited() << std::endl;
    std::cout << "  getter: " << std::boolalpha << getter.has_inited() << std::endl;
    std::cout << "  server: " << std::boolalpha << server.has_inited() << std::endl;
    std::cout << "  client: " << std::boolalpha << client.has_inited() << std::endl;
  }

  VLOG_I("Lifecycle example complete.");
  return 0;
}
