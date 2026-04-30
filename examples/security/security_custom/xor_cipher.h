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

#ifndef EXAMPLES_SECURITY_SECURITY_CUSTOM_XOR_CIPHER_H_
#define EXAMPLES_SECURITY_SECURITY_CUSTOM_XOR_CIPHER_H_

#include <vlink/base/logger.h>
#include <vlink/vlink.h>

#include <cstddef>
#include <cstdint>

namespace xor_cipher {

// A simple XOR cipher for demonstration purposes.
// In production, use a proper cryptographic algorithm like AES, ChaCha20, etc.
// The key is XOR'd with each byte of the input data.
static constexpr uint8_t kDefaultKey[] = {0xDE, 0xAD, 0xBE, 0xEF, 0xCA, 0xFE, 0xBA, 0xBE};
static constexpr size_t kDefaultKeyLen = sizeof(kDefaultKey);

// XOR transform: since XOR is symmetric, encrypt and decrypt are the same operation.
// Parameters:
//   in:  input bytes (plaintext for encrypt, ciphertext for decrypt)
//   out: output buffer (ciphertext for encrypt, plaintext for decrypt)
//   key: XOR key bytes
//   key_len: length of key
// Returns: true on success
inline bool xor_transform(const vlink::Bytes& in, vlink::Bytes& out, const uint8_t* key, size_t key_len) {
  if (in.empty()) {
    return true;  // Empty input is a no-op
  }

  out = vlink::Bytes::create(in.size());
  for (size_t i = 0; i < in.size(); ++i) {
    out[i] = in.data()[i] ^ key[i % key_len];
  }
  return true;
}

// Encrypt callback using the default XOR key.
// Signature: bool(const Bytes& in, Bytes& out)
inline bool encrypt(const vlink::Bytes& in, vlink::Bytes& out) {
  bool ok = xor_transform(in, out, kDefaultKey, kDefaultKeyLen);
  if (ok && !in.empty()) {
    VLOG_I("[XOR Encrypt] Encrypted ", in.size(), " bytes");
  }
  return ok;
}

// Decrypt callback using the default XOR key.
// XOR is symmetric: decryption is the same operation as encryption.
inline bool decrypt(const vlink::Bytes& in, vlink::Bytes& out) {
  bool ok = xor_transform(in, out, kDefaultKey, kDefaultKeyLen);
  if (ok && !in.empty()) {
    VLOG_I("[XOR Decrypt] Decrypted ", in.size(), " bytes");
  }
  return ok;
}

}  // namespace xor_cipher

#endif  // EXAMPLES_SECURITY_SECURITY_CUSTOM_XOR_CIPHER_H_
