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
#include <thread>

// FlatBuffers generated message types
#if __has_include("./someip_flat.fbs.hpp")
#include "./someip_flat.fbs.hpp"
#else
#include "./someip_flat_generated.h"
#endif

using namespace vlink;                 // NOLINT(build/namespaces, google-build-using-namespace)
using namespace std::chrono_literals;  // NOLINT(build/namespaces, google-build-using-namespace)

/// SOME/IP + FlatBuffers example
///
/// This example demonstrates all three VLink communication models using FlatBuffers
/// serialization over the SOME/IP transport backend.
///
/// SOME/IP URL format:
///   someip://ServiceID/InstanceID?method=MethodID        -- method model
///   someip://ServiceID/InstanceID?groups=GID&event=EID   -- event/field model
///   field=1 parameter indicates field mode (Getter/Setter)
///
/// FlatBuffers serialization types:
///   - fbs::RequestT  (NativeTable) -> kFlatTableType  -- value type, data is copied
///   - fbs::Request*  (Table pointer) -> kFlatPtrType  -- zero-copy read pointer
///
/// Prerequisite: the vsomeip daemon must be running in the background
int main() {
  // ======== Method Model (RPC) ========
  // Using fire-and-forget mode (Server only receives requests, no response)
  std::atomic_bool flag{false};

  // Server receives FlatBuffers pointer type (zero-copy read, no deserialization needed)
  // kFlatPtrType: directly obtains a read-only pointer to the FlatBuffers Table
  Server<fbs::Request*> server("someip://0x1/0x2?method=0x3");

  server.listen([&flag](const fbs::Request* req) {
    VLOG_I("type:", req->type());  // Read field via FlatBuffers accessor
    flag = true;
  });

  // Client uses NativeTable type (object API, automatically serialized)
  // kFlatTableType: uses the FlatBuffers object API, operates like a regular struct
  Client<fbs::RequestT> client("someip://0x1/0x2?method=0x3");

  fbs::RequestT req;
  req.type = 100;

  // Wait for the Server to come online
  client.wait_for_connected();

  // Fire-and-forget send (Client<ReqT> with no RespT template parameter -> send mode)
  client.send(req);

  // Wait for the Server to finish processing
  while (!flag) {
    std::this_thread::sleep_for(1s);
  }

  // ======== Event Model (Pub/Sub) ========
  // Subscriber uses pointer type for receiving (zero-copy), Publisher uses value type for sending
  Subscriber<fbs::Message*> sub("someip://0x1/0x3?groups=0x1,0x2&event=0x3");

  // FlatBuffers pointer type is valid within the callback; it must not be used outside the callback
  sub.listen([](const fbs::Message* msg) { VLOG_I("event:", msg->type(), ", value:", msg->value()->c_str()); });

  Publisher<fbs::MessageT> pub("someip://0x1/0x3?groups=0x1,0x2&event=0x3");

  fbs::MessageT msg;

  std::this_thread::sleep_for(1s);

  // Publish three FlatBuffers messages in succession
  msg.type = 1;
  msg.value = "one";
  pub.publish(msg);

  std::this_thread::sleep_for(1s);
  msg.type = 2;
  msg.value = "two";
  pub.publish(msg);

  std::this_thread::sleep_for(1s);

  msg.type = 3;
  msg.value = "three";
  pub.publish(msg);

  // ======== Field Model (Getter/Setter) ========
  // field=1 in the URL indicates field mode
  Setter<fbs::MessageT> setter("someip://0x1/0x4?groups=0x1,0x2&event=0x4&field=1");

  fbs::MessageT value;
  value.type = 1000;
  value.value = "hi";
  setter.set(value);  // Write the field value

  Getter<fbs::MessageT> getter("someip://0x1/0x4?groups=0x1,0x2&event=0x4&field=1");

  std::this_thread::sleep_for(100ms);

  // Read the latest field value
  auto ret = getter.get();

  if (ret.has_value()) {
    VLOG_I("Getter type:", ret.value().type);
    VLOG_I("Getter value:", ret.value().value);
  } else {
    VLOG_W("Getter get failed.");
  }

  return 0;
}
