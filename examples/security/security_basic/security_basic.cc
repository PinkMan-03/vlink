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

// Security Basic Example
// Demonstrates SecurityPublisher/SecuritySubscriber with:
//   1. Default encryption key ("vlink")
//   2. Custom encryption key via set_security_key()
//   3. Key mismatch failure scenario
// Uses AES-128-CBC encryption. Default IV: "thun.lu@zohomail.cn"
//
// WARNING: intra:// does NOT support security encryption (messages stay in-process).
// Security requires a cross-process transport such as dds://, shm://, zenoh://, mqtt://, etc.

#include <vlink/base/logger.h>
#include <vlink/vlink.h>

#include <atomic>
#include <iostream>
#include <thread>

#include "security_common.h"

using namespace std::chrono_literals;  // NOLINT(build/namespaces, google-build-using-namespace)

int main() {
  // ======== Section 1: Default Key Encryption ========
  // SecurityPublisher and SecuritySubscriber use AES-128-CBC encryption.
  // The default key is "vlink" and the default IV is "thun.lu@zohomail.cn".
  // Both sides must use the same key to communicate.
  {
    std::cout << "\n[1] Default Key Encryption (key=\"vlink\")" << std::endl;

    std::atomic<int> received{0};

    // SecuritySubscriber automatically decrypts incoming messages
    vlink::SecuritySubscriber<std::string> sub("dds://security_basic/default_key");
    // No set_security_key() call => uses default key "vlink"
    sub.listen([&received](const std::string& msg) {
      received++;
      VLOG_I("[Default Key] Received:", msg);
    });

    // SecurityPublisher automatically encrypts outgoing messages
    vlink::SecurityPublisher<std::string> pub("dds://security_basic/default_key");
    // No set_security_key() call => uses default key "vlink"

    pub.wait_for_subscribers();

    pub.publish("Hello with default encryption!");
    pub.publish("AES-128-CBC encrypted message");
    pub.publish("Default key is 'vlink'");

    std::this_thread::sleep_for(100ms);
    VLOG_I("Default key: received", received.load(), "messages");
  }

  // ======== Section 2: Custom Key Encryption ========
  // Both publisher and subscriber must call set_security_key() with the SAME key.
  // The key string is used as the AES key (OpenSSL uses the first 16 bytes).
  {
    std::cout << "\n[2] Custom Key Encryption" << std::endl;

    std::atomic<int> received{0};

    vlink::SecuritySubscriber<std::string> sub("dds://security_basic/custom_key");
    sub.set_security_key("my_secret_key_123");  // Custom 16-byte key
    sub.listen([&received](const std::string& msg) {
      received++;
      VLOG_I("[Custom Key] Received:", msg);
    });

    vlink::SecurityPublisher<std::string> pub("dds://security_basic/custom_key");
    pub.set_security_key("my_secret_key_123");  // Must match subscriber's key

    pub.wait_for_subscribers();

    pub.publish("Encrypted with custom key");
    pub.publish("Only matching keys can decrypt");

    std::this_thread::sleep_for(100ms);
    VLOG_I("Custom key: received", received.load(), "messages");
  }

  // ======== Section 3: Key Mismatch Failure ========
  // When the publisher and subscriber use different keys, decryption fails.
  // The subscriber's callback will NOT be invoked for messages it cannot decrypt.
  {
    std::cout << "\n[3] Key Mismatch Failure Demo" << std::endl;
    std::cout << "   Publisher key: 'key_alpha'" << std::endl;
    std::cout << "   Subscriber key: 'key_beta' (DIFFERENT)" << std::endl;

    std::atomic<int> received{0};

    vlink::SecuritySubscriber<std::string> sub("dds://security_basic/mismatch");
    sub.set_security_key("key_beta");  // Different key!
    sub.listen([&received](const std::string& msg) {
      received++;
      VLOG_I("[Mismatch] Received (unexpected):", msg);
    });

    vlink::SecurityPublisher<std::string> pub("dds://security_basic/mismatch");
    pub.set_security_key("key_alpha");  // Different from subscriber

    pub.wait_for_subscribers();

    // These messages will be encrypted with "key_alpha"
    // but the subscriber tries to decrypt with "key_beta" => failure
    pub.publish("This message should fail to decrypt");
    pub.publish("Key mismatch prevents decryption");

    std::this_thread::sleep_for(200ms);
    VLOG_I("Key mismatch: received", received.load(), "messages (expected 0 due to decryption failure)");
    std::cout << "   Messages encrypted with a different key cannot be decrypted." << std::endl;
    std::cout << "   The subscriber's callback is NOT invoked on decryption failure." << std::endl;
  }

  // ======== Section 4: Security with Bytes Type ========
  // Security works with any supported message type, including raw Bytes.
  {
    std::cout << "\n[4] Security with Bytes Type" << std::endl;

    std::atomic<int> received{0};

    vlink::SecuritySubscriber<vlink::Bytes> sub("dds://security_basic/bytes");
    sub.set_security_key("bytes_key");
    sub.listen([&received](const vlink::Bytes& msg) {
      received++;
      // NOLINTNEXTLINE(readability-container-size-empty)
      VLOG_I("[Bytes] Received size:", msg.size(), "first_byte:", msg.size() > 0 ? +msg.data()[0] : -1);
    });

    vlink::SecurityPublisher<vlink::Bytes> pub("dds://security_basic/bytes");
    pub.set_security_key("bytes_key");

    pub.wait_for_subscribers();

    // Publish raw binary data with encryption
    vlink::Bytes data = vlink::Bytes::create(256);
    for (size_t i = 0; i < 256; ++i) {
      data[i] = static_cast<uint8_t>(i);
    }
    pub.publish(data);

    std::this_thread::sleep_for(100ms);
    VLOG_I("Bytes security: received", received.load(), "messages");
  }

  // ======== Section 5: Security Limitations ========
  {
    std::cout << "\n[5] Security Limitations" << std::endl;
    std::cout << "   Security is NOT supported with:" << std::endl;
    std::cout << "   - intra:// transport (messages stay in-process, encryption not supported)" << std::endl;
    std::cout << "   - dds:// with CDR serialization (CDR encoding is incompatible)" << std::endl;
    std::cout << std::endl;
    std::cout << "   Supported transports: shm://, shm2://, zenoh://, mqtt://, fdbus://, etc." << std::endl;
    security_common::print_security_defaults();
    security_common::print_supported_transports();
  }

  VLOG_I("Security basic example complete.");
  return 0;
}
