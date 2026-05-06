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

// NOLINTBEGIN

#include "./base/bytes.h"

#include <doctest/doctest.h>

#include <algorithm>
#include <cstring>
#include <limits>
#include <sstream>
#include <string>
#include <vector>

//
#include "../common_test.h"

// ---------------------------------------------------------------------------
// Helper: fill a Bytes buffer with a simple ascending pattern
// ---------------------------------------------------------------------------
static void fill_pattern(Bytes& b) {
  for (size_t i = 0; i < b.size(); ++i) {
    b.data()[i] = static_cast<uint8_t>(i & 0xFFU);
  }
}

// ---------------------------------------------------------------------------
TEST_SUITE("base-Bytes") {
  // -------------------------------------------------------------------------
  TEST_CASE("default constructor produces empty object") {
    Bytes b;

    CHECK(b.empty());
    CHECK(b.size() == 0U);
    CHECK(b.real_size() == 0U);
    CHECK(b.capacity() == 0U);
    CHECK(b.offset() == 0U);
    CHECK(b.data() == nullptr);
    CHECK(b.real_data() == nullptr);
    CHECK(b.is_owner() == false);
    CHECK(b.is_loaned() == false);
    CHECK(b.is_ptr() == false);
    CHECK(b.begin() == nullptr);
    CHECK(b.end() == nullptr);
    CHECK(b.real_begin() == nullptr);
    CHECK(b.real_end() == nullptr);
  }

  // -------------------------------------------------------------------------
  TEST_CASE("initializer_list constructor") {
    Bytes b{0x01U, 0x02U, 0x03U};

    REQUIRE(b.size() == 3U);
    CHECK(!b.empty());
    CHECK(b.is_owner());
    CHECK(b.data() != nullptr);
    CHECK(b.data()[0] == 0x01U);
    CHECK(b.data()[1] == 0x02U);
    CHECK(b.data()[2] == 0x03U);
  }

  // -------------------------------------------------------------------------
  TEST_CASE("vector constructor") {
    std::vector<uint8_t> vec{0xAAU, 0xBBU, 0xCCU, 0xDDU};
    Bytes b(vec);

    REQUIRE(b.size() == 4U);
    CHECK(b.is_owner());
    CHECK(b == vec);
  }

  // -------------------------------------------------------------------------
  TEST_CASE("create - SBO path (size <= kStackSize uses inline storage)") {
    constexpr size_t kSmall = 64U;
    Bytes b = Bytes::create(kSmall);

    CHECK(b.size() == kSmall);
    CHECK(b.real_size() == kSmall);
    CHECK(b.offset() == 0U);
    CHECK(b.is_owner());
    CHECK(!b.is_loaned());
    CHECK(!b.is_ptr());
    CHECK(!b.empty());
    CHECK(b.data() != nullptr);
    // SBO capacity == kStackSize
    CHECK(b.capacity() <= Bytes::stack_size());

    fill_pattern(b);
    CHECK(b.data()[0] == 0x00u);
    CHECK(b.data()[63] == 63u);
  }

  // -------------------------------------------------------------------------
  TEST_CASE("create - heap path (size > kStackSize)") {
    constexpr size_t kLarge = 200U;
    Bytes b = Bytes::create(kLarge);

    CHECK(b.size() == kLarge);
    CHECK(b.is_owner());
    CHECK(b.capacity() >= kLarge);
    CHECK(b.data() != nullptr);

    fill_pattern(b);
    CHECK(b.data()[0] == 0x00U);
    CHECK(b.data()[199] == static_cast<uint8_t>(199U & 0xFFU));
  }

  // -------------------------------------------------------------------------
  TEST_CASE("create with offset - SBO") {
    constexpr size_t kSize = 32U;
    constexpr uint8_t kOffset = 4U;
    Bytes b = Bytes::create(kSize, kOffset);

    CHECK(b.size() == kSize);
    CHECK(b.offset() == kOffset);
    CHECK(b.real_size() == kSize + kOffset);
    CHECK(b.data() == b.real_data() + kOffset);
    CHECK(b.is_owner());
  }

  // -------------------------------------------------------------------------
  TEST_CASE("create - exactly kStackSize fits SBO") {
    Bytes b = Bytes::create(Bytes::stack_size());

    CHECK(b.size() == Bytes::stack_size());
    CHECK(b.capacity() == Bytes::stack_size());
    CHECK(b.is_owner());
  }

  // -------------------------------------------------------------------------
  TEST_CASE("shallow_copy (mutable) - non-owning alias") {
    std::vector<uint8_t> ext{0x10U, 0x20U, 0x30U};
    Bytes b = Bytes::shallow_copy(ext.data(), ext.size());

    CHECK(b.size() == 3U);
    CHECK(b.is_owner() == false);
    CHECK(b.is_loaned() == false);
    CHECK(b.data() == ext.data());
    CHECK(b.data()[0] == 0x10U);

    // Mutate through the alias and verify the original buffer is updated
    b.data()[0] = 0xFFU;
    CHECK(ext[0] == 0xFFU);
  }

  // -------------------------------------------------------------------------
  TEST_CASE("shallow_copy (const) - non-owning read-only alias") {
    const uint8_t kBuf[] = {0x01U, 0x02U, 0x03U};
    Bytes b = Bytes::shallow_copy(kBuf, 3U);

    CHECK(b.size() == 3U);
    CHECK(b.is_owner() == false);
    // const data() accessor returns valid pointer
    CHECK(b.data() != nullptr);
    CHECK(b.data()[2] == 0x03U);
  }

  // -------------------------------------------------------------------------
  TEST_CASE("shallow_copy_ptr wraps opaque pointer") {
    int sentinel = 42;
    Bytes b = Bytes::shallow_copy_ptr(&sentinel);

    CHECK(b.is_ptr());
    CHECK(b.size() == 0U);
    CHECK(b.offset() == 0U);
    CHECK(b.is_owner() == false);
    CHECK(b.to_ptr<int>() == &sentinel);
  }

  // -------------------------------------------------------------------------
  TEST_CASE("deep_copy (mutable) - owns its data") {
    uint8_t src[] = {0xAAU, 0xBBU, 0xCCU};
    Bytes b = Bytes::deep_copy(src, 3U);

    CHECK(b.size() == 3U);
    CHECK(b.is_owner());
    // Data pointer is separate from src
    CHECK(static_cast<void*>(b.data()) != static_cast<void*>(src));
    CHECK(b.data()[0] == 0xAAU);
    CHECK(b.data()[1] == 0xBBU);
    CHECK(b.data()[2] == 0xCCU);

    // Modifying src does not affect b
    src[0] = 0x00U;
    CHECK(b.data()[0] == 0xAAU);
  }

  // -------------------------------------------------------------------------
  TEST_CASE("deep_copy (const) - owns its data") {
    const uint8_t src[] = {0x11u, 0x22u, 0x33u};
    Bytes b = Bytes::deep_copy(src, 3u);

    CHECK(b.size() == 3u);
    CHECK(b.is_owner());
    CHECK(b.data()[0] == 0x11u);
  }

  // -------------------------------------------------------------------------
  TEST_CASE("deep_copy with offset") {
    const uint8_t src[] = {0xA1u, 0xA2u};
    Bytes b = Bytes::deep_copy(src, 2u, 8u);

    CHECK(b.offset() == 8u);
    CHECK(b.size() == 2u);
    CHECK(b.real_size() == 10u);
    CHECK(b.data() == b.real_data() + 8u);
    CHECK(b.data()[0] == 0xA1u);
  }

  // -------------------------------------------------------------------------
  TEST_CASE("from_string") {
    std::string s = "hello";
    Bytes b = Bytes::from_string(s);

    REQUIRE(b.size() == s.size());
    CHECK(b.is_owner());
    CHECK(b.to_string() == s);
  }

  // -------------------------------------------------------------------------
  TEST_CASE("from_string with offset") {
    std::string s = "world";
    Bytes b = Bytes::from_string(s, 4u);

    CHECK(b.size() == s.size());
    CHECK(b.offset() == 4u);
    CHECK(b.to_string() == s);
  }

  // -------------------------------------------------------------------------
  TEST_CASE("from_user_input - valid hex string") {
    bool ok = false;
    Bytes b = Bytes::from_user_input("01 02 03", &ok);

    CHECK(ok);
    REQUIRE(b.size() == 3u);
    CHECK(b.data()[0] == 0x01u);
    CHECK(b.data()[1] == 0x02u);
    CHECK(b.data()[2] == 0x03u);
  }

  // -------------------------------------------------------------------------
  TEST_CASE("from_user_input - uppercase hex") {
    bool ok = false;
    Bytes b = Bytes::from_user_input("AB CD EF", &ok);

    CHECK(ok);
    REQUIRE(b.size() == 3u);
    CHECK(b.data()[0] == 0xABu);
    CHECK(b.data()[1] == 0xCDu);
    CHECK(b.data()[2] == 0xEFu);
  }

  // -------------------------------------------------------------------------
  TEST_CASE("from_user_input - invalid string returns empty and ok=false") {
    bool ok = true;
    Bytes b = Bytes::from_user_input("ZZ QQ", &ok);

    CHECK(!ok);
    CHECK(b.empty());
  }

  // -------------------------------------------------------------------------
  TEST_CASE("from_user_input - ok pointer may be null") {
    // Should not crash when ok is nullptr
    Bytes b = Bytes::from_user_input("FF", nullptr);
    (void)b;
  }

  // -------------------------------------------------------------------------
  TEST_CASE("convert_to_hex_str") {
    const uint8_t data[] = {0x1Au, 0xB2u};
    std::string hex = Bytes::convert_to_hex_str(data, 2u);

    CHECK(!hex.empty());
    // Result should contain '1', 'A', 'B', '2' characters in some form
    CHECK(hex.find('1') != std::string::npos);
    bool has_a = hex.find('A') != std::string::npos || hex.find('a') != std::string::npos;
    CHECK(has_a);
  }

  // -------------------------------------------------------------------------
  TEST_CASE("reverse_order") {
    Bytes orig{0x01u, 0x02u, 0x03u, 0x04u};
    Bytes rev = Bytes::reverse_order(orig);

    REQUIRE(rev.size() == orig.size());
    CHECK(rev.data()[0] == 0x04u);
    CHECK(rev.data()[1] == 0x03u);
    CHECK(rev.data()[2] == 0x02u);
    CHECK(rev.data()[3] == 0x01u);

    // Original is unchanged
    CHECK(orig.data()[0] == 0x01u);
  }

  // -------------------------------------------------------------------------
  TEST_CASE("reverse_order of single byte") {
    Bytes orig{0x42u};
    Bytes rev = Bytes::reverse_order(orig);

    REQUIRE(rev.size() == 1u);
    CHECK(rev.data()[0] == 0x42u);
  }

  // -------------------------------------------------------------------------
  TEST_CASE("reverse_order of empty bytes") {
    Bytes orig;
    Bytes rev = Bytes::reverse_order(orig);

    CHECK(rev.empty());
  }

  // -------------------------------------------------------------------------
  TEST_CASE("encode_to_base64 and decode_from_base64 round-trip") {
    Bytes original{0xDEu, 0xADu, 0xBEu, 0xEFu};
    std::string encoded = Bytes::encode_to_base64(original);

    CHECK(!encoded.empty());

    Bytes decoded = Bytes::decode_from_base64(encoded);

    REQUIRE(decoded.size() == original.size());
    CHECK(decoded == original);
  }

  // -------------------------------------------------------------------------
  TEST_CASE("encode_to_base64 and decode_from_base64 - longer data") {
    Bytes original = Bytes::create(48u);
    fill_pattern(original);

    std::string encoded = Bytes::encode_to_base64(original);
    Bytes decoded = Bytes::decode_from_base64(encoded);

    REQUIRE(decoded.size() == original.size());
    CHECK(decoded == original);
  }

  // -------------------------------------------------------------------------
  TEST_CASE("encode_to_base64 - empty bytes") {
    Bytes empty;
    std::string encoded = Bytes::encode_to_base64(empty);
    // May return empty string or a valid base64 encoding of zero bytes
    Bytes decoded = Bytes::decode_from_base64(encoded);
    CHECK(decoded.size() == 0u);
  }

  // -------------------------------------------------------------------------
  TEST_CASE("decode_from_base64 - invalid input returns empty") {
    Bytes decoded = Bytes::decode_from_base64("!!!invalid!!!");
    CHECK(decoded.empty());
  }

  // -------------------------------------------------------------------------
  TEST_CASE("decode_from_base64 rejects malformed padding") {
    CHECK(Bytes::decode_from_base64("TQ=A").empty());
    CHECK(Bytes::decode_from_base64("TQ==AAAA").empty());
    CHECK(Bytes::decode_from_base64("T===").empty());
  }

  // -------------------------------------------------------------------------
  TEST_CASE("get_crc_32 - deterministic") {
    Bytes a{0x01u, 0x02u, 0x03u};
    Bytes b{0x01u, 0x02u, 0x03u};

    uint32_t crc_a = Bytes::get_crc_32(a);
    uint32_t crc_b = Bytes::get_crc_32(b);

    CHECK(crc_a == crc_b);
  }

  // -------------------------------------------------------------------------
  TEST_CASE("get_crc_32 - different data yields different CRC") {
    Bytes a{0x01u, 0x02u, 0x03u};
    Bytes b{0x01u, 0x02u, 0x04u};

    CHECK(Bytes::get_crc_32(a) != Bytes::get_crc_32(b));
  }

  // -------------------------------------------------------------------------
  TEST_CASE("compress_data and uncompress_data round-trip") {
    // Build a highly compressible 512-byte buffer
    Bytes original = Bytes::create(512u);
    std::memset(original.data(), 0x5Au, 512u);

    Bytes compressed = Bytes::compress_data(original.data(), original.size());

    REQUIRE(!compressed.empty());
    CHECK(Bytes::is_compress_data(compressed.data(), compressed.size()));

    Bytes decompressed = Bytes::uncompress_data(compressed.data(), compressed.size());

    REQUIRE(decompressed.size() == original.size());
    CHECK(std::memcmp(decompressed.data(), original.data(), original.size()) == 0);
  }

  // -------------------------------------------------------------------------
  TEST_CASE("compress_data high_ratio mode round-trip") {
    Bytes original = Bytes::create(256u);
    fill_pattern(original);

    Bytes compressed = Bytes::compress_data(original.data(), original.size(), true);

    REQUIRE(!compressed.empty());
    CHECK(Bytes::is_compress_data(compressed.data(), compressed.size()));

    Bytes decompressed = Bytes::uncompress_data(compressed.data(), compressed.size());

    REQUIRE(decompressed.size() == original.size());
    CHECK(std::memcmp(decompressed.data(), original.data(), original.size()) == 0);
  }

  // -------------------------------------------------------------------------
  TEST_CASE("is_compress_data - uncompressed data returns false") {
    Bytes plain{0x01u, 0x02u, 0x03u, 0x04u, 0x05u, 0x06u, 0x07u, 0x08u};
    CHECK(!Bytes::is_compress_data(plain.data(), plain.size()));
  }

  // -------------------------------------------------------------------------
  TEST_CASE("is_compress_data - too small buffer returns false") {
    const uint8_t tiny[] = {0x17u, 0x49u};
    CHECK(!Bytes::is_compress_data(tiny, 2u));
  }

  // -------------------------------------------------------------------------
  TEST_CASE("uncompress_data - invalid data with check_valid returns empty") {
    Bytes junk{0x00u, 0x01u, 0x02u, 0x03u, 0x04u, 0x05u, 0x06u, 0x07u};
    Bytes result = Bytes::uncompress_data(junk.data(), junk.size(), true);
    CHECK(result.empty());
  }

  // -------------------------------------------------------------------------
  TEST_CASE("to_string and to_string_view") {
    std::string text = "VLink";
    Bytes b = Bytes::from_string(text);

    CHECK(b.to_string() == text);
    CHECK(b.to_string_view() == text);
    // string_view points into the same buffer
    CHECK(b.to_string_view().data() == reinterpret_cast<const char*>(b.data()));
  }

  // -------------------------------------------------------------------------
  TEST_CASE("to_raw_data returns vector copy") {
    Bytes b{0x11u, 0x22u, 0x33u};
    std::vector<uint8_t> vec = b.to_raw_data();

    REQUIRE(vec.size() == 3u);
    CHECK(vec[0] == 0x11u);
    CHECK(vec[1] == 0x22u);
    CHECK(vec[2] == 0x33u);
  }

  // -------------------------------------------------------------------------
  TEST_CASE("iterators - begin/end span user data") {
    Bytes b{0x0Au, 0x0Bu, 0x0Cu};

    size_t count = 0;
    for (const uint8_t* it = b.begin(); it != b.end(); ++it) {
      ++count;
    }

    CHECK(count == b.size());
    CHECK(b.begin() == b.data());
    CHECK(b.end() == b.data() + b.size());
  }

  // -------------------------------------------------------------------------
  TEST_CASE("iterators - real_begin/real_end span full backing buffer with offset") {
    Bytes b = Bytes::create(10u, 4u);

    CHECK(b.real_begin() == b.real_data());
    CHECK(b.real_end() == b.real_data() + b.real_size());
    // real range is larger than user range
    CHECK(b.real_end() - b.real_begin() == static_cast<ptrdiff_t>(b.real_size()));
    CHECK(b.end() - b.begin() == static_cast<ptrdiff_t>(b.size()));
  }

  // -------------------------------------------------------------------------
  TEST_CASE("subscript operator") {
    Bytes b{0x10u, 0x20u, 0x30u};

    CHECK(b[0] == 0x10u);
    CHECK(b[1] == 0x20u);
    CHECK(b[2] == 0x30u);

    b[1] = 0xFFu;
    CHECK(b[1] == 0xFFu);
  }

  // -------------------------------------------------------------------------
  TEST_CASE("operator== and operator!= with Bytes") {
    Bytes a{0x01u, 0x02u, 0x03u};
    Bytes b{0x01u, 0x02u, 0x03u};
    Bytes c{0x01u, 0x02u, 0x04u};

    CHECK(a == b);
    CHECK(!(a != b));
    CHECK(a != c);
    CHECK(!(a == c));
  }

  // -------------------------------------------------------------------------
  TEST_CASE("operator== and operator!= with vector") {
    std::vector<uint8_t> vec{0xAAu, 0xBBu};
    Bytes b{0xAAu, 0xBBu};
    Bytes c{0xAAu, 0xCCu};

    CHECK(b == vec);
    CHECK(!(b != vec));
    CHECK(c != vec);
  }

  // -------------------------------------------------------------------------
  TEST_CASE("copy constructor - owner deep-copies data") {
    Bytes original{0x01u, 0x02u, 0x03u};
    Bytes copy(original);

    CHECK(copy.size() == original.size());
    CHECK(copy.is_owner());
    CHECK(copy == original);
    // Separate backing storage
    CHECK(copy.data() != original.data());

    // Mutating copy does not affect original
    copy.data()[0] = 0xFFu;
    CHECK(original.data()[0] == 0x01u);
  }

  // -------------------------------------------------------------------------
  TEST_CASE("copy constructor - shallow alias is deep-copied") {
    uint8_t ext[] = {0x11u, 0x22u};
    Bytes alias = Bytes::shallow_copy(ext, 2u);
    Bytes copy(alias);

    // Copy constructor always deep-copies: copy is an owner with its own storage
    CHECK(copy.is_owner());
    CHECK(copy.data() != ext);
    CHECK(copy.size() == 2u);
    CHECK(copy.data()[0] == 0x11u);
    CHECK(copy.data()[1] == 0x22u);
  }

  // -------------------------------------------------------------------------
  TEST_CASE("move constructor transfers ownership") {
    Bytes original = Bytes::create(32u);
    fill_pattern(original);

    Bytes moved(std::move(original));

    CHECK(moved.size() == 32u);
    CHECK(moved.is_owner());
    // For heap-allocated (>kStackSize) the pointer is stable after move;
    // for SBO the data is in the object itself, so just verify correctness
    CHECK(moved.data() != nullptr);
    CHECK(moved.data()[0] == 0x00u);
    CHECK(original.empty());  // NOLINT(bugprone-use-after-move)
  }

  // -------------------------------------------------------------------------
  TEST_CASE("copy assignment operator") {
    Bytes a{0x01u, 0x02u};
    Bytes b{0xFFu};

    b = a;

    CHECK(b.size() == 2u);
    CHECK(b.is_owner());
    CHECK(b == a);
    CHECK(b.data() != a.data());
  }

  // -------------------------------------------------------------------------
  TEST_CASE("move assignment operator") {
    Bytes a{0x10u, 0x20u, 0x30u};
    Bytes b;

    b = std::move(a);

    CHECK(b.size() == 3u);
    CHECK(b.data()[0] == 0x10u);
    CHECK(a.empty());  // NOLINT(bugprone-use-after-move)
  }

  // -------------------------------------------------------------------------
  TEST_CASE("vector assignment operator") {
    std::vector<uint8_t> vec{0xA1u, 0xA2u, 0xA3u};
    Bytes b;
    b = vec;

    REQUIRE(b.size() == 3u);
    CHECK(b.is_owner());
    CHECK(b == vec);
  }

  // -------------------------------------------------------------------------
  TEST_CASE("clear resets to empty") {
    Bytes b = Bytes::create(64u);

    CHECK(!b.empty());
    b.clear();
    CHECK(b.empty());
    CHECK(b.data() == nullptr);
    CHECK(b.size() == 0u);
  }

  // -------------------------------------------------------------------------
  TEST_CASE("shrink_to reduces size") {
    Bytes b = Bytes::create(16u);
    fill_pattern(b);

    bool ok = b.shrink_to(8u);

    CHECK(ok);
    CHECK(b.size() == 8u);
    // Data is still intact for first 8 bytes
    CHECK(b.data()[0] == 0x00u);
    CHECK(b.data()[7] == 0x07u);
  }

  // -------------------------------------------------------------------------
  TEST_CASE("shrink_to with size larger than current returns false") {
    Bytes b = Bytes::create(8u);
    bool ok = b.shrink_to(16u);
    CHECK(!ok);
    CHECK(b.size() == 8u);
  }

  // -------------------------------------------------------------------------
  TEST_CASE("reserve grows capacity") {
    Bytes b = Bytes::create(10u);
    size_t old_cap = b.capacity();

    bool ok = b.reserve(old_cap + 100u);

    CHECK(ok);
    CHECK(b.capacity() >= old_cap + 100u);
    // Logical size unchanged
    CHECK(b.size() == 10u);
  }

  // -------------------------------------------------------------------------
  TEST_CASE("reserve no-op when capacity already sufficient") {
    Bytes b = Bytes::create(64u);
    size_t cap_before = b.capacity();

    bool ok = b.reserve(10u);

    CHECK(ok);
    CHECK(b.capacity() == cap_before);
  }

  // -------------------------------------------------------------------------
  TEST_CASE("resize grows logical size") {
    Bytes b = Bytes::create(8u);

    bool ok = b.resize(32u);

    CHECK(ok);
    CHECK(b.size() == 32u);
    CHECK(b.capacity() >= 32u);
  }

  // -------------------------------------------------------------------------
  TEST_CASE("resize shrinks logical size") {
    Bytes b = Bytes::create(64u);
    fill_pattern(b);

    bool ok = b.resize(16u);

    CHECK(ok);
    CHECK(b.size() == 16u);
    CHECK(b.data()[0] == 0x00u);
  }

  // -------------------------------------------------------------------------
  TEST_CASE("instance shallow_copy makes non-owning alias") {
    Bytes owner = Bytes::create(8u);
    fill_pattern(owner);

    Bytes alias;
    alias.shallow_copy(owner);

    CHECK(!alias.is_owner());
    CHECK(alias.data() == owner.data());
    CHECK(alias.size() == owner.size());
  }

  // -------------------------------------------------------------------------
  TEST_CASE("instance deep_copy produces independent owner") {
    Bytes original = Bytes::create(8u);
    fill_pattern(original);

    Bytes copy;
    copy.deep_copy(original);

    CHECK(copy.is_owner());
    CHECK(copy.size() == original.size());
    CHECK(copy == original);
    CHECK(copy.data() != original.data());
  }

  // -------------------------------------------------------------------------
  TEST_CASE("deep_copy_self converts alias to owner") {
    uint8_t ext[] = {0x10u, 0x20u, 0x30u};
    Bytes alias = Bytes::shallow_copy(ext, 3u);

    CHECK(!alias.is_owner());

    alias.deep_copy_self();

    CHECK(alias.is_owner());
    CHECK(alias.data() != ext);
    CHECK(alias.data()[0] == 0x10u);
  }

  // -------------------------------------------------------------------------
  TEST_CASE("deep_copy_self on owner is a no-op") {
    Bytes owner = Bytes::create(4u);
    fill_pattern(owner);
    const uint8_t* ptr_before = owner.real_data();

    owner.deep_copy_self();

    // For SBO the stack_data_ address stays the same, for heap it may differ;
    // the important invariant is that it is still an owner with correct data
    CHECK(owner.is_owner());
    CHECK(owner.size() == 4u);
    CHECK(owner.data()[0] == 0x00u);
    (void)ptr_before;
  }

  // -------------------------------------------------------------------------
  TEST_CASE("loan_internal mutable - is_loaned true, is_owner false") {
    uint8_t buf[] = {0x01u, 0x02u, 0x03u};
    Bytes loaned = Bytes::loan_internal(buf, 3u);

    CHECK(loaned.is_loaned());
    CHECK(!loaned.is_owner());
    CHECK(loaned.size() == 3u);
    CHECK(loaned.data() == buf);
  }

  // -------------------------------------------------------------------------
  TEST_CASE("loan_internal const - is_loaned true") {
    const uint8_t buf[] = {0xFFu, 0xFEu};
    Bytes loaned = Bytes::loan_internal(buf, 2u);

    CHECK(loaned.is_loaned());
    CHECK(!loaned.is_owner());
    CHECK(loaned.size() == 2u);
  }

  // -------------------------------------------------------------------------
  TEST_CASE("is_ptr returns false for normal owned buffer") {
    Bytes b = Bytes::create(8u);
    CHECK(!b.is_ptr());
  }

  // -------------------------------------------------------------------------
  TEST_CASE("to_ptr reinterprets real_data") {
    Bytes b{0x01u, 0x02u, 0x03u, 0x04u};
    const uint8_t* p = b.to_ptr<uint8_t>();
    CHECK(p == b.real_data());
  }

  // -------------------------------------------------------------------------
  TEST_CASE("stack_size returns 96") { CHECK(Bytes::stack_size() == 96u); }

  // -------------------------------------------------------------------------
  TEST_CASE("endianness helpers are consistent") {
    CHECK((Bytes::is_little_endian() || Bytes::is_big_endian()));
    CHECK(Bytes::is_little_endian() != Bytes::is_big_endian());
  }

  // -------------------------------------------------------------------------
  TEST_CASE("ostream operator does not crash") {
    Bytes b{0x01u, 0x02u, 0x03u};
    std::ostringstream oss;
    oss << b;
    CHECK(!oss.str().empty());
  }

  // -------------------------------------------------------------------------
  TEST_CASE("SBO boundary: size == kStackSize stays on stack, size == kStackSize+1 goes to heap") {
    Bytes sbo = Bytes::create(Bytes::stack_size());
    Bytes heap = Bytes::create(Bytes::stack_size() + 1u);

    CHECK(sbo.capacity() == Bytes::stack_size());
    CHECK(heap.capacity() > Bytes::stack_size());
    CHECK(sbo.is_owner());
    CHECK(heap.is_owner());
  }

  // -------------------------------------------------------------------------
  TEST_CASE("move of heap-allocated Bytes transfers pointer without copy") {
    constexpr size_t kHeapSize = 200u;
    Bytes a = Bytes::create(kHeapSize);
    fill_pattern(a);
    const uint8_t* old_real = a.real_data();

    Bytes b(std::move(a));

    CHECK(b.size() == kHeapSize);
    // For heap buffers the pointer should be transferred (no allocation)
    CHECK(b.real_data() == old_real);
    CHECK(a.empty());  // NOLINT(bugprone-use-after-move)
  }

  // -------------------------------------------------------------------------
  TEST_CASE("empty Bytes comparison") {
    Bytes a;
    Bytes b;

    CHECK(a == b);
    CHECK(!(a != b));
  }

  // -------------------------------------------------------------------------
  TEST_CASE("from_user_input hex string creates Bytes from hex") {
    bool ok = false;
    Bytes b = Bytes::from_user_input("0xDEAD", &ok);
    CHECK(ok);
    REQUIRE(b.size() == 2u);
    CHECK(b.data()[0] == 0xDEu);
    CHECK(b.data()[1] == 0xADu);
  }

  // -------------------------------------------------------------------------
  TEST_CASE("from_user_input rejects partially parsed tokens") {
    bool ok = true;
    Bytes b = Bytes::from_user_input("1G", &ok);
    CHECK_FALSE(ok);
    CHECK(b.empty());
  }

  // -------------------------------------------------------------------------
  TEST_CASE("create rejects size overflow") {
    Bytes b = Bytes::create(std::numeric_limits<size_t>::max(), 1u);
    CHECK(b.empty());
    CHECK(b.data() == nullptr);
  }

  // -------------------------------------------------------------------------
  TEST_CASE("loan_internal mutable write-back changes underlying data") {
    Bytes b = Bytes::create(8u);
    uint8_t* ptr = b.data();
    REQUIRE(ptr != nullptr);

    for (size_t i = 0; i < 8u; ++i) {
      ptr[i] = 0xAA;
    }

    Bytes loaned = Bytes::loan_internal(ptr, b.size());
    CHECK(loaned.is_loaned());

    uint8_t* lptr = loaned.data();
    REQUIRE(lptr != nullptr);

    lptr[0] = 0xFF;

    // Change via loaned view is visible in original since they share the same pointer
    CHECK(ptr[0] == 0xFF);
  }

  // -------------------------------------------------------------------------
  TEST_CASE("loan_internal const creates read-only loaned view") {
    const uint8_t raw[] = {0x01, 0x02, 0x03};
    Bytes loaned = Bytes::loan_internal(raw, sizeof(raw));
    CHECK(loaned.is_loaned());
    CHECK(loaned.size() == 3u);
  }

  // -------------------------------------------------------------------------
  TEST_CASE("deep_copy on empty data produces empty Bytes") {
    const Bytes empty;
    Bytes copy = Bytes::deep_copy(static_cast<const uint8_t*>(nullptr), 0u);
    CHECK(copy.empty());
  }

  // -------------------------------------------------------------------------
  TEST_CASE("shallow_copy wraps external mutable pointer without copying") {
    static uint8_t raw[8] = {0, 1, 2, 3, 4, 5, 6, 7};
    Bytes b = Bytes::shallow_copy(raw, sizeof(raw));
    CHECK(!b.is_owner());
    CHECK(b.size() == 8u);
    CHECK(b[3] == 3u);
  }

  // -------------------------------------------------------------------------
  TEST_CASE("operator[] access on all SBO bytes stays within bounds") {
    Bytes b = Bytes::create(Bytes::stack_size());
    uint8_t* ptr = b.data();
    REQUIRE(ptr != nullptr);

    for (size_t i = 0; i < Bytes::stack_size(); ++i) {
      ptr[i] = static_cast<uint8_t>(i & 0xFF);
    }

    for (size_t i = 0; i < Bytes::stack_size(); ++i) {
      CHECK(b[i] == static_cast<uint8_t>(i & 0xFF));
    }
  }

  // -------------------------------------------------------------------------
  TEST_CASE("resize then shrink_to round-trip preserves data") {
    Bytes b = Bytes::create(32u);
    uint8_t* ptr = b.data();
    REQUIRE(ptr != nullptr);

    for (size_t i = 0; i < 32u; ++i) {
      ptr[i] = static_cast<uint8_t>(i);
    }

    (void)b.resize(64u);
    CHECK(b.size() == 64u);
    CHECK(b[0] == 0u);
    CHECK(b[31] == 31u);

    (void)b.shrink_to(32u);
    CHECK(b.size() == 32u);
    CHECK(b[31] == 31u);
  }

  // -------------------------------------------------------------------------
  TEST_CASE("bytes_malloc allocates and bytes_free releases memory") {
    constexpr size_t kSize = 128u;
    uint8_t* mem = Bytes::bytes_malloc(kSize);
    REQUIRE(mem != nullptr);

    // Write and read back to ensure the memory is usable
    for (size_t i = 0; i < kSize; ++i) {
      mem[i] = static_cast<uint8_t>(i & 0xFF);
    }

    for (size_t i = 0; i < kSize; ++i) {
      CHECK(mem[i] == static_cast<uint8_t>(i & 0xFF));
    }

    Bytes::bytes_free(mem, kSize);
    CHECK(true);  // No crash
  }

  // -------------------------------------------------------------------------
  TEST_CASE("create with non-zero offset") {
    Bytes b = Bytes::create(16u, 8u);
    // With offset=8, real_data() has 8 bytes of prefix; data() starts after
    CHECK(b.size() == 16u);
    REQUIRE(b.data() != nullptr);
  }

  // -------------------------------------------------------------------------
  TEST_CASE("release_memory_pool keeps live Bytes valid and pool usable") {
    Bytes::init_memory_pool();

    Bytes pinned = Bytes::create(2048u);
    REQUIRE(pinned.data() != nullptr);
    std::memset(pinned.data(), 0xA5, pinned.size());

    {
      Bytes scratch_a = Bytes::create(2048u);
      Bytes scratch_b = Bytes::create(64u * 1024u);
      REQUIRE(scratch_a.data() != nullptr);
      REQUIRE(scratch_b.data() != nullptr);
    }

    Bytes::release_memory_pool();

    for (size_t i = 0; i < pinned.size(); ++i) {
      CHECK(static_cast<uint8_t*>(pinned.data())[i] == 0xA5);
    }
    std::memset(pinned.data(), 0x5A, pinned.size());
    for (size_t i = 0; i < pinned.size(); ++i) {
      CHECK(static_cast<uint8_t*>(pinned.data())[i] == 0x5A);
    }

    Bytes after = Bytes::create(2048u);
    REQUIRE(after.data() != nullptr);
    std::memset(after.data(), 0xCC, after.size());
  }
}

// NOLINTEND
