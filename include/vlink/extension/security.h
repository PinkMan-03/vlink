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
 * @brief AES-128-CBC encryption/decryption with optional custom callback override.
 *
 * @details
 * @c Security provides message-level encryption and decryption for VLink transports.
 * When compiled with @c VLINK_ENABLE_SECURITY (requires OpenSSL), it uses
 * AES-128-CBC via the EVP API with PKCS7 padding.  The default AES key is
 * @c "vlink" and the IV is @c "thun.lu@zohomail.cn" (OpenSSL uses the first
 * 16 bytes of each).
 *
 * Custom crypto implementations can replace the built-in algorithm by registering
 * a pair of @c Callback functions via @c set_callbacks().  When custom callbacks
 * are installed, the AES implementation is bypassed entirely.
 *
 * @par Compile requirements
 * - Built-in AES: link with @c -lssl @c -lcrypto and define @c VLINK_ENABLE_SECURITY.
 * - Custom callback: no additional dependencies.
 *
 * @par Typical usage
 * @code
 * vlink::Security security;
 * security.set_key("my_secret_key");
 *
 * vlink::Bytes cipher;
 * security.encrypt(plain_bytes, cipher);
 *
 * vlink::Bytes recovered;
 * security.decrypt(cipher, recovered);
 * @endcode
 *
 * @note
 * - All public methods are thread-safe (protected by an internal mutex).
 * - @c encrypt() and @c decrypt() return @c true on success, @c false on failure.
 * - If @c VLINK_ENABLE_SECURITY is not defined, @c encrypt() and @c decrypt()
 *   log a warning and return @c false.
 * - Passing an empty @c Bytes to @c encrypt() or @c decrypt() is a no-op that
 *   returns @c true immediately.
 */

#pragma once

#include <memory>
#include <string>

#include "../base/bytes.h"
#include "../base/functional.h"
#include "../base/macros.h"

namespace vlink {

/**
 * @class Security
 * @brief Thread-safe AES-128-CBC encryption/decryption utility with custom callback support.
 *
 * @details
 * Each @c Security instance holds its own EVP context and key material.
 * Copy and assignment are disabled; instances are intended to be owned by a single
 * transport endpoint.
 */
class VLINK_EXPORT Security final {
 public:
  /**
   * @brief Callback type for custom encryption or decryption.
   *
   * @details
   * Called with the input @c Bytes; the implementation must write the result into
   * @p out and return @c true on success.  When a custom callback is installed via
   * @c set_callbacks(), the built-in AES implementation is bypassed.
   */
  using Callback = MoveFunction<bool(const Bytes& in, Bytes& out)>;

  /**
   * @brief Constructs a @c Security object with the default AES key and IV.
   *
   * @details
   * Initialises an OpenSSL EVP context with AES-128-CBC and PKCS7 padding
   * when @c VLINK_ENABLE_SECURITY is defined.  Otherwise logs a warning.
   */
  Security();

  /**
   * @brief Destroys the @c Security object and frees the EVP context.
   */
  ~Security();

  /**
   * @brief Sets the AES encryption key.
   *
   * @details
   * The key string is stored as @c Bytes.  If @p key is empty, the default
   * built-in key @c "vlink" is restored.  The key length is not validated here;
   * OpenSSL will use the first 16 bytes for AES-128.
   *
   * @param key  Key string.  Pass an empty string to restore the default.
   */
  void set_key(const std::string& key);

  /**
   * @brief Installs custom encrypt and decrypt callbacks, bypassing the built-in AES.
   *
   * @details
   * When both callbacks are set, @c encrypt() delegates to @p encrypt_callback and
   * @c decrypt() delegates to @p decrypt_callback.  Set to @c nullptr to fall back
   * to the built-in AES implementation.
   *
   * @param encrypt_callback  Custom encryption function.
   * @param decrypt_callback  Custom decryption function.
   */
  void set_callbacks(Callback&& encrypt_callback, Callback&& decrypt_callback);

  /**
   * @brief Encrypts @p in and writes the ciphertext into @p out.
   *
   * @details
   * Uses the custom encrypt callback if installed, otherwise uses AES-128-CBC.
   * If @p in is empty, @p out is unchanged and @c true is returned immediately.
   *
   * @param in   Plaintext bytes.
   * @param out  Output buffer for ciphertext.  Overwritten on success.
   * @return @c true on success; @c false if encryption fails or the feature is disabled.
   */
  bool encrypt(const Bytes& in, Bytes& out);

  /**
   * @brief Decrypts @p in and writes the plaintext into @p out.
   *
   * @details
   * Uses the custom decrypt callback if installed, otherwise uses AES-128-CBC.
   * If @p in is empty, @p out is unchanged and @c true is returned immediately.
   *
   * @param in   Ciphertext bytes.
   * @param out  Output buffer for plaintext.  Overwritten on success.
   * @return @c true on success; @c false if decryption fails or the feature is disabled.
   */
  bool decrypt(const Bytes& in, Bytes& out);

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;

  VLINK_DISALLOW_COPY_AND_ASSIGN(Security)
};

}  // namespace vlink
