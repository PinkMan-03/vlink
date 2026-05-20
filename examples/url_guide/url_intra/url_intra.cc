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

// VLink core communication API
#include <vlink/base/logger.h>
#include <vlink/vlink.h>

#include <atomic>
#include <string>
#include <thread>

using namespace std::chrono_literals;  // NOLINT(build/namespaces, google-build-using-namespace)

/// Comprehensive intra:// URL examples
///
/// The intra:// transport delivers messages between publishers and subscribers within
/// the same OS process. No serialization or IPC overhead is incurred -- data is passed
/// directly through an in-process message queue (or direct callback).
///
/// URL format:
///   intra://address[?event=event_name&pipeline=N][#queue|#direct]
///
/// Parameters:
///   - address:  topic name (host + "/" + path), must not be empty
///   - event:    optional secondary filter string for topic segmentation
///   - pipeline: queue pipeline depth (0 = disabled, N = depth of N)
///   - fragment: delivery mode -- "queue" (default, buffered) or "direct" (bypass queue)
///
/// Use cases:
///   - Unit testing without external transport dependencies
///   - Same-process module-to-module communication
///   - High-performance in-process data pipelines
int main() {
  // ======== Example 1: Basic intra:// address ========
  // The simplest form: just a topic address
  // Messages are delivered asynchronously via the internal queue (queue mode is default)
  {
    VLOG_I("=== Example 1: Basic intra:// address ===");

    vlink::Subscriber<std::string> sub("intra://basic/topic");
    sub.listen([](const std::string& msg) { VLOG_I("[basic] Received:", msg); });

    vlink::Publisher<std::string> pub("intra://basic/topic");
    pub.wait_for_subscribers();

    pub.publish("hello from basic topic");
    std::this_thread::sleep_for(50ms);
  }

  // ======== Example 2: With event parameter ========
  // The ?event= parameter provides a secondary filter within the same address.
  // Subscriber only receives messages from publishers with the same address AND event.
  {
    VLOG_I("=== Example 2: With event parameter ===");

    vlink::Subscriber<std::string> sub_scan("intra://sensor/lidar?event=scan");
    sub_scan.listen([](const std::string& msg) { VLOG_I("[scan] Received:", msg); });

    vlink::Subscriber<std::string> sub_status("intra://sensor/lidar?event=status");
    sub_status.listen([](const std::string& msg) { VLOG_I("[status] Received:", msg); });

    vlink::Publisher<std::string> pub_scan("intra://sensor/lidar?event=scan");
    vlink::Publisher<std::string> pub_status("intra://sensor/lidar?event=status");

    pub_scan.wait_for_subscribers();
    pub_status.wait_for_subscribers();

    // Each publisher only reaches the subscriber with the matching event
    pub_scan.publish("point cloud data frame 1");
    pub_status.publish("lidar operational status: OK");

    std::this_thread::sleep_for(50ms);
  }

  // ======== Example 3: Pipeline depth ========
  // The ?pipeline=N parameter sets the depth of the pipeline message queue.
  // When pipeline > 0, the subscriber's callback is dispatched through a
  // dedicated pipeline MessageLoop with the given queue depth.
  // This is useful for load balancing and backpressure control.
  {
    VLOG_I("=== Example 3: Pipeline depth ===");

    // pipeline=4 means the internal pipeline queue can buffer up to 4 pending messages
    vlink::Subscriber<std::string> sub("intra://pipeline/demo?pipeline=4");
    sub.listen([](const std::string& msg) {
      VLOG_I("[pipeline] Processing:", msg);
      std::this_thread::sleep_for(10ms);  // Simulate processing time
    });

    vlink::Publisher<std::string> pub("intra://pipeline/demo?pipeline=4");
    pub.wait_for_subscribers();

    // Publish multiple messages rapidly; the pipeline will buffer them
    for (int i = 0; i < 8; ++i) {
      pub.publish("pipeline message " + std::to_string(i));
    }

    std::this_thread::sleep_for(200ms);
  }

  // ======== Example 4: Queue mode (default) ========
  // In queue mode (#queue or no fragment), messages go through the internal
  // message queue before being delivered to the subscriber callback.
  // This provides asynchronous, non-blocking publish behavior.
  {
    VLOG_I("=== Example 4: Queue mode (default) ===");

    // Both of these are equivalent -- queue mode is the default
    vlink::Subscriber<std::string> sub("intra://mode/queue_demo#queue");
    sub.listen([](const std::string& msg) { VLOG_I("[queue] Received:", msg); });

    vlink::Publisher<std::string> pub("intra://mode/queue_demo#queue");
    pub.wait_for_subscribers();
    pub.publish("message via queue mode");

    std::this_thread::sleep_for(50ms);
  }

  // ======== Example 5: Direct mode ========
  // In direct mode (#direct), the subscriber callback is invoked directly
  // inside the publisher's publish() call -- no queue, no threading overhead.
  // The callback runs on the publisher's thread, which means:
  //   - Lower latency (no queue dispatch)
  //   - publish() blocks until the callback completes
  //   - Not suitable if the callback is slow or does blocking I/O
  {
    VLOG_I("=== Example 5: Direct mode ===");

    vlink::Subscriber<std::string> sub("intra://mode/direct_demo#direct");
    sub.listen([](const std::string& msg) { VLOG_I("[direct] Received:", msg, " (on publisher thread)"); });

    vlink::Publisher<std::string> pub("intra://mode/direct_demo#direct");
    pub.wait_for_subscribers();

    // This publish() call will block until the subscriber callback finishes
    pub.publish("message via direct mode");

    VLOG_I("[direct] publish() returned -- callback already completed");
  }

  // ======== Example 6: Multiple topics in the same process ========
  // Multiple independent intra:// topics can coexist without interference.
  // Each topic has its own subscriber/publisher pool.
  {
    VLOG_I("=== Example 6: Multiple topics ===");

    vlink::Subscriber<int> sub_speed("intra://vehicle/speed");
    sub_speed.listen([](const int& v) { VLOG_I("[speed] km/h:", v); });

    vlink::Subscriber<double> sub_temp("intra://vehicle/temperature");
    sub_temp.listen([](const double& v) { VLOG_I("[temp] celsius:", v); });

    vlink::Subscriber<std::string> sub_status("intra://vehicle/status");
    sub_status.listen([](const std::string& v) { VLOG_I("[status]:", v); });

    vlink::Publisher<int> pub_speed("intra://vehicle/speed");
    vlink::Publisher<double> pub_temp("intra://vehicle/temperature");
    vlink::Publisher<std::string> pub_status("intra://vehicle/status");

    pub_speed.wait_for_subscribers();
    pub_temp.wait_for_subscribers();
    pub_status.wait_for_subscribers();

    pub_speed.publish(120);
    pub_temp.publish(23.5);
    pub_status.publish("engine running");

    std::this_thread::sleep_for(50ms);
  }

  // ======== Example 7: Method model (RPC) over intra:// ========
  // intra:// supports all six VLink node types, including Server/Client
  {
    VLOG_I("=== Example 7: Method model (RPC) ===");

    vlink::Server<std::string, std::string> server("intra://rpc/echo");
    server.listen([](const std::string& req, std::string& resp) { resp = "echo: " + req; });

    vlink::Client<std::string, std::string> client("intra://rpc/echo");
    client.wait_for_connected();

    auto result = client.invoke("hello RPC");

    if (result.has_value()) {
      VLOG_I("[rpc] Response:", result.value());
    }
  }

  // ======== Example 8: Field model (Getter/Setter) over intra:// ========
  {
    VLOG_I("=== Example 8: Field model ===");

    vlink::Setter<int> setter("intra://config/brightness");
    setter.set(75);

    vlink::Getter<int> getter("intra://config/brightness");
    std::this_thread::sleep_for(50ms);

    auto val = getter.get();

    if (val.has_value()) {
      VLOG_I("[field] Brightness:", val.value());
    }
  }

  // ======== Example 9: Combined event + pipeline + direct ========
  // All URL parameters can be combined
  {
    VLOG_I("=== Example 9: Combined parameters ===");

    vlink::Subscriber<std::string> sub("intra://sensor/camera?event=frame&pipeline=2#direct");
    sub.listen([](const std::string& msg) { VLOG_I("[combined] Frame:", msg); });

    vlink::Publisher<std::string> pub("intra://sensor/camera?event=frame&pipeline=2#direct");
    pub.wait_for_subscribers();
    pub.publish("frame_001");
    pub.publish("frame_002");

    std::this_thread::sleep_for(50ms);
  }

  return 0;
}
