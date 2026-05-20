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

// Status Monitoring Example
//// Demonstrates register_status_handler, get_cpu_usage, and status inspection.

#include <vlink/base/cpu_profiler.h>
#include <vlink/base/logger.h>
#include <vlink/vlink.h>

#include <atomic>
#include <iostream>
#include <thread>

using namespace std::chrono_literals;  // NOLINT(build/namespaces, google-build-using-namespace)

int main() {
  // ======== Section 1: get_cpu_usage ========
  {
    std::cout << "\n[1] get_cpu_usage" << std::endl;

    vlink::Publisher<std::string> pub("dds://status/cpu");

    // get_cpu_usage() returns the CPU utilization percentage for this node's
    // processing thread. Returns -1.0 if the profiler is not enabled.
    // Enable profiler: export VLINK_PROFILER_ENABLE=1
    // NOTE: CpuProfiler may not be available in all builds.
    double cpu_usage = pub.get_cpu_usage();
    std::cout << "  get_cpu_usage() = " << cpu_usage << std::endl;

    if (cpu_usage < 0) {
      std::cout << "  (Profiler disabled. Enable with: export VLINK_PROFILER_ENABLE=1)" << std::endl;
    }
  }

  // ======== Section 2: register_status_handler ========
  {
    std::cout << "\n[2] register_status_handler" << std::endl;

    vlink::MessageLoop loop;
    loop.set_name("status_loop");
    loop.async_run();

    std::atomic<int> status_events{0};

    vlink::Subscriber<std::string> sub("dds://status/handler");
    sub.attach(&loop);

    // Register a handler that is called when the node status changes.
    // Status events include: subscriber connected, subscriber disconnected, etc.
    sub.register_status_handler([&status_events](vlink::Status::BasePtr status) {
      status_events++;
      VLOG_I("[Status] Event received, type:", static_cast<int>(status->get_type()));
    });

    sub.listen([](const std::string& msg) { VLOG_I("[Sub]", msg); });

    vlink::Publisher<std::string> pub("dds://status/handler");
    pub.wait_for_subscribers();

    // Publish some messages to trigger activity
    for (int i = 0; i < 5; ++i) {
      pub.publish("status_test_" + std::to_string(i));
    }

    std::this_thread::sleep_for(200ms);
    std::cout << "  Status events received: " << status_events.load() << std::endl;

    loop.quit();
    loop.wait_for_quit();
  }

  // ======== Section 3: Publisher Status Monitoring ========
  {
    std::cout << "\n[3] Publisher Status Monitoring" << std::endl;

    vlink::Publisher<std::string> pub("dds://status/pub_monitor");

    pub.register_status_handler(
        [](vlink::Status::BasePtr status) { VLOG_I("[PubStatus] type:", static_cast<int>(status->get_type())); });

    // Create a subscriber to trigger a connection status event
    vlink::Subscriber<std::string> sub("dds://status/pub_monitor");
    sub.listen([](const std::string& msg) { (void)msg; });

    pub.wait_for_subscribers();
    std::cout << "  Publisher detected subscriber connection" << std::endl;

    std::this_thread::sleep_for(100ms);
  }

  // ======== Section 4: CPU Usage Over Time ========
  {
    std::cout << "\n[4] CPU Usage Over Time" << std::endl;

    vlink::Publisher<std::string> pub("dds://status/cpu_monitor");

    // NOTE: get_cpu_usage() requires CpuProfiler to be compiled in.
    // It may return -1.0 if the profiler is not available in this build.
    for (int i = 0; i < 3; ++i) {
      std::cout << "  Sample " << (i + 1) << ": check with pub.get_cpu_usage()" << std::endl;
      std::this_thread::sleep_for(100ms);
    }
  }

  // ======== Section 5: Status Summary ========
  {
    std::cout << "\n[5] Status API Summary" << std::endl;
    std::cout << "  +-------------------------------+-------------------------------------+" << std::endl;
    std::cout << "  | Method                        | Description                         |" << std::endl;
    std::cout << "  +-------------------------------+-------------------------------------+" << std::endl;
    std::cout << "  | register_status_handler(cb)   | Callback on status changes          |" << std::endl;
    std::cout << "  | get_cpu_usage()               | CPU usage [0..100] or -1 if off     |" << std::endl;
    std::cout << "  +-------------------------------+-------------------------------------+" << std::endl;
    std::cout << std::endl;
    std::cout << "  Status events include:" << std::endl;
    std::cout << "    - Subscriber connected / disconnected" << std::endl;
    std::cout << "    - Server connected / disconnected" << std::endl;
    std::cout << "    - Discovery updates" << std::endl;
  }

  VLOG_I("Status monitoring example complete.");
  return 0;
}
