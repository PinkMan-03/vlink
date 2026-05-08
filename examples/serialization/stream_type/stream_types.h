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
#include <iostream>

/// A color type with stream operators (but no Bytes operator>>/<<).
/// Serialized as text: "r g b a" via stringstream.
/// Detected as kStreamType because it only provides iostream operators.
struct Color {
  int r = 0;    // Red channel (0-255)
  int g = 0;    // Green channel (0-255)
  int b = 0;    // Blue channel (0-255)
  int a = 255;  // Alpha channel (0-255, default opaque)

  // Stream insertion: used by VLink for serialization (writes to stringstream)
  friend std::ostream& operator<<(std::ostream& os, const Color& c) {
    return os << c.r << " " << c.g << " " << c.b << " " << c.a;
  }

  // Stream extraction: used by VLink for deserialization (reads from stringstream)
  friend std::istream& operator>>(std::istream& is, Color& c) { return is >> c.r >> c.g >> c.b >> c.a; }
};

// Verify that Color is detected as kStreamType
static_assert(vlink::Serializer::get_type_of<Color>() == vlink::Serializer::kStreamType,
              "Color must be detected as kStreamType");

/// A 2D size type serialized as "width height".
/// Detected as kStreamType.
struct Size2D {
  double width = 0.0;   // Width dimension
  double height = 0.0;  // Height dimension

  friend std::ostream& operator<<(std::ostream& os, const Size2D& s) { return os << s.width << " " << s.height; }

  friend std::istream& operator>>(std::istream& is, Size2D& s) { return is >> s.width >> s.height; }
};

static_assert(vlink::Serializer::get_type_of<Size2D>() == vlink::Serializer::kStreamType,
              "Size2D must be detected as kStreamType");

/// A hybrid type with BOTH stream and Bytes operators.
/// Demonstrates that kCustomType is checked before kStreamType in the detection chain,
/// so Bytes operators take priority over stringstream operators.
struct Hybrid {
  int value = 0;  // The stored integer value

  // Stream operators (would make it kStreamType alone)
  friend std::ostream& operator<<(std::ostream& os, const Hybrid& h) { return os << h.value; }
  friend std::istream& operator>>(std::istream& is, Hybrid& h) { return is >> h.value; }

  // Bytes operators (these take priority -> kCustomType)
  void operator>>(vlink::Bytes& out) const {  // NOLINT(google-runtime-operator)
    out = vlink::Bytes::create(sizeof(int));
    std::memcpy(out.data(), &value, sizeof(int));
  }

  void operator<<(const vlink::Bytes& in) {
    if (in.size() >= sizeof(int)) {
      std::memcpy(&value, in.data(), sizeof(int));
    }
  }
};

// Hybrid is kCustomType because Bytes operators are checked before stringstream operators
static_assert(vlink::Serializer::get_type_of<Hybrid>() == vlink::Serializer::kCustomType,
              "Hybrid must be kCustomType (Bytes operators take priority)");
