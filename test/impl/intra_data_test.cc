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

#include "./impl/intra_data.h"

#include <doctest/doctest.h>

#include <memory>
#include <string>
#include <type_traits>

//
#include "../common_test.h"

// ---------------------------------------------------------------------------
// Generated typed wrappers used by the test cases below.
// ---------------------------------------------------------------------------

VLINK_INTRA_DATA_DECLARE(vlink::Bytes, BytesIntra)
VLINK_INTRA_DATA_DECLARE(std::string, StringIntra)

// ---------------------------------------------------------------------------
// TEST SUITE: IntraDataType base
// ---------------------------------------------------------------------------

TEST_SUITE("impl-IntraData - base type") {
  TEST_CASE("IntraDataType is default-constructible") {
    IntraDataType obj;
    (void)obj;
    CHECK(std::is_default_constructible_v<IntraDataType>);
  }

  TEST_CASE("IntraData alias is shared_ptr<IntraDataType>") {
    CHECK((std::is_same_v<IntraData, std::shared_ptr<IntraDataType>>));
  }

  TEST_CASE("IntraData can hold the base type") {
    IntraData ptr = std::make_shared<IntraDataType>();
    CHECK(ptr != nullptr);
  }

  TEST_CASE("IntraDataType has a virtual destructor (polymorphic)") {
    CHECK(std::has_virtual_destructor_v<IntraDataType>);
  }
}

// ---------------------------------------------------------------------------
// TEST SUITE: VLINK_INTRA_DATA_DECLARE - generated type contract
// ---------------------------------------------------------------------------

TEST_SUITE("impl-IntraData - VLINK_INTRA_DATA_DECLARE generated type") {
  TEST_CASE("generated *Type derives from IntraDataType") {
    CHECK((std::is_base_of_v<IntraDataType, BytesIntraType>));
    CHECK((std::is_base_of_v<IntraDataType, StringIntraType>));
  }

  TEST_CASE("generated *Type has a kValueType corresponding to its target") {
    CHECK(BytesIntraType::kValueType == Serializer::get_type_of<Bytes>());
    CHECK(StringIntraType::kValueType == Serializer::get_type_of<std::string>());
  }

  TEST_CASE("generated *Type kValueType is supported") {
    CHECK(Serializer::is_supported(BytesIntraType::kValueType));
    CHECK(Serializer::is_supported(StringIntraType::kValueType));
  }

  TEST_CASE("get_serialized_type returns a non-empty string for std::string wrapper") {
    auto name = StringIntraType::get_serialized_type();
    CHECK_FALSE(name.empty());
  }

  TEST_CASE("get_schema_type compiles and returns a SchemaType value") {
    constexpr SchemaType s1 = BytesIntraType::get_schema_type();
    constexpr SchemaType s2 = StringIntraType::get_schema_type();
    (void)s1;
    (void)s2;
    CHECK(SchemaData::is_valid_type(s1));
    CHECK(SchemaData::is_valid_type(s2));
  }

  TEST_CASE("get_serialized_size for string-style serializer is 0 (length is encoded in the wire stream itself)") {
    StringIntraType empty;
    CHECK(empty.get_serialized_size() == 0U);

    StringIntraType nonempty;
    nonempty.value = "hello";
    CHECK(nonempty.get_serialized_size() == 0U);
  }
}

// ---------------------------------------------------------------------------
// TEST SUITE: VLINK_INTRA_DATA_DECLARE - shared_ptr wrapper and create()
// ---------------------------------------------------------------------------

TEST_SUITE("impl-IntraData - VLINK_INTRA_DATA_DECLARE shared_ptr wrapper") {
  TEST_CASE("default-constructed wrapper is empty") {
    StringIntra w;
    CHECK(w.get() == nullptr);
  }

  TEST_CASE("create() returns a non-null wrapper") {
    StringIntra w = StringIntra::create();
    REQUIRE(w.get() != nullptr);
    CHECK(w->value.empty());
  }

  TEST_CASE("create() instances are independent") {
    StringIntra a = StringIntra::create();
    StringIntra b = StringIntra::create();
    REQUIRE(a.get() != nullptr);
    REQUIRE(b.get() != nullptr);
    CHECK(a.get() != b.get());

    a->value = "alpha";
    b->value = "beta";

    CHECK(a->value == "alpha");
    CHECK(b->value == "beta");
  }

  TEST_CASE("wrapper supports copy semantics on the underlying shared_ptr") {
    StringIntra a = StringIntra::create();
    a->value = "shared";

    StringIntra b = a;
    REQUIRE(b.get() == a.get());
    CHECK(b->value == "shared");

    b->value = "modified";
    CHECK(a->value == "modified");
  }

  TEST_CASE("wrapper is implicitly constructible from a base shared_ptr") {
    std::shared_ptr<StringIntraType> base = std::make_shared<StringIntraType>();
    base->value = "from_base";

    StringIntra w(base);
    REQUIRE(w.get() == base.get());
    CHECK(w->value == "from_base");
  }
}

// ---------------------------------------------------------------------------
// TEST SUITE: VLINK_INTRA_DATA_DECLARE - serialize / deserialize round-trip
// ---------------------------------------------------------------------------

TEST_SUITE("impl-IntraData - serialize round-trip") {
  TEST_CASE("string round-trip preserves the value") {
    StringIntraType src;
    src.value = "round_trip_payload";

    Bytes bytes;
    REQUIRE(src.operator>>(bytes));

    StringIntraType dst;
    REQUIRE(dst.operator<<(bytes));
    CHECK(dst.value == "round_trip_payload");
  }

  TEST_CASE("empty-string round-trip yields an empty string") {
    StringIntraType src;
    Bytes bytes;
    REQUIRE(src.operator>>(bytes));

    StringIntraType dst;
    dst.value = "preset";
    REQUIRE(dst.operator<<(bytes));
    CHECK(dst.value.empty());
  }
}

// NOLINTEND
