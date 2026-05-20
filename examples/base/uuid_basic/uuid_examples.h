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

#include <vlink/base/logger.h>
#include <vlink/base/uuid.h>

#include <array>
#include <random>
#include <string>
#include <unordered_set>

namespace uuid_examples {

inline void demo_default_nil() {
  VLOG_I("--- demo: default-constructed Uuid is nil ---");

  vlink::Uuid id;

  VLOG_I("is_nil()    = ", id.is_nil());
  VLOG_I("to_string() = ", id.to_string());
}

inline void demo_construct_from_array() {
  VLOG_I("--- demo: construct from std::array ---");

  std::array<uint8_t, 16> data{0x47, 0xac, 0x10, 0xb8, 0x58, 0xcc, 0x4a, 0x3c,
                               0x8c, 0x5b, 0x0e, 0x77, 0x88, 0x99, 0xaa, 0xbb};
  vlink::Uuid id{data};

  VLOG_I("to_string()         = ", id.to_string());
  VLOG_I("to_compact_string() = ", id.to_compact_string());
}

inline void demo_construct_from_raw_array() {
  VLOG_I("--- demo: construct from raw C array ---");

  const uint8_t raw[16] = {0xfe, 0xed, 0xfa, 0xce, 0xde, 0xad, 0xbe, 0xef,
                           0xca, 0xfe, 0xba, 0xbe, 0x12, 0x34, 0x56, 0x78};
  vlink::Uuid id{raw};

  VLOG_I("to_string() = ", id.to_string());
}

inline void demo_iterator_range() {
  VLOG_I("--- demo: construct from iterator range ---");

  std::vector<uint8_t> source(16U, 0xa5U);
  vlink::Uuid id{source.begin(), source.end()};

  VLOG_I("16-byte source -> to_string() = ", id.to_string());

  std::vector<uint8_t> too_short(8U, 0xffU);
  vlink::Uuid bad{too_short.begin(), too_short.end()};

  VLOG_I("8-byte source  -> is_nil()    = ", bad.is_nil());
}

inline void demo_generate_random() {
  VLOG_I("--- demo: generate random v4 UUID ---");

  vlink::Uuid id = vlink::Uuid::generate_random();

  VLOG_I("to_string() = ", id.to_string());
  VLOG_I("variant     = ", static_cast<int>(id.variant()), " (1 = kRfc)");
  VLOG_I("version     = ", static_cast<int>(id.version()), " (4 = kRandomBased)");
}

inline void demo_generate_random_with_engine() {
  VLOG_I("--- demo: generate with caller-supplied engine ---");

  std::mt19937 engine_one(0xdeadbeefU);
  std::mt19937 engine_two(0xdeadbeefU);

  vlink::Uuid a = vlink::Uuid::generate_random(engine_one);
  vlink::Uuid b = vlink::Uuid::generate_random(engine_two);

  VLOG_I("a = ", a.to_string());
  VLOG_I("b = ", b.to_string());
  VLOG_I("a == b (same seed) = ", a == b);

  vlink::Uuid c = vlink::Uuid::generate_random(engine_one);

  VLOG_I("c = ", c.to_string());
  VLOG_I("c != a (engine advanced) = ", c != a);
}

inline void demo_from_string_round_trip() {
  VLOG_I("--- demo: from_string round-trip ---");

  const std::string text = "47ac10b8-58cc-4a3c-8c5b-0e778899aabb";
  auto parsed = vlink::Uuid::from_string(text);

  if (parsed.has_value()) {
    VLOG_I("parsed       = ", parsed->to_string());
    VLOG_I("round-trip ok = ", parsed->to_string() == text);
  } else {
    VLOG_E("from_string failed");
  }

  auto braced = vlink::Uuid::from_string("{47ac10b8-58cc-4a3c-8c5b-0e778899aabb}");

  VLOG_I("braced form parsed = ", braced.has_value());

  auto compact = vlink::Uuid::from_string("47ac10b858cc4a3c8c5b0e778899aabb");

  VLOG_I("compact form parsed = ", compact.has_value());

  auto bad = vlink::Uuid::from_string("not-a-uuid");

  VLOG_I("malformed parsed = ", bad.has_value());
}

inline void demo_is_valid() {
  VLOG_I("--- demo: is_valid checks ---");

  VLOG_I("canonical   -> ", vlink::Uuid::is_valid("47ac10b8-58cc-4a3c-8c5b-0e778899aabb"));
  VLOG_I("braced      -> ", vlink::Uuid::is_valid("{47ac10b8-58cc-4a3c-8c5b-0e778899aabb}"));
  VLOG_I("compact     -> ", vlink::Uuid::is_valid("47ac10b858cc4a3c8c5b0e778899aabb"));
  VLOG_I("empty       -> ", vlink::Uuid::is_valid(""));
  VLOG_I("not-uuid    -> ", vlink::Uuid::is_valid("not-a-uuid"));
  VLOG_I("nullptr     -> ", vlink::Uuid::is_valid(static_cast<const char*>(nullptr)));
}

inline void demo_variant_version_detect() {
  VLOG_I("--- demo: variant / version detection ---");

  std::array<uint8_t, 16> data{};

  data[8] = 0x00U;
  VLOG_I("octet8=0x00 -> variant = NCS, value = ", static_cast<int>(vlink::Uuid{data}.variant()));

  data[8] = 0x80U;
  VLOG_I("octet8=0x80 -> variant = RFC, value = ", static_cast<int>(vlink::Uuid{data}.variant()));

  data[8] = 0xC0U;
  VLOG_I("octet8=0xC0 -> variant = Microsoft, value = ", static_cast<int>(vlink::Uuid{data}.variant()));

  data[8] = 0xE0U;
  VLOG_I("octet8=0xE0 -> variant = Reserved, value = ", static_cast<int>(vlink::Uuid{data}.variant()));

  data[8] = 0x00U;
  data[6] = 0x10U;
  VLOG_I("octet6=0x10 -> version = TimeBased, value = ", static_cast<int>(vlink::Uuid{data}.version()));

  data[6] = 0x40U;
  VLOG_I("octet6=0x40 -> version = RandomBased, value = ", static_cast<int>(vlink::Uuid{data}.version()));

  data[6] = 0x50U;
  VLOG_I("octet6=0x50 -> version = NameBasedSha1, value = ", static_cast<int>(vlink::Uuid{data}.version()));
}

inline void demo_byte_access() {
  VLOG_I("--- demo: raw byte access via bytes() ---");

  vlink::Uuid id = vlink::Uuid::generate_random();
  const auto& raw = id.bytes();

  VLOG_I("to_string()   = ", id.to_string());
  VLOG_I("bytes()[0]    = ", static_cast<int>(raw[0]));
  VLOG_I("bytes()[6]    = ", static_cast<int>(raw[6]), " (high nibble == 0x40, v4)");
  VLOG_I("bytes()[8]    = ", static_cast<int>(raw[8]), " (top two bits == 10, RFC)");
  VLOG_I("bytes()[15]   = ", static_cast<int>(raw[15]));
  VLOG_I("kByteSize     = ", static_cast<size_t>(vlink::Uuid::kByteSize));
  VLOG_I("kStringSize   = ", static_cast<size_t>(vlink::Uuid::kStringSize));
}

inline void demo_comparisons_and_hash() {
  VLOG_I("--- demo: comparison operators and std::hash ---");

  vlink::Uuid nil_a;
  vlink::Uuid nil_b;

  std::array<uint8_t, 16> data{};
  data[0] = 1U;
  vlink::Uuid other{data};

  VLOG_I("nil_a == nil_b = ", nil_a == nil_b);
  VLOG_I("nil_a != other = ", nil_a != other);
  VLOG_I("nil_a <  other = ", nil_a < other);

  std::unordered_set<vlink::Uuid> set;

  for (int i = 0; i < 10; ++i) {
    set.insert(vlink::Uuid::generate_random());
  }

  VLOG_I("10 random UUIDs in unordered_set, size = ", set.size());
}

inline void demo_random_bytes_and_hex() {
  VLOG_I("--- demo: random_bytes / random_hex ---");

  auto buf = vlink::Uuid::random_bytes(8U);
  VLOG_I("random_bytes(8U).size() = ", buf.size());

  auto buf_zero = vlink::Uuid::random_bytes(0U);
  VLOG_I("random_bytes(0U).empty() = ", buf_zero.empty());

  auto hex = vlink::Uuid::random_hex(16U);
  VLOG_I("random_hex(16U) = ", hex);
  VLOG_I("random_hex(16U).size() = ", hex.size());

  auto hex_default = vlink::Uuid::random_hex();
  VLOG_I("random_hex() default 16 bytes -> 32 hex chars = ", hex_default);

  auto hex_zero = vlink::Uuid::random_hex(0U);
  VLOG_I("random_hex(0U).empty() = ", hex_zero.empty());
}

inline void demo_swap() {
  VLOG_I("--- demo: swap ---");

  std::array<uint8_t, 16> data{};
  data[0] = 0x77U;
  vlink::Uuid a{data};
  vlink::Uuid b;

  VLOG_I("before: a[0]=", static_cast<int>(a.bytes()[0]), "  b.is_nil()=", b.is_nil());

  a.swap(b);

  VLOG_I("after : a.is_nil()=", a.is_nil(), "  b[0]=", static_cast<int>(b.bytes()[0]));
}

inline void demo_constexpr_use() {
  VLOG_I("--- demo: compile-time (constexpr) usage ---");

  constexpr vlink::Uuid nil_id;
  static_assert(nil_id.is_nil(), "default Uuid must be nil at compile time");
  static_assert(nil_id.variant() == vlink::Uuid::Variant::kNcs, "nil variant must be NCS");
  static_assert(nil_id.version() == vlink::Uuid::Version::kNone, "nil version must be kNone");

  constexpr std::array<uint8_t, 16> sample{
      0x47, 0xac, 0x10, 0xb8, 0x58, 0xcc, 0x4a, 0x3c, 0x8c, 0x5b, 0x0e, 0x77, 0x88, 0x99, 0xaa, 0xbb,
  };
  constexpr vlink::Uuid sample_id{sample};
  static_assert(sample_id.bytes()[0] == 0x47U, "constexpr bytes() must work");
  static_assert(!sample_id.is_nil(), "non-zero data must not be nil");

  VLOG_I("static_assert checks passed for constexpr default + array ctors");
}

}  // namespace uuid_examples
