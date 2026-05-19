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

/**
 * @file security.h
 * @brief Authenticated message-level encryption (AEAD) with optional RSA hybrid handshake.
 *
 * @details
 * @c Security provides per-message encryption for VLink transports.  It operates at the
 * application layer and is independent of, and complementary to, the transport-layer TLS
 * configured through @c SslOptions.  When compiled with @c VLINK_ENABLE_SECURITY (requires
 * OpenSSL), three modes are available and selected automatically per call:
 *
 * | Mode       | Sender state                       | Receiver state                       |
 * | ---------- | ---------------------------------- | ------------------------------------ |
 * | Custom     | @c encrypt_callback installed      | @c decrypt_callback installed        |
 * | Asymmetric | @c public_key_pem installed        | @c private_key_pem installed         |
 * | Symmetric  | @c key or @c passphrase installed  | Same @c key or @c passphrase + salt  |
 *
 * Cryptographic primitives:
 * - AEAD: AES-128-GCM with a 12-byte nonce, authenticated envelope header, and 16-byte tag.
 * - Envelope binding: version, mode, flags, sender id, sequence, nonce, and @c aad_context
 *   are authenticated as AAD; receivers reject replayed sequence numbers within @c replay_window.
 * - Asymmetric key wrap: RSA-OAEP-SHA256 wrapping a fresh 16-byte AES session key per message.
 * - Optional sender authentication: RSA-PSS-SHA256 over the AAD-bound envelope and ciphertext/tag.
 * - Symmetric key derivation: SHA-256 truncation (from @c key) or PBKDF2-HMAC-SHA256
 *   (from @c passphrase + @c pbkdf2_salt + @c pbkdf2_iterations).
 *
 * Configuration is one-shot at construction time via @c Security::Config.  There are no
 * runtime setters; rebuild the @c Security instance to change settings.
 *
 * @par Example (symmetric passphrase)
 * @code
 * vlink::Security::Config cfg;
 * cfg.passphrase = "correct horse battery staple";
 * cfg.pbkdf2_salt = shared_salt;
 * vlink::Security sec(cfg);
 * @endcode
 *
 * @par Example (asymmetric with sender authentication)
 * @code
 * vlink::Security::Config sender;
 * sender.public_key_pem = peer_pub_pem;
 * sender.advanced.signing_key_pem = own_priv_pem;
 * vlink::Security sender_sec(sender);
 *
 * vlink::Security::Config receiver;
 * receiver.private_key_pem = own_priv_pem;
 * receiver.advanced.verify_key_pem = peer_pub_pem;
 * vlink::Security receiver_sec(receiver);
 * @endcode
 *
 * @note
 * - All public methods are thread-safe (internal mutex).
 * - @c encrypt() and @c decrypt() return @c true on success.  An empty input @b fails
 *   with @c false and an empty output; every built-in ciphertext carries an envelope,
 *   a 16-byte tag, and at least one byte of authenticated plaintext.
 * - When @c VLINK_ENABLE_SECURITY is undefined, only the custom-callback path works.
 */

#pragma once

#include <cstdint>
#include <memory>
#include <string>

#include "../base/bytes.h"
#include "../base/functional.h"
#include "../base/macros.h"

namespace vlink {

/**
 * @class Security
 * @brief Thread-safe authenticated encryption with symmetric, asymmetric, and custom modes.
 *
 * @details
 * Each instance is owned by exactly one transport endpoint.  Configuration is supplied at
 * construction time; copy and assignment are disabled.
 */
class VLINK_EXPORT Security final {
 public:
  /**
   * @brief Plain-bytes encrypt or decrypt callback.
   *
   * @details
   * When both @c Config::encrypt_callback and @c Config::decrypt_callback are installed
   * the built-in AEAD path is bypassed entirely.  Implementations must write the result
   * into @p out and return @c true on success.  @c Function is copyable so a @c Config
   * value can be passed by const reference.
   */
  using Callback = Function<bool(const Bytes& in, Bytes& out)>;

  /**
   * @struct Config
   * @brief Aggregate of every parameter accepted by the @c Security constructor.
   *
   * @details
   * Fields are processed independently.  Empty PEM strings, empty @c key, empty @c passphrase,
   * and null callbacks mean "do not install this slot"; non-empty values are validated and
   * installed.  Validation failures (bad PEM, weak RSA key, missing salt, etc.) are logged and the
   * corresponding slot is left empty.
   *
   * @par Mode selection
   * - @c encrypt_callback and @c decrypt_callback override everything else when present.
   * - When @c public_key_pem is installed, outbound messages take the asymmetric path.
   * - When @c private_key_pem is installed, inbound messages take the asymmetric path.
   * - Otherwise the symmetric path is used with the derived AES-128 key.
   *
   * @par Symmetric key sources
   * Provide either @c key (raw seed, hashed via SHA-256 truncation) or @c passphrase
   * (low-entropy, normalised via PBKDF2-HMAC-SHA256 with @c pbkdf2_salt and
   * @c pbkdf2_iterations).  When both are empty no symmetric key is installed.
   *
   * @par RSA constraints
   * All four PEM fields require RSA keys of at least 2048 bits.
   */
  struct Config final {
    struct Advanced final {
      std::string aad_context;        ///< Application/channel context (<=65535 bytes) bound into AEAD.
      uint32_t replay_window{4096U};  ///< Sliding replay window size in messages; 0 disables checks.
      std::string signing_key_pem;    ///< Local RSA private key (PEM) for RSA-PSS signing.
      std::string verify_key_pem;     ///< Peer's RSA public key (PEM) for RSA-PSS verification.
    };

    std::string key;                      ///< Raw symmetric seed; SHA-256 truncated to 16 bytes.
    std::string passphrase;               ///< Low-entropy passphrase fed into PBKDF2-HMAC-SHA256.
    Bytes pbkdf2_salt;                    ///< Per-deployment salt (>=16 bytes), shared out-of-band.
    uint32_t pbkdf2_iterations{200000U};  ///< PBKDF2 iteration count.
    std::string public_key_pem;           ///< Peer's RSA public key (PEM) for outbound encryption.
    std::string private_key_pem;          ///< Local RSA private key (PEM) for inbound decryption.
    Callback encrypt_callback;            ///< Custom encrypt; bypasses AEAD.
    Callback decrypt_callback;            ///< Custom decrypt; paired with @c encrypt_callback.
    Advanced advanced;                    ///< Low-frequency knobs: AAD, replay, and signing.

    Config() = default;
  };

  /**
   * @brief Constructs an empty @c Security instance.
   *
   * @details
   * Equivalent to @c Security(Config{}).  @c encrypt() and @c decrypt() return @c false
   * until a fresh @c Security is constructed with a non-empty @c Config.
   */
  Security();

  /**
   * @brief Constructs a @c Security instance from @p cfg.
   *
   * @details
   * Each non-empty field of @p cfg is validated and installed under an internal mutex
   * during construction.  Validation failures are logged and the offending slot is left
   * empty.  When @c VLINK_ENABLE_SECURITY is undefined only the callback slots are honoured
   * and a warning is logged for the symmetric / asymmetric fields.
   *
   * @param cfg  Configuration aggregate.
   */
  explicit Security(const Config& cfg);

  /**
   * @brief Constructs a @c Security instance by moving @p cfg into the implementation.
   *
   * @details
   * This overload avoids copying callback targets and PEM / key strings when the
   * caller no longer needs the configuration.  The stored config is normalised
   * during construction (for example AAD context validation and replay-window bounds).
   *
   * @param cfg  Configuration aggregate to consume.
   */
  explicit Security(Config&& cfg);

  /**
   * @brief Destroys the instance and zeroises the symmetric key material in place.
   */
  ~Security();

  /**
   * @brief Move-constructs from another instance.  The source is left empty
   * (equivalent to a default-constructed @c Security).
   */
  Security(Security&&) noexcept;

  /**
   * @brief Move-assigns from another instance.  The source is left empty.
   */
  Security& operator=(Security&&) noexcept;

  /**
   * @brief Encrypts @p in into @p out using the currently active mode.
   *
   * @details
   * Mode selection order: custom callback > asymmetric (public key present) > symmetric.
   * Empty input fails with @c false (AEAD requires at least one byte of plaintext or a
   * non-empty AAD).  Inputs larger than @c INT_MAX bytes are rejected.
   *
   * @param in   Plaintext bytes.
   * @param out  Ciphertext output buffer.  Overwritten on success; set to empty on failure.
   * @return @c true on success.
   */
  bool encrypt(const Bytes& in, Bytes& out);

  /**
   * @brief Decrypts @p in into @p out using the currently active mode.
   *
   * @details
   * Mode selection order: custom callback > asymmetric (private key present) > symmetric.
   * Empty input fails with @c false (a valid built-in ciphertext carries an envelope,
   * at least one ciphertext byte, and a 16-byte tag).  Authentication
   * failures (tampered ciphertext, wrong key, missing or invalid RSA-PSS signature
   * when @c advanced.verify_key_pem is set) also cause @c decrypt() to return @c false.
   *
   * @param in   Ciphertext bytes produced by a peer's @c encrypt().
   * @param out  Plaintext output buffer.  Overwritten on success; set to empty on failure.
   * @return @c true on success.
   */
  bool decrypt(const Bytes& in, Bytes& out);

  /**
   * @brief Returns @c true if at least one usable cryptographic slot is installed.
   *
   * @details
   * Returns @c true when any of the following is present:
   *  - Both @c encrypt_callback and @c decrypt_callback (custom path), OR
   *  - A symmetric AES-128 key (derived from @c Config::key or PBKDF2 passphrase), OR
   *  - A peer @c public_key_pem (outbound asymmetric encrypt path), OR
   *  - A local @c private_key_pem (inbound asymmetric decrypt path).
   *
   * Returns @c false when every field of @p Config was empty or every non-empty field
   * failed validation (bad PEM, weak RSA, short salt, etc.).  In that state
   * @c encrypt() and @c decrypt() always return @c false.
   *
   * @note This is the role-agnostic check.  Sender nodes (Publisher / Client /
   * Setter) should additionally verify @c can_encrypt(), and receiver nodes
   * (Subscriber / Server / Getter) should additionally verify @c can_decrypt()
   * to catch direction-specific RSA misconfigurations (e.g. a subscriber
   * supplied only @c public_key_pem, which is unusable for decryption).
   */
  [[nodiscard]] bool is_configured() const noexcept;

  /**
   * @brief Returns @c true if a slot capable of @c encrypt() is installed.
   *
   * @details
   * Capability requirements:
   *  - Both @c encrypt_callback and @c decrypt_callback (custom path), OR
   *  - A symmetric AES-128 key, OR
   *  - A peer @c public_key_pem (RSA-OAEP wraps a fresh session key).
   *
   * Sender-side nodes (Publisher / Client / Setter) require this; mere
   * @c private_key_pem is insufficient for outbound encryption.
   */
  [[nodiscard]] bool can_encrypt() const noexcept;

  /**
   * @brief Returns @c true if a slot capable of @c decrypt() is installed.
   *
   * @details
   * Capability requirements:
   *  - Both @c encrypt_callback and @c decrypt_callback (custom path), OR
   *  - A symmetric AES-128 key, OR
   *  - A local @c private_key_pem (RSA-OAEP unwraps the session key).
   *
   * Receiver-side nodes (Subscriber / Server / Getter) require this; mere
   * @c public_key_pem is insufficient for inbound decryption.
   */
  [[nodiscard]] bool can_decrypt() const noexcept;

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;

  VLINK_DISALLOW_COPY_AND_ASSIGN(Security)
};

}  // namespace vlink
