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

/**
 * @file qos_history_depth.cc
 * @brief QoS History & Depth -- Controls how many samples are retained for late joiners.
 *
 * History QoS policy:
 *   - KeepLast (kind=0): Keep only the `depth` most recent samples per instance.
 *     Default depth=1 means only the latest value is available.
 *   - KeepAll (kind=1): Keep all samples (subject to ResourceLimits).
 *     May consume significant memory for high-rate topics.
 *
 * depth parameter can also be set directly in the URL: "dds://topic?depth=N"
 * This overrides the QoS profile's depth value for that specific endpoint.
 *
 * DDS URL examples:
 *   "dds://vehicle/speed?depth=10"           -- depth=10 override
 *   "dds://vehicle/speed?qos=sensor&depth=5" -- sensor QoS with depth=5 override
 *
 * Requires: DDS module (dds://) for actual transport behavior.
 */

#include <vlink/base/logger.h>
#include <vlink/extension/qos.h>
#include <vlink/vlink.h>

#ifdef VLINK_SUPPORT_DDS
#include <vlink/modules/dds_conf.h>
#endif

#include <atomic>
#include <cstring>
#include <iostream>
#include <thread>

using namespace std::chrono_literals;  // NOLINT(build/namespaces, google-build-using-namespace)

int main() {
  // ================================================================
  // Section 1: KeepLast with different depths
  // ================================================================
  VLOG_I("--- Section 1: KeepLast History ---");
  {
    // depth=1: Only the latest value is kept. Good for "current state" topics
    // like vehicle speed, temperature, battery level.
    vlink::Qos qos_depth1;
    std::strncpy(qos_depth1.name, "depth1", sizeof(qos_depth1.name) - 1);
    qos_depth1.valid = true;
    qos_depth1.history.kind = vlink::Qos::History::kKeepLast;
    qos_depth1.history.depth = 1;
    VLOG_I("KeepLast depth=1: Only latest value. Good for 'current state' topics.");
    VLOG_I("  Example: vehicle speed, GPS position, battery level");

    // depth=10: Keep the 10 most recent samples. Late joiners get up to 10
    // historical values. Good for event streams where some history matters.
    vlink::Qos qos_depth10;
    std::strncpy(qos_depth10.name, "depth10", sizeof(qos_depth10.name) - 1);
    qos_depth10.valid = true;
    qos_depth10.history.kind = vlink::Qos::History::kKeepLast;
    qos_depth10.history.depth = 10;
    VLOG_I("KeepLast depth=10: Last 10 samples. Good for event streams with history.");
    VLOG_I("  Example: sensor readings, log events, status updates");

    // depth=100: Large history buffer for data analysis topics.
    vlink::Qos qos_depth100;
    std::strncpy(qos_depth100.name, "depth100", sizeof(qos_depth100.name) - 1);
    qos_depth100.valid = true;
    qos_depth100.history.kind = vlink::Qos::History::kKeepLast;
    qos_depth100.history.depth = 100;
    VLOG_I("KeepLast depth=100: Last 100 samples. Good for analysis pipelines.");
    VLOG_I("  Example: camera frames, point clouds, trajectory history");

#ifdef VLINK_SUPPORT_DDS
    vlink::DdsConf::register_qos("depth1", qos_depth1);
    vlink::DdsConf::register_qos("depth10", qos_depth10);
    vlink::DdsConf::register_qos("depth100", qos_depth100);
    VLOG_I("Registered depth profiles with DDS.");
#endif
  }

  // ================================================================
  // Section 2: KeepAll mode
  // ================================================================
  VLOG_I("\n--- Section 2: KeepAll History ---");
  {
    vlink::Qos qos_keepall;
    std::strncpy(qos_keepall.name, "keepall", sizeof(qos_keepall.name) - 1);
    qos_keepall.valid = true;
    qos_keepall.history.kind = vlink::Qos::History::kKeepAll;
    // With KeepAll, ResourceLimits controls the maximum queue size.
    qos_keepall.resource_limits.max_samples = 10000;
    qos_keepall.resource_limits.max_samples_per_instance = 5000;

    VLOG_I("KeepAll: All samples buffered until consumed.");
    VLOG_I("  Combined with Reliable QoS = guaranteed zero-loss delivery.");
    VLOG_I("  WARNING: High-rate topics can exhaust memory!");
    VLOG_I("  Always set ResourceLimits.max_samples to prevent OOM.");
    VLOG_I("  max_samples=", qos_keepall.resource_limits.max_samples);

#ifdef VLINK_SUPPORT_DDS
    vlink::DdsConf::register_qos("keepall", qos_keepall);
#endif
  }

  // ================================================================
  // Section 3: URL depth parameter
  // ================================================================
  VLOG_I("\n--- Section 3: URL depth parameter ---");
  {
    // The depth can be set directly in the URL without a QoS profile.
    // This is the simplest way to control history depth.
    VLOG_I("DDS URL depth parameter examples:");
    VLOG_I("  dds://vehicle/speed?depth=1     -- default, latest value only");
    VLOG_I("  dds://vehicle/speed?depth=10    -- keep last 10");
    VLOG_I("  dds://vehicle/speed?depth=50    -- keep last 50");
    VLOG_I("");
    VLOG_I("Combine with QoS profile (depth overrides profile value):");
    VLOG_I("  dds://sensor/lidar?qos=sensor&depth=20");
    VLOG_I("");
    VLOG_I("Combine with domain:");
    VLOG_I("  dds://sensor/lidar?domain=5&depth=20");

#ifdef VLINK_SUPPORT_DDS
    // Demo: publisher with depth=5 and subscriber with depth=50.
    // Each endpoint can have different depth values.
    vlink::Publisher<std::string> pub("dds://qos/depth_demo?depth=5");
    vlink::Subscriber<std::string> sub("dds://qos/depth_demo?depth=50");

    std::atomic<int> count{0};
    sub.listen([&count](const std::string& msg) {
      count++;
      if (count <= 3) VLOG_I("Received: ", msg);
    });

    pub.wait_for_subscribers(2s);

    // Publish 20 messages. Publisher keeps last 5, subscriber keeps last 50.
    for (int i = 1; i <= 20; ++i) {
      pub.publish("depth-msg-" + std::to_string(i));
    }

    std::this_thread::sleep_for(300ms);
    VLOG_I("Published 20, received ", count.load());
#else
    VLOG_I("DDS not available. Build with -DSKIP_DDS=OFF to test.");
#endif
  }

  // ================================================================
  // Section 4: Depth selection guidelines
  // ================================================================
  VLOG_I("\n--- Section 4: Depth selection guide ---");
  VLOG_I("Recommended depth values by use case:");
  VLOG_I("  depth=1:    Current state (speed, position, status)");
  VLOG_I("  depth=5-10: Control commands (brake, steer, throttle)");
  VLOG_I("  depth=20-50: Sensor data (lidar, radar, ultrasonic)");
  VLOG_I("  depth=100+:  Recording/analysis (camera, trajectory)");
  VLOG_I("  KeepAll:     Critical audit logs, transaction records");
  VLOG_I("");
  VLOG_I("WARNING: Large depth + large message size = high memory usage!");
  VLOG_I("  Example: depth=100 * 1MB message = 100MB per topic");

  // ================================================================
  // Section 5: ResourceLimits interaction with KeepAll
  // ================================================================
  VLOG_I("\n--- Section 5: ResourceLimits interaction ---");
  {
    // When using KeepAll, ResourceLimits prevents unbounded memory growth.
    // If max_samples is reached, publish() behaviour depends on the transport:
    //   - Some transports drop the oldest sample
    //   - Some transports block until a sample is consumed
    //   - Some transports return failure from publish()
    vlink::Qos bounded_keepall;
    std::strncpy(bounded_keepall.name, "bounded_keepall", sizeof(bounded_keepall.name) - 1);
    bounded_keepall.valid = true;
    bounded_keepall.history.kind = vlink::Qos::History::kKeepAll;
    bounded_keepall.resource_limits.max_samples = 500;
    bounded_keepall.resource_limits.max_instances = 1;
    bounded_keepall.resource_limits.max_samples_per_instance = 500;

    VLOG_I("KeepAll with ResourceLimits: max_samples=500");
    VLOG_I("  When the buffer is full, behaviour is transport-specific.");
    VLOG_I("  Always set ResourceLimits when using KeepAll to prevent OOM.");

#ifdef VLINK_SUPPORT_DDS
    vlink::DdsConf::register_qos("bounded_keepall", bounded_keepall);
#endif
  }

  // ================================================================
  // Section 6: Memory estimation formula
  // ================================================================
  VLOG_I("\n--- Section 6: Memory estimation ---");
  {
    // Memory usage per topic = depth * avg_message_size
    // For N topics: total = N * depth * avg_message_size
    //
    // Example: 10 topics, depth=50, avg 100KB message
    //   = 10 * 50 * 100KB = 50MB
    //
    // Example: 1 topic, depth=1000, avg 1MB message (camera)
    //   = 1 * 1000 * 1MB = 1GB (!)
    VLOG_I("Memory estimation: topics * depth * avg_msg_size");
    VLOG_I("  Example: 10 topics * 50 depth * 100KB = 50MB");
    VLOG_I("  Example: 1 topic * 1000 depth * 1MB = 1GB (!)");
    VLOG_I("  Keep depth as small as your use case allows.");
  }

  VLOG_I("\n=== QoS History & Depth Example Complete ===");
  return 0;
}
