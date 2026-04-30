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

#pragma once

/**
 * @file encoding_demo.h
 * @brief Demonstrates Bytes encoding operations: base64, hex, user-input parsing,
 *        reverse, resize/reserve/shrink, and stream output.
 */

#include <vlink/base/bytes.h>
#include <vlink/base/logger.h>

#include <iostream>
#include <string>

namespace encoding_demo {

// Demonstrate Base64 encode / decode.
inline void demo_base64() {
  VLOG_I("--- 2. Base64 ---");
  auto src = vlink::Bytes::from_string("Hello, VLink Base64!");
  std::string encoded = vlink::Bytes::encode_to_base64(src);
  VLOG_I("Base64 encoded: ", encoded);

  auto decoded = vlink::Bytes::decode_from_base64(encoded);
  VLOG_I("Base64 decoded: \"", decoded.to_string(), "\"");
  VLOG_I("Round-trip match: ", (decoded == src));
}

// Demonstrate hex string conversion.
inline void demo_hex() {
  VLOG_I("--- 4. Hex string ---");
  vlink::Bytes bytes{0xDE, 0xAD, 0xBE, 0xEF};
  std::string hex = vlink::Bytes::convert_to_hex_str(bytes.data(), bytes.size());
  VLOG_I("Hex: ", hex);
}

// Demonstrate from_user_input: parse hex or binary string.
inline void demo_user_input() {
  VLOG_I("--- 5. from_user_input ---");
  bool ok = false;

  auto hex_bytes = vlink::Bytes::from_user_input("0x48656C6C6F", &ok);
  VLOG_I("Hex input ok: ", ok, " result: \"", hex_bytes.to_string(), "\"");

  auto bad = vlink::Bytes::from_user_input("not_hex", &ok);
  VLOG_I("Invalid input ok: ", ok, " empty: ", bad.empty());
}

// Demonstrate reverse_order.
inline void demo_reverse() {
  VLOG_I("--- 6. reverse_order ---");
  vlink::Bytes original{0x01, 0x02, 0x03, 0x04};
  auto reversed = vlink::Bytes::reverse_order(original);

  VLOG_I("Original: ", vlink::Bytes::convert_to_hex_str(original.data(), original.size()));
  VLOG_I("Reversed: ", vlink::Bytes::convert_to_hex_str(reversed.data(), reversed.size()));
}

// Demonstrate resize / reserve / shrink_to.
inline void demo_resize() {
  VLOG_I("--- 7. resize / reserve / shrink_to ---");
  auto buf = vlink::Bytes::create(20);
  VLOG_I("Initial: size=", buf.size(), " cap=", buf.capacity());

  bool ok = buf.reserve(500);
  VLOG_I("reserve(500): ok=", ok, " size=", buf.size(), " cap=", buf.capacity());

  ok = buf.resize(300);
  VLOG_I("resize(300): ok=", ok, " size=", buf.size());

  ok = buf.shrink_to(100);
  VLOG_I("shrink_to(100): ok=", ok, " size=", buf.size());

  ok = buf.shrink_to(200);
  VLOG_I("shrink_to(200) [too large]: ok=", ok);
}

// Demonstrate stream output operator.
inline void demo_stream_output() {
  VLOG_I("--- 9. Stream output ---");
  vlink::Bytes bytes{0xAA, 0xBB, 0xCC};
  std::cout << "Bytes via operator<<: " << bytes << std::endl;
}

}  // namespace encoding_demo
