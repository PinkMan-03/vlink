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
// Demonstrates the custom-callback slot of vlink::Security::Config.
// When both encrypt_callback and decrypt_callback are installed, the built-in
// AES-128-GCM AEAD path is bypassed entirely and the user-supplied cipher runs
// for every encrypt() / decrypt() call.
//
// Callback signature: bool(const vlink::Bytes& in, vlink::Bytes& out)

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
  // Install custom encrypt/decrypt callbacks through Security::Config.
  // When both callbacks are set, the built-in AES-128-GCM path is skipped.
  {
    std::cout << "\n[1] Custom XOR Cipher via Security::Config callbacks" << std::endl;

    std::atomic<int> received{0};

    vlink::Security::Config sub_cfg;
    sub_cfg.encrypt_callback = xor_cipher::encrypt;
    sub_cfg.decrypt_callback = xor_cipher::decrypt;

    vlink::SecuritySubscriber<std::string> sub("dds://security_custom/xor", sub_cfg);
    sub.listen([&received](const std::string& msg) {
      received++;
      VLOG_I("[XOR] Received:", msg);
    });

    vlink::Security::Config pub_cfg;
    pub_cfg.encrypt_callback = xor_cipher::encrypt;
    pub_cfg.decrypt_callback = xor_cipher::decrypt;

    vlink::SecurityPublisher<std::string> pub("dds://security_custom/xor", pub_cfg);

    pub.wait_for_subscribers();

    pub.publish("Hello with XOR cipher!");
    pub.publish("Custom encryption bypasses AES");

    std::this_thread::sleep_for(100ms);
    VLOG_I("XOR cipher: received", received.load(), "messages");
  }

  // ======== Section 2: Lambda-based Callbacks ========
  // Callbacks may be lambdas, capturing external state if needed.
  {
    std::cout << "\n[2] Lambda-based Custom Cipher" << std::endl;

    const uint8_t rotation = 13;

    auto rot_encrypt = [rotation](const vlink::Bytes& in, vlink::Bytes& out) -> bool {
      if (in.empty()) return true;
      out = vlink::Bytes::create(in.size());
      for (size_t i = 0; i < in.size(); ++i) {
        out[i] = in.data()[i] + rotation;
      }
      return true;
    };

    auto rot_decrypt = [rotation](const vlink::Bytes& in, vlink::Bytes& out) -> bool {
      if (in.empty()) return true;
      out = vlink::Bytes::create(in.size());
      for (size_t i = 0; i < in.size(); ++i) {
        out[i] = in.data()[i] - rotation;
      }
      return true;
    };

    std::atomic<int> received{0};

    vlink::Security::Config sub_cfg;
    sub_cfg.encrypt_callback = rot_encrypt;
    sub_cfg.decrypt_callback = rot_decrypt;

    vlink::SecuritySubscriber<std::string> sub("dds://security_custom/lambda", sub_cfg);
    sub.listen([&received](const std::string& msg) {
      received++;
      VLOG_I("[ROT-N] Received:", msg);
    });

    vlink::Security::Config pub_cfg;
    pub_cfg.encrypt_callback = rot_encrypt;
    pub_cfg.decrypt_callback = rot_decrypt;

    vlink::SecurityPublisher<std::string> pub("dds://security_custom/lambda", pub_cfg);

    pub.wait_for_subscribers();

    pub.publish("Lambda ROT-13 encrypted");

    std::this_thread::sleep_for(100ms);
    VLOG_I("Lambda cipher: received", received.load(), "messages");
  }

  // ======== Section 3: Direct Security Class Usage ========
  // The Security class can be used standalone, independent of the pub/sub
  // framework.  Configuration is one-shot at construction time; to change
  // settings construct a fresh Security instance.
  {
    std::cout << "\n[3] Direct Security Class Usage" << std::endl;

    vlink::Security::Config sec_cfg;
    sec_cfg.encrypt_callback = xor_cipher::encrypt;
    sec_cfg.decrypt_callback = xor_cipher::decrypt;
    vlink::Security security(sec_cfg);

    vlink::Bytes plaintext = vlink::Bytes::from_string("Hello, Security!");
    vlink::Bytes ciphertext;
    bool enc_ok = security.encrypt(plaintext, ciphertext);
    VLOG_I("Encrypt success:", enc_ok, "plaintext_size:", plaintext.size(), "cipher_size:", ciphertext.size());

    vlink::Bytes recovered;
    bool dec_ok = security.decrypt(ciphertext, recovered);
    VLOG_I("Decrypt success:", dec_ok, "recovered:", recovered.to_string());

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
    std::cout << "   Configuration:" << std::endl;
    std::cout << "     vlink::Security::Config cfg;" << std::endl;
    std::cout << "     cfg.encrypt_callback = my_encrypt;" << std::endl;
    std::cout << "     cfg.decrypt_callback = my_decrypt;" << std::endl;
    std::cout << "     SecurityPublisher pub(url, cfg);    // cfg passed via ctor (no runtime setter)" << std::endl;
    std::cout << "     // or standalone:" << std::endl;
    std::cout << "     vlink::Security security(cfg);" << std::endl;
    std::cout << std::endl;
    std::cout << "   When both callbacks are set, the built-in AES-128-GCM path is bypassed." << std::endl;
    std::cout << "   Security::Config is one-shot at construction; rebuild to change settings." << std::endl;
  }

  VLOG_I("Security custom cipher example complete.");
  return 0;
}
