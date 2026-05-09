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

#include "./extension/security.h"

#include <mutex>
#include <string>
#include <utility>
#include <vector>

#include "./base/logger.h"

#ifdef VLINK_ENABLE_SECURITY
#include <openssl/aes.h>
#include <openssl/evp.h>
#endif

namespace vlink {

[[maybe_unused]] static constexpr const char* kDefaultAesKey = "vlink_default_16";
[[maybe_unused]] static constexpr const char* kAesIV = "thun.lu@zohomail";

// Security::Impl
struct Security::Impl final {
  Bytes key;
  Bytes iv;
  std::mutex mtx;

  Security::Callback encrypt_callback;
  Security::Callback decrypt_callback;

#ifdef VLINK_ENABLE_SECURITY
  EVP_CIPHER_CTX* evp_ctx{nullptr};
  const EVP_CIPHER* cipher{nullptr};
  std::vector<uint8_t> buffer;
  std::vector<uint8_t> cache;
#endif
};

Security::Security() : impl_(std::make_unique<Impl>()) {
#ifdef VLINK_ENABLE_SECURITY
  impl_->key = Bytes::from_string(kDefaultAesKey);
  impl_->iv = Bytes::from_string(kAesIV);

  impl_->evp_ctx = EVP_CIPHER_CTX_new();
  impl_->cipher = EVP_aes_128_cbc();

  EVP_CIPHER_CTX_set_padding(impl_->evp_ctx, 1);
#else
  VLOG_F("Security: The compile macro VLINK_ENABLE_SECURITY is not turned on.");
#endif
}

// Security
Security::~Security() {
#ifdef VLINK_ENABLE_SECURITY
  std::lock_guard lock(impl_->mtx);
  EVP_CIPHER_CTX_free(impl_->evp_ctx);
  impl_->evp_ctx = nullptr;
  impl_->cipher = nullptr;
  impl_->buffer.clear();
  impl_->cache.clear();
  // impl_->buffer.shrink_to_fit();
  // impl_->cache.shrink_to_fit();
#endif
}

void Security::set_key(const std::string& key) {
  std::lock_guard lock(impl_->mtx);
  if (key.empty()) {
    impl_->key = Bytes::from_string(kDefaultAesKey);
  } else {
    impl_->key = Bytes::from_string(key);
  }
}

void Security::set_callbacks(Callback&& encrypt_callback, Callback&& decrypt_callback) {
  std::lock_guard lock(impl_->mtx);
  impl_->encrypt_callback = std::move(encrypt_callback);
  impl_->decrypt_callback = std::move(decrypt_callback);
}

bool Security::encrypt(const Bytes& in, Bytes& out) {
  std::lock_guard lock(impl_->mtx);

  if VUNLIKELY (in.empty()) {
    return true;
  }

  if (impl_->encrypt_callback) {
    return impl_->encrypt_callback(in, out);
  }

#ifdef VLINK_ENABLE_SECURITY
  if VUNLIKELY (impl_->evp_ctx == nullptr || impl_->cipher == nullptr) {
    return false;
  }

  int ret = 0;
  int length = 0;

  ret = EVP_EncryptInit_ex(impl_->evp_ctx, impl_->cipher, nullptr, impl_->key.data(), impl_->iv.data());

  if VUNLIKELY (ret != 1) {
    return false;
  }

  impl_->buffer.clear();
  impl_->cache.resize(in.size() + EVP_CIPHER_CTX_block_size(impl_->evp_ctx));

  ret = EVP_EncryptUpdate(impl_->evp_ctx, impl_->cache.data(), &length, in.data(), in.size());

  if VUNLIKELY (ret != 1) {
    return false;
  }

  impl_->buffer.insert(impl_->buffer.end(), impl_->cache.data(), impl_->cache.data() + length);

  ret = EVP_EncryptFinal_ex(impl_->evp_ctx, impl_->cache.data(), &length);

  if VUNLIKELY (ret != 1) {
    return false;
  }

  impl_->buffer.insert(impl_->buffer.end(), impl_->cache.data(), impl_->cache.data() + length);

  out = impl_->buffer;
  return true;
#else
  (void)in;
  (void)out;
  VLOG_W("Security: Function [encrypt] is not supported (VLINK_ENABLE_SECURITY not enabled).");
  return false;
#endif
}

bool Security::decrypt(const Bytes& in, Bytes& out) {
  std::lock_guard lock(impl_->mtx);

  if VUNLIKELY (in.empty()) {
    return true;
  }

  if (impl_->decrypt_callback) {
    return impl_->decrypt_callback(in, out);
  }

#ifdef VLINK_ENABLE_SECURITY
  if VUNLIKELY (impl_->evp_ctx == nullptr || impl_->cipher == nullptr) {
    return false;
  }

  int ret = 0;
  int length = 0;

  ret = EVP_DecryptInit_ex(impl_->evp_ctx, impl_->cipher, nullptr, impl_->key.data(), impl_->iv.data());

  if VUNLIKELY (ret != 1) {
    return false;
  }

  impl_->buffer.clear();
  impl_->cache.resize(in.size() + EVP_CIPHER_CTX_block_size(impl_->evp_ctx));

  ret = EVP_DecryptUpdate(impl_->evp_ctx, impl_->cache.data(), &length, in.data(), in.size());

  if VUNLIKELY (ret != 1) {
    return false;
  }

  impl_->buffer.insert(impl_->buffer.end(), impl_->cache.data(), impl_->cache.data() + length);

  ret = EVP_DecryptFinal_ex(impl_->evp_ctx, impl_->cache.data(), &length);

  if VUNLIKELY (ret != 1) {
    return false;
  }

  impl_->buffer.insert(impl_->buffer.end(), impl_->cache.data(), impl_->cache.data() + length);

  out = impl_->buffer;

  return true;
#else
  (void)in;
  (void)out;
  VLOG_W("Security: Function [decrypt] is not supported (VLINK_ENABLE_SECURITY not enabled).");

  return false;
#endif
}

}  // namespace vlink
