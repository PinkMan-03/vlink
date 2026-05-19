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

#include <algorithm>
#include <array>
#include <cstring>
#include <limits>
#include <mutex>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

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
[[maybe_unused]] static constexpr uint8_t kEnvelopeMagic0 = 'V';
[[maybe_unused]] static constexpr uint8_t kEnvelopeMagic1 = 'S';
[[maybe_unused]] static constexpr uint8_t kEnvelopeVersion = 2U;
[[maybe_unused]] static constexpr uint8_t kEnvelopeModeSymmetric = 1U;
[[maybe_unused]] static constexpr uint8_t kEnvelopeModeAsymmetric = 2U;
[[maybe_unused]] static constexpr size_t kEnvelopeFixedHeaderSize = 35U;
[[maybe_unused]] static constexpr uint32_t kReplayWindowMax = 65536U;
[[maybe_unused]] static constexpr char kAadDomain[] = "vlink-security-v2";
[[maybe_unused]] static constexpr size_t kAadDomainSize = sizeof(kAadDomain) - 1U;

#ifdef RSA_PSS_SALTLEN_DIGEST
[[maybe_unused]] static constexpr int kRsaPssSaltLenDigest = RSA_PSS_SALTLEN_DIGEST;
#else
[[maybe_unused]] static constexpr int kRsaPssSaltLenDigest = -1;
#endif

struct ReplayWindow final {
  uint64_t highest{0};
  std::vector<uint64_t> words;
};

struct PeerReplay final {
  uint64_t sender_id{0};
  ReplayWindow window;
};

struct SymmetricKeySlot final {
  std::string key_id;
  Bytes key;
  bool can_encrypt{false};
  bool can_decrypt{true};
  std::vector<PeerReplay> peers;
};

struct EnvelopeHeader final {
  uint8_t mode{0};
  uint16_t flags{0};
  std::string_view key_id;
  uint64_t sender_id{0};
  uint64_t seq{0};
  const uint8_t* nonce{nullptr};
  size_t size{0};
};

struct AadParts final {
  const std::string* context{nullptr};
  const uint8_t* header{nullptr};
  size_t header_len{0};
  const uint8_t* extra{nullptr};
  size_t extra_len{0};
};

struct DigestScrub final {
  uint8_t* ptr{nullptr};
  size_t size{0};

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

[[nodiscard]] static inline bool size_fits_int(size_t value) noexcept {
  return value <= static_cast<size_t>(std::numeric_limits<int>::max());
}

static inline void write_u16_le(uint8_t* dst, uint16_t value) noexcept {
  dst[0] = static_cast<uint8_t>(value & 0xFFU);
  dst[1] = static_cast<uint8_t>((value >> 8U) & 0xFFU);
}

static inline uint16_t read_u16_le(const uint8_t* src) noexcept {
  return static_cast<uint16_t>(static_cast<uint16_t>(src[0]) | (static_cast<uint16_t>(src[1]) << 8U));
}

static inline void write_u64_le(uint8_t* dst, uint64_t value) noexcept {
  for (size_t i = 0; i < sizeof(value); ++i) {
    dst[i] = static_cast<uint8_t>((value >> (i * 8U)) & 0xFFU);
  }
}

static inline uint64_t read_u64_le(const uint8_t* src) noexcept {
  uint64_t value = 0;

  for (size_t i = 0; i < sizeof(value); ++i) {
    value |= static_cast<uint64_t>(src[i]) << (i * 8U);
  }

  return value;
}

static inline void copy_string_bytes(uint8_t* dst, std::string_view value) noexcept {
  std::copy_n(reinterpret_cast<const uint8_t*>(value.data()), value.size(), dst);
}

static std::string normalize_key_id(const std::string& key_id) {
  return key_id.empty() ? std::string{"default"} : key_id;
}

static std::string normalize_key_id(std::string_view key_id) {
  return key_id.empty() ? std::string{"default"} : std::string{key_id};
}

static bool is_default_key_id(const std::string& key_id) noexcept { return key_id == "default"; }

static bool key_id_equal(const std::string& lhs, std::string_view rhs) noexcept {
  if (rhs.empty()) {
    return is_default_key_id(lhs);
  }

  return lhs.size() == rhs.size() && std::memcmp(lhs.data(), rhs.data(), rhs.size()) == 0;
}

static size_t wire_key_id_size(const std::string& key_id) noexcept {
  return is_default_key_id(key_id) ? 0U : key_id.size();
}

static uint32_t normalize_replay_window(uint32_t window) noexcept {
  return (window > kReplayWindowMax) ? kReplayWindowMax : window;
}

static ReplayWindow& get_peer_window(std::vector<PeerReplay>& peers, uint64_t sender_id) {
  for (auto& peer : peers) {
    if (peer.sender_id == sender_id) {
      return peer.window;
    }
  }

  peers.push_back(PeerReplay{sender_id, {}});

  return peers.back().window;
}

static bool accept_replay(ReplayWindow& replay, uint64_t seq, uint32_t window_bits) {
  if VUNLIKELY (window_bits == 0U) {
    return true;
  }

  if VUNLIKELY (seq == 0U) {
    return false;
  }

  const auto word_count = static_cast<size_t>((window_bits + 63U) / 64U);

  if VUNLIKELY (word_count == 0U) {
    return true;
  }

  if VUNLIKELY (replay.words.size() != word_count) {
    replay.words.assign(word_count, 0U);
    replay.highest = 0U;
  }

  const auto slot_count = static_cast<uint64_t>(word_count * 64U);

  auto clear_seq = [&replay, slot_count](uint64_t value) {
    const auto bit = value % slot_count;
    replay.words[static_cast<size_t>(bit / 64U)] &= ~(uint64_t{1} << (bit % 64U));
  };

  auto test_seq = [&replay, slot_count](uint64_t value) -> bool {
    const auto bit = value % slot_count;
    return (replay.words[static_cast<size_t>(bit / 64U)] & (uint64_t{1} << (bit % 64U))) != 0U;
  };

  auto set_seq = [&replay, slot_count](uint64_t value) {
    const auto bit = value % slot_count;
    replay.words[static_cast<size_t>(bit / 64U)] |= uint64_t{1} << (bit % 64U);
  };

  if VLIKELY (seq > replay.highest) {
    const auto gap = seq - replay.highest;

    if VUNLIKELY (gap >= slot_count) {
      std::fill(replay.words.begin(), replay.words.end(), 0U);
    } else if VLIKELY (gap == 1U) {
      clear_seq(seq);
    } else {
      for (uint64_t offset = 1U; offset <= gap; ++offset) {
        clear_seq(replay.highest + offset);
      }
    }

    replay.highest = seq;
  } else if VUNLIKELY (replay.highest - seq >= static_cast<uint64_t>(window_bits)) {
    return false;
  }

  if VUNLIKELY (test_seq(seq)) {
    return false;
  }

  set_seq(seq);

  return true;
}

static bool write_envelope_header(uint8_t mode, const std::string& key_id, uint64_t sender_id, uint64_t seq,
                                  const uint8_t* nonce, uint8_t* dst, size_t dst_size, size_t& header_size) {
  const size_t key_id_size = wire_key_id_size(key_id);

  if VUNLIKELY (key_id_size > 0xFFU || nonce == nullptr || dst == nullptr) {
    return false;
  }

  header_size = kEnvelopeFixedHeaderSize + key_id_size;

  if VUNLIKELY (dst_size < header_size) {
    return false;
  }

  dst[0] = kEnvelopeMagic0;
  dst[1] = kEnvelopeMagic1;
  dst[2] = kEnvelopeVersion;
  dst[3] = mode;
  write_u16_le(dst + 4U, 0U);
  dst[6] = static_cast<uint8_t>(key_id_size);
  write_u64_le(dst + 7U, sender_id);
  write_u64_le(dst + 15U, seq);

  std::memcpy(dst + 23U, nonce, kAesNonceSize);

  if VUNLIKELY (key_id_size > 0U) {
    copy_string_bytes(dst + kEnvelopeFixedHeaderSize, std::string_view{key_id.data(), key_id_size});
  }

  return true;
}

static bool build_envelope_header(uint8_t mode, const std::string& key_id, uint64_t sender_id, uint64_t seq,
                                  const uint8_t* nonce, Bytes& out) {
  const size_t key_id_size = wire_key_id_size(key_id);

  if VUNLIKELY (key_id_size > 0xFFU) {
    return false;
  }

  const size_t header_size = kEnvelopeFixedHeaderSize + key_id_size;
  out = Bytes::create(header_size);

  if VUNLIKELY (out.data() == nullptr) {
    return false;
  }

  size_t written = 0U;

  return write_envelope_header(mode, key_id, sender_id, seq, nonce, out.data(), out.size(), written);
}

static bool parse_envelope_header(const Bytes& in, EnvelopeHeader& header) {
  if VUNLIKELY (in.size() < kEnvelopeFixedHeaderSize || in.data() == nullptr) {
    return false;
  }

  const uint8_t* src = in.data();

  if VUNLIKELY (src[0] != kEnvelopeMagic0 || src[1] != kEnvelopeMagic1 || src[2] != kEnvelopeVersion) {
    return false;
  }

  const size_t key_id_len = src[6];
  const size_t header_size = kEnvelopeFixedHeaderSize + key_id_len;

  if VUNLIKELY (in.size() < header_size) {
    return false;
  }

  header.mode = src[3];
  header.flags = read_u16_le(src + 4U);
  header.sender_id = read_u64_le(src + 7U);
  header.seq = read_u64_le(src + 15U);
  header.nonce = src + 23U;
  header.size = header_size;
  header.key_id = std::string_view{reinterpret_cast<const char*>(src + kEnvelopeFixedHeaderSize), key_id_len};

  return true;
}

static Bytes build_aad(const std::string& context, const uint8_t* header, size_t header_len,
                       const uint8_t* extra = nullptr, size_t extra_len = 0U) {
  if VUNLIKELY (context.size() > 0xFFFFU || header == nullptr || header_len == 0U ||
                (extra_len > 0U && extra == nullptr)) {
    return Bytes{};
  }

  const size_t total = kAadDomainSize + sizeof(uint16_t) + context.size() + header_len + extra_len;
  Bytes out = Bytes::create(total);

  if VUNLIKELY (out.data() == nullptr) {
    return Bytes{};
  }

  uint8_t* dst = out.data();
  std::memcpy(dst, kAadDomain, kAadDomainSize);
  dst += kAadDomainSize;
  write_u16_le(dst, static_cast<uint16_t>(context.size()));
  dst += sizeof(uint16_t);

  if (!context.empty()) {
    copy_string_bytes(dst, context);
    dst += context.size();
  }

  std::memcpy(dst, header, header_len);
  dst += header_len;

  if (extra_len > 0U) {
    std::memcpy(dst, extra, extra_len);
  }

  return out;
}

static bool aad_parts_valid(const AadParts& aad) noexcept {
  return aad.context != nullptr && aad.context->size() <= 0xFFFFU && aad.header != nullptr && aad.header_len > 0U &&
         size_fits_int(kAadDomainSize) && size_fits_int(sizeof(uint16_t)) && size_fits_int(aad.context->size()) &&
         size_fits_int(aad.header_len) && size_fits_int(aad.extra_len) && (aad.extra_len == 0U || aad.extra != nullptr);
}

static bool encrypt_aad_chunk(EVP_CIPHER_CTX* ctx, const uint8_t* data, size_t size) noexcept {
  if (size == 0U) {
    return true;
  }

  int len_update = 0;

  return EVP_EncryptUpdate(ctx, nullptr, &len_update, data, static_cast<int>(size)) == 1;
}

static bool decrypt_aad_chunk(EVP_CIPHER_CTX* ctx, const uint8_t* data, size_t size) noexcept {
  if (size == 0U) {
    return true;
  }

  int len_update = 0;

  return EVP_DecryptUpdate(ctx, nullptr, &len_update, data, static_cast<int>(size)) == 1;
}

static bool encrypt_aad_parts(EVP_CIPHER_CTX* ctx, const AadParts& aad) noexcept {
  if VUNLIKELY (!aad_parts_valid(aad)) {
    return false;
  }

  uint8_t context_len[sizeof(uint16_t)] = {};
  write_u16_le(context_len, static_cast<uint16_t>(aad.context->size()));

  if VUNLIKELY (!encrypt_aad_chunk(ctx, reinterpret_cast<const uint8_t*>(kAadDomain), kAadDomainSize) ||
                !encrypt_aad_chunk(ctx, context_len, sizeof(context_len)) ||
                !encrypt_aad_chunk(ctx, reinterpret_cast<const uint8_t*>(aad.context->data()), aad.context->size()) ||
                !encrypt_aad_chunk(ctx, aad.header, aad.header_len) ||
                !encrypt_aad_chunk(ctx, aad.extra, aad.extra_len)) {
    return false;
  }

  return true;
}

static bool decrypt_aad_parts(EVP_CIPHER_CTX* ctx, const AadParts& aad) noexcept {
  if VUNLIKELY (!aad_parts_valid(aad)) {
    return false;
  }

  uint8_t context_len[sizeof(uint16_t)] = {};
  write_u16_le(context_len, static_cast<uint16_t>(aad.context->size()));

  if VUNLIKELY (!decrypt_aad_chunk(ctx, reinterpret_cast<const uint8_t*>(kAadDomain), kAadDomainSize) ||
                !decrypt_aad_chunk(ctx, context_len, sizeof(context_len)) ||
                !decrypt_aad_chunk(ctx, reinterpret_cast<const uint8_t*>(aad.context->data()), aad.context->size()) ||
                !decrypt_aad_chunk(ctx, aad.header, aad.header_len) ||
                !decrypt_aad_chunk(ctx, aad.extra, aad.extra_len)) {
    return false;
  }

  return true;
}

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
                            const uint8_t* aad, size_t aad_len, uint8_t* cipher_out, uint8_t* tag_out) noexcept {
  if VUNLIKELY (!size_fits_int(in_len) || !size_fits_int(aad_len)) {
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

  if VLIKELY (aad_len > 0U) {
    if VUNLIKELY (EVP_EncryptUpdate(ctx.get(), nullptr, &len_update, aad, static_cast<int>(aad_len)) != 1) {
      return false;
    }
  }

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

static bool aes_gcm_encrypt_parts(const uint8_t* key, const uint8_t* nonce, const uint8_t* in, size_t in_len,
                                  const AadParts& aad, uint8_t* cipher_out, uint8_t* tag_out) noexcept {
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

  if VUNLIKELY (!encrypt_aad_parts(ctx.get(), aad)) {
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
                            const uint8_t* aad, size_t aad_len, const uint8_t* tag, uint8_t* plain_out) noexcept {
  if VUNLIKELY (!size_fits_int(cipher_len) || !size_fits_int(aad_len)) {
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

  if VLIKELY (aad_len > 0U) {
    if VUNLIKELY (EVP_DecryptUpdate(ctx.get(), nullptr, &len_update, aad, static_cast<int>(aad_len)) != 1) {
      return false;
    }
  }

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

static bool aes_gcm_decrypt_parts(const uint8_t* key, const uint8_t* nonce, const uint8_t* cipher, size_t cipher_len,
                                  const AadParts& aad, const uint8_t* tag, uint8_t* plain_out) noexcept {
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

  if VUNLIKELY (!decrypt_aad_parts(ctx.get(), aad)) {
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

  if VUNLIKELY (EVP_PKEY_CTX_set_rsa_pss_saltlen(ctx.get(), kRsaPssSaltLenDigest) <= 0) {
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

  if VUNLIKELY (EVP_PKEY_CTX_set_rsa_pss_saltlen(ctx.get(), kRsaPssSaltLenDigest) <= 0) {
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

static bool derive_symmetric_slot_key(const std::string& key, const std::string& passphrase, const Bytes& salt,
                                      uint32_t iterations, Bytes& out) {
  out = Bytes{};

  if VUNLIKELY (!passphrase.empty()) {
    if VUNLIKELY (salt.size() < kPbkdf2MinSaltSize || salt.data() == nullptr) {
      VLOG_W("Security: rejected passphrase: salt must be >= ", kPbkdf2MinSaltSize, " bytes");
      return false;
    }

    if VUNLIKELY (iterations == 0U) {
      VLOG_W("Security: rejected passphrase: iterations must be > 0");
      return false;
    }

    out = derive_aes_key_pbkdf2(passphrase, salt.data(), salt.size(), iterations);

    if VUNLIKELY (out.size() != kAesKeySize) {
      VLOG_W("Security: PBKDF2 derivation failed");
      return false;
    }

    return true;
  }

  if VLIKELY (!key.empty()) {
    out = derive_aes_key_sha256(key);

    if VUNLIKELY (out.size() != kAesKeySize) {
      VLOG_W("Security: SHA-256 derivation failed");
      return false;
    }

    return true;
  }

  return false;
}

static bool install_symmetric_key(std::vector<SymmetricKeySlot>& keys, const std::string& key_id,
                                  const std::string& key, const std::string& passphrase, const Bytes& salt,
                                  uint32_t iterations, bool can_encrypt, bool can_decrypt) {
  const auto id = normalize_key_id(key_id);

  if VUNLIKELY (wire_key_id_size(id) > 0xFFU) {
    VLOG_W("Security: rejected symmetric key: key_id exceeds 255 wire bytes");
    return false;
  }

  Bytes derived;

  if (!derive_symmetric_slot_key(key, passphrase, salt, iterations, derived)) {
    return false;
  }

  for (auto& slot : keys) {
    if VLIKELY (slot.key_id != id) {
      continue;
    }

    VLOG_W("Security: symmetric key with key_id=", id, " overwrites earlier slot");

    if (!slot.key.empty()) {
      OPENSSL_cleanse(slot.key.data(), slot.key.size());
    }

    slot.key = std::move(derived);
    slot.can_encrypt = can_encrypt;
    slot.can_decrypt = can_decrypt;
    slot.peers.clear();

    return true;
  }

  SymmetricKeySlot slot;
  slot.key_id = id;
  slot.key = std::move(derived);
  slot.can_encrypt = can_encrypt;
  slot.can_decrypt = can_decrypt;
  keys.push_back(std::move(slot));

  return true;
}

static SymmetricKeySlot* find_encrypt_key(std::vector<SymmetricKeySlot>& keys,
                                          const std::string& active_key_id) noexcept {
  for (auto& slot : keys) {
    if VLIKELY (slot.key_id == active_key_id && slot.can_encrypt && slot.key.size() >= kAesKeySize &&
                slot.key.data() != nullptr) {
      return &slot;
    }
  }

  return nullptr;
}

static SymmetricKeySlot* find_decrypt_key(std::vector<SymmetricKeySlot>& keys, std::string_view key_id) noexcept {
  for (auto& slot : keys) {
    if VLIKELY (key_id_equal(slot.key_id, key_id) && slot.can_decrypt && slot.key.size() >= kAesKeySize &&
                slot.key.data() != nullptr) {
      return &slot;
    }
  }

  return nullptr;
}

static bool next_nonce(uint64_t& send_seq, uint64_t& sender_id, std::array<uint8_t, kAesNonceSize>& nonce_base,
                       bool& nonce_ready, uint64_t& seq, uint8_t* nonce) noexcept {
  if VUNLIKELY (nonce == nullptr) {
    return false;
  }

  if VUNLIKELY (!nonce_ready) {
    std::array<uint8_t, sizeof(uint64_t) + kAesNonceSize> seed{};

    if VUNLIKELY (RAND_bytes(seed.data(), static_cast<int>(seed.size())) != 1) {
      return false;
    }

    sender_id = read_u64_le(seed.data());

    if VUNLIKELY (sender_id == 0U) {
      sender_id = 1U;
    }

    std::memcpy(nonce_base.data(), seed.data() + sizeof(uint64_t), kAesNonceSize);
    nonce_ready = true;
  }

  if VUNLIKELY (send_seq == std::numeric_limits<uint64_t>::max()) {
    return false;
  }

  seq = ++send_seq;
  std::memcpy(nonce, nonce_base.data(), kAesNonceSize);

  for (size_t i = 0; i < sizeof(seq); ++i) {
    nonce[4U + i] ^= static_cast<uint8_t>((seq >> (i * 8U)) & 0xFFU);
  }

  return true;
}

static void cleanse_symmetric_keys(std::vector<SymmetricKeySlot>& keys) noexcept {
  for (auto& slot : keys) {
    if VLIKELY (!slot.key.empty() && slot.key.data() != nullptr) {
      OPENSSL_cleanse(slot.key.data(), slot.key.size());
    }
  }

  keys.clear();
}

static void cleanse_string(std::string& value) noexcept {
  if (!value.empty()) {
    OPENSSL_cleanse(value.data(), value.size());
    value.clear();
  }
}

static void cleanse_bytes(Bytes& value) noexcept {
  if (!value.empty() && value.data() != nullptr) {
    OPENSSL_cleanse(value.data(), value.size());
    value.clear();
  }
}

static void cleanse_config(Security::Config& config) noexcept {
  cleanse_string(config.key);
  cleanse_string(config.passphrase);
  cleanse_bytes(config.pbkdf2_salt);
  cleanse_string(config.public_key_pem);
  cleanse_string(config.private_key_pem);
  cleanse_string(config.advanced.signing_key_pem);
  cleanse_string(config.advanced.verify_key_pem);

  for (auto& slot : config.advanced.previous_keys) {
    cleanse_string(slot.key);
    cleanse_string(slot.passphrase);
    cleanse_bytes(slot.pbkdf2_salt);
  }

  config.advanced.previous_keys.clear();
  config.encrypt_callback = nullptr;
  config.decrypt_callback = nullptr;
}

#endif  // VLINK_ENABLE_SECURITY

//////////////////////////////////////////

// Security::Impl
struct Security::Impl final {  // NOLINT(clang-analyzer-optin.performance.Padding)
  mutable std::mutex mtx;
  Security::Config config;
  bool aad_context_valid{true};

#ifdef VLINK_ENABLE_SECURITY
  std::vector<SymmetricKeySlot> keys;
  bool active_key_id_valid{true};
  SymmetricKeySlot* encrypt_key{nullptr};
  uint64_t send_seq{0};
  uint64_t sender_id{0};
  std::array<uint8_t, kAesNonceSize> nonce_base{};
  bool nonce_ready{false};
  std::vector<PeerReplay> asym_peers;
  EvpPkeyPtr public_key;
  EvpPkeyPtr private_key;
  EvpPkeyPtr signing_key;
  EvpPkeyPtr verify_key;
#endif
};

// Security
Security::Security() : Security(Config{}) {}

Security::Security(const Config& cfg) : Security(Config{cfg}) {}

Security::Security(Config&& cfg) : impl_(std::make_unique<Impl>()) {
  impl_->config = std::move(cfg);

  if VUNLIKELY (static_cast<bool>(impl_->config.encrypt_callback) !=
                static_cast<bool>(impl_->config.decrypt_callback)) {
    VLOG_W(
        "Security: encrypt_callback and decrypt_callback must be installed as a pair; ignoring lone "
        "callback to avoid asymmetric encrypt/decrypt behaviour.");
    impl_->config.encrypt_callback = nullptr;
    impl_->config.decrypt_callback = nullptr;
  }

#ifdef VLINK_ENABLE_SECURITY
  impl_->config.advanced.key_id = normalize_key_id(impl_->config.advanced.key_id);
  impl_->config.advanced.replay_window = normalize_replay_window(impl_->config.advanced.replay_window);
  impl_->aad_context_valid = impl_->config.advanced.aad_context.size() <= 0xFFFFU;
  impl_->active_key_id_valid = wire_key_id_size(impl_->config.advanced.key_id) <= 0xFFU;

  if VUNLIKELY (!impl_->aad_context_valid) {
    VLOG_W("Security: rejected aad_context: context exceeds 65535 bytes");
  }

  if VUNLIKELY (!impl_->active_key_id_valid) {
    VLOG_W("Security: rejected active key_id: key_id exceeds 255 wire bytes");
  } else {
    (void)install_symmetric_key(impl_->keys, impl_->config.advanced.key_id, impl_->config.key, impl_->config.passphrase,
                                impl_->config.pbkdf2_salt, impl_->config.pbkdf2_iterations, true, true);
  }

  for (const auto& slot : impl_->config.advanced.previous_keys) {
    const auto previous_key_id = normalize_key_id(slot.key_id);

    if VUNLIKELY (previous_key_id == impl_->config.advanced.key_id) {
      VLOG_W("Security: rejected previous key: key_id duplicates the current outbound key_id");
      continue;
    }

    (void)install_symmetric_key(impl_->keys, slot.key_id, slot.key, slot.passphrase, slot.pbkdf2_salt,
                                slot.pbkdf2_iterations, false, true);
  }

  impl_->encrypt_key = find_encrypt_key(impl_->keys, impl_->config.advanced.key_id);

  if VUNLIKELY (!impl_->config.public_key_pem.empty()) {
    auto pkey = load_pubkey_from_pem(impl_->config.public_key_pem);

    if VUNLIKELY (!pkey) {
      VLOG_W("Security: rejected public_key_pem (parse failed, non-RSA, or <2048 bits)");
    } else {
      impl_->public_key = std::move(pkey);
    }
  }

  if VUNLIKELY (!impl_->config.private_key_pem.empty()) {
    auto pkey = load_privkey_from_pem(impl_->config.private_key_pem);

    if VUNLIKELY (!pkey) {
      VLOG_W("Security: rejected private_key_pem (parse failed, non-RSA, or <2048 bits)");
    } else {
      impl_->private_key = std::move(pkey);
    }
  }

  if VUNLIKELY (!impl_->config.advanced.signing_key_pem.empty()) {
    auto pkey = load_privkey_from_pem(impl_->config.advanced.signing_key_pem);

    if VUNLIKELY (!pkey) {
      VLOG_W("Security: rejected signing_key_pem (parse failed, non-RSA, or <2048 bits)");
    } else {
      impl_->signing_key = std::move(pkey);
    }
  }

  if VUNLIKELY (!impl_->config.advanced.verify_key_pem.empty()) {
    auto pkey = load_pubkey_from_pem(impl_->config.advanced.verify_key_pem);

    if VUNLIKELY (!pkey) {
      VLOG_W("Security: rejected verify_key_pem (parse failed, non-RSA, or <2048 bits)");
    } else {
      impl_->verify_key = std::move(pkey);
    }
  }
#else
  if VUNLIKELY (!impl_->config.key.empty() || !impl_->config.passphrase.empty() ||
                !impl_->config.public_key_pem.empty() || !impl_->config.private_key_pem.empty() ||
                !impl_->config.advanced.signing_key_pem.empty() || !impl_->config.advanced.verify_key_pem.empty()) {
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
  cleanse_symmetric_keys(impl_->keys);
  cleanse_config(impl_->config);
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
    cleanse_symmetric_keys(impl_->keys);
    cleanse_config(impl_->config);
#endif
  }

  impl_ = std::move(other.impl_);
  other.impl_ = std::make_unique<Impl>();

  return *this;
}

bool Security::is_configured() const noexcept {
  std::lock_guard lock(impl_->mtx);

#ifdef VLINK_ENABLE_SECURITY
  if VLIKELY (impl_->aad_context_valid) {
    for (const auto& slot : impl_->keys) {
      if VLIKELY (slot.key.size() >= kAesKeySize && slot.key.data() != nullptr &&
                  (slot.can_encrypt || slot.can_decrypt)) {
        return true;
      }
    }

    if VUNLIKELY ((impl_->public_key && impl_->active_key_id_valid) || impl_->private_key) {
      return true;
    }
  }
#endif

  if (impl_->config.encrypt_callback && impl_->config.decrypt_callback) {
    return true;
  }

  return false;
}

bool Security::can_encrypt() const noexcept {
  std::lock_guard lock(impl_->mtx);

  if (impl_->config.encrypt_callback && impl_->config.decrypt_callback) {
    return true;
  }

#ifdef VLINK_ENABLE_SECURITY
  if VUNLIKELY (!impl_->aad_context_valid) {
    return false;
  }

  if VLIKELY (impl_->encrypt_key != nullptr) {
    return true;
  }

  if VUNLIKELY (impl_->public_key && impl_->active_key_id_valid) {
    return true;
  }
#endif

  return false;
}

bool Security::can_decrypt() const noexcept {
  std::lock_guard lock(impl_->mtx);

  if (impl_->config.encrypt_callback && impl_->config.decrypt_callback) {
    return true;
  }

#ifdef VLINK_ENABLE_SECURITY
  if VUNLIKELY (!impl_->aad_context_valid) {
    return false;
  }

  for (const auto& slot : impl_->keys) {
    if VLIKELY (slot.can_decrypt && slot.key.size() >= kAesKeySize && slot.key.data() != nullptr) {
      return true;
    }
  }

  if VUNLIKELY (impl_->private_key) {
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

  if VUNLIKELY (impl_->config.encrypt_callback) {
    if VUNLIKELY (!impl_->config.encrypt_callback(in, out)) {
      out = Bytes{};
      return false;
    }

    return true;
  }

#ifdef VLINK_ENABLE_SECURITY
  if VUNLIKELY (!impl_->aad_context_valid) {
    VLOG_W("Security::encrypt aad_context exceeds 65535 bytes");
    return false;
  }

  if VUNLIKELY (in.size() > static_cast<size_t>(std::numeric_limits<int>::max())) {
    VLOG_W("Security::encrypt input exceeds INT_MAX bytes");
    return false;
  }

  if VUNLIKELY (impl_->public_key) {
    if VUNLIKELY (!impl_->active_key_id_valid) {
      VLOG_W("Security::encrypt active key_id exceeds 255 wire bytes");
      return false;
    }

    uint8_t session_key[kAesKeySize] = {};
    DigestScrub session_scrub{session_key, sizeof session_key};
    uint8_t nonce[kAesNonceSize] = {};
    uint64_t seq = 0U;

    if VUNLIKELY (RAND_bytes(session_key, sizeof session_key) != 1) {
      return false;
    }

    if VUNLIKELY (!next_nonce(impl_->send_seq, impl_->sender_id, impl_->nonce_base, impl_->nonce_ready, seq, nonce)) {
      return false;
    }

    Bytes wrapped;

    if VUNLIKELY (!rsa_oaep_encrypt(impl_->public_key.get(), session_key, sizeof session_key, wrapped)) {
      return false;
    }

    if VUNLIKELY (wrapped.size() > 0xFFFFU) {
      VLOG_W("Security::encrypt RSA-wrapped key exceeds 65535 bytes");
      return false;
    }

    Bytes header;

    if VUNLIKELY (!build_envelope_header(kEnvelopeModeAsymmetric, impl_->config.advanced.key_id, impl_->sender_id, seq,
                                         nonce, header)) {
      return false;
    }

    Bytes extra = Bytes::create(kRsaWrapLenFieldSize + wrapped.size());

    if VUNLIKELY (extra.data() == nullptr) {
      return false;
    }

    write_u16_le(extra.data(), static_cast<uint16_t>(wrapped.size()));
    std::memcpy(extra.data() + kRsaWrapLenFieldSize, wrapped.data(), wrapped.size());

    Bytes aad = build_aad(impl_->config.advanced.aad_context, header.data(), header.size(), extra.data(), extra.size());

    if VUNLIKELY (aad.empty()) {
      return false;
    }

    const size_t body_size = in.size() + kAesTagSize;
    Bytes body = Bytes::create(body_size);

    if VUNLIKELY (body.data() == nullptr) {
      return false;
    }

    uint8_t* body_cipher = body.data();
    uint8_t* body_tag = body_cipher + in.size();

    const bool gcm_ok =
        aes_gcm_encrypt(session_key, nonce, in.data(), in.size(), aad.data(), aad.size(), body_cipher, body_tag);

    if VUNLIKELY (!gcm_ok) {
      return false;
    }

    Bytes signature;

    if VUNLIKELY (impl_->signing_key) {
      Bytes signed_range = Bytes::create(aad.size() + body_size);
      if VUNLIKELY (signed_range.data() == nullptr) {
        return false;
      }

      std::memcpy(signed_range.data(), aad.data(), aad.size());
      std::memcpy(signed_range.data() + aad.size(), body.data(), body_size);

      if VUNLIKELY (!rsa_pss_sign(impl_->signing_key.get(), signed_range.data(), signed_range.size(), signature)) {
        VLOG_W("Security::encrypt RSA-PSS sign failed");
        return false;
      }

      if VUNLIKELY (signature.size() > 0xFFFFU) {
        VLOG_W("Security::encrypt signature exceeds 65535 bytes");
        return false;
      }
    }

    const size_t total = header.size() + kAsymHeaderFieldsSize + wrapped.size() + signature.size() + body_size;
    out = Bytes::create(total);

    if VUNLIKELY (out.data() == nullptr) {
      out = Bytes{};
      return false;
    }

    uint8_t* dst = out.data();
    const auto wrap_len_le = static_cast<uint16_t>(wrapped.size());
    const auto sig_len_le = static_cast<uint16_t>(signature.size());

    std::memcpy(dst, header.data(), header.size());
    dst += header.size();
    write_u16_le(dst, wrap_len_le);
    write_u16_le(dst + kRsaWrapLenFieldSize, sig_len_le);

    std::memcpy(dst + kAsymHeaderFieldsSize, wrapped.data(), wrapped.size());

    if VUNLIKELY (!signature.empty()) {
      std::memcpy(dst + kAsymHeaderFieldsSize + wrapped.size(), signature.data(), signature.size());
    }

    std::memcpy(dst + kAsymHeaderFieldsSize + wrapped.size() + signature.size(), body.data(), body_size);

    return true;
  }

  auto* key_slot = impl_->encrypt_key;

  if VUNLIKELY (key_slot == nullptr) {
    VLOG_W("Security::encrypt no symmetric key installed; construct with a non-empty Config");
    return false;
  }

  uint8_t nonce[kAesNonceSize] = {};
  uint64_t seq = 0U;

  if VUNLIKELY (!next_nonce(impl_->send_seq, impl_->sender_id, impl_->nonce_base, impl_->nonce_ready, seq, nonce)) {
    return false;
  }

  const size_t header_size = kEnvelopeFixedHeaderSize + wire_key_id_size(key_slot->key_id);
  const size_t total = header_size + in.size() + kAesTagSize;
  out = Bytes::create(total);

  if VUNLIKELY (out.data() == nullptr) {
    out = Bytes{};
    return false;
  }

  size_t written_header_size = 0U;

  if VUNLIKELY (!write_envelope_header(kEnvelopeModeSymmetric, key_slot->key_id, impl_->sender_id, seq, nonce,
                                       out.data(), out.size(), written_header_size) ||
                written_header_size != header_size) {
    out = Bytes{};
    return false;
  }

  uint8_t* cipher_dst = out.data() + header_size;
  uint8_t* tag_dst = cipher_dst + in.size();
  const AadParts aad{&impl_->config.advanced.aad_context, out.data(), header_size};

  if VUNLIKELY (!aes_gcm_encrypt_parts(key_slot->key.data(), nonce, in.data(), in.size(), aad, cipher_dst, tag_dst)) {
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

  if VUNLIKELY (impl_->config.decrypt_callback) {
    if VUNLIKELY (!impl_->config.decrypt_callback(in, out)) {
      out = Bytes{};
      return false;
    }

    return true;
  }

#ifdef VLINK_ENABLE_SECURITY
  if VUNLIKELY (!impl_->aad_context_valid) {
    VLOG_W("Security::decrypt aad_context exceeds 65535 bytes");
    return false;
  }

  if VUNLIKELY (in.size() > static_cast<size_t>(std::numeric_limits<int>::max())) {
    VLOG_W("Security::decrypt input exceeds INT_MAX bytes");
    return false;
  }

  EnvelopeHeader header;

  if VUNLIKELY (!parse_envelope_header(in, header) || header.flags != 0U || header.sender_id == 0U) {
    return false;
  }

  const uint8_t* src = in.data();

  if VUNLIKELY (header.mode == kEnvelopeModeAsymmetric) {
    if VUNLIKELY (!impl_->private_key) {
      VLOG_W("Security::decrypt no private key installed for asymmetric envelope");
      return false;
    }

    if VUNLIKELY (in.size() <= header.size + kAsymHeaderFieldsSize + kAesTagSize) {
      return false;
    }

    const uint8_t* fields_ptr = src + header.size;
    const auto wrap_len = read_u16_le(fields_ptr);
    const auto sig_len = read_u16_le(fields_ptr + kRsaWrapLenFieldSize);
    const size_t meta_size =
        header.size + kAsymHeaderFieldsSize + static_cast<size_t>(wrap_len) + static_cast<size_t>(sig_len);

    if VUNLIKELY (wrap_len == 0U || in.size() <= meta_size + kAesTagSize) {
      return false;
    }

    const uint8_t* wrapped_ptr = fields_ptr + kAsymHeaderFieldsSize;
    const uint8_t* sig_ptr = wrapped_ptr + wrap_len;
    const uint8_t* cipher = sig_ptr + sig_len;
    const size_t cipher_len = in.size() - meta_size - kAesTagSize;
    const uint8_t* tag = cipher + cipher_len;

    Bytes extra = Bytes::create(kRsaWrapLenFieldSize + wrap_len);

    if VUNLIKELY (extra.data() == nullptr) {
      return false;
    }

    write_u16_le(extra.data(), wrap_len);
    std::memcpy(extra.data() + kRsaWrapLenFieldSize, wrapped_ptr, wrap_len);

    Bytes aad = build_aad(impl_->config.advanced.aad_context, src, header.size, extra.data(), extra.size());

    if VUNLIKELY (aad.empty()) {
      return false;
    }

    if VUNLIKELY (impl_->verify_key) {
      if VUNLIKELY (sig_len == 0U) {
        VLOG_W("Security::decrypt verify_key set but message is unsigned");
        return false;
      }

      Bytes signed_range = Bytes::create(aad.size() + cipher_len + kAesTagSize);

      if VUNLIKELY (signed_range.data() == nullptr) {
        return false;
      }

      std::memcpy(signed_range.data(), aad.data(), aad.size());
      std::memcpy(signed_range.data() + aad.size(), cipher, cipher_len + kAesTagSize);

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

    Bytes plain = Bytes::create(cipher_len);

    if VUNLIKELY (plain.data() == nullptr) {
      OPENSSL_cleanse(session_key.data(), session_key.size());
      return false;
    }

    const bool ok = aes_gcm_decrypt(session_key.data(), header.nonce, cipher, cipher_len, aad.data(), aad.size(), tag,
                                    plain.data());
    OPENSSL_cleanse(session_key.data(), session_key.size());

    if VUNLIKELY (!ok) {
      OPENSSL_cleanse(plain.data(), plain.size());
      return false;
    }

    if VUNLIKELY (!accept_replay(get_peer_window(impl_->asym_peers, header.sender_id), header.seq,
                                 impl_->config.advanced.replay_window)) {
      OPENSSL_cleanse(plain.data(), plain.size());
      return false;
    }

    out = std::move(plain);

    return true;
  }

  if VLIKELY (header.mode == kEnvelopeModeSymmetric) {
    auto* key_slot = find_decrypt_key(impl_->keys, header.key_id);

    if VUNLIKELY (key_slot == nullptr) {
      VLOG_W("Security::decrypt no symmetric key installed for key_id=", normalize_key_id(header.key_id));
      return false;
    }

    if VUNLIKELY (in.size() <= header.size + kAesTagSize) {
      return false;
    }

    const size_t cipher_len = in.size() - header.size - kAesTagSize;
    const uint8_t* cipher = src + header.size;
    const uint8_t* tag = cipher + cipher_len;
    const AadParts aad{&impl_->config.advanced.aad_context, src, header.size};

    Bytes plain = Bytes::create(cipher_len);

    if VUNLIKELY (plain.data() == nullptr) {
      return false;
    }

    if VUNLIKELY (!aes_gcm_decrypt_parts(key_slot->key.data(), header.nonce, cipher, cipher_len, aad, tag,
                                         plain.data())) {
      OPENSSL_cleanse(plain.data(), plain.size());
      return false;
    }

    if VUNLIKELY (!accept_replay(get_peer_window(key_slot->peers, header.sender_id), header.seq,
                                 impl_->config.advanced.replay_window)) {
      OPENSSL_cleanse(plain.data(), plain.size());
      return false;
    }

    out = std::move(plain);

    return true;
  }

  return false;
#else
  (void)in;
  (void)out;

  VLOG_W("Security: Function [decrypt] is not supported (VLINK_ENABLE_SECURITY not enabled).");

  return false;
#endif
}

}  // namespace vlink
