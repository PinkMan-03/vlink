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

#include <thread>

using namespace vlink;                 // NOLINT(build/namespaces, google-build-using-namespace)
using namespace std::chrono_literals;  // NOLINT(build/namespaces, google-build-using-namespace)

/// Shared memory (shm://) transport + security encryption example
/// This example demonstrates all three VLink communication models over the shm:// transport backend,
/// with security encryption enabled (Security* variants).
///
/// Prerequisite: the Iceoryx RouDi daemon must be running in the background
///   $ iox-roudi &
///
/// Contains three sections:
///   1. Method model (RPC): encrypted Client/Server communication
///   2. Event model (Pub/Sub): encrypted message publishing with a custom key
///   3. Field model (Getter/Setter): encrypted field read/write
int main() {
  Security::Config method_sec_cfg;
  method_sec_cfg.key = "rpc-shared-key";

  // ======== Method Model (RPC + Encryption) ========
  // SecurityServer is the secure variant of Server; it automatically encrypts/decrypts requests and responses using
  // AES-128-GCM (AEAD).  Security::Config is passed as the second constructor argument.
  SecurityServer<Bytes, Bytes> server("shm://example_raw/method", method_sec_cfg);

  // Register a synchronous callback: when receiving the request {0x1, 0x2, 0x3}, return a 1MB response
  server.listen([](const Bytes& req, Bytes& resp) {
    if (req == Bytes{0x1, 0x2, 0x3}) {
      resp = Bytes::create(1024 * 1024);  // Allocate a 1MB buffer
      resp[0] = 0xA;                      // Set the first byte
      resp[(1024 * 1024) - 1] = 0xB;      // Set the last byte
    }
  });

  // SecurityClient automatically encrypts requests and decrypts responses
  SecurityClient<Bytes, Bytes> client("shm://example_raw/method", method_sec_cfg);

  // Synchronous call: send a request and wait for the response (returns std::optional)
  auto resp = client.invoke(Bytes{0x1, 0x2, 0x3});
  if (resp.has_value()) {
    VLOG_I("Client invoke size:", resp.value().size());
    VLOG_I("Client invoke first:", +resp.value().data()[0]);
    VLOG_I("Client invoke last:", +resp.value().data()[(1024 * 1024) - 1]);
  } else {
    VLOG_W("Client invoke failed.");
  }

  // ======== Event Model (Pub/Sub + Custom Key) ========
  Security::Config sub_sec_cfg;
  sub_sec_cfg.key = "custom-key-16b!!";

  SecuritySubscriber<Bytes> sub("shm://example_raw/event", sub_sec_cfg);
  // Register receive callback: convert the received Bytes to a string and print it
  sub.listen([](const Bytes& msg) { VLOG_I("sub:", msg.to_string()); });

  Security::Config pub_sec_cfg;
  pub_sec_cfg.key = "custom-key-16b!!";

  SecurityPublisher<Bytes> pub("shm://example_raw/event", pub_sec_cfg);

  // Wait for at least one subscriber to be ready before publishing
  pub.wait_for_subscribers();
  // Publish three encrypted messages in succession
  pub.publish(Bytes::from_string("hello1"));
  pub.publish(Bytes::from_string("hello2"));
  pub.publish(Bytes::from_string("hello3"));

  Security::Config field_sec_cfg;
  field_sec_cfg.key = "field-shared-key";

  // ======== Field Model (Getter/Setter + Encryption) ========
  // SecuritySetter writes encrypted field values
  SecuritySetter<Bytes> setter("shm://example_raw/field", field_sec_cfg);

  // Write the field value {0x0A, 0x0B, 0x0C}
  setter.set(Bytes{0xA, 0XB, 0XC});

  // SecurityGetter reads encrypted field values
  SecurityGetter<Bytes> getter("shm://example_raw/field", field_sec_cfg);

  // Wait briefly to ensure the field value has been transmitted
  std::this_thread::sleep_for(100ms);

  // Read the latest field value
  auto ret = getter.get();

  if (ret.has_value()) {
    VLOG_I("Getter value:", ret.value());
  } else {
    VLOG_W("Getter get failed.");
  }

  return 0;
}
