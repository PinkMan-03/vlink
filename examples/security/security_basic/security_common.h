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

#ifndef EXAMPLES_SECURITY_SECURITY_BASIC_SECURITY_COMMON_H_
#define EXAMPLES_SECURITY_SECURITY_BASIC_SECURITY_COMMON_H_

#include <iostream>
#include <string>

namespace security_common {

// Algorithm metadata for the construction-only Security::Config API.
// All endpoints must construct Security with matching Config fields; there is
// no built-in default key.  Inject your own key via:
//   vlink::Security::Config cfg;
//   cfg.key = "...";  // or cfg.passphrase + cfg.pbkdf2_salt
//   vlink::SecurityPublisher<T> pub(url, cfg);  // cfg passed as the 2nd ctor arg
constexpr const char* kDefaultAlgorithm = "AES-128-GCM";
constexpr const char* kAsymmetricWrap = "RSA-OAEP-SHA256";
constexpr const char* kSignatureScheme = "RSA-PSS-SHA256";
constexpr const char* kPassphraseKdf = "PBKDF2-HMAC-SHA256";

// Print a summary of VLink security primitives.
inline void print_security_defaults() {
  std::cout << "  VLink Security Primitives:" << std::endl;
  std::cout << "    AEAD:       " << kDefaultAlgorithm << " (12-byte nonce, 16-byte tag)" << std::endl;
  std::cout << "    RSA wrap:   " << kAsymmetricWrap << " (>=2048-bit RSA)" << std::endl;
  std::cout << "    Signature:  " << kSignatureScheme << " (optional sender auth)" << std::endl;
  std::cout << "    KDF:        " << kPassphraseKdf << " (passphrase -> 16-byte AES key)" << std::endl;
  std::cout << "    Configure:  vlink::Security::Config{ ... } as the 2nd ctor arg of SecurityXxx" << std::endl;
}

// Print a summary of supported transports for security.
inline void print_supported_transports() {
  std::cout << "  Supported transports for security:" << std::endl;
  std::cout << "    shm://, shm2://, zenoh://, mqtt://, fdbus://" << std::endl;
  std::cout << "  NOT supported:" << std::endl;
  std::cout << "    intra:// (messages stay in-process)" << std::endl;
}

}  // namespace security_common

#endif  // EXAMPLES_SECURITY_SECURITY_BASIC_SECURITY_COMMON_H_
