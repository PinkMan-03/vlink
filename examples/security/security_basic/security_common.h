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

// Algorithm name only -- the actual default key/IV are not exposed by the
// example to avoid leaking the demonstration values.  Use `set_security_key()`
// to inject your own key in any real deployment.
constexpr const char* kDefaultAlgorithm = "AES-128-CBC";

// Print a summary of VLink security defaults.
inline void print_security_defaults() {
  std::cout << "  VLink Security Defaults:" << std::endl;
  std::cout << "    Algorithm: " << kDefaultAlgorithm << std::endl;
  std::cout << "    Key:       <built-in 16-byte default; replace via set_security_key()>" << std::endl;
  std::cout << "    IV:        <built-in 16-byte default; replace by paired key rotation>" << std::endl;
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
