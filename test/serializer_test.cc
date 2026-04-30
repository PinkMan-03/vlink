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

#include <vlink/zerocopy/raw_data.h>

#include <cstring>
#include <string>

#include "./common_test.h"
#include "./impl/intra_data.h"

// ---------------------------------------------------------------------------
// Local helpers / test types
// ---------------------------------------------------------------------------

// A trivial standard-layout POD struct — should map to kStandardType.
// NSDMI would break trivial-default-constructibility, so omit them.
struct PodMsg {
  int x;
  float y;
  double z;
};

static_assert(std::is_trivial_v<PodMsg>, "PodMsg must be trivial");
static_assert(std::is_standard_layout_v<PodMsg>, "PodMsg must be standard-layout");

// A custom type implementing operator>>(Bytes&) / operator<<(const Bytes&).
// Stores a single int32_t value, serialised as 4 raw bytes (little-endian).
struct CustomMsg {
  int32_t value{0};

  void operator>>(vlink::Bytes& out) const {
    out = vlink::Bytes::create(sizeof(int32_t));
    std::memcpy(out.data(), &value, sizeof(int32_t));
  }

  void operator<<(const vlink::Bytes& in) {
    if (in.size() == sizeof(int32_t)) {
      std::memcpy(&value, in.data(), sizeof(int32_t));
    }
  }
};

// A type with an operator<<(std::stringstream) — should map to kStreamType.
struct StreamMsg {
  int number{0};
};

inline std::stringstream& operator<<(std::stringstream& ss, const StreamMsg& m) {
  ss << m.number;
  return ss;
}

inline std::stringstream& operator>>(std::stringstream& ss, StreamMsg& m) {
  ss >> m.number;
  return ss;
}

// A struct with both operator>> and operator<< for Bytes — custom codec.
// (Used to verify the custom-type predicate.)
struct AnotherCustom {
  uint8_t byte{0};

  void operator>>(vlink::Bytes& out) const { out = vlink::Bytes{byte}; }

  void operator<<(const vlink::Bytes& in) {
    if (!in.empty()) {
      byte = in.data()[0];
    }
  }
};

VLINK_INTRA_DATA_DECLARE(vlink::zerocopy::RawData, WrappedRawData);

// ---------------------------------------------------------------------------
// TEST: compile-time type classification
// ---------------------------------------------------------------------------

TEST_SUITE("serializer-types") {
  TEST_CASE("Bytes maps to kBytesType") {
    constexpr auto t = Serializer::get_type_of<Bytes>();
    CHECK(t == Serializer::kBytesType);
    CHECK(Serializer::is_supported(t));
  }

  TEST_CASE("std::string maps to kStringType") {
    constexpr auto t = Serializer::get_type_of<std::string>();
    CHECK(t == Serializer::kStringType);
    CHECK(Serializer::is_supported(t));
  }

  TEST_CASE("POD struct maps to kStandardType") {
    constexpr auto t = Serializer::get_type_of<PodMsg>();
    CHECK(t == Serializer::kStandardType);
    CHECK(Serializer::is_supported(t));
  }

  TEST_CASE("Custom type maps to kCustomType") {
    constexpr auto t = Serializer::get_type_of<CustomMsg>();
    CHECK(t == Serializer::kCustomType);
    CHECK(Serializer::is_supported(t));
  }

  TEST_CASE("AnotherCustom maps to kCustomType") {
    constexpr auto t = Serializer::get_type_of<AnotherCustom>();
    CHECK(t == Serializer::kCustomType);
    CHECK(Serializer::is_supported(t));
  }

  TEST_CASE("kUnknownType is the only unsupported type") {
    CHECK_FALSE(Serializer::is_supported(Serializer::kUnknownType));
  }

  TEST_CASE("All named types are supported") {
    CHECK(Serializer::is_supported(Serializer::kBytesType));
    CHECK(Serializer::is_supported(Serializer::kDynamicType));
    CHECK(Serializer::is_supported(Serializer::kCustomType));
    CHECK(Serializer::is_supported(Serializer::kCdrType));
    CHECK(Serializer::is_supported(Serializer::kProtoType));
    CHECK(Serializer::is_supported(Serializer::kProtoPtrType));
    CHECK(Serializer::is_supported(Serializer::kFlatTableType));
    CHECK(Serializer::is_supported(Serializer::kFlatPtrType));
    CHECK(Serializer::is_supported(Serializer::kFlatBuilderType));
    CHECK(Serializer::is_supported(Serializer::kStringType));
    CHECK(Serializer::is_supported(Serializer::kCharsType));
    CHECK(Serializer::is_supported(Serializer::kStreamType));
    CHECK(Serializer::is_supported(Serializer::kStandardType));
    CHECK(Serializer::is_supported(Serializer::kStandardPtrType));
  }

  TEST_CASE("std::string infers raw schema family") {
    constexpr auto schema_type = Serializer::get_schema_type<std::string>();
    CHECK(schema_type == vlink::SchemaType::kRaw);
  }

  TEST_CASE("Bytes infers raw schema family") {
    constexpr auto schema_type = Serializer::get_schema_type<Bytes>();
    CHECK(schema_type == vlink::SchemaType::kRaw);
  }

  TEST_CASE("POD struct infers raw schema family") {
    constexpr auto schema_type = Serializer::get_schema_type<PodMsg>();
    CHECK(schema_type == vlink::SchemaType::kRaw);
  }

  TEST_CASE("custom codec infers raw schema family") {
    constexpr auto schema_type = Serializer::get_schema_type<CustomMsg>();
    CHECK(schema_type == vlink::SchemaType::kRaw);
  }

  TEST_CASE("stream codec infers raw schema family") {
    constexpr auto schema_type = Serializer::get_schema_type<StreamMsg>();
    CHECK(schema_type == vlink::SchemaType::kRaw);
  }

  TEST_CASE("zerocopy payload infers zerocopy schema family") {
    constexpr auto schema_type = Serializer::get_schema_type<vlink::zerocopy::RawData>();
    CHECK(schema_type == vlink::SchemaType::kZeroCopy);
  }

  TEST_CASE("intra wrapper preserves inner zerocopy schema family") {
    constexpr auto schema_type = Serializer::get_schema_type<WrappedRawData>();
    CHECK(schema_type == vlink::SchemaType::kZeroCopy);
  }
}

// ---------------------------------------------------------------------------
// TEST: type trait predicates
// ---------------------------------------------------------------------------

TEST_SUITE("serializer-type-predicates") {
  TEST_CASE("is_bytes_type") {
    CHECK(Serializer::is_bytes_type<Bytes>());
    CHECK_FALSE(Serializer::is_bytes_type<std::string>());
    CHECK_FALSE(Serializer::is_bytes_type<PodMsg>());
  }

  TEST_CASE("is_string_type") {
    CHECK(Serializer::is_string_type<std::string>());
    CHECK_FALSE(Serializer::is_string_type<Bytes>());
    CHECK_FALSE(Serializer::is_string_type<PodMsg>());
  }

  TEST_CASE("is_custom_type") {
    CHECK(Serializer::is_custom_type<CustomMsg>());
    CHECK(Serializer::is_custom_type<AnotherCustom>());
    CHECK_FALSE(Serializer::is_custom_type<PodMsg>());
    CHECK_FALSE(Serializer::is_custom_type<std::string>());
    CHECK_FALSE(Serializer::is_custom_type<Bytes>());
  }

  TEST_CASE("is_standard_type") {
    CHECK(Serializer::is_standard_type<PodMsg>());
    CHECK_FALSE(Serializer::is_standard_type<std::string>());
    CHECK_FALSE(Serializer::is_standard_type<Bytes>());
    CHECK_FALSE(Serializer::is_standard_type<CustomMsg>());
  }

  TEST_CASE("is_standard_ptr_type") {
    CHECK(Serializer::is_standard_ptr_type<PodMsg*>());
    CHECK_FALSE(Serializer::is_standard_ptr_type<PodMsg>());
    CHECK_FALSE(Serializer::is_standard_ptr_type<std::string*>());
  }

  TEST_CASE("is_chars_type") {
    CHECK(Serializer::is_chars_type<const char*>());
    CHECK(Serializer::is_chars_type<char*>());
    CHECK_FALSE(Serializer::is_chars_type<std::string>());
  }
}

// ---------------------------------------------------------------------------
// TEST: get_serialized_size
// ---------------------------------------------------------------------------

TEST_SUITE("serializer-get-serialized-size") {
  TEST_CASE("Bytes returns 0 (unknown ahead of time)") {
    Bytes b{0x01, 0x02, 0x03};
    CHECK(Serializer::get_serialized_size(b) == 0);
  }

  TEST_CASE("std::string returns 0") {
    std::string s = "hello";
    CHECK(Serializer::get_serialized_size(s) == 0);
  }

  TEST_CASE("POD returns sizeof(T)") {
    PodMsg pod{};
    CHECK(Serializer::get_serialized_size(pod) == 0);
  }
}

// ---------------------------------------------------------------------------
// TEST: serialize/deserialize — Bytes
// ---------------------------------------------------------------------------

TEST_SUITE("serializer-bytes") {
  TEST_CASE("round-trip with initializer-list Bytes") {
    Bytes original{0x01, 0x02, 0x03, 0x04, 0x05};

    Bytes serialized;
    bool ok = Serializer::serialize(original, serialized);
    CHECK(ok);
    CHECK_FALSE(serialized.empty());
    CHECK(serialized.size() == original.size());

    Bytes result;
    bool dok = Serializer::deserialize(serialized, result);
    CHECK(dok);
    CHECK(result == original);
  }

  TEST_CASE("empty Bytes round-trip") {
    Bytes empty{};

    Bytes serialized;
    Serializer::serialize(empty, serialized);

    Bytes result;
    Serializer::deserialize(serialized, result);
    // Both should be empty
    CHECK(result.empty());
  }

  TEST_CASE("Bytes created via create()") {
    auto original = Bytes::create(8);
    for (size_t i = 0; i < 8; ++i) {
      original.data()[i] = static_cast<uint8_t>(i * 11);
    }

    Bytes serialized;
    REQUIRE(Serializer::serialize(original, serialized));

    Bytes result;
    REQUIRE(Serializer::deserialize(serialized, result));
    CHECK(result == original);
  }
}

// ---------------------------------------------------------------------------
// TEST: serialize/deserialize — std::string
// ---------------------------------------------------------------------------

TEST_SUITE("serializer-string") {
  TEST_CASE("basic round-trip") {
    std::string original = "hello world";

    Bytes serialized;
    bool ok = Serializer::serialize(original, serialized);
    CHECK(ok);
    CHECK(serialized.size() == original.size());

    std::string result;
    bool dok = Serializer::deserialize(serialized, result);
    CHECK(dok);
    CHECK(result == original);
  }

  TEST_CASE("empty string round-trip") {
    std::string original;

    Bytes serialized;
    Serializer::serialize(original, serialized);

    std::string result;
    Serializer::deserialize(serialized, result);
    CHECK(result.empty());
  }

  TEST_CASE("string with special characters") {
    std::string original = "line1\nline2\ttab\r\n";

    Bytes serialized;
    REQUIRE(Serializer::serialize(original, serialized));

    std::string result;
    REQUIRE(Serializer::deserialize(serialized, result));
    CHECK(result == original);
  }

  TEST_CASE("long string") {
    std::string original(1024, 'x');

    Bytes serialized;
    REQUIRE(Serializer::serialize(original, serialized));
    CHECK(serialized.size() == 1024);

    std::string result;
    REQUIRE(Serializer::deserialize(serialized, result));
    CHECK(result == original);
  }

  TEST_CASE("get_serialized_type returns string") { CHECK(Serializer::get_serialized_type<std::string>() == "string"); }
}

// ---------------------------------------------------------------------------
// TEST: serialize/deserialize — POD struct (kStandardType)
// ---------------------------------------------------------------------------

TEST_SUITE("serializer-pod") {
  TEST_CASE("basic round-trip") {
    PodMsg original{42, 3.14f, 2.718281828};

    Bytes serialized;
    bool ok = Serializer::serialize(original, serialized);
    CHECK(ok);
    CHECK(serialized.size() == sizeof(PodMsg));

    PodMsg result{};
    bool dok = Serializer::deserialize(serialized, result);
    CHECK(dok);
    CHECK(result.x == original.x);
    CHECK(result.y == doctest::Approx(original.y));
    CHECK(result.z == doctest::Approx(original.z));
  }

  TEST_CASE("zero-value struct") {
    PodMsg original{0, 0.0f, 0.0};

    Bytes serialized;
    REQUIRE(Serializer::serialize(original, serialized));

    PodMsg result{99, 1.0f, 1.0};
    REQUIRE(Serializer::deserialize(serialized, result));
    CHECK(result.x == 0);
    CHECK(result.y == doctest::Approx(0.0f));
    CHECK(result.z == doctest::Approx(0.0));
  }

  TEST_CASE("negative values") {
    PodMsg original{-1, -3.14f, -2.71828};

    Bytes serialized;
    REQUIRE(Serializer::serialize(original, serialized));

    PodMsg result{};
    REQUIRE(Serializer::deserialize(serialized, result));
    CHECK(result.x == -1);
    CHECK(result.y == doctest::Approx(-3.14f));
    CHECK(result.z == doctest::Approx(-2.71828));
  }

  TEST_CASE("serialized size equals sizeof(PodMsg)") {
    PodMsg pod{1, 2.0f, 3.0};
    Bytes serialized;
    REQUIRE(Serializer::serialize(pod, serialized));
    CHECK(serialized.size() == sizeof(PodMsg));
  }

  TEST_CASE("deserialize fails when bytes size mismatches") {
    // Feed too few bytes
    Bytes bad = Bytes::create(1);
    bad.data()[0] = 0xFF;

    PodMsg result{};
    bool ok = Serializer::deserialize(bad, result);
    CHECK_FALSE(ok);
  }
}

// ---------------------------------------------------------------------------
// TEST: serialize/deserialize — POD pointer (kStandardPtrType)
// ---------------------------------------------------------------------------

TEST_SUITE("serializer-pod-ptr") {
  TEST_CASE("pointer round-trip via shallow_copy") {
    PodMsg original{7, 1.5f, 0.1};
    const PodMsg* src = &original;

    Bytes serialized;
    bool ok = Serializer::serialize(src, serialized);
    CHECK(ok);
    // Shallow copy: the Bytes view points into the original struct
    CHECK(serialized.size() == sizeof(PodMsg));

    // Deserialise back to a pointer pointing into the Bytes buffer
    PodMsg* dest = nullptr;
    bool dok = Serializer::deserialize(serialized, dest);
    CHECK(dok);
    REQUIRE(dest != nullptr);
    CHECK(dest->x == original.x);
    CHECK(dest->y == doctest::Approx(original.y));
  }
}

// ---------------------------------------------------------------------------
// TEST: serialize/deserialize — custom type
// ---------------------------------------------------------------------------

TEST_SUITE("serializer-custom") {
  TEST_CASE("CustomMsg round-trip") {
    CustomMsg original;
    original.value = 0x12345678;

    Bytes serialized;
    bool ok = Serializer::serialize(original, serialized);
    CHECK(ok);
    CHECK(serialized.size() == sizeof(int32_t));

    CustomMsg result;
    bool dok = Serializer::deserialize(serialized, result);
    CHECK(dok);
    CHECK(result.value == original.value);
  }

  TEST_CASE("CustomMsg negative value") {
    CustomMsg original;
    original.value = -999;

    Bytes serialized;
    REQUIRE(Serializer::serialize(original, serialized));

    CustomMsg result;
    REQUIRE(Serializer::deserialize(serialized, result));
    CHECK(result.value == -999);
  }

  TEST_CASE("AnotherCustom single-byte round-trip") {
    AnotherCustom original;
    original.byte = 0xAB;

    Bytes serialized;
    REQUIRE(Serializer::serialize(original, serialized));

    AnotherCustom result;
    REQUIRE(Serializer::deserialize(serialized, result));
    CHECK(result.byte == 0xAB);
  }

  TEST_CASE("kCustomType is detected") {
    CHECK(Serializer::get_type_of<CustomMsg>() == Serializer::kCustomType);
    CHECK(Serializer::get_type_of<AnotherCustom>() == Serializer::kCustomType);
  }
}

// ---------------------------------------------------------------------------
// TEST: convert() helper
// ---------------------------------------------------------------------------

TEST_SUITE("serializer-convert") {
  TEST_CASE("Bytes -> Bytes is shallow copy") {
    Bytes src{0xAA, 0xBB, 0xCC};
    Bytes dst;
    bool ok = Serializer::convert(src, dst);
    CHECK(ok);
    CHECK(dst == src);
  }

  TEST_CASE("std::string -> Bytes") {
    std::string s = "convert_test";
    Bytes dst;
    bool ok = Serializer::convert(s, dst);
    CHECK(ok);
    CHECK(dst.size() == s.size());
  }

  TEST_CASE("Bytes -> std::string") {
    std::string expected = "round_trip";
    Bytes src = Bytes::from_string(expected);
    std::string dst;
    bool ok = Serializer::convert(src, dst);
    CHECK(ok);
    CHECK(dst == expected);
  }
}

// ---------------------------------------------------------------------------
// TEST: deref() unwraps shared_ptr
// ---------------------------------------------------------------------------

TEST_SUITE("serializer-deref") {
  TEST_CASE("deref value type returns the value itself") {
    int v = 42;
    auto& ref = Serializer::deref(v);
    CHECK(ref == 42);
  }

  TEST_CASE("deref shared_ptr returns underlying value") {
    auto sp = std::make_shared<PodMsg>();
    sp->x = 99;
    auto& ref = Serializer::deref(sp);
    CHECK(ref.x == 99);
  }
}

// ---------------------------------------------------------------------------
// TEST: get_serialized_type for various types
// ---------------------------------------------------------------------------

TEST_SUITE("serializer-get-serialized-type") {
  TEST_CASE("Bytes returns empty string") { CHECK(Serializer::get_serialized_type<Bytes>().empty()); }

  TEST_CASE("std::string returns \"string\"") { CHECK(Serializer::get_serialized_type<std::string>() == "string"); }
}

// ---------------------------------------------------------------------------
// TEST: large Bytes round-trip (> kStackSize = 96 bytes)
// ---------------------------------------------------------------------------

TEST_SUITE("serializer-large-bytes") {
  TEST_CASE("serializer-large-bytes") {
    constexpr size_t kLarge = 1024;
    auto original = Bytes::create(kLarge);
    for (size_t i = 0; i < kLarge; ++i) {
      original.data()[i] = static_cast<uint8_t>(i & 0xFF);
    }

    Bytes serialized;
    REQUIRE(Serializer::serialize(original, serialized));
    CHECK(serialized.size() == kLarge);

    Bytes result;
    REQUIRE(Serializer::deserialize(serialized, result));
    CHECK(result == original);
  }
}

// ---------------------------------------------------------------------------
// TEST: shared_ptr<PodMsg> is serialised via kStandardType path
// ---------------------------------------------------------------------------

TEST_SUITE("serializer-shared-ptr-pod") {
  TEST_CASE("serializer-shared-ptr-pod") {
    auto sp = std::make_shared<PodMsg>();
    sp->x = 123;
    sp->y = 4.56f;
    sp->z = 7.89;

    Bytes serialized;
    bool ok = Serializer::serialize(sp, serialized);
    CHECK(ok);
    CHECK(serialized.size() == sizeof(PodMsg));

    auto sp_out = std::make_shared<PodMsg>();
    bool dok = Serializer::deserialize(serialized, sp_out);
    CHECK(dok);
    CHECK(sp_out->x == 123);
    CHECK(sp_out->y == doctest::Approx(4.56f));
    CHECK(sp_out->z == doctest::Approx(7.89));
  }
}

// ---------------------------------------------------------------------------
// Conditional: Protobuf tests
// ---------------------------------------------------------------------------

#if defined(VLINK_TEST_SUPPORT_PROTOBUF)

TEST_SUITE("serializer-protobuf") {
  TEST_CASE("get_type_of<pb::Message> == kProtoType") {
    constexpr auto t = Serializer::get_type_of<pb::Message>();
    CHECK(t == Serializer::kProtoType);
    CHECK(Serializer::is_supported(t));
  }

  TEST_CASE("round-trip") {
    pb::Message original;
    original.set_value("test_proto");
    original.set_type(42);

    Bytes serialized;
    bool ok = Serializer::serialize(original, serialized);
    CHECK(ok);
    CHECK_FALSE(serialized.empty());

    pb::Message result;
    bool dok = Serializer::deserialize(serialized, result);
    CHECK(dok);
    CHECK(result.value() == "test_proto");
    CHECK(result.type() == 42u);
  }

  TEST_CASE("empty proto round-trip") {
    pb::Message original;

    Bytes serialized;
    REQUIRE(Serializer::serialize(original, serialized));

    pb::Message result;
    result.set_value("old");
    bool dok = Serializer::deserialize(serialized, result);
    CHECK(dok);
    CHECK(result.value().empty());
  }
}

#endif  // VLINK_TEST_SUPPORT_PROTOBUF

// ---------------------------------------------------------------------------
// Conditional: FlatBuffers tests
// ---------------------------------------------------------------------------

// ---------------------------------------------------------------------------
// TEST: serialize/deserialize — stream type
// ---------------------------------------------------------------------------

TEST_SUITE("serializer-stream-type") {
  TEST_CASE("StreamMsg is detected as kStreamType or kCustomType") {
    constexpr auto t = Serializer::get_type_of<StreamMsg>();
    CHECK(Serializer::is_supported(t));
  }
}

// ---------------------------------------------------------------------------
// TEST: type enum edge values
// ---------------------------------------------------------------------------

TEST_SUITE("serializer-type-enum-values") {
  TEST_CASE("kUnknownType is 0") { CHECK(static_cast<uint8_t>(Serializer::kUnknownType) == 0); }

  TEST_CASE("kBytesType is 1") { CHECK(static_cast<uint8_t>(Serializer::kBytesType) == 1); }

  TEST_CASE("kStandardPtrType is 14") { CHECK(static_cast<uint8_t>(Serializer::kStandardPtrType) == 14); }

  TEST_CASE("all type values are unique") {
    CHECK(Serializer::kBytesType != Serializer::kDynamicType);
    CHECK(Serializer::kCustomType != Serializer::kCdrType);
    CHECK(Serializer::kProtoType != Serializer::kProtoPtrType);
    CHECK(Serializer::kFlatTableType != Serializer::kFlatPtrType);
    CHECK(Serializer::kStringType != Serializer::kCharsType);
    CHECK(Serializer::kStreamType != Serializer::kStandardType);
    CHECK(Serializer::kStandardType != Serializer::kStandardPtrType);
  }
}

// ---------------------------------------------------------------------------
// TEST: serialize/deserialize — int POD
// ---------------------------------------------------------------------------

TEST_SUITE("serializer-single-int") {
  TEST_CASE("int type detection") {
    constexpr auto t = Serializer::get_type_of<int>();
    CHECK(t == Serializer::kStandardType);
  }

  TEST_CASE("int round-trip") {
    int original = 42;
    Bytes serialized;
    bool ok = Serializer::serialize(original, serialized);
    CHECK(ok);
    CHECK(!serialized.empty());

    int result = 0;
    bool dok = Serializer::deserialize(serialized, result);
    CHECK(dok);
    CHECK(result == 42);
  }

  TEST_CASE("negative int") {
    int original = -12345;
    Bytes serialized;
    REQUIRE(Serializer::serialize(original, serialized));

    int result = 0;
    REQUIRE(Serializer::deserialize(serialized, result));
    CHECK(result == -12345);
  }
}

// ---------------------------------------------------------------------------
// TEST: additional convert tests
// ---------------------------------------------------------------------------

TEST_SUITE("serializer-convert-pod") {
  TEST_CASE("POD -> Bytes -> POD round-trip via convert") {
    PodMsg original{100, 2.5f, 9.99};
    Bytes bytes;
    bool ok = Serializer::convert(original, bytes);
    CHECK(ok);
    CHECK(bytes.size() == sizeof(PodMsg));

    PodMsg result{};
    bool dok = Serializer::convert(bytes, result);
    CHECK(dok);
    CHECK(result.x == 100);
    CHECK(result.y == doctest::Approx(2.5f));
  }
}

// ---------------------------------------------------------------------------
// TEST: get_serialized_size for custom type
// ---------------------------------------------------------------------------

TEST_SUITE("serializer-get-serialized-size-custom") {
  TEST_CASE("CustomMsg size is 0 (unknown ahead of time)") {
    CustomMsg m;
    m.value = 42;
    CHECK(Serializer::get_serialized_size(m) == 0);
  }
}

// ---------------------------------------------------------------------------
// TEST: get_serialized_type for various types
// ---------------------------------------------------------------------------

TEST_SUITE("serializer-get-serialized-type-additional") {
  TEST_CASE("POD type returns non-empty type name") {
    auto t = Serializer::get_serialized_type<PodMsg>();
    CHECK_FALSE(t.empty());
  }

  TEST_CASE("POD pointer normalizes to pointee type name") {
    auto value_name = Serializer::get_serialized_type<PodMsg>();
    auto pointer_name = Serializer::get_serialized_type<PodMsg*>();
    CHECK(pointer_name == value_name);
  }

  TEST_CASE("zerocopy pointer normalizes to pointee type name") {
    auto value_name = Serializer::get_serialized_type<vlink::zerocopy::RawData>();
    auto pointer_name = Serializer::get_serialized_type<vlink::zerocopy::RawData*>();
    CHECK(pointer_name == value_name);
  }
}

// ---------------------------------------------------------------------------
// TEST: multiple sequential serializations
// ---------------------------------------------------------------------------

TEST_SUITE("serializer-sequential-operations") {
  TEST_CASE("serialize multiple strings into different buffers") {
    std::string s1 = "first";
    std::string s2 = "second";
    std::string s3 = "third";

    Bytes b1, b2, b3;
    CHECK(Serializer::serialize(s1, b1));
    CHECK(Serializer::serialize(s2, b2));
    CHECK(Serializer::serialize(s3, b3));

    CHECK(b1.size() == 5);
    CHECK(b2.size() == 6);
    CHECK(b3.size() == 5);

    std::string r1, r2, r3;
    CHECK(Serializer::deserialize(b1, r1));
    CHECK(Serializer::deserialize(b2, r2));
    CHECK(Serializer::deserialize(b3, r3));
    CHECK(r1 == "first");
    CHECK(r2 == "second");
    CHECK(r3 == "third");
  }
}

// ---------------------------------------------------------------------------
// TEST: type traits - additional checks
// ---------------------------------------------------------------------------

TEST_SUITE("serializer-additional-type-predicates") {
  TEST_CASE("is_dynamic_type is false for common types") {
    CHECK_FALSE(Serializer::is_dynamic_type<int>());
    CHECK_FALSE(Serializer::is_dynamic_type<std::string>());
    CHECK_FALSE(Serializer::is_dynamic_type<Bytes>());
    CHECK_FALSE(Serializer::is_dynamic_type<PodMsg>());
  }

  TEST_CASE("is_proto_type is false for non-proto types") {
    CHECK_FALSE(Serializer::is_proto_type<int>());
    CHECK_FALSE(Serializer::is_proto_type<std::string>());
    CHECK_FALSE(Serializer::is_proto_type<PodMsg>());
  }

  TEST_CASE("is_proto_ptr_type is false for non-proto ptr types") {
    CHECK_FALSE(Serializer::is_proto_ptr_type<int*>());
    CHECK_FALSE(Serializer::is_proto_ptr_type<PodMsg*>());
  }

  TEST_CASE("is_flat_table_type is false for non-flat types") {
    CHECK_FALSE(Serializer::is_flat_table_type<int>());
    CHECK_FALSE(Serializer::is_flat_table_type<PodMsg>());
  }

  TEST_CASE("is_flat_builder_type is false for non-builder types") {
    CHECK_FALSE(Serializer::is_flat_builder_type<int>());
    CHECK_FALSE(Serializer::is_flat_builder_type<std::string>());
  }

  TEST_CASE("is_flat_ptr_type is false for non-flat-ptr types") {
    CHECK_FALSE(Serializer::is_flat_ptr_type<int*>());
    CHECK_FALSE(Serializer::is_flat_ptr_type<PodMsg*>());
  }

  TEST_CASE("is_cdr_type is false for non-cdr types") {
    CHECK_FALSE(Serializer::is_cdr_type<int>());
    CHECK_FALSE(Serializer::is_cdr_type<std::string>());
    CHECK_FALSE(Serializer::is_cdr_type<PodMsg>());
  }

  TEST_CASE("is_stream_type is false for bytes") {
    CHECK_FALSE(Serializer::is_stream_type<Bytes>());
    // std::string is streamable via operator<<(stringstream), so it may return true
  }
}

// ---------------------------------------------------------------------------
// TEST: Bytes::from_string helper in serialize context
// ---------------------------------------------------------------------------

TEST_SUITE("serializer-bytes-from-string") {
  TEST_CASE("Bytes::from_string round-trip through serializer") {
    auto original = Bytes::from_string("test data");
    Bytes serialized;
    REQUIRE(Serializer::serialize(original, serialized));

    Bytes result;
    REQUIRE(Serializer::deserialize(serialized, result));
    CHECK(result == original);
  }
}

#if defined(VLINK_TEST_SUPPORT_FLATBUFFERS)

TEST_SUITE("serializer-flatbuffers") {
  TEST_CASE("get_type_of<fbs::MessageT> == kFlatTableType") {
    constexpr auto t = Serializer::get_type_of<fbs::MessageT>();
    CHECK(t == Serializer::kFlatTableType);
    CHECK(Serializer::is_supported(t));
  }

  TEST_CASE("round-trip") {
    fbs::MessageT original;
    original.value = "test_fbs";
    original.type = 99;

    Bytes serialized;
    bool ok = Serializer::serialize(original, serialized);
    CHECK(ok);
    CHECK_FALSE(serialized.empty());

    fbs::MessageT result;
    bool dok = Serializer::deserialize(serialized, result);
    CHECK(dok);
    CHECK(result.value == "test_fbs");
    CHECK(result.type == 99u);
  }
}

#endif  // VLINK_TEST_SUPPORT_FLATBUFFERS

// NOLINTEND
