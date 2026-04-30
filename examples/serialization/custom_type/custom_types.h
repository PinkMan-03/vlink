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

#include <vlink/vlink.h>

#include <cstring>
#include <string>
#include <type_traits>

/// A 3D vector with custom binary encoding (12 bytes total).
/// Detected as kCustomType because it provides Bytes operator>> and operator<<.
struct Vec3 {
  float x = 0.0f;  // X component
  float y = 0.0f;  // Y component
  float z = 0.0f;  // Z component

  // Serialize: write the three floats directly into the Bytes buffer
  void operator>>(vlink::Bytes& out) const {  // NOLINT(google-runtime-operator)
    out = vlink::Bytes::create(sizeof(float) * 3);
    std::memcpy(out.data(), &x, sizeof(float));
    std::memcpy(out.data() + sizeof(float), &y, sizeof(float));
    std::memcpy(out.data() + sizeof(float) * 2, &z, sizeof(float));
  }

  // Deserialize: read three floats from the Bytes buffer
  void operator<<(const vlink::Bytes& in) {
    if (in.size() >= sizeof(float) * 3) {
      std::memcpy(&x, in.data(), sizeof(float));
      std::memcpy(&y, in.data() + sizeof(float), sizeof(float));
      std::memcpy(&z, in.data() + sizeof(float) * 2, sizeof(float));
    }
  }
};

// Verify that Vec3 is detected as kCustomType (NOT kStandardType even though it is POD-like)
static_assert(vlink::Serializer::get_type_of<Vec3>() == vlink::Serializer::kCustomType,
              "Vec3 must be detected as kCustomType");

/// A named message with a variable-length string and a fixed-size payload.
/// Wire format: [4 bytes: name_len] [name_len bytes: name] [4 bytes: code] [8 bytes: value]
struct NamedValue {
  std::string name;    // Variable-length name (length-prefixed on wire)
  int32_t code = 0;    // Integer code
  double value = 0.0;  // Floating-point value

  // Serialize: encode variable-length name with a length prefix
  void operator>>(vlink::Bytes& out) const {  // NOLINT(google-runtime-operator)
    uint32_t name_len = static_cast<uint32_t>(name.size());
    size_t total = sizeof(uint32_t) + name_len + sizeof(int32_t) + sizeof(double);
    out = vlink::Bytes::create(total);

    uint8_t* ptr = out.data();

    // Write name length (4 bytes)
    std::memcpy(ptr, &name_len, sizeof(uint32_t));
    ptr += sizeof(uint32_t);

    // Write name content
    if (name_len > 0) {
      std::memcpy(ptr, name.data(), name_len);
      ptr += name_len;
    }

    // Write code (4 bytes)
    std::memcpy(ptr, &code, sizeof(int32_t));
    ptr += sizeof(int32_t);

    // Write value (8 bytes)
    std::memcpy(ptr, &value, sizeof(double));
  }

  // Deserialize: read length-prefixed name, then fixed-size fields
  void operator<<(const vlink::Bytes& in) {
    if (in.size() < sizeof(uint32_t)) return;

    const uint8_t* ptr = in.data();

    // Read name length
    uint32_t name_len = 0;
    std::memcpy(&name_len, ptr, sizeof(uint32_t));
    ptr += sizeof(uint32_t);

    // Read name content
    if (in.size() < sizeof(uint32_t) + name_len + sizeof(int32_t) + sizeof(double)) return;
    name.assign(reinterpret_cast<const char*>(ptr), name_len);
    ptr += name_len;

    // Read code
    std::memcpy(&code, ptr, sizeof(int32_t));
    ptr += sizeof(int32_t);

    // Read value
    std::memcpy(&value, ptr, sizeof(double));
  }
};

// NamedValue has std::string, so it is NOT trivial/standard-layout
static_assert(!std::is_trivial_v<NamedValue>, "NamedValue is not trivial (has std::string)");
static_assert(vlink::Serializer::get_type_of<NamedValue>() == vlink::Serializer::kCustomType,
              "NamedValue must be detected as kCustomType");
