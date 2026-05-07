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

// Security Custom Cipher Example
// Demonstrates set_security_callbacks() with a custom XOR cipher.
// Shows the encrypt/decrypt callback signature: bool(const Bytes& in, Bytes& out)
// When custom callbacks are set, the built-in AES-128-CBC is bypassed entirely.

#include <vlink/base/logger.h>
#include <vlink/extension/security.h>
#include <vlink/vlink.h>

#include <atomic>
#include <cstring>
#include <iostream>
#include <thread>

#include "xor_cipher.h"

using namespace std::chrono_literals;  // NOLINT(build/namespaces, google-build-using-namespace)

int main() {
  // ======== Section 1: Custom XOR Cipher ========
  // Replace the built-in AES with a custom XOR cipher using set_security_callbacks().
  // When custom callbacks are installed, the AES implementation is bypassed entirely.
  {
    std::cout << "\n[1] Custom XOR Cipher via set_security_callbacks()" << std::endl;

    std::atomic<int> received{0};

    vlink::SecuritySubscriber<std::string> sub("dds://security_custom/xor");
    // Install custom decrypt callback
    // Signature: void set_security_callbacks(
    //     Security::Callback&& encrypt_callback,
    //     Security::Callback&& decrypt_callback)
    // where Security::Callback = vlink::Function<bool(const Bytes& in, Bytes& out)>
    sub.set_security_callbacks(xor_cipher::encrypt, xor_cipher::decrypt);
    sub.listen([&received](const std::string& msg) {
      received++;
      VLOG_I("[XOR] Received:", msg);
    });

    vlink::SecurityPublisher<std::string> pub("dds://security_custom/xor");
    // Install custom encrypt callback - must match the subscriber's decrypt
    pub.set_security_callbacks(xor_cipher::encrypt, xor_cipher::decrypt);

    pub.wait_for_subscribers();

    pub.publish("Hello with XOR cipher!");
    pub.publish("Custom encryption bypasses AES");

    std::this_thread::sleep_for(100ms);
    VLOG_I("XOR cipher: received", received.load(), "messages");
  }

  // ======== Section 2: Lambda-based Callbacks ========
  // Callbacks can be lambdas, capturing external state if needed.
  {
    std::cout << "\n[2] Lambda-based Custom Cipher" << std::endl;

    // A simple ROT-N cipher using lambda captures
    const uint8_t rotation = 13;

    auto rot_encrypt = [rotation](const vlink::Bytes& in, vlink::Bytes& out) -> bool {
      (void)rotation;
      if (in.empty()) return true;
      out = vlink::Bytes::create(in.size());
      for (size_t i = 0; i < in.size(); ++i) {
        out[i] = in.data()[i] + rotation;
      }
      return true;
    };

    auto rot_decrypt = [rotation](const vlink::Bytes& in, vlink::Bytes& out) -> bool {
      (void)rotation;
      if (in.empty()) return true;
      out = vlink::Bytes::create(in.size());
      for (size_t i = 0; i < in.size(); ++i) {
        out[i] = in.data()[i] - rotation;
      }
      return true;
    };

    std::atomic<int> received{0};

    vlink::SecuritySubscriber<std::string> sub("dds://security_custom/lambda");
    sub.set_security_callbacks(rot_encrypt,   // encrypt callback (not used by subscriber, but required)
                               rot_decrypt);  // decrypt callback
    sub.listen([&received](const std::string& msg) {
      received++;
      VLOG_I("[ROT-N] Received:", msg);
    });

    vlink::SecurityPublisher<std::string> pub("dds://security_custom/lambda");
    pub.set_security_callbacks(rot_encrypt,   // encrypt callback
                               rot_decrypt);  // decrypt callback (not used by publisher, but required)

    pub.wait_for_subscribers();

    pub.publish("Lambda ROT-13 encrypted");

    std::this_thread::sleep_for(100ms);
    VLOG_I("Lambda cipher: received", received.load(), "messages");
  }

  // ======== Section 3: Direct Security Class Usage ========
  // The Security class can also be used directly for encrypt/decrypt operations,
  // independent of the pub/sub framework.
  {
    std::cout << "\n[3] Direct Security Class Usage" << std::endl;

    vlink::Security security;

    // Set a custom key
    security.set_key("direct_test_key");

    // Set custom callbacks (overrides AES)
    security.set_callbacks(xor_cipher::encrypt, xor_cipher::decrypt);

    // Encrypt some data
    vlink::Bytes plaintext = vlink::Bytes::from_string("Hello, Security!");
    vlink::Bytes ciphertext;
    bool enc_ok = security.encrypt(plaintext, ciphertext);
    VLOG_I("Encrypt success:", enc_ok, "plaintext_size:", plaintext.size(), "cipher_size:", ciphertext.size());

    // Decrypt the ciphertext
    vlink::Bytes recovered;
    bool dec_ok = security.decrypt(ciphertext, recovered);
    VLOG_I("Decrypt success:", dec_ok, "recovered:", recovered.to_string());

    // Verify roundtrip
    if (plaintext == recovered) {
      VLOG_I("Roundtrip verification: PASS");
    } else {
      VLOG_W("Roundtrip verification: FAIL");
    }
  }

  // ======== Section 4: Callback Signature Reference ========
  {
    std::cout << "\n[4] Callback Signature Reference" << std::endl;
    std::cout << "   Security::Callback = vlink::Function<bool(const Bytes& in, Bytes& out)>" << std::endl;
    std::cout << std::endl;
    std::cout << "   Encrypt callback:" << std::endl;
    std::cout << "     Input:  'in'  = plaintext bytes from the serialized message" << std::endl;
    std::cout << "     Output: 'out' = ciphertext bytes to be sent over the transport" << std::endl;
    std::cout << "     Return: true on success, false on encryption failure" << std::endl;
    std::cout << std::endl;
    std::cout << "   Decrypt callback:" << std::endl;
    std::cout << "     Input:  'in'  = ciphertext bytes received from the transport" << std::endl;
    std::cout << "     Output: 'out' = plaintext bytes to be deserialized into MsgT" << std::endl;
    std::cout << "     Return: true on success, false on decryption failure" << std::endl;
    std::cout << std::endl;
    std::cout << "   API:" << std::endl;
    std::cout << "     node.set_security_callbacks(encrypt_cb, decrypt_cb);" << std::endl;
    std::cout << "     // or on Security class directly:" << std::endl;
    std::cout << "     security.set_callbacks(encrypt_cb, decrypt_cb);" << std::endl;
    std::cout << std::endl;
    std::cout << "   When callbacks are set, built-in AES-128-CBC is bypassed entirely." << std::endl;
    std::cout << "   Set callbacks to nullptr to fall back to built-in AES." << std::endl;
  }

  VLOG_I("Security custom cipher example complete.");
  return 0;
}
