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

#include "./zerocopy/point_cloud.h"

#include <doctest/doctest.h>

#include <cstdint>
#include <string>
#include <vector>

#include "./base/bytes.h"

//
#include "../common_test.h"

TEST_SUITE("zerocopy-PointCloud - construction") {
  TEST_CASE("default construction yields invalid empty cloud") {
    zerocopy::PointCloud pc;

    CHECK(!pc.is_valid());
    CHECK(pc.size() == 0);
    CHECK(pc.pack_size() == 0);
    CHECK(!pc.is_owner());
    CHECK(pc.get_internal_data() == nullptr);
  }
}

TEST_SUITE("zerocopy-PointCloud - create_v3f") {
  TEST_CASE("create with xyz only - pack_size is 12 bytes") {
    zerocopy::PointCloud pc;

    CHECK(pc.create_v3f<>(100, {}));

    CHECK(pc.is_owner());
    CHECK(pc.pack_size() == 12);
    CHECK(pc.size() == 0);
    CHECK(pc.get_reserved_size() == 100);
  }

  TEST_CASE("create with intensity extra field - pack_size is 16 bytes") {
    zerocopy::PointCloud pc;

    CHECK(pc.create_v3f<float>(1000, {"intensity"}));

    CHECK(pc.is_owner());
    CHECK(pc.pack_size() == 16);
    CHECK(pc.get_reserved_size() == 1000);
  }

  TEST_CASE("push and size increments") {
    zerocopy::PointCloud pc;
    REQUIRE(pc.create_v3f<float>(10, {"intensity"}));

    CHECK(pc.push_value_v3f(1.0F, 2.0F, 3.0F, 0.5F));
    CHECK(pc.push_value_v3f(4.0F, 5.0F, 6.0F, 0.8F));

    CHECK(pc.size() == 2);
  }

  TEST_CASE("push overflow returns false") {
    zerocopy::PointCloud pc;
    REQUIRE(pc.create_v3f<>(2, {}));

    CHECK(pc.push_value_v3f(1.0F, 2.0F, 3.0F));
    CHECK(pc.push_value_v3f(4.0F, 5.0F, 6.0F));
    CHECK(!pc.push_value_v3f(7.0F, 8.0F, 9.0F));
  }

  TEST_CASE("get_value_v3f by index reads correct values") {
    zerocopy::PointCloud pc;
    REQUIRE(pc.create_v3f<float>(10, {"intensity"}));

    pc.push_value_v3f(1.0F, 2.0F, 3.0F, 0.9F);
    pc.push_value_v3f(4.0F, 5.0F, 6.0F, 0.1F);

    float x = 0;
    float y = 0;
    float z = 0;

    CHECK(pc.get_value_v3f(x, y, z, 0));
    CHECK(x == doctest::Approx(1.0F));
    CHECK(y == doctest::Approx(2.0F));
    CHECK(z == doctest::Approx(3.0F));

    CHECK(pc.get_value_v3f(x, y, z, 1));
    CHECK(x == doctest::Approx(4.0F));
    CHECK(y == doctest::Approx(5.0F));
    CHECK(z == doctest::Approx(6.0F));
  }

  TEST_CASE("get_value_v3f out of range returns false") {
    zerocopy::PointCloud pc;
    REQUIRE(pc.create_v3f<>(5, {}));

    pc.push_value_v3f(0.0F, 0.0F, 0.0F);

    float x = 0;
    float y = 0;
    float z = 0;

    CHECK(!pc.get_value_v3f(x, y, z, 99));
  }

  TEST_CASE("get_value_v3f via struct overload") {
    zerocopy::PointCloud pc;
    REQUIRE(pc.create_v3f<>(4, {}));

    pc.push_value_v3f(7.0F, 8.0F, 9.0F);

    zerocopy::PointCloud::Vector3f v;
    CHECK(pc.get_value_v3f(v, 0));
    CHECK(v.x == doctest::Approx(7.0F));
    CHECK(v.y == doctest::Approx(8.0F));
    CHECK(v.z == doctest::Approx(9.0F));
  }

  TEST_CASE("get_value via key_map retrieves named fields") {
    zerocopy::PointCloud pc;
    REQUIRE(pc.create_v3f<float>(10, {"intensity"}));

    pc.push_value_v3f(1.5F, 2.5F, 3.5F, 0.75F);

    auto key_map = pc.get_key_map();

    float x = pc.get_value<float>(0, key_map, "x");
    float y = pc.get_value<float>(0, key_map, "y");
    float z = pc.get_value<float>(0, key_map, "z");
    float intensity = pc.get_value<float>(0, key_map, "intensity");

    CHECK(x == doctest::Approx(1.5F));
    CHECK(y == doctest::Approx(2.5F));
    CHECK(z == doctest::Approx(3.5F));
    CHECK(intensity == doctest::Approx(0.75F));
  }

  TEST_CASE("get_value with nonexistent key returns zero") {
    zerocopy::PointCloud pc;
    REQUIRE(pc.create_v3f<>(4, {}));

    pc.push_value_v3f(1.0F, 2.0F, 3.0F);

    auto key_map = pc.get_key_map();
    float val = 99.0F;

    CHECK(!pc.get_value<float>(val, 0, key_map, "nonexistent"));
    CHECK(val == doctest::Approx(0.0F));
  }

  TEST_CASE("set_value_v3f overwrites existing point") {
    zerocopy::PointCloud pc;
    REQUIRE(pc.create_v3f<>(5, {}));

    for (int i = 0; i < 5; ++i) {
      pc.push_value_v3f(static_cast<float>(i), 0.0F, 0.0F);
    }

    CHECK(pc.resize(3));
    CHECK(pc.size() == 3);

    CHECK(pc.set_value_v3f(0, 10.0F, 20.0F, 30.0F));

    float x = 0;
    float y = 0;
    float z = 0;

    CHECK(pc.get_value_v3f(x, y, z, 0));
    CHECK(x == doctest::Approx(10.0F));
    CHECK(y == doctest::Approx(20.0F));
    CHECK(z == doctest::Approx(30.0F));
  }
}

TEST_SUITE("zerocopy-PointCloud - create_v3d") {
  TEST_CASE("create v3d xyz only - pack_size is 24 bytes") {
    zerocopy::PointCloud pc;

    CHECK(pc.create_v3d<>(200, {}));

    CHECK(pc.is_owner());
    CHECK(pc.pack_size() == 24);
    CHECK(pc.get_reserved_size() == 200);
  }

  TEST_CASE("push and get v3d values") {
    zerocopy::PointCloud pc;
    REQUIRE(pc.create_v3d<>(10, {}));

    CHECK(pc.push_value_v3d(1.1, 2.2, 3.3));

    double x = 0;
    double y = 0;
    double z = 0;

    CHECK(pc.get_value_v3d(x, y, z, 0));
    CHECK(x == doctest::Approx(1.1));
    CHECK(y == doctest::Approx(2.2));
    CHECK(z == doctest::Approx(3.3));
  }

  TEST_CASE("push and get v3d via struct overload") {
    zerocopy::PointCloud pc;
    REQUIRE(pc.create_v3d<>(4, {}));

    zerocopy::PointCloud::Vector3d v(4.4, 5.5, 6.6);
    CHECK(pc.push_value_v3d(v));

    zerocopy::PointCloud::Vector3d out;
    CHECK(pc.get_value_v3d(out, 0));
    CHECK(out.x == doctest::Approx(4.4));
    CHECK(out.y == doctest::Approx(5.5));
    CHECK(out.z == doctest::Approx(6.6));
  }
}

TEST_SUITE("zerocopy-PointCloud - serialization") {
  TEST_CASE("serialize and deserialize round-trip preserves all fields") {
    zerocopy::PointCloud src;
    REQUIRE(src.create_v3f<float>(50, {"intensity"}));

    for (int i = 0; i < 5; ++i) {
      src.push_value_v3f(static_cast<float>(i), static_cast<float>(i * 2), static_cast<float>(i * 3),
                         static_cast<float>(i) * 0.1F);
    }

    src.header.seq = 42;

    Bytes wire;
    CHECK((src >> wire));
    CHECK(zerocopy::PointCloud::check_valid(wire));
    CHECK(wire.size() == src.get_serialized_size());

    zerocopy::PointCloud dst;
    CHECK((dst << wire));

    CHECK(dst.is_valid());
    CHECK(!dst.is_owner());
    CHECK(dst.size() == 5);
    CHECK(dst.pack_size() == 16);
    CHECK(dst.header.seq == 42);

    auto key_map = dst.get_key_map();

    for (int i = 0; i < 5; ++i) {
      float x = dst.get_value<float>(static_cast<size_t>(i), key_map, "x");
      float y = dst.get_value<float>(static_cast<size_t>(i), key_map, "y");
      float z = dst.get_value<float>(static_cast<size_t>(i), key_map, "z");
      float intensity = dst.get_value<float>(static_cast<size_t>(i), key_map, "intensity");

      CHECK(x == doctest::Approx(static_cast<float>(i)));
      CHECK(y == doctest::Approx(static_cast<float>(i * 2)));
      CHECK(z == doctest::Approx(static_cast<float>(i * 3)));
      CHECK(intensity == doctest::Approx(static_cast<float>(i) * 0.1F));
    }
  }

  TEST_CASE("check_valid on empty bytes returns false") {
    Bytes empty;
    CHECK(!zerocopy::PointCloud::check_valid(empty));
  }

  TEST_CASE("check_valid on corrupted bytes returns false") {
    zerocopy::PointCloud pc;
    REQUIRE(pc.create_v3f<>(4, {}));
    pc.push_value_v3f(1.0F, 2.0F, 3.0F);

    Bytes wire;
    pc >> wire;

    wire[0] ^= 0xFF;
    CHECK(!zerocopy::PointCloud::check_valid(wire));
  }

  TEST_CASE("get_serialized_size equals magic + struct + payload + magic") {
    zerocopy::PointCloud pc;
    REQUIRE(pc.create_v3f<>(5, {}));

    for (int i = 0; i < 3; ++i) {
      pc.push_value_v3f(static_cast<float>(i), 0.0F, 0.0F);
    }

    size_t expected = sizeof(uint32_t) + sizeof(zerocopy::PointCloud) + 3 * 12 + sizeof(uint32_t);
    CHECK(pc.get_serialized_size() == expected);
  }
}

TEST_SUITE("zerocopy-PointCloud - copy and move") {
  TEST_CASE("deep_copy is independent from source") {
    zerocopy::PointCloud src;
    REQUIRE(src.create_v3f<>(5, {}));
    src.push_value_v3f(1.0F, 2.0F, 3.0F);
    src.push_value_v3f(4.0F, 5.0F, 6.0F);

    zerocopy::PointCloud dst;
    CHECK(dst.deep_copy(src));

    CHECK(dst.is_owner());
    CHECK(dst.size() == 2);
    CHECK(dst.pack_size() == 12);
    CHECK(dst.get_internal_data() != src.get_internal_data());
  }

  TEST_CASE("shallow_copy shares underlying buffer") {
    zerocopy::PointCloud src;
    REQUIRE(src.create_v3f<>(5, {}));
    src.push_value_v3f(1.0F, 2.0F, 3.0F);

    zerocopy::PointCloud dst;
    CHECK(dst.shallow_copy(src));

    CHECK(!dst.is_owner());
    CHECK(dst.size() == 1);
    CHECK(dst.get_internal_data() == src.get_internal_data());
  }

  TEST_CASE("move_copy transfers ownership and invalidates source") {
    zerocopy::PointCloud src;
    REQUIRE(src.create_v3f<>(10, {}));
    src.push_value_v3f(0.0F, 0.0F, 0.0F);

    const uint8_t* ptr = src.get_internal_data();

    zerocopy::PointCloud dst;
    CHECK(dst.move_copy(src));

    CHECK(dst.is_owner());
    CHECK(dst.get_internal_data() == ptr);
    CHECK(dst.size() == 1);
    CHECK(!src.is_valid());
    CHECK(!src.is_owner());
  }

  TEST_CASE("deep_copy self returns false") {
    zerocopy::PointCloud pc;
    REQUIRE(pc.create_v3f<>(4, {}));

    CHECK(!pc.deep_copy(pc));
  }

  TEST_CASE("shallow_copy self returns false") {
    zerocopy::PointCloud pc;
    REQUIRE(pc.create_v3f<>(4, {}));

    CHECK(!pc.shallow_copy(pc));
  }

  TEST_CASE("move_copy self returns false") {
    zerocopy::PointCloud pc;
    REQUIRE(pc.create_v3f<>(4, {}));

    CHECK(!pc.move_copy(pc));
  }

  TEST_CASE("copy constructor performs deep copy") {
    zerocopy::PointCloud src;
    REQUIRE(src.create_v3f<float>(10, {"intensity"}));
    src.push_value_v3f(1.0F, 2.0F, 3.0F, 0.5F);

    zerocopy::PointCloud copy(src);

    CHECK(copy.is_owner());
    CHECK(copy.size() == 1);
    CHECK(copy.pack_size() == 16);
    CHECK(copy.get_internal_data() != src.get_internal_data());
  }

  TEST_CASE("move constructor transfers ownership") {
    zerocopy::PointCloud src;
    REQUIRE(src.create_v3f<>(4, {}));
    src.push_value_v3f(1.0F, 2.0F, 3.0F);

    const uint8_t* ptr = src.get_internal_data();

    zerocopy::PointCloud moved(std::move(src));

    CHECK(moved.is_owner());
    CHECK(moved.get_internal_data() == ptr);
    CHECK(moved.size() == 1);
    CHECK(!src.is_valid());
  }

  TEST_CASE("copy assignment operator performs deep copy") {
    zerocopy::PointCloud src;
    REQUIRE(src.create_v3f<>(4, {}));
    src.push_value_v3f(7.0F, 8.0F, 9.0F);

    zerocopy::PointCloud dst;
    dst = src;

    CHECK(dst.is_owner());
    CHECK(dst.size() == 1);

    float x = 0;
    float y = 0;
    float z = 0;

    CHECK(dst.get_value_v3f(x, y, z, 0));
    CHECK(x == doctest::Approx(7.0F));
  }

  TEST_CASE("move assignment operator transfers ownership") {
    zerocopy::PointCloud src;
    REQUIRE(src.create_v3f<>(4, {}));
    src.push_value_v3f(1.0F, 2.0F, 3.0F);

    const uint8_t* ptr = src.get_internal_data();

    zerocopy::PointCloud dst;
    dst = std::move(src);

    CHECK(dst.is_owner());
    CHECK(dst.get_internal_data() == ptr);
    CHECK(!src.is_valid());
  }
}

TEST_SUITE("zerocopy-PointCloud - clear") {
  TEST_CASE("clear(false) resets size but retains buffer") {
    zerocopy::PointCloud pc;
    REQUIRE(pc.create_v3f<>(10, {}));
    pc.push_value_v3f(1.0F, 2.0F, 3.0F);

    pc.clear(false);

    CHECK(pc.size() == 0);
    CHECK(pc.pack_size() > 0);
    CHECK(pc.is_owner());
  }

  TEST_CASE("clear(true) fully resets cloud") {
    zerocopy::PointCloud pc;
    REQUIRE(pc.create_v3f<>(10, {}));
    pc.push_value_v3f(1.0F, 2.0F, 3.0F);

    pc.clear(true);

    CHECK(!pc.is_valid());
    CHECK(!pc.is_owner());
    CHECK(pc.size() == 0);
    CHECK(pc.pack_size() == 0);
  }
}

TEST_SUITE("zerocopy-PointCloud - key_map and protocol") {
  TEST_CASE("get_key_map returns expected field names") {
    zerocopy::PointCloud pc;
    REQUIRE(pc.create_v3f<float>(4, {"intensity"}));

    zerocopy::PointCloud::KeyList key_list;
    auto key_map = pc.get_key_map(&key_list);

    CHECK(!key_map.empty());
    CHECK(!key_list.empty());

    CHECK(key_map.count("x") > 0);
    CHECK(key_map.count("y") > 0);
    CHECK(key_map.count("z") > 0);
    CHECK(key_map.count("intensity") > 0);

    CHECK(key_list[0].name == "x");
    CHECK(key_list[1].name == "y");
    CHECK(key_list[2].name == "z");
    CHECK(key_list[3].name == "intensity");
  }

  TEST_CASE("get_protocol_size/name/type strings are non-empty") {
    zerocopy::PointCloud pc;
    REQUIRE(pc.create_v3f<float>(10, {"intensity"}));

    std::string size_str = pc.get_protocol_size_str();
    std::string name_str = pc.get_protocol_name_str();
    std::string type_str = pc.get_protocol_type_str();

    CHECK(!size_str.empty());
    CHECK(!name_str.empty());
    CHECK(!type_str.empty());

    CHECK(name_str.find("x") != std::string::npos);
    CHECK(name_str.find("y") != std::string::npos);
    CHECK(name_str.find("z") != std::string::npos);
    CHECK(name_str.find("intensity") != std::string::npos);
  }

  TEST_CASE("get_protocol_size_num and type_num are non-zero") {
    zerocopy::PointCloud pc;
    REQUIRE(pc.create_v3f<>(4, {}));

    CHECK(pc.get_protocol_size_num() != 0);
    CHECK(pc.get_protocol_type_num() != 0);
  }

  TEST_CASE("get_value_for_double_float converts field") {
    zerocopy::PointCloud pc;
    REQUIRE(pc.create_v3f<float>(4, {"intensity"}));

    pc.push_value_v3f(3.14f, 2.71f, 1.41f, 0.99f);

    auto key_map = pc.get_key_map();
    double x_d = pc.get_value_for_double_float(0, key_map, "x", zerocopy::PointCloud::kFloatType);

    CHECK(x_d == doctest::Approx(3.14).epsilon(0.001));
  }

  TEST_CASE("get_value_for_print returns non-empty string") {
    zerocopy::PointCloud pc;
    REQUIRE(pc.create_v3f<>(4, {}));

    pc.push_value_v3f(1.0F, 2.0F, 3.0F);

    auto key_map = pc.get_key_map();
    std::string xs = pc.get_value_for_print(0, key_map, "x", zerocopy::PointCloud::kFloatType);

    CHECK(!xs.empty());
  }
}

TEST_SUITE("zerocopy-PointCloud - fill_packed_data") {
  TEST_CASE("fill_packed_data from valid source") {
    zerocopy::PointCloud src;
    REQUIRE(src.create_v3f<>(4, {}));

    for (int i = 0; i < 4; ++i) {
      src.push_value_v3f(static_cast<float>(i), static_cast<float>(i), static_cast<float>(i));
    }

    zerocopy::PointCloud dst;
    REQUIRE(dst.create_v3f<>(4, {}));

    CHECK(dst.fill_packed_data(src.get_internal_data(), 4));
    CHECK(dst.size() == 4);

    float x = 0;
    float y = 0;
    float z = 0;

    CHECK(dst.get_value_v3f(x, y, z, 2));
    CHECK(x == doctest::Approx(2.0F));
  }

  TEST_CASE("fill_packed_data with null data returns false") {
    zerocopy::PointCloud pc;
    REQUIRE(pc.create_v3f<>(4, {}));

    CHECK(!pc.fill_packed_data(nullptr, 4));
  }

  TEST_CASE("fill_packed_data with zero count returns false") {
    zerocopy::PointCloud pc;
    REQUIRE(pc.create_v3f<>(4, {}));

    std::vector<uint8_t> buf(48, 0);
    CHECK(!pc.fill_packed_data(buf.data(), 0));
  }
}

TEST_SUITE("zerocopy-PointCloud - low-level create") {
  TEST_CASE("create with raw protocol params") {
    // Obtain size_num and type_num from a reference PointCloud using the public API.
    zerocopy::PointCloud ref;
    ref.create<float, float, float>(10, {"a", "b", "c"});
    uint64_t size_num = ref.get_protocol_size_num();
    uint64_t type_num = ref.get_protocol_type_num();
    std::string key_str = "a,b,c";

    zerocopy::PointCloud pc;
    CHECK(pc.create(10, size_num, type_num, key_str));

    CHECK(pc.is_owner());
    CHECK(pc.pack_size() == 12);
    CHECK(pc.get_reserved_size() == 10);

    auto key_map = pc.get_key_map();
    CHECK(key_map.count("a") > 0);
    CHECK(key_map.count("b") > 0);
    CHECK(key_map.count("c") > 0);
  }
}

TEST_SUITE("zerocopy-PointCloud - Vector3 types") {
  TEST_CASE("Vector3f constructor sets xyz") {
    zerocopy::PointCloud::Vector3f v(1.0F, 2.0F, 3.0F);

    CHECK(v.x == doctest::Approx(1.0F));
    CHECK(v.y == doctest::Approx(2.0F));
    CHECK(v.z == doctest::Approx(3.0F));
  }

  TEST_CASE("Vector3d constructor sets xyz") {
    zerocopy::PointCloud::Vector3d v(1.1, 2.2, 3.3);

    CHECK(v.x == doctest::Approx(1.1));
    CHECK(v.y == doctest::Approx(2.2));
    CHECK(v.z == doctest::Approx(3.3));
  }
}

// NOLINTEND
