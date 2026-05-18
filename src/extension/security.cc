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

#include <cstring>
#include <limits>
#include <mutex>
#include <string>
#include <utility>

#include "./base/logger.h"

#ifdef VLINK_ENABLE_SECURITY
#include <openssl/bio.h>
#include <openssl/evp.h>
#include <openssl/pem.h>
#include <openssl/rand.h>
#include <openssl/rsa.h>
#endif

namespace vlink {

#ifdef VLINK_ENABLE_SECURITY

[[maybe_unused]] static constexpr size_t kAesKeySize = 16U;
[[maybe_unused]] static constexpr size_t kAesNonceSize = 12U;
[[maybe_unused]] static constexpr size_t kAesTagSize = 16U;
[[maybe_unused]] static constexpr size_t kRsaWrapLenFieldSize = 2U;
[[maybe_unused]] static constexpr size_t kRsaSigLenFieldSize = 2U;
[[maybe_unused]] static constexpr size_t kAsymHeaderFieldsSize = kRsaWrapLenFieldSize + kRsaSigLenFieldSize;
[[maybe_unused]] static constexpr int kRsaMinBits = 2048;
[[maybe_unused]] static constexpr size_t kPbkdf2MinSaltSize = 16U;

[[nodiscard]] static inline bool size_fits_int(size_t value) noexcept {
  return value <= static_cast<size_t>(std::numeric_limits<int>::max());
}

struct DigestScrub final {
  uint8_t* ptr;
  size_t size;

  DigestScrub(uint8_t* p, size_t n) noexcept : ptr(p), size(n) {}

  ~DigestScrub() noexcept { OPENSSL_cleanse(ptr, size); }

  DigestScrub(const DigestScrub&) = delete;
  DigestScrub& operator=(const DigestScrub&) = delete;
  DigestScrub(DigestScrub&&) = delete;
  DigestScrub& operator=(DigestScrub&&) = delete;
};

struct EvpCipherCtxDeleter final {
  void operator()(EVP_CIPHER_CTX* ptr) const noexcept { EVP_CIPHER_CTX_free(ptr); }
};

struct EvpPkeyDeleter final {
  void operator()(EVP_PKEY* ptr) const noexcept { EVP_PKEY_free(ptr); }
};

struct EvpPkeyCtxDeleter final {
  void operator()(EVP_PKEY_CTX* ptr) const noexcept { EVP_PKEY_CTX_free(ptr); }
};

struct BioDeleter final {
  void operator()(BIO* ptr) const noexcept { BIO_free(ptr); }
};

using EvpCipherCtxPtr = std::unique_ptr<EVP_CIPHER_CTX, EvpCipherCtxDeleter>;
using EvpPkeyPtr = std::unique_ptr<EVP_PKEY, EvpPkeyDeleter>;
using EvpPkeyCtxPtr = std::unique_ptr<EVP_PKEY_CTX, EvpPkeyCtxDeleter>;
using BioPtr = std::unique_ptr<BIO, BioDeleter>;

static Bytes derive_aes_key_sha256(const std::string& seed) noexcept {
  Bytes out = Bytes::create(kAesKeySize);

  if VUNLIKELY (out.data() == nullptr) {
    return Bytes{};
  }

  uint8_t digest[EVP_MAX_MD_SIZE];
  DigestScrub digest_scrub{digest, sizeof(digest)};
  unsigned int digest_len = 0;

  if VUNLIKELY (EVP_Digest(seed.data(), seed.size(), digest, &digest_len, EVP_sha256(), nullptr) != 1) {
    return Bytes{};
  }

  std::memcpy(out.data(), digest, kAesKeySize);

  return out;
}

static Bytes derive_aes_key_pbkdf2(const std::string& passphrase, const uint8_t* salt, size_t salt_len,
                                   uint32_t iterations) noexcept {
  if VUNLIKELY (!size_fits_int(passphrase.size()) || !size_fits_int(salt_len) ||
                iterations > static_cast<uint32_t>(std::numeric_limits<int>::max())) {
    return Bytes{};
  }

  Bytes out = Bytes::create(kAesKeySize);

  if VUNLIKELY (out.data() == nullptr) {
    return Bytes{};
  }

  const int rv =
      PKCS5_PBKDF2_HMAC(passphrase.data(), static_cast<int>(passphrase.size()), salt, static_cast<int>(salt_len),
                        static_cast<int>(iterations), EVP_sha256(), static_cast<int>(kAesKeySize), out.data());
  if VUNLIKELY (rv != 1) {
    OPENSSL_cleanse(out.data(), out.size());
    return Bytes{};
  }

  return out;
}

static bool aes_gcm_encrypt(const uint8_t* key, const uint8_t* nonce, const uint8_t* in, size_t in_len,
                            uint8_t* cipher_out, uint8_t* tag_out) noexcept {
  if VUNLIKELY (!size_fits_int(in_len)) {
    return false;
  }

  EvpCipherCtxPtr ctx{EVP_CIPHER_CTX_new()};

  if VUNLIKELY (!ctx) {
    return false;
  }

  if VUNLIKELY (EVP_EncryptInit_ex(ctx.get(), EVP_aes_128_gcm(), nullptr, nullptr, nullptr) != 1) {
    return false;
  }

  if VUNLIKELY (EVP_CIPHER_CTX_ctrl(ctx.get(), EVP_CTRL_GCM_SET_IVLEN, static_cast<int>(kAesNonceSize), nullptr) != 1) {
    return false;
  }

  if VUNLIKELY (EVP_EncryptInit_ex(ctx.get(), nullptr, nullptr, key, nonce) != 1) {
    return false;
  }

  int len_update = 0;

  if VLIKELY (in_len > 0U) {
    if VUNLIKELY (EVP_EncryptUpdate(ctx.get(), cipher_out, &len_update, in, static_cast<int>(in_len)) != 1) {
      return false;
    }
  }

  int len_final = 0;

  if VUNLIKELY (EVP_EncryptFinal_ex(ctx.get(), cipher_out + len_update, &len_final) != 1) {
    return false;
  }

  if VUNLIKELY (static_cast<size_t>(len_update) + static_cast<size_t>(len_final) != in_len) {
    return false;
  }

  if VUNLIKELY (EVP_CIPHER_CTX_ctrl(ctx.get(), EVP_CTRL_GCM_GET_TAG, static_cast<int>(kAesTagSize), tag_out) != 1) {
    return false;
  }

  return true;
}

static bool aes_gcm_decrypt(const uint8_t* key, const uint8_t* nonce, const uint8_t* cipher, size_t cipher_len,
                            const uint8_t* tag, uint8_t* plain_out) noexcept {
  if VUNLIKELY (!size_fits_int(cipher_len)) {
    return false;
  }

  EvpCipherCtxPtr ctx{EVP_CIPHER_CTX_new()};

  if VUNLIKELY (!ctx) {
    return false;
  }

  if VUNLIKELY (EVP_DecryptInit_ex(ctx.get(), EVP_aes_128_gcm(), nullptr, nullptr, nullptr) != 1) {
    return false;
  }

  if VUNLIKELY (EVP_CIPHER_CTX_ctrl(ctx.get(), EVP_CTRL_GCM_SET_IVLEN, static_cast<int>(kAesNonceSize), nullptr) != 1) {
    return false;
  }

  if VUNLIKELY (EVP_DecryptInit_ex(ctx.get(), nullptr, nullptr, key, nonce) != 1) {
    return false;
  }

  int len_update = 0;

  if VLIKELY (cipher_len > 0U) {
    if VUNLIKELY (EVP_DecryptUpdate(ctx.get(), plain_out, &len_update, cipher, static_cast<int>(cipher_len)) != 1) {
      return false;
    }
  }

  if VUNLIKELY (EVP_CIPHER_CTX_ctrl(ctx.get(), EVP_CTRL_GCM_SET_TAG, static_cast<int>(kAesTagSize),
                                    const_cast<uint8_t*>(tag)) != 1) {
    return false;
  }

  int len_final = 0;

  if VUNLIKELY (EVP_DecryptFinal_ex(ctx.get(), plain_out + len_update, &len_final) <= 0) {
    return false;
  }

  if VUNLIKELY (static_cast<size_t>(len_update) + static_cast<size_t>(len_final) != cipher_len) {
    return false;
  }

  return true;
}

static bool rsa_oaep_encrypt(EVP_PKEY* pkey, const uint8_t* in, size_t in_len, Bytes& out) noexcept {
  if VUNLIKELY (!size_fits_int(in_len)) {
    return false;
  }

  EvpPkeyCtxPtr ctx{EVP_PKEY_CTX_new(pkey, nullptr)};

  if VUNLIKELY (!ctx) {
    return false;
  }

  if VUNLIKELY (EVP_PKEY_encrypt_init(ctx.get()) <= 0) {
    return false;
  }

  if VUNLIKELY (EVP_PKEY_CTX_set_rsa_padding(ctx.get(), RSA_PKCS1_OAEP_PADDING) <= 0) {
    return false;
  }

  if VUNLIKELY (EVP_PKEY_CTX_set_rsa_oaep_md(ctx.get(), EVP_sha256()) <= 0) {
    return false;
  }

  if VUNLIKELY (EVP_PKEY_CTX_set_rsa_mgf1_md(ctx.get(), EVP_sha256()) <= 0) {
    return false;
  }

  size_t cipher_len = 0;

  if VUNLIKELY (EVP_PKEY_encrypt(ctx.get(), nullptr, &cipher_len, in, in_len) <= 0) {
    return false;
  }

  out = Bytes::create(cipher_len);

  if VUNLIKELY (out.data() == nullptr) {
    return false;
  }

  if VUNLIKELY (EVP_PKEY_encrypt(ctx.get(), out.data(), &cipher_len, in, in_len) <= 0) {
    return false;
  }

  if VUNLIKELY (!out.resize(cipher_len)) {
    return false;
  }

  return true;
}

static bool rsa_oaep_decrypt(EVP_PKEY* pkey, const uint8_t* in, size_t in_len, Bytes& out) noexcept {
  if VUNLIKELY (!size_fits_int(in_len)) {
    return false;
  }

  EvpPkeyCtxPtr ctx{EVP_PKEY_CTX_new(pkey, nullptr)};

  if VUNLIKELY (!ctx) {
    return false;
  }

  if VUNLIKELY (EVP_PKEY_decrypt_init(ctx.get()) <= 0) {
    return false;
  }

  if VUNLIKELY (EVP_PKEY_CTX_set_rsa_padding(ctx.get(), RSA_PKCS1_OAEP_PADDING) <= 0) {
    return false;
  }

  if VUNLIKELY (EVP_PKEY_CTX_set_rsa_oaep_md(ctx.get(), EVP_sha256()) <= 0) {
    return false;
  }

  if VUNLIKELY (EVP_PKEY_CTX_set_rsa_mgf1_md(ctx.get(), EVP_sha256()) <= 0) {
    return false;
  }

  size_t plain_len = 0;

  if VUNLIKELY (EVP_PKEY_decrypt(ctx.get(), nullptr, &plain_len, in, in_len) <= 0) {
    return false;
  }

  out = Bytes::create(plain_len);

  if VUNLIKELY (out.data() == nullptr) {
    return false;
  }

  if VUNLIKELY (EVP_PKEY_decrypt(ctx.get(), out.data(), &plain_len, in, in_len) <= 0) {
    OPENSSL_cleanse(out.data(), out.size());
    out = Bytes{};
    return false;
  }

  if VUNLIKELY (!out.resize(plain_len)) {
    OPENSSL_cleanse(out.data(), out.size());
    out = Bytes{};
    return false;
  }

  return true;
}

static bool rsa_pss_sign(EVP_PKEY* pkey, const uint8_t* data, size_t data_len, Bytes& sig_out) noexcept {
  if VUNLIKELY (!size_fits_int(data_len)) {
    return false;
  }

  uint8_t digest[EVP_MAX_MD_SIZE];
  DigestScrub digest_scrub{digest, sizeof(digest)};
  unsigned int digest_len = 0;

  if VUNLIKELY (EVP_Digest(data, data_len, digest, &digest_len, EVP_sha256(), nullptr) != 1) {
    return false;
  }

  EvpPkeyCtxPtr ctx{EVP_PKEY_CTX_new(pkey, nullptr)};

  if VUNLIKELY (!ctx) {
    return false;
  }

  if VUNLIKELY (EVP_PKEY_sign_init(ctx.get()) <= 0) {
    return false;
  }

  if VUNLIKELY (EVP_PKEY_CTX_set_rsa_padding(ctx.get(), RSA_PKCS1_PSS_PADDING) <= 0) {
    return false;
  }

  if VUNLIKELY (EVP_PKEY_CTX_set_rsa_pss_saltlen(ctx.get(), RSA_PSS_SALTLEN_DIGEST) <= 0) {
    return false;
  }

  if VUNLIKELY (EVP_PKEY_CTX_set_signature_md(ctx.get(), EVP_sha256()) <= 0) {
    return false;
  }

  size_t sig_len = 0;

  if VUNLIKELY (EVP_PKEY_sign(ctx.get(), nullptr, &sig_len, digest, digest_len) <= 0) {
    return false;
  }

  sig_out = Bytes::create(sig_len);

  if VUNLIKELY (sig_out.data() == nullptr) {
    return false;
  }

  if VUNLIKELY (EVP_PKEY_sign(ctx.get(), sig_out.data(), &sig_len, digest, digest_len) <= 0) {
    return false;
  }

  if VUNLIKELY (!sig_out.resize(sig_len)) {
    return false;
  }

  return true;
}

static bool rsa_pss_verify(EVP_PKEY* pkey, const uint8_t* data, size_t data_len, const uint8_t* sig,
                           size_t sig_len) noexcept {
  if VUNLIKELY (!size_fits_int(data_len) || !size_fits_int(sig_len)) {
    return false;
  }

  uint8_t digest[EVP_MAX_MD_SIZE];
  DigestScrub digest_scrub{digest, sizeof(digest)};
  unsigned int digest_len = 0;

  if VUNLIKELY (EVP_Digest(data, data_len, digest, &digest_len, EVP_sha256(), nullptr) != 1) {
    return false;
  }

  EvpPkeyCtxPtr ctx{EVP_PKEY_CTX_new(pkey, nullptr)};

  if VUNLIKELY (!ctx) {
    return false;
  }

  if VUNLIKELY (EVP_PKEY_verify_init(ctx.get()) <= 0) {
    return false;
  }

  if VUNLIKELY (EVP_PKEY_CTX_set_rsa_padding(ctx.get(), RSA_PKCS1_PSS_PADDING) <= 0) {
    return false;
  }

  if VUNLIKELY (EVP_PKEY_CTX_set_rsa_pss_saltlen(ctx.get(), RSA_PSS_SALTLEN_DIGEST) <= 0) {
    return false;
  }

  if VUNLIKELY (EVP_PKEY_CTX_set_signature_md(ctx.get(), EVP_sha256()) <= 0) {
    return false;
  }

  return EVP_PKEY_verify(ctx.get(), sig, sig_len, digest, digest_len) == 1;
}

static bool validate_rsa_key(EVP_PKEY* pkey) noexcept {
  if VUNLIKELY (pkey == nullptr) {
    return false;
  }

  if VUNLIKELY (EVP_PKEY_id(pkey) != EVP_PKEY_RSA) {
    VLOG_W("Security: key is not RSA (id=", EVP_PKEY_id(pkey), "); only RSA is supported");
    return false;
  }

  const int bits = EVP_PKEY_bits(pkey);

  if VUNLIKELY (bits < kRsaMinBits) {
    VLOG_W("Security: RSA key has only ", bits, " bits; require >= ", kRsaMinBits);
    return false;
  }

  return true;
}

static EvpPkeyPtr load_pubkey_from_pem(const std::string& pem) noexcept {
  if VUNLIKELY (!size_fits_int(pem.size())) {
    return nullptr;
  }

  BioPtr bio{BIO_new_mem_buf(pem.data(), static_cast<int>(pem.size()))};

  if VUNLIKELY (!bio) {
    return nullptr;
  }

  EvpPkeyPtr pkey{PEM_read_bio_PUBKEY(bio.get(), nullptr, nullptr, nullptr)};

  if VUNLIKELY (!validate_rsa_key(pkey.get())) {
    return nullptr;
  }

  return pkey;
}

static EvpPkeyPtr load_privkey_from_pem(const std::string& pem) noexcept {
  if VUNLIKELY (!size_fits_int(pem.size())) {
    return nullptr;
  }

  BioPtr bio{BIO_new_mem_buf(pem.data(), static_cast<int>(pem.size()))};

  if VUNLIKELY (!bio) {
    return nullptr;
  }

  EvpPkeyPtr pkey{PEM_read_bio_PrivateKey(bio.get(), nullptr, nullptr, nullptr)};

  if VUNLIKELY (!validate_rsa_key(pkey.get())) {
    return nullptr;
  }

  return pkey;
}

#endif  // VLINK_ENABLE_SECURITY

struct Security::Impl final {
  mutable std::mutex mtx;
  Bytes key;
  Security::Callback encrypt_callback;
  Security::Callback decrypt_callback;

#ifdef VLINK_ENABLE_SECURITY
  EvpPkeyPtr public_key;
  EvpPkeyPtr private_key;
  EvpPkeyPtr signing_key;
  EvpPkeyPtr verify_key;
#endif
};

Security::Security() : Security(Config{}) {}

Security::Security(const Config& cfg) : impl_(std::make_unique<Impl>()) {
  if (cfg.encrypt_callback && cfg.decrypt_callback) {
    impl_->encrypt_callback = cfg.encrypt_callback;
    impl_->decrypt_callback = cfg.decrypt_callback;
  } else if VUNLIKELY (static_cast<bool>(cfg.encrypt_callback) != static_cast<bool>(cfg.decrypt_callback)) {
    VLOG_W(
        "Security: encrypt_callback and decrypt_callback must be installed as a pair; ignoring lone "
        "callback to avoid asymmetric encrypt/decrypt behaviour.");
  }

#ifdef VLINK_ENABLE_SECURITY
  if VUNLIKELY (!cfg.passphrase.empty()) {
    if VUNLIKELY (cfg.pbkdf2_salt.size() < kPbkdf2MinSaltSize || cfg.pbkdf2_salt.data() == nullptr) {
      VLOG_W("Security: rejected passphrase: salt must be >= ", kPbkdf2MinSaltSize, " bytes");
    } else if VUNLIKELY (cfg.pbkdf2_iterations == 0U) {
      VLOG_W("Security: rejected passphrase: iterations must be > 0");
    } else {
      Bytes derived =
          derive_aes_key_pbkdf2(cfg.passphrase, cfg.pbkdf2_salt.data(), cfg.pbkdf2_salt.size(), cfg.pbkdf2_iterations);

      if VUNLIKELY (derived.size() != kAesKeySize) {
        VLOG_W("Security: PBKDF2 derivation failed");
      } else {
        impl_->key = std::move(derived);
      }
    }
  }

  if (impl_->key.empty() && !cfg.key.empty()) {
    Bytes derived = derive_aes_key_sha256(cfg.key);

    if VUNLIKELY (derived.size() != kAesKeySize) {
      VLOG_W("Security: SHA-256 derivation failed");
    } else {
      impl_->key = std::move(derived);
    }
  }

  if VUNLIKELY (!cfg.public_key_pem.empty()) {
    auto pkey = load_pubkey_from_pem(cfg.public_key_pem);

    if VUNLIKELY (!pkey) {
      VLOG_W("Security: rejected public_key_pem (parse failed, non-RSA, or <2048 bits)");
    } else {
      impl_->public_key = std::move(pkey);
    }
  }

  if VUNLIKELY (!cfg.private_key_pem.empty()) {
    auto pkey = load_privkey_from_pem(cfg.private_key_pem);

    if VUNLIKELY (!pkey) {
      VLOG_W("Security: rejected private_key_pem (parse failed, non-RSA, or <2048 bits)");
    } else {
      impl_->private_key = std::move(pkey);
    }
  }

  if VUNLIKELY (!cfg.signing_key_pem.empty()) {
    auto pkey = load_privkey_from_pem(cfg.signing_key_pem);

    if VUNLIKELY (!pkey) {
      VLOG_W("Security: rejected signing_key_pem (parse failed, non-RSA, or <2048 bits)");
    } else {
      impl_->signing_key = std::move(pkey);
    }
  }

  if VUNLIKELY (!cfg.verify_key_pem.empty()) {
    auto pkey = load_pubkey_from_pem(cfg.verify_key_pem);

    if VUNLIKELY (!pkey) {
      VLOG_W("Security: rejected verify_key_pem (parse failed, non-RSA, or <2048 bits)");
    } else {
      impl_->verify_key = std::move(pkey);
    }
  }
#else
  if VUNLIKELY (!cfg.key.empty() || !cfg.passphrase.empty() || !cfg.public_key_pem.empty() ||
                !cfg.private_key_pem.empty() || !cfg.signing_key_pem.empty() || !cfg.verify_key_pem.empty()) {
    VLOG_W(
        "Security: ignoring built-in algorithm fields (VLINK_ENABLE_SECURITY off); "
        "only Config::encrypt_callback / decrypt_callback will function.");
  }
#endif
}

Security::~Security() {
  if VUNLIKELY (!impl_) {
    return;
  }

#ifdef VLINK_ENABLE_SECURITY
  std::lock_guard lock(impl_->mtx);

  if VLIKELY (!impl_->key.empty()) {
    OPENSSL_cleanse(impl_->key.data(), impl_->key.size());
  }
#endif
}

Security::Security(Security&& other) noexcept : impl_(std::move(other.impl_)) {
  other.impl_ = std::make_unique<Impl>();
}

Security& Security::operator=(Security&& other) noexcept {
  if VUNLIKELY (this == &other) {
    return *this;
  }

  if VLIKELY (impl_) {
#ifdef VLINK_ENABLE_SECURITY
    std::lock_guard lock(impl_->mtx);
    if VLIKELY (!impl_->key.empty()) {
      OPENSSL_cleanse(impl_->key.data(), impl_->key.size());
    }
#endif
  }

  impl_ = std::move(other.impl_);
  other.impl_ = std::make_unique<Impl>();

  return *this;
}

bool Security::is_configured() const noexcept {
  std::lock_guard lock(impl_->mtx);

#ifdef VLINK_ENABLE_SECURITY
  if VLIKELY (impl_->key.size() >= kAesKeySize && impl_->key.data() != nullptr) {
    return true;
  }

  if (impl_->public_key || impl_->private_key) {
    return true;
  }
#endif

  if (impl_->encrypt_callback && impl_->decrypt_callback) {
    return true;
  }

  return false;
}

bool Security::can_encrypt() const noexcept {
  std::lock_guard lock(impl_->mtx);

  if (impl_->encrypt_callback && impl_->decrypt_callback) {
    return true;
  }

#ifdef VLINK_ENABLE_SECURITY
  if VLIKELY (impl_->key.size() >= kAesKeySize && impl_->key.data() != nullptr) {
    return true;
  }

  if (impl_->public_key) {
    return true;
  }
#endif

  return false;
}

bool Security::can_decrypt() const noexcept {
  std::lock_guard lock(impl_->mtx);

  if (impl_->encrypt_callback && impl_->decrypt_callback) {
    return true;
  }

#ifdef VLINK_ENABLE_SECURITY
  if VLIKELY (impl_->key.size() >= kAesKeySize && impl_->key.data() != nullptr) {
    return true;
  }

  if (impl_->private_key) {
    return true;
  }
#endif

  return false;
}

bool Security::encrypt(const Bytes& in, Bytes& out) {
  std::lock_guard lock(impl_->mtx);

  out = Bytes{};

  if VUNLIKELY (in.empty()) {
    return false;
  }

  if (impl_->encrypt_callback) {
    if VUNLIKELY (!impl_->encrypt_callback(in, out)) {
      out = Bytes{};
      return false;
    }

    return true;
  }

#ifdef VLINK_ENABLE_SECURITY
  if VUNLIKELY (in.size() > static_cast<size_t>(std::numeric_limits<int>::max())) {
    VLOG_W("Security::encrypt input exceeds INT_MAX bytes");
    return false;
  }

  if (impl_->public_key) {
    uint8_t session_key[kAesKeySize];
    uint8_t nonce[kAesNonceSize];

    if VUNLIKELY (RAND_bytes(session_key, sizeof session_key) != 1) {
      return false;
    }

    if VUNLIKELY (RAND_bytes(nonce, sizeof nonce) != 1) {
      OPENSSL_cleanse(session_key, sizeof session_key);
      return false;
    }

    Bytes wrapped;

    if VUNLIKELY (!rsa_oaep_encrypt(impl_->public_key.get(), session_key, sizeof session_key, wrapped)) {
      OPENSSL_cleanse(session_key, sizeof session_key);
      return false;
    }

    if VUNLIKELY (wrapped.size() > 0xFFFFU) {
      OPENSSL_cleanse(session_key, sizeof session_key);
      VLOG_W("Security::encrypt RSA-wrapped key exceeds 65535 bytes");
      return false;
    }

    const size_t body_size = kAesNonceSize + in.size() + kAesTagSize;
    Bytes body = Bytes::create(body_size);

    if VUNLIKELY (body.data() == nullptr) {
      OPENSSL_cleanse(session_key, sizeof session_key);
      return false;
    }

    std::memcpy(body.data(), nonce, kAesNonceSize);
    uint8_t* body_cipher = body.data() + kAesNonceSize;
    uint8_t* body_tag = body_cipher + in.size();

    const bool gcm_ok = aes_gcm_encrypt(session_key, nonce, in.data(), in.size(), body_cipher, body_tag);
    OPENSSL_cleanse(session_key, sizeof session_key);

    if VUNLIKELY (!gcm_ok) {
      return false;
    }

    Bytes signature;

    if VUNLIKELY (impl_->signing_key) {
      const auto wrap_len_le_sig = static_cast<uint16_t>(wrapped.size());
      const uint8_t wrap_len_bytes[kRsaWrapLenFieldSize] = {
          static_cast<uint8_t>(wrap_len_le_sig & 0xFFU),
          static_cast<uint8_t>((wrap_len_le_sig >> 8U) & 0xFFU),
      };

      Bytes signed_range = Bytes::create(kRsaWrapLenFieldSize + wrapped.size() + body_size);
      if VUNLIKELY (signed_range.data() == nullptr) {
        return false;
      }

      std::memcpy(signed_range.data(), wrap_len_bytes, kRsaWrapLenFieldSize);
      std::memcpy(signed_range.data() + kRsaWrapLenFieldSize, wrapped.data(), wrapped.size());
      std::memcpy(signed_range.data() + kRsaWrapLenFieldSize + wrapped.size(), body.data(), body_size);

      if VUNLIKELY (!rsa_pss_sign(impl_->signing_key.get(), signed_range.data(), signed_range.size(), signature)) {
        VLOG_W("Security::encrypt RSA-PSS sign failed");
        return false;
      }

      if VUNLIKELY (signature.size() > 0xFFFFU) {
        VLOG_W("Security::encrypt signature exceeds 65535 bytes");
        return false;
      }
    }

    const size_t total = kAsymHeaderFieldsSize + wrapped.size() + signature.size() + body_size;
    out = Bytes::create(total);

    if VUNLIKELY (out.data() == nullptr) {
      out = Bytes{};
      return false;
    }

    uint8_t* dst = out.data();
    const auto wrap_len_le = static_cast<uint16_t>(wrapped.size());
    const auto sig_len_le = static_cast<uint16_t>(signature.size());

    dst[0] = static_cast<uint8_t>(wrap_len_le & 0xFFU);
    dst[1] = static_cast<uint8_t>((wrap_len_le >> 8U) & 0xFFU);
    dst[2] = static_cast<uint8_t>(sig_len_le & 0xFFU);
    dst[3] = static_cast<uint8_t>((sig_len_le >> 8U) & 0xFFU);

    std::memcpy(dst + kAsymHeaderFieldsSize, wrapped.data(), wrapped.size());

    if VUNLIKELY (!signature.empty()) {
      std::memcpy(dst + kAsymHeaderFieldsSize + wrapped.size(), signature.data(), signature.size());
    }

    std::memcpy(dst + kAsymHeaderFieldsSize + wrapped.size() + signature.size(), body.data(), body_size);

    return true;
  }

  if VUNLIKELY (impl_->key.size() < kAesKeySize || impl_->key.data() == nullptr) {
    VLOG_W("Security::encrypt no symmetric key installed; construct with a non-empty Config");
    return false;
  }

  uint8_t nonce[kAesNonceSize];

  if VUNLIKELY (RAND_bytes(nonce, sizeof nonce) != 1) {
    return false;
  }

  const size_t total = kAesNonceSize + in.size() + kAesTagSize;
  out = Bytes::create(total);

  if VUNLIKELY (out.data() == nullptr) {
    out = Bytes{};
    return false;
  }

  std::memcpy(out.data(), nonce, kAesNonceSize);

  uint8_t* cipher_dst = out.data() + kAesNonceSize;
  uint8_t* tag_dst = cipher_dst + in.size();

  if VUNLIKELY (!aes_gcm_encrypt(impl_->key.data(), nonce, in.data(), in.size(), cipher_dst, tag_dst)) {
    out = Bytes{};
    return false;
  }

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

  out = Bytes{};

  if VUNLIKELY (in.empty()) {
    return false;
  }

  if (impl_->decrypt_callback) {
    if VUNLIKELY (!impl_->decrypt_callback(in, out)) {
      out = Bytes{};
      return false;
    }

    return true;
  }

#ifdef VLINK_ENABLE_SECURITY
  if VUNLIKELY (in.size() > static_cast<size_t>(std::numeric_limits<int>::max())) {
    VLOG_W("Security::decrypt input exceeds INT_MAX bytes");
    return false;
  }

  if (impl_->private_key) {
    if VUNLIKELY (in.size() <= kAsymHeaderFieldsSize + kAesNonceSize + kAesTagSize) {
      return false;
    }

    const uint8_t* src = in.data();

    if VUNLIKELY (src == nullptr) {
      return false;
    }

    const auto wrap_len = static_cast<uint16_t>(static_cast<uint16_t>(src[0]) | (static_cast<uint16_t>(src[1]) << 8U));
    const auto sig_len = static_cast<uint16_t>(static_cast<uint16_t>(src[2]) | (static_cast<uint16_t>(src[3]) << 8U));
    const size_t header = kAsymHeaderFieldsSize + static_cast<size_t>(wrap_len) + static_cast<size_t>(sig_len);

    if VUNLIKELY (in.size() <= header + kAesNonceSize + kAesTagSize) {
      return false;
    }

    const uint8_t* wrapped_ptr = src + kAsymHeaderFieldsSize;
    const uint8_t* sig_ptr = wrapped_ptr + wrap_len;
    const uint8_t* body_ptr = sig_ptr + sig_len;
    const size_t body_size = in.size() - header;

    if VUNLIKELY (impl_->verify_key) {
      if VUNLIKELY (sig_len == 0U) {
        VLOG_W("Security::decrypt verify_key set but message is unsigned");
        return false;
      }

      Bytes signed_range = Bytes::create(kRsaWrapLenFieldSize + wrap_len + body_size);

      if VUNLIKELY (signed_range.data() == nullptr) {
        return false;
      }

      std::memcpy(signed_range.data(), src, kRsaWrapLenFieldSize);
      std::memcpy(signed_range.data() + kRsaWrapLenFieldSize, wrapped_ptr, wrap_len);
      std::memcpy(signed_range.data() + kRsaWrapLenFieldSize + wrap_len, body_ptr, body_size);

      if VUNLIKELY (!rsa_pss_verify(impl_->verify_key.get(), signed_range.data(), signed_range.size(), sig_ptr,
                                    sig_len)) {
        VLOG_W("Security::decrypt RSA-PSS signature verification failed");
        return false;
      }
    }

    Bytes session_key;

    if VUNLIKELY (!rsa_oaep_decrypt(impl_->private_key.get(), wrapped_ptr, wrap_len, session_key)) {
      return false;
    }

    if VUNLIKELY (session_key.size() != kAesKeySize || session_key.data() == nullptr) {
      if (!session_key.empty() && session_key.data() != nullptr) {
        OPENSSL_cleanse(session_key.data(), session_key.size());
      }

      return false;
    }

    const uint8_t* nonce = body_ptr;
    const size_t cipher_len = body_size - kAesNonceSize - kAesTagSize;
    const uint8_t* cipher = nonce + kAesNonceSize;
    const uint8_t* tag = cipher + cipher_len;

    Bytes plain = Bytes::create(cipher_len);

    if VUNLIKELY (plain.data() == nullptr) {
      OPENSSL_cleanse(session_key.data(), session_key.size());
      return false;
    }

    const bool ok = aes_gcm_decrypt(session_key.data(), nonce, cipher, cipher_len, tag, plain.data());
    OPENSSL_cleanse(session_key.data(), session_key.size());

    if VUNLIKELY (!ok) {
      OPENSSL_cleanse(plain.data(), plain.size());
      return false;
    }

    out = std::move(plain);

    return true;
  }

  if VUNLIKELY (in.size() <= kAesNonceSize + kAesTagSize) {
    return false;
  }

  if VUNLIKELY (impl_->key.size() < kAesKeySize || impl_->key.data() == nullptr) {
    VLOG_W("Security::decrypt no symmetric key installed; construct with a non-empty Config");
    return false;
  }

  const uint8_t* src = in.data();

  if VUNLIKELY (src == nullptr) {
    return false;
  }

  const uint8_t* nonce = src;
  const size_t body_len = in.size() - kAesNonceSize - kAesTagSize;
  const uint8_t* cipher = src + kAesNonceSize;
  const uint8_t* tag = cipher + body_len;

  Bytes plain = Bytes::create(body_len);

  if VUNLIKELY (plain.data() == nullptr) {
    return false;
  }

  if VUNLIKELY (!aes_gcm_decrypt(impl_->key.data(), nonce, cipher, body_len, tag, plain.data())) {
    OPENSSL_cleanse(plain.data(), plain.size());
    return false;
  }

  out = std::move(plain);

  return true;
#else
  (void)in;
  (void)out;

  VLOG_W("Security: Function [decrypt] is not supported (VLINK_ENABLE_SECURITY not enabled).");

  return false;
#endif
}

}  // namespace vlink
