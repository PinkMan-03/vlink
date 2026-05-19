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

// Security RSA Hybrid Example
// Demonstrates the asymmetric path of vlink::Security::Config:
//   - RSA-OAEP-SHA256 wraps a fresh 16-byte AES-128 session key per message
//   - AES-128-GCM encrypts the payload with that session key
//   - RSA-PSS-SHA256 (optional) signs AAD + ciphertext/tag for sender authentication
//
// Configuration on the sender:
//   cfg.public_key_pem  = peer_recv_pub_pem;     // wrap session key for receiver
//   cfg.advanced.signing_key_pem = own_sign_priv_pem;     // (optional) sign AAD + ciphertext/tag
//
// Configuration on the receiver:
//   cfg.private_key_pem = own_recv_priv_pem;     // unwrap session key
//   cfg.advanced.verify_key_pem  = peer_sign_pub_pem;     // (optional) verify signature
//
// Keys must be RSA >= 2048 bits.  This example generates ephemeral RSA-2048 key
// pairs on startup via OpenSSL for self-contained demonstration.  In production,
// keys are typically loaded from PEM files provisioned out of band.

#include <openssl/bio.h>
#include <openssl/evp.h>
#include <openssl/pem.h>
#include <openssl/rsa.h>
#include <vlink/base/logger.h>
#include <vlink/extension/security.h>
#include <vlink/vlink.h>

#include <atomic>
#include <iostream>
#include <memory>
#include <string>
#include <thread>

using namespace std::chrono_literals;  // NOLINT(build/namespaces, google-build-using-namespace)

namespace {

struct RsaKeyPair {
  std::string public_pem;
  std::string private_pem;
};

RsaKeyPair generate_rsa_keypair(int bits) {
  RsaKeyPair kp;

  EVP_PKEY_CTX* gctx = EVP_PKEY_CTX_new_id(EVP_PKEY_RSA, nullptr);
  if (gctx == nullptr) {
    VLOG_W("EVP_PKEY_CTX_new_id failed");
    return kp;
  }
  std::unique_ptr<EVP_PKEY_CTX, decltype(&EVP_PKEY_CTX_free)> gctx_guard(gctx, &EVP_PKEY_CTX_free);

  if (EVP_PKEY_keygen_init(gctx) <= 0 || EVP_PKEY_CTX_set_rsa_keygen_bits(gctx, bits) <= 0) {
    VLOG_W("RSA keygen init failed");
    return kp;
  }

  EVP_PKEY* pkey = nullptr;
  if (EVP_PKEY_keygen(gctx, &pkey) <= 0 || pkey == nullptr) {
    VLOG_W("RSA keygen failed");
    return kp;
  }
  std::unique_ptr<EVP_PKEY, decltype(&EVP_PKEY_free)> pkey_guard(pkey, &EVP_PKEY_free);

  {
    BIO* bio = BIO_new(BIO_s_mem());
    if (bio == nullptr) {
      VLOG_W("BIO_new(public) failed");
      return RsaKeyPair{};
    }
    std::unique_ptr<BIO, decltype(&BIO_free)> bio_guard(bio, &BIO_free);
    if (PEM_write_bio_PUBKEY(bio, pkey) != 1) {
      VLOG_W("PEM_write_bio_PUBKEY failed");
      return RsaKeyPair{};
    }
    char* buf = nullptr;
    const auto len = BIO_get_mem_data(bio, &buf);  // NOLINT(runtime/int, google-runtime-int)
    if (buf == nullptr || len <= 0) {
      VLOG_W("BIO_get_mem_data(public) returned empty");
      return RsaKeyPair{};
    }
    kp.public_pem.assign(buf, static_cast<size_t>(len));
  }
  {
    BIO* bio = BIO_new(BIO_s_mem());
    if (bio == nullptr) {
      VLOG_W("BIO_new(private) failed");
      return RsaKeyPair{};
    }
    std::unique_ptr<BIO, decltype(&BIO_free)> bio_guard(bio, &BIO_free);
    if (PEM_write_bio_PrivateKey(bio, pkey, nullptr, nullptr, 0, nullptr, nullptr) != 1) {
      VLOG_W("PEM_write_bio_PrivateKey failed");
      return RsaKeyPair{};
    }
    char* buf = nullptr;
    const auto len = BIO_get_mem_data(bio, &buf);  // NOLINT(runtime/int, google-runtime-int)
    if (buf == nullptr || len <= 0) {
      VLOG_W("BIO_get_mem_data(private) returned empty");
      return RsaKeyPair{};
    }
    kp.private_pem.assign(buf, static_cast<size_t>(len));
  }

  return kp;
}

}  // namespace

int main() {
  VLOG_I("Generating ephemeral RSA-2048 key pairs (receiver + signing)");
  const RsaKeyPair recv_kp = generate_rsa_keypair(2048);
  const RsaKeyPair sign_kp = generate_rsa_keypair(2048);
  if (recv_kp.public_pem.empty() || sign_kp.public_pem.empty()) {
    VLOG_W("RSA key generation failed -- aborting example.");
    return 1;
  }

  // ======== Section 1: RSA-OAEP Hybrid (Encryption Only) ========
  // Sender wraps a fresh AES-128 session key with the receiver's RSA public key,
  // then AES-128-GCM encrypts the payload with that session key.
  {
    std::cout << "\n[1] RSA-OAEP Hybrid (AES-128-GCM payload)" << std::endl;

    std::atomic<int> received{0};

    vlink::Security::Config sub_cfg;
    sub_cfg.private_key_pem = recv_kp.private_pem;

    vlink::SecuritySubscriber<std::string> sub("dds://security_rsa/hybrid", sub_cfg);
    sub.listen([&received](const std::string& msg) {
      received++;
      VLOG_I("[RSA-Hybrid] Received:", msg);
    });

    vlink::Security::Config pub_cfg;
    pub_cfg.public_key_pem = recv_kp.public_pem;

    vlink::SecurityPublisher<std::string> pub("dds://security_rsa/hybrid", pub_cfg);

    pub.wait_for_subscribers();

    pub.publish("RSA-OAEP wraps an AES-128 session key");
    pub.publish("Each message uses a fresh session key");
    pub.publish("AES-128-GCM provides the authenticated payload");

    std::this_thread::sleep_for(100ms);
    VLOG_I("RSA hybrid: received", received.load(), "messages");
  }

  // ======== Section 2: RSA-OAEP Hybrid + RSA-PSS Sender Authentication ========
  // Adds RSA-PSS-SHA256 signing/verification on top of the hybrid encryption.
  // The sender signs AAD + ciphertext/tag with its own private key, and the
  // receiver verifies with the matching public key before decrypting.
  {
    std::cout << "\n[2] RSA Hybrid + RSA-PSS Signed (sender authenticated)" << std::endl;

    std::atomic<int> received{0};

    vlink::Security::Config sub_cfg;
    sub_cfg.private_key_pem = recv_kp.private_pem;
    sub_cfg.advanced.verify_key_pem = sign_kp.public_pem;

    vlink::SecuritySubscriber<std::string> sub("dds://security_rsa/signed", sub_cfg);
    sub.listen([&received](const std::string& msg) {
      received++;
      VLOG_I("[RSA-Signed] Received:", msg);
    });

    vlink::Security::Config pub_cfg;
    pub_cfg.public_key_pem = recv_kp.public_pem;
    pub_cfg.advanced.signing_key_pem = sign_kp.private_pem;

    vlink::SecurityPublisher<std::string> pub("dds://security_rsa/signed", pub_cfg);

    pub.wait_for_subscribers();

    pub.publish("Signed with RSA-PSS-SHA256");
    pub.publish("Receiver verifies before decrypting");

    std::this_thread::sleep_for(100ms);
    VLOG_I("RSA signed: received", received.load(), "messages");
  }

  // ======== Section 3: Wrong Signing Key Rejection ========
  // The receiver expects messages signed by sign_kp.  A sender that signs with
  // a different key fails verification and the messages are dropped before the
  // user callback.
  {
    std::cout << "\n[3] Wrong Signing Key -- Verification Failure" << std::endl;

    const RsaKeyPair impostor_kp = generate_rsa_keypair(2048);

    std::atomic<int> received{0};

    vlink::Security::Config sub_cfg;
    sub_cfg.private_key_pem = recv_kp.private_pem;
    sub_cfg.advanced.verify_key_pem = sign_kp.public_pem;  // Trust only sign_kp

    vlink::SecuritySubscriber<std::string> sub("dds://security_rsa/impostor", sub_cfg);
    sub.listen([&received](const std::string& msg) {
      received++;
      VLOG_I("[Impostor] Received (unexpected):", msg);
    });

    vlink::Security::Config pub_cfg;
    pub_cfg.public_key_pem = recv_kp.public_pem;
    pub_cfg.advanced.signing_key_pem = impostor_kp.private_pem;  // Signs with the wrong key

    vlink::SecurityPublisher<std::string> pub("dds://security_rsa/impostor", pub_cfg);

    pub.wait_for_subscribers();

    pub.publish("This message should be rejected on verification");
    pub.publish("Impostor signing key does not match advanced.verify_key_pem");

    std::this_thread::sleep_for(200ms);
    VLOG_I("Impostor: received", received.load(), "messages (expected 0 due to RSA-PSS failure)");
  }

  // ======== Section 4: Standalone Security Class with RSA ========
  // The Security class can be used directly for RSA hybrid encryption,
  // independent of the pub/sub framework.
  {
    std::cout << "\n[4] Standalone vlink::Security with RSA Hybrid" << std::endl;

    vlink::Security::Config sender_cfg;
    sender_cfg.public_key_pem = recv_kp.public_pem;
    sender_cfg.advanced.signing_key_pem = sign_kp.private_pem;
    vlink::Security sender(sender_cfg);

    vlink::Security::Config receiver_cfg;
    receiver_cfg.private_key_pem = recv_kp.private_pem;
    receiver_cfg.advanced.verify_key_pem = sign_kp.public_pem;
    vlink::Security receiver(receiver_cfg);

    vlink::Bytes plaintext = vlink::Bytes::from_string("Standalone RSA hybrid payload");
    vlink::Bytes ciphertext;
    const bool enc_ok = sender.encrypt(plaintext, ciphertext);
    VLOG_I("Encrypt success:", enc_ok, "plaintext_size:", plaintext.size(), "cipher_size:", ciphertext.size());

    vlink::Bytes recovered;
    const bool dec_ok = receiver.decrypt(ciphertext, recovered);
    VLOG_I("Decrypt success:", dec_ok, "recovered:", recovered.to_string());

    if (plaintext == recovered) {
      VLOG_I("Roundtrip verification: PASS");
    } else {
      VLOG_W("Roundtrip verification: FAIL");
    }
  }

  // ======== Section 5: Configuration Reference ========
  {
    std::cout << "\n[5] RSA Configuration Reference" << std::endl;
    std::cout << "   Sender Security::Config:" << std::endl;
    std::cout << "     public_key_pem  -- peer's RSA public key (PEM); wraps session key" << std::endl;
    std::cout << "     advanced.signing_key_pem -- local RSA private key (PEM); optional RSA-PSS sign" << std::endl;
    std::cout << std::endl;
    std::cout << "   Receiver Security::Config:" << std::endl;
    std::cout << "     private_key_pem -- local RSA private key (PEM); unwraps session key" << std::endl;
    std::cout << "     advanced.verify_key_pem  -- peer's RSA public key (PEM); optional RSA-PSS verify" << std::endl;
    std::cout << std::endl;
    std::cout << "   Constraints:" << std::endl;
    std::cout << "     - RSA keys must be >= 2048 bits" << std::endl;
    std::cout << "     - PEM strings must contain a valid RSA key; EC keys are rejected" << std::endl;
    std::cout << "     - Configuration is one-shot at construction; rebuild Security to change" << std::endl;
    std::cout << std::endl;
    std::cout << "   Algorithms:" << std::endl;
    std::cout << "     Wrap:      RSA-OAEP-SHA256 (per-message 16-byte AES session key)" << std::endl;
    std::cout << "     Payload:   AES-128-GCM (envelope AAD, sequence nonce, 16-byte tag)" << std::endl;
    std::cout << "     Signature: RSA-PSS-SHA256 (salt length = digest length)" << std::endl;
  }

  VLOG_I("Security RSA hybrid example complete.");
  return 0;
}
