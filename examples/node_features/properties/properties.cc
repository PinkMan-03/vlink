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

// Node Properties Example
// Demonstrates set_property, get_property, set_ser_type, get_schema_type, set_discovery_enabled.

#include <vlink/base/logger.h>
#include <vlink/vlink.h>

#include <iostream>
#include <thread>

using namespace std::chrono_literals;  // NOLINT(build/namespaces, google-build-using-namespace)

int main() {
  // ======== Section 1: set_property / get_property ========
  {
    std::cout << "\n[1] set_property / get_property" << std::endl;

    // Use kWithoutInit so properties are set before transport creation
    vlink::Publisher<std::string> pub("dds://properties/demo", vlink::InitType::kWithoutInit);

    // Set QoS properties as key-value strings
    pub.set_property("qos.reliability.kind", "1");  // 1 = Reliable
    pub.set_property("qos.history.kind", "0");      // 0 = KeepLast
    pub.set_property("qos.history.depth", "50");
    pub.set_property("qos.durability.kind", "1");    // 1 = TransientLocal
    pub.set_property("qos.publish_mode.kind", "0");  // 0 = Sync
    pub.set_property("qos.reliability.block_time", "200");
    pub.set_property("qos.reliability.heartbeat_time", "5000");

    // Read back properties
    std::cout << "  qos.reliability.kind:       " << pub.get_property("qos.reliability.kind") << std::endl;
    std::cout << "  qos.history.depth:          " << pub.get_property("qos.history.depth") << std::endl;
    std::cout << "  qos.durability.kind:        " << pub.get_property("qos.durability.kind") << std::endl;
    std::cout << "  qos.publish_mode.kind:      " << pub.get_property("qos.publish_mode.kind") << std::endl;
    std::cout << "  qos.reliability.block_time: " << pub.get_property("qos.reliability.block_time") << std::endl;

    pub.init();
    std::cout << "  Node initialised with custom properties." << std::endl;
  }

  // ======== Section 2: set_ser_type ========
  {
    std::cout << "\n[2] set_ser_type" << std::endl;

    vlink::Publisher<vlink::Bytes> pub("dds://properties/ser_type", vlink::InitType::kWithoutInit);

    // Set both the concrete ser_type string and the coarse schema family.
    // This metadata is propagated to discovery, proxy, and bag recording.
    pub.set_ser_type("demo.proto.Message", vlink::SchemaType::kProtobuf);
    std::cout << "  set_ser_type(\"demo.proto.Message\", kProtobuf) called" << std::endl;

    // Omitting schema_type keeps the current protobuf family instead of overwriting it with kUnknown.
    pub.set_ser_type("demo.proto.MessageV2");
    std::cout << "  set_ser_type(\"demo.proto.MessageV2\") called" << std::endl;
    std::cout << "  get_ser_type: " << pub.get_ser_type() << std::endl;
    std::cout << "  get_schema_type: " << static_cast<int>(pub.get_schema_type()) << std::endl;

    pub.init();

    // Wire metadata is descriptive only -- VLink does not validate payload bytes here.
  }

  // ======== Section 3: set_discovery_enabled ========
  {
    std::cout << "\n[3] set_discovery_enabled" << std::endl;

    vlink::Publisher<std::string> pub("dds://properties/discovery", vlink::InitType::kWithoutInit);

    // Disable discovery for this node.
    // The node will NOT be visible to ProxyServer or other discovery viewers.
    // Useful for internal/hidden topics.
    pub.set_discovery_enabled(false);
    std::cout << "  set_discovery_enabled(false) called" << std::endl;

    pub.init();
    std::cout << "  Node initialised but hidden from discovery." << std::endl;
    std::cout << "  ProxyAPI and other tools will not see this topic." << std::endl;
  }

  // ======== Section 4: Property-Based QoS on Subscriber ========
  {
    std::cout << "\n[4] Property-Based QoS on Subscriber" << std::endl;

    vlink::Subscriber<std::string> sub("dds://properties/qos_sub", vlink::InitType::kWithoutInit);
    sub.set_property("qos.reliability.kind", "1");
    sub.set_property("qos.history.depth", "100");
    sub.set_property("qos.durability.kind", "1");
    sub.init();

    sub.listen([](const std::string& msg) { VLOG_I("Received:", msg); });

    vlink::Publisher<std::string> pub("dds://properties/qos_sub");
    pub.wait_for_subscribers();

    pub.publish("property-configured subscriber");
    std::this_thread::sleep_for(100ms);
    std::cout << "  Subscriber with property-based QoS is working." << std::endl;
  }

  // ======== Section 5: Available Property Keys ========
  {
    std::cout << "\n[5] Available Property Keys" << std::endl;
    std::cout << "  QoS properties (set before init):" << std::endl;
    std::cout << "    qos.reliability.kind           0=BestEffort, 1=Reliable" << std::endl;
    std::cout << "    qos.reliability.block_time     ms" << std::endl;
    std::cout << "    qos.reliability.heartbeat_time ms" << std::endl;
    std::cout << "    qos.history.kind               0=KeepLast, 1=KeepAll" << std::endl;
    std::cout << "    qos.history.depth              int" << std::endl;
    std::cout << "    qos.durability.kind            0=Volatile, 1=TransientLocal, 2=Transient, 3=Persistent"
              << std::endl;
    std::cout << "    qos.publish_mode.kind          0=Sync, 1=ASync" << std::endl;
    std::cout << "    qos.liveliness.kind            0=Auto, 1=ManualParticipant, 2=ManualTopic" << std::endl;
    std::cout << "    qos.liveliness.duration        ms" << std::endl;
    std::cout << "    qos.deadline.period            ms" << std::endl;
    std::cout << "    qos.lifespan.duration          ms" << std::endl;
    std::cout << "    qos.latency_budget.duration    ms" << std::endl;
    std::cout << "    qos.resource_limits.max_samples          int" << std::endl;
    std::cout << "    qos.resource_limits.max_instances        int" << std::endl;
    std::cout << "    qos.resource_limits.max_samples_per_instance int" << std::endl;
    std::cout << "    qos.additions.priority         1=RealTime, 2=High, 4=Normal, 6=Low, 7=Background" << std::endl;
    std::cout << "    qos.additions.is_express       true/false" << std::endl;
  }

  VLOG_I("Properties example complete.");
  return 0;
}
