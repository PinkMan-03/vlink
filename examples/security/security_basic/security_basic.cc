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
// Demonstrates SecurityPublisher/SecuritySubscriber with the construction-only
// Security::Config API:
//   1. Symmetric raw key (AES-128-GCM, SHA-256 truncated)
//   2. PBKDF2 passphrase derivation (AES-128-GCM)
//   3. Key mismatch failure scenario
//   4. Encryption with raw Bytes payloads
//   5. Transport / serializer limitations
//
// Algorithm: AES-128-GCM (AEAD) with envelope AAD, sequence nonce, and 16-byte tag.
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
  // ======== Section 1: Symmetric Raw Key ========
  // Provide a raw seed via Security::Config::key.  Both endpoints must supply
  // the same seed; internally it is SHA-256 truncated to a 16-byte AES key.
  {
    std::cout << "\n[1] Symmetric Raw Key (AES-128-GCM)" << std::endl;

    std::atomic<int> received{0};

    vlink::Security::Config sub_cfg;
    sub_cfg.key = "my-secret-key-16";

    vlink::SecuritySubscriber<std::string> sub("dds://security_basic/raw_key", sub_cfg);
    sub.listen([&received](const std::string& msg) {
      received++;
      VLOG_I("[Raw Key] Received:", msg);
    });

    vlink::Security::Config pub_cfg;
    pub_cfg.key = "my-secret-key-16";

    vlink::SecurityPublisher<std::string> pub("dds://security_basic/raw_key", pub_cfg);

    pub.wait_for_subscribers();

    pub.publish("Hello with AES-128-GCM!");
    pub.publish("Authenticated encryption is automatic");
    pub.publish("Both endpoints derive the same 16-byte AES key");

    std::this_thread::sleep_for(100ms);
    VLOG_I("Raw key: received", received.load(), "messages");
  }

  // ======== Section 2: PBKDF2 Passphrase ========
  // Low-entropy passphrases must be stretched via PBKDF2-HMAC-SHA256.
  // Both endpoints must share the same passphrase, salt and iteration count.
  {
    std::cout << "\n[2] PBKDF2 Passphrase (AES-128-GCM)" << std::endl;

    std::atomic<int> received{0};

    const vlink::Bytes shared_salt = vlink::Bytes::from_string("vlink-example-salt-v1");

    vlink::Security::Config sub_cfg;
    sub_cfg.passphrase = "correct horse battery staple";
    sub_cfg.pbkdf2_salt = shared_salt;
    sub_cfg.pbkdf2_iterations = 200000U;

    vlink::SecuritySubscriber<std::string> sub("dds://security_basic/passphrase", sub_cfg);
    sub.listen([&received](const std::string& msg) {
      received++;
      VLOG_I("[Passphrase] Received:", msg);
    });

    vlink::Security::Config pub_cfg;
    pub_cfg.passphrase = "correct horse battery staple";
    pub_cfg.pbkdf2_salt = shared_salt;
    pub_cfg.pbkdf2_iterations = 200000U;

    vlink::SecurityPublisher<std::string> pub("dds://security_basic/passphrase", pub_cfg);

    pub.wait_for_subscribers();

    pub.publish("Passphrase-derived AES key in use");
    pub.publish("PBKDF2 normalises low-entropy inputs");

    std::this_thread::sleep_for(100ms);
    VLOG_I("Passphrase: received", received.load(), "messages");
  }

  // ======== Section 3: Key Mismatch Failure ========
  // When the publisher and subscriber derive different keys, GCM authentication
  // fails on the receiver and the message is dropped before the user callback.
  {
    std::cout << "\n[3] Key Mismatch Failure Demo" << std::endl;
    std::cout << "   Publisher key: 'alpha-key-16byte'" << std::endl;
    std::cout << "   Subscriber key: 'beta--key-16byte' (DIFFERENT)" << std::endl;

    std::atomic<int> received{0};

    vlink::Security::Config sub_cfg;
    sub_cfg.key = "beta--key-16byte";

    vlink::SecuritySubscriber<std::string> sub("dds://security_basic/mismatch", sub_cfg);
    sub.listen([&received](const std::string& msg) {
      received++;
      VLOG_I("[Mismatch] Received (unexpected):", msg);
    });

    vlink::Security::Config pub_cfg;
    pub_cfg.key = "alpha-key-16byte";

    vlink::SecurityPublisher<std::string> pub("dds://security_basic/mismatch", pub_cfg);

    pub.wait_for_subscribers();

    pub.publish("This message should fail GCM authentication");
    pub.publish("Key mismatch prevents decryption");

    std::this_thread::sleep_for(200ms);
    VLOG_I("Key mismatch: received", received.load(), "messages (expected 0 due to auth failure)");
    std::cout << "   Messages encrypted with a different key fail GCM authentication." << std::endl;
    std::cout << "   The subscriber's callback is NOT invoked on decryption failure." << std::endl;
  }

  // ======== Section 4: Security with Bytes Type ========
  // Security works with any supported message type, including raw Bytes.
  {
    std::cout << "\n[4] Security with Bytes Type" << std::endl;

    std::atomic<int> received{0};

    vlink::Security::Config sub_cfg;
    sub_cfg.key = "bytes--key-16b!!";

    vlink::SecuritySubscriber<vlink::Bytes> sub("dds://security_basic/bytes", sub_cfg);
    sub.listen([&received](const vlink::Bytes& msg) {
      received++;
      // NOLINTNEXTLINE(readability-container-size-empty)
      VLOG_I("[Bytes] Received size:", msg.size(), "first_byte:", msg.size() > 0 ? +msg.data()[0] : -1);
    });

    vlink::Security::Config pub_cfg;
    pub_cfg.key = "bytes--key-16b!!";

    vlink::SecurityPublisher<vlink::Bytes> pub("dds://security_basic/bytes", pub_cfg);

    pub.wait_for_subscribers();

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
