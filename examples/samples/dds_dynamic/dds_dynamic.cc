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

// DynamicData: runtime dynamic type container
#include <vlink/extension/dynamic_data.h>
// VLink core communication API
#include <vlink/base/logger.h>
#include <vlink/vlink.h>

#include <string>
#include <thread>

// Protobuf generated message types
#if defined(__ANDROID__) && __has_include("dds_dynamic/dds_dynamic.pb.h")
#include "dds_dynamic/dds_dynamic.pb.h"
#else
#include "dds_dynamic.pb.h"
#endif

using namespace vlink;                 // NOLINT(build/namespaces, google-build-using-namespace)
using namespace std::chrono_literals;  // NOLINT(build/namespaces, google-build-using-namespace)

/// DynamicData dynamic type example
///
/// DynamicData is VLink's runtime type-erased container that allows transmitting different
/// message types on the same channel. Each message carries a type tag (up to 19 characters),
/// and the receiver selects the deserialization method based on the tag.
///
/// This example demonstrates:
///   1. Method model: Client/Server using DynamicData to transmit different request and response types
///   2. Event model: Publisher/Subscriber using DynamicData to transmit mixed-type messages
///
/// Core API:
///   - DynamicData().load("TypeName", value)  -- serialize and tag with a type name
///   - dd.get_type()                          -- get the type tag string
///   - dd.as<T>()                             -- deserialize to a concrete type
///   - dd.convert(T& out)                     -- deserialize into an output parameter
int main() {
  // ======== Method Model (RPC) + DynamicData ========
  // Server receives DynamicData requests and dispatches based on the type tag
  Server<DynamicData, DynamicData> server("dds://dynamic/method");

  server.listen([](const DynamicData& req, DynamicData& res) {
    // Dispatch based on the type tag

    if (req.get_type() == "type1") {
      // Deserialize DynamicData to pb::Request
      if (req.as<pb::Request>().type() == 521) {
        // Respond with a string-typed result
        res = DynamicData().load("type1", "I love you");
      }
    } else if (req.get_type() == "type2") {
      // Deserialize DynamicData to pb::Message
      if (req.as<pb::Message>().value() == "forever") {
        // Respond with an int-typed result
        res = DynamicData().load("type2", 1314);
      }
    }
  });

  // Client sends different types of requests
  Client<DynamicData, DynamicData> client("dds://dynamic/method");

  // Listen for Server online/offline events
  client.detect_connected([](bool connected) { VLOG_I("server status:", connected); });

  // Construct the first type of request
  pb::Request req1;
  req1.set_type(521);

  // Send a type1 request; load() automatically serializes the Protobuf message and tags it
  auto resp1 = client.invoke(DynamicData().load("type1", req1));

  if (resp1.has_value()) {
    // Deserialize the response to std::string
    VLOG_I("resp1:", resp1.value().as<std::string>());
  }

  // Construct the second type of request
  pb::Message req2;
  req2.set_value("forever");

  // Send a type2 request
  auto resp2 = client.invoke(DynamicData().load("type2", req2));

  if (resp2.has_value()) {
    // Deserialize the response to int
    VLOG_I("resp2:", resp2.value().as<int>());
  }

  // ======== Event Model (Pub/Sub) + DynamicData ========
  // Subscriber receives mixed-type event messages
  Subscriber<DynamicData> sub("dds://dynamic/event");

  sub.listen([](const DynamicData& msg) {
    // Select the deserialization method based on the type tag

    if (msg.get_type() == "Request") {
      VLOG_I("msg1:", msg.as<pb::Request>().type());
    } else if (msg.get_type() == "Response") {
      VLOG_I("msg2:", msg.as<pb::Response>().value());
    }
  });

  // Publisher publishes different types of messages to the same topic
  Publisher<DynamicData> pub("dds://dynamic/event");
  pub.wait_for_subscribers();

  // Publish a Request-type message
  pb::Request req;
  req.set_type(521);
  pub.publish(DynamicData().load("Request", req));

  // Publish a Response-type message (the same topic can carry different types)
  pb::Response resp;
  resp.set_value("love");
  pub.publish(DynamicData().load("Response", resp));

  // Wait for message delivery to complete
  std::this_thread::sleep_for(100ms);

  return 0;
}
