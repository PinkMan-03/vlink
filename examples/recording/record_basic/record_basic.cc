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
#include <vlink/extension/bag_writer.h>
#include <vlink/vlink.h>

#include <thread>

using namespace std::chrono_literals;  // NOLINT(build/namespaces, google-build-using-namespace)

/// Basic recording example
///
/// Demonstrates two recording mechanisms:
///   1. Per-node recording via set_record_path() -- each node writes to its own bag file
///   2. Global recording via VLINK_BAG_PATH environment variable -- all nodes share one bag file
///
/// The VLINK_BAG_TAG environment variable can optionally add a tag string to recorded bag files.
///
/// Usage:
///   # Per-node recording (default):
///   ./example_record_basic
///
///   # Global recording via environment variable:
///   VLINK_BAG_PATH=/tmp/global_record.vdb ./example_record_basic
///
///   # Global recording with a tag:
///   VLINK_BAG_PATH=/tmp/global_record.vdb VLINK_BAG_TAG=test_session ./example_record_basic
int main() {
  // ======== Per-Node Recording ========
  // Create a Publisher and enable per-node recording.
  // All messages published by this node will be recorded to the specified bag file.
  vlink::Publisher<vlink::Bytes> pub("dds://record_basic/event");
  pub.set_record_path("/tmp/record_basic_pub.vdb");

  // Create a Subscriber and enable per-node recording on the subscriber side as well.
  // This records all received messages independently of the publisher's recording.
  vlink::Subscriber<vlink::Bytes> sub("dds://record_basic/event");
  sub.set_record_path("/tmp/record_basic_sub.vdb");

  // Register the subscriber callback
  sub.listen([](const vlink::Bytes& msg) { VLOG_I("Subscriber received:", msg.size(), "bytes"); });

  // Wait for the subscriber to be ready
  pub.wait_for_subscribers();

  // Publish several messages -- each will be recorded by both pub and sub nodes
  for (int i = 0; i < 10; ++i) {
    std::string payload = "message_" + std::to_string(i);
    pub.publish(vlink::Bytes::from_string(payload));
    VLOG_I("Published:", payload);
    std::this_thread::sleep_for(100ms);
  }

  // ======== Global Recording via VLINK_BAG_PATH ========
  // If the VLINK_BAG_PATH environment variable is set, VLink automatically creates
  // a global BagWriter. All nodes in the process will record to that single bag file,
  // even without calling set_record_path() on each node.
  //
  // Check whether the global writer is active:
  auto* global_writer = vlink::BagWriter::global_get();

  if (global_writer) {
    VLOG_I("Global BagWriter is active. All traffic will be recorded.");
  } else {
    VLOG_I("No global BagWriter. Set VLINK_BAG_PATH to enable global recording.");
  }

  // ======== Method Model with Recording ========
  // Recording also works for Server/Client RPC communications.
  vlink::Server<vlink::Bytes, vlink::Bytes> server("dds://record_basic/method");
  server.set_record_path("/tmp/record_basic_rpc.vdb");

  server.listen([](const vlink::Bytes& req, vlink::Bytes& resp) {
    VLOG_I("Server received request:", req.size(), "bytes");
    resp = vlink::Bytes::from_string("response_ok");
  });

  vlink::Client<vlink::Bytes, vlink::Bytes> client("dds://record_basic/method");
  client.set_record_path("/tmp/record_basic_rpc.vdb");

  auto resp = client.invoke(vlink::Bytes::from_string("request_data"));

  if (resp.has_value()) {
    VLOG_I("Client received response:", resp.value().to_string());
  }

  // ======== Field Model with Recording ========
  // Recording works for Setter/Getter field communications as well.
  vlink::Setter<vlink::Bytes> setter("dds://record_basic/field");
  setter.set_record_path("/tmp/record_basic_field.vdb");

  vlink::Getter<vlink::Bytes> getter("dds://record_basic/field");
  getter.set_record_path("/tmp/record_basic_field.vdb");

  setter.set(vlink::Bytes::from_string("field_value_1"));
  std::this_thread::sleep_for(100ms);

  auto val = getter.get();

  if (val.has_value()) {
    VLOG_I("Getter value:", val.value().to_string());
  }

  // Allow time for async recording to complete
  std::this_thread::sleep_for(500ms);

  VLOG_I("Recording complete. Check /tmp/record_basic_*.vdb files.");

  return 0;
}
