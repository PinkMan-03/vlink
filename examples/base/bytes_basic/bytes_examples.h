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
 * @file bytes_examples.h
 * @brief Helper functions demonstrating Bytes basic operations.
 *
 * Each function encapsulates a self-contained demo section that can be
 * called from main().  Splitting them into a header improves readability
 * and makes individual sections reusable as copy-paste snippets.
 */

#include <vlink/base/bytes.h>
#include <vlink/base/logger.h>

#include <algorithm>
#include <cstring>
#include <numeric>
#include <string>
#include <vector>

namespace bytes_examples {

// Print Bytes metadata in a consistent format.
inline void print_info(const char* label, const vlink::Bytes& b) {
  VLOG_I(label, ": size=", b.size(), " capacity=", b.capacity(), " offset=", static_cast<int>(b.offset()),
         " is_owner=", b.is_owner(), " empty=", b.empty());
}

// Demonstrate SBO path: create() with size <= 96 bytes.
// When size <= Bytes::stack_size() (96), data lives in the inline
// stack_data_ array.  No heap allocation occurs.
inline void demo_sbo_path() {
  VLOG_I("--- 1. SBO path (size <= 96) ---");
  auto sbo = vlink::Bytes::create(64);
  print_info("SBO Bytes(64)", sbo);

  for (size_t i = 0; i < sbo.size(); ++i) {
    sbo[i] = static_cast<uint8_t>(i & 0xFF);
  }
  VLOG_I("First byte: ", static_cast<int>(sbo[0]), ", last byte: ", static_cast<int>(sbo[63]));
  VLOG_I("stack_size() = ", static_cast<int>(vlink::Bytes::stack_size()));
}

// Demonstrate heap path: create() with size > 96 bytes.
// Larger buffers are allocated from the memory pool or system heap.
inline void demo_heap_path() {
  VLOG_I("--- 2. Heap path (size > 96) ---");
  auto heap = vlink::Bytes::create(256);
  print_info("Heap Bytes(256)", heap);

  std::memset(heap.data(), 0xAB, heap.size());
  VLOG_I("First byte: 0x", std::hex, static_cast<int>(heap[0]));
}

// Demonstrate offset reservation for transport header prepending.
inline void demo_offset() {
  VLOG_I("--- 3. Create with offset ---");
  uint8_t offset = 8;
  auto buf = vlink::Bytes::create(100, offset);
  print_info("Bytes(100, offset=8)", buf);

  VLOG_I("real_data() + offset == data(): ", (buf.real_data() + buf.offset() == buf.data()));
  VLOG_I("real_size() = ", buf.real_size(), " (size + offset = ", buf.size(), " + ", static_cast<int>(buf.offset()),
         ")");
}

// Demonstrate from_string() deep copy.
inline void demo_from_string() {
  VLOG_I("--- 4. from_string ---");
  auto bytes = vlink::Bytes::from_string("Hello, VLink Bytes!");
  print_info("from_string", bytes);
  VLOG_I("to_string(): \"", bytes.to_string(), "\"");
  VLOG_I("to_string_view(): \"", bytes.to_string_view(), "\"");
}

// Demonstrate construction from std::vector<uint8_t>.
inline void demo_from_vector() {
  VLOG_I("--- 5. From vector ---");
  std::vector<uint8_t> vec = {0x01, 0x02, 0x03, 0x04, 0x05};
  vlink::Bytes bytes(vec);
  print_info("from vector", bytes);
  VLOG_I("bytes == vec: ", (bytes == vec));
}

// Demonstrate construction from initializer_list.
inline void demo_initializer_list() {
  VLOG_I("--- 6. From initializer_list ---");
  vlink::Bytes bytes{0xCA, 0xFE, 0xBA, 0xBE};
  print_info("initializer_list", bytes);
  VLOG_I("bytes[0]=0x", std::hex, static_cast<int>(bytes[0]), " bytes[3]=0x", static_cast<int>(bytes[3]));
}

// Demonstrate range-for iteration.
inline void demo_range_for() {
  VLOG_I("--- 7. Range-for iteration ---");
  vlink::Bytes bytes{10, 20, 30, 40, 50};
  std::string values;
  for (uint8_t b : bytes) {
    if (!values.empty()) {
      values += ", ";
    }

    values += std::to_string(b);
  }
  VLOG_I("Range-for: [", values, "]");
}

// Demonstrate to_raw_data() copying usable bytes into a new vector.
inline void demo_to_raw_data() {
  VLOG_I("--- 8. to_raw_data ---");
  vlink::Bytes bytes{1, 2, 3};
  auto raw = bytes.to_raw_data();
  VLOG_I("to_raw_data size: ", raw.size(), " values: ", static_cast<int>(raw[0]), ",", static_cast<int>(raw[1]), ",",
         static_cast<int>(raw[2]));
}

// Demonstrate empty() and clear().
inline void demo_empty_clear() {
  VLOG_I("--- 9. empty() and clear() ---");
  vlink::Bytes bytes;
  VLOG_I("Default-constructed: empty=", bytes.empty());

  bytes = vlink::Bytes::create(32);
  VLOG_I("After create(32): empty=", bytes.empty(), " size=", bytes.size());

  bytes.clear();
  VLOG_I("After clear(): empty=", bytes.empty(), " size=", bytes.size());
}

// Demonstrate equality comparison.
inline void demo_equality() {
  VLOG_I("--- 10. Equality comparison ---");
  auto a = vlink::Bytes::from_string("test");
  auto b = vlink::Bytes::from_string("test");
  auto c = vlink::Bytes::from_string("other");

  VLOG_I("a == b: ", (a == b));
  VLOG_I("a != c: ", (a != c));
}

// Demonstrate resize, reserve, shrink_to.
inline void demo_resize() {
  VLOG_I("--- 11. resize / reserve / shrink_to ---");
  auto buf = vlink::Bytes::create(10);
  VLOG_I("Initial: size=", buf.size(), " capacity=", buf.capacity());

  bool ret = false;

  ret = buf.reserve(200);

  if (ret) {
    VLOG_I("After reserve(200): size=", buf.size(), " capacity=", buf.capacity());
  }

  ret = buf.resize(150);

  if (ret) {
    VLOG_I("After resize(150): size=", buf.size());
  }

  ret = buf.shrink_to(50);

  if (ret) {
    VLOG_I("After shrink_to(50): size=", buf.size());
  }
}

// Demonstrate endianness check.
inline void demo_endianness() {
  VLOG_I("--- 12. Endianness ---");
  VLOG_I("is_little_endian: ", vlink::Bytes::is_little_endian());
  VLOG_I("is_big_endian: ", vlink::Bytes::is_big_endian());
}

}  // namespace bytes_examples
