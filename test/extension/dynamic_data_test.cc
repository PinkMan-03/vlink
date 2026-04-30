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

#include "./extension/dynamic_data.h"

#include <doctest/doctest.h>

#include <cstring>
#include <string>

#include "./base/bytes.h"

//
#include "../common_test.h"

// ---------------------------------------------------------------------------
// Helpers - simple POD struct usable as kStandardType
// ---------------------------------------------------------------------------

struct Vec3 {
  float x;
  float y;
  float z;
};

static_assert(std::is_standard_layout_v<Vec3>);

// ---------------------------------------------------------------------------
// TEST SUITE: Static constexpr traits
// ---------------------------------------------------------------------------

TEST_SUITE("extension-DynamicData - static traits") {
  TEST_CASE("is_vlink_dynamic_data() returns true") { CHECK(DynamicData::is_vlink_dynamic_data() == true); }

  TEST_CASE("get_offset() returns 20") { CHECK(DynamicData::get_offset() == 20U); }
}

// ---------------------------------------------------------------------------
// TEST SUITE: Default construction
// ---------------------------------------------------------------------------

TEST_SUITE("extension-DynamicData - default construction") {
  TEST_CASE("default constructed DynamicData is empty") {
    DynamicData dd;
    CHECK(dd.is_empty());
  }

  TEST_CASE("get_type() is empty for default constructed instance") {
    DynamicData dd;
    CHECK(dd.get_type().empty());
  }

  TEST_CASE("get_data() is empty for default constructed instance") {
    DynamicData dd;
    CHECK(dd.get_data().empty());
  }
}

// ---------------------------------------------------------------------------
// TEST SUITE: load / as with int
// ---------------------------------------------------------------------------

TEST_SUITE("extension-DynamicData - int") {
  TEST_CASE("load and as<int> round-trip") {
    DynamicData dd;
    dd.load("int", 42);

    CHECK(!dd.is_empty());
    CHECK(dd.as<int>() == 42);
  }

  TEST_CASE("get_type() returns stored type name for int") {
    DynamicData dd;
    dd.load("int", 100);

    CHECK(!dd.get_type().empty());
    // The type view starts with "int"
    CHECK(dd.get_type().find("int") != std::string_view::npos);
  }

  TEST_CASE("convert<int>() succeeds after load") {
    DynamicData dd;
    dd.load("myint", 7);

    int out = 0;
    bool ok = dd.convert(out);
    CHECK(ok);
    CHECK(out == 7);
  }

  TEST_CASE("load with zero") {
    DynamicData dd;
    dd.load("int", 0);
    CHECK(dd.as<int>() == 0);
  }

  TEST_CASE("load with negative int") {
    DynamicData dd;
    dd.load("int", -99);
    CHECK(dd.as<int>() == -99);
  }

  TEST_CASE("load overwrites previous value") {
    DynamicData dd;
    dd.load("int", 1);
    dd.load("int", 2);
    CHECK(dd.as<int>() == 2);
  }
}

// ---------------------------------------------------------------------------
// TEST SUITE: load / as with double
// ---------------------------------------------------------------------------

TEST_SUITE("extension-DynamicData - double") {
  TEST_CASE("load and as<double> round-trip") {
    DynamicData dd;
    dd.load("double", 3.14);

    CHECK(dd.as<double>() == doctest::Approx(3.14));
  }

  TEST_CASE("load and as<double> negative value") {
    DynamicData dd;
    dd.load("dbl", -1.5);

    CHECK(dd.as<double>() == doctest::Approx(-1.5));
  }

  TEST_CASE("convert<double>() after load") {
    DynamicData dd;

    double value = 2.718281828;

    dd.load("dbl", value);

    double out = 0.0;
    bool ok = dd.convert(out);
    CHECK(ok);
    CHECK(out == doctest::Approx(2.718281828));
  }
}

// ---------------------------------------------------------------------------
// TEST SUITE: load / as with std::string
// ---------------------------------------------------------------------------

TEST_SUITE("extension-DynamicData - std::string") {
  TEST_CASE("load and as<std::string> round-trip") {
    DynamicData dd;
    std::string msg = "hello vlink";
    dd.load("string", msg);

    CHECK(dd.as<std::string>() == "hello vlink");
  }

  TEST_CASE("load empty std::string") {
    DynamicData dd;
    std::string empty;
    dd.load("str", empty);

    CHECK(dd.as<std::string>().empty());
  }

  TEST_CASE("load std::string with special characters") {
    DynamicData dd;
    std::string s = "abc\n\t\r123";
    dd.load("str", s);

    CHECK(dd.as<std::string>() == s);
  }
}

// ---------------------------------------------------------------------------
// TEST SUITE: load / as with Bytes
// ---------------------------------------------------------------------------

TEST_SUITE("extension-DynamicData - Bytes") {
  TEST_CASE("load and as<Bytes> round-trip") {
    DynamicData dd;
    Bytes src = Bytes::create(8);
    uint8_t* p = src.data();

    for (int i = 0; i < 8; ++i) {
      p[i] = static_cast<uint8_t>(i + 1);
    }

    dd.load("bytes", src);

    auto out = dd.as<Bytes>();
    REQUIRE(out.size() == src.size());

    for (size_t i = 0; i < out.size(); ++i) {
      CHECK(out.data()[i] == src.data()[i]);
    }
  }
}

// ---------------------------------------------------------------------------
// TEST SUITE: load / as with POD struct (Vec3)
// ---------------------------------------------------------------------------

TEST_SUITE("extension-DynamicData - POD struct") {
  TEST_CASE("load and as<Vec3> round-trip") {
    DynamicData dd;
    Vec3 v{1.0F, 2.0F, 3.0F};
    dd.load("Vec3", v);

    Vec3 out = dd.as<Vec3>();
    CHECK(out.x == doctest::Approx(1.0F));
    CHECK(out.y == doctest::Approx(2.0F));
    CHECK(out.z == doctest::Approx(3.0F));
  }

  TEST_CASE("convert<Vec3>() after load") {
    DynamicData dd;
    Vec3 v{-1.0F, 0.5F, 9.9F};
    dd.load("Vec3", v);

    Vec3 out{};
    bool ok = dd.convert(out);
    CHECK(ok);
    CHECK(out.x == doctest::Approx(-1.0F));
    CHECK(out.y == doctest::Approx(0.5F));
    CHECK(out.z == doctest::Approx(9.9F));
  }
}

// ---------------------------------------------------------------------------
// TEST SUITE: Equality operators
// ---------------------------------------------------------------------------

TEST_SUITE("extension-DynamicData - equality") {
  TEST_CASE("two DynamicDatas loaded with same value are equal") {
    DynamicData a;
    DynamicData b;
    a.load("int", 42);
    b.load("int", 42);

    CHECK(a == b);
    CHECK(!(a != b));
  }

  TEST_CASE("two DynamicDatas loaded with different values are not equal") {
    DynamicData a;
    DynamicData b;
    a.load("int", 1);
    b.load("int", 2);

    CHECK(a != b);
    CHECK(!(a == b));
  }

  TEST_CASE("default constructed DynamicDatas are equal") {
    DynamicData a;
    DynamicData b;
    CHECK(a == b);
  }

  TEST_CASE("loaded vs empty is not equal") {
    DynamicData a;
    DynamicData b;
    a.load("int", 5);

    CHECK(a != b);
  }
}

// ---------------------------------------------------------------------------
// TEST SUITE: Serialization round-trip (operator>> / operator<<)
// ---------------------------------------------------------------------------

TEST_SUITE("extension-DynamicData - wire serialization") {
  TEST_CASE("int: serialize to Bytes and deserialize back") {
    DynamicData orig;
    orig.load("int", 42);

    Bytes wire;
    bool wrote = (orig >> wire);
    CHECK(wrote);
    CHECK(!wire.empty());

    DynamicData copy;
    bool read = (copy << wire);
    CHECK(read);
    CHECK(copy.as<int>() == 42);
  }

  TEST_CASE("double: serialize/deserialize round-trip") {
    DynamicData orig;

    double value = 2.71828;

    orig.load("dbl", value);

    Bytes wire;
    orig >> wire;

    DynamicData copy;
    copy << wire;

    CHECK(copy.as<double>() == doctest::Approx(2.71828));
  }

  TEST_CASE("std::string: serialize/deserialize round-trip") {
    DynamicData orig;
    std::string s = "transport test";
    orig.load("str", s);

    Bytes wire;
    orig >> wire;

    DynamicData copy;
    copy << wire;

    CHECK(copy.as<std::string>() == s);
  }

  TEST_CASE("Vec3: serialize/deserialize round-trip") {
    DynamicData orig;
    Vec3 v{10.F, 20.F, 30.F};
    orig.load("Vec3", v);

    Bytes wire;
    orig >> wire;

    DynamicData copy;
    copy << wire;

    Vec3 out = copy.as<Vec3>();
    CHECK(out.x == doctest::Approx(10.F));
    CHECK(out.y == doctest::Approx(20.F));
    CHECK(out.z == doctest::Approx(30.F));
  }

  TEST_CASE("operator>> on empty DynamicData returns false") {
    DynamicData dd;
    Bytes wire;
    bool ok = (dd >> wire);
    CHECK(!ok);
  }

  TEST_CASE("serialized data is non-empty after load") {
    DynamicData dd;
    dd.load("int", 100);

    Bytes wire;
    dd >> wire;
    CHECK(!wire.empty());
  }
}

// ---------------------------------------------------------------------------
// TEST SUITE: get_data and get_type
// ---------------------------------------------------------------------------

TEST_SUITE("extension-DynamicData - get_data and get_type") {
  TEST_CASE("get_data() is non-empty after load") {
    DynamicData dd;
    dd.load("int", 1);
    CHECK(!dd.get_data().empty());
  }

  TEST_CASE("get_type() for Vec3 contains expected name") {
    DynamicData dd;
    Vec3 v{};
    dd.load("Vec3", v);

    CHECK(dd.get_type().find("Vec3") != std::string_view::npos);
  }

  TEST_CASE("is_empty() is false after load") {
    DynamicData dd;
    dd.load("int", 99);
    CHECK(!dd.is_empty());
  }

  TEST_CASE("load chaining: method returns reference to self") {
    DynamicData dd;
    DynamicData& ref = dd.load("int", 5);
    CHECK(&ref == &dd);
  }

  TEST_CASE("sequential loads with different types") {
    DynamicData dd;

    dd.load("int", 10);
    CHECK(dd.as<int>() == 10);

    dd.load("dbl", 3.14);
    CHECK(dd.as<double>() == doctest::Approx(3.14));

    std::string s = "updated";
    dd.load("str", s);
    CHECK(dd.as<std::string>() == "updated");
  }
}

// ---------------------------------------------------------------------------
// TEST SUITE: DynamicData - additional edge cases
// ---------------------------------------------------------------------------

TEST_SUITE("extension-DynamicData - edge cases") {
  TEST_CASE("convert on empty DynamicData returns false") {
    DynamicData dd;
    int val = 0;
    CHECK(!dd.convert(val));
    CHECK(val == 0);
  }

  TEST_CASE("as<int> on empty DynamicData throws") {
    DynamicData dd;
    CHECK_THROWS((void)dd.as<int>());
  }

  TEST_CASE("as<double> on empty DynamicData throws") {
    DynamicData dd;
    CHECK_THROWS((void)dd.as<double>());
  }

  TEST_CASE("as<std::string> on empty DynamicData throws") {
    DynamicData dd;
    CHECK_THROWS((void)dd.as<std::string>());
  }

  TEST_CASE("operator== is reflexive") {
    DynamicData dd;
    dd.load("int", 42);
    CHECK(dd == dd);
  }

  TEST_CASE("operator!= with different types") {
    DynamicData a;
    DynamicData b;
    a.load("int", 42);
    b.load("dbl", 42.0);
    CHECK(a != b);
  }

  TEST_CASE("copy constructor creates equal copy") {
    DynamicData orig;
    orig.load("int", 99);

    DynamicData copy(orig);
    CHECK(copy == orig);
    CHECK(copy.as<int>() == 99);
  }

  TEST_CASE("copy assignment creates equal copy") {
    DynamicData orig;
    orig.load("int", 77);

    DynamicData copy;
    copy = orig;
    CHECK(copy == orig);
    CHECK(copy.as<int>() == 77);
  }

  TEST_CASE("move constructor transfers data") {
    DynamicData orig;
    orig.load("int", 55);

    DynamicData moved(std::move(orig));
    CHECK(moved.as<int>() == 55);
    CHECK(!moved.is_empty());
  }

  TEST_CASE("move assignment transfers data") {
    DynamicData orig;
    orig.load("int", 33);

    DynamicData moved;
    moved = std::move(orig);
    CHECK(moved.as<int>() == 33);
  }

  TEST_CASE("operator<< from empty Bytes returns false") {
    DynamicData dd;
    Bytes empty;
    bool ok = (dd << empty);
    CHECK(!ok);
  }

  TEST_CASE("double serialize/deserialize preserves value") {
    DynamicData orig;
    orig.load("int", 123);

    Bytes wire;
    orig >> wire;

    DynamicData copy;
    copy << wire;

    CHECK(copy.as<int>() == 123);
  }

  TEST_CASE("get_type survives serialization round-trip") {
    DynamicData orig;
    orig.load("Vec3", Vec3{1.F, 2.F, 3.F});

    Bytes wire;
    orig >> wire;

    DynamicData copy;
    copy << wire;

    CHECK(copy.get_type().find("Vec3") != std::string_view::npos);
  }

  TEST_CASE("load with uint64_t") {
    DynamicData dd;
    uint64_t val = 0xFFFFFFFFFFFFFFFFULL;
    dd.load("u64", val);
    CHECK(dd.as<uint64_t>() == val);
  }

  TEST_CASE("load with float") {
    DynamicData dd;
    float val = 1.5F;
    dd.load("float", val);
    CHECK(dd.as<float>() == doctest::Approx(1.5F));
  }

  TEST_CASE("load with bool") {
    DynamicData dd;
    dd.load("bool", true);
    CHECK(dd.as<bool>() == true);
  }

  TEST_CASE("load with char") {
    DynamicData dd;
    char val = 'A';
    dd.load("char", val);
    CHECK(dd.as<char>() == 'A');
  }

  TEST_CASE("get_offset is constexpr 20") {
    constexpr uint8_t kOffset = DynamicData::get_offset();
    CHECK(kOffset == 20);
  }

  TEST_CASE("is_vlink_dynamic_data is constexpr true") {
    constexpr bool kIsDd = DynamicData::is_vlink_dynamic_data();
    CHECK(kIsDd == true);
  }

  TEST_CASE("multiple wire round-trips preserve data") {
    DynamicData dd;
    dd.load("int", 42);

    for (int i = 0; i < 3; ++i) {
      Bytes wire;
      dd >> wire;

      DynamicData dd2;
      dd2 << wire;
      CHECK(dd2.as<int>() == 42);

      dd = dd2;
    }
  }
}

// NOLINTEND
