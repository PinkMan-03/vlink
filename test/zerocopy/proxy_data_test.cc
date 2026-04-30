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

#include "./zerocopy/proxy_data.h"

#include <doctest/doctest.h>

#include <cstring>
#include <string>

#include "./base/bytes.h"

//
#include "../common_test.h"

// ---------------------------------------------------------------------------
// TEST SUITE: ProxyData - default construction
// ---------------------------------------------------------------------------

TEST_SUITE("zerocopy-ProxyData - default construction") {
  TEST_CASE("default-constructed ProxyData is empty/invalid") {
    zerocopy::ProxyData pd;
    CHECK(!pd.is_valid());
    CHECK(pd.size() == 0);
    CHECK(!pd.is_owner());
    CHECK(pd.control_id() == 0);
    CHECK(pd.mode() == 0);
    CHECK(pd.timestamp() == 0);
    CHECK(pd.seq() == 0);
    CHECK(pd.raw().empty());
    CHECK(pd.url().empty());
    CHECK(pd.ser().empty());
    CHECK(pd.hostname().empty());
  }

  TEST_CASE("sizeof(ProxyData) is 80 bytes") { CHECK(sizeof(zerocopy::ProxyData) == 80u); }
}

// ---------------------------------------------------------------------------
// TEST SUITE: ProxyData - setters/getters
// ---------------------------------------------------------------------------

TEST_SUITE("zerocopy-ProxyData - setters and getters") {
  TEST_CASE("set_control_id / control_id round-trip") {
    zerocopy::ProxyData pd;
    pd.set_control_id(42);
    CHECK(pd.control_id() == 42);
  }

  TEST_CASE("set_mode / mode round-trip") {
    zerocopy::ProxyData pd;
    pd.set_mode(7);
    CHECK(pd.mode() == 7);
  }

  TEST_CASE("set_timestamp / timestamp round-trip") {
    zerocopy::ProxyData pd;
    pd.set_timestamp(1234567890LL);
    CHECK(pd.timestamp() == 1234567890LL);
  }

  TEST_CASE("set_seq / seq round-trip") {
    zerocopy::ProxyData pd;
    pd.set_seq(999LL);
    CHECK(pd.seq() == 999LL);
  }
}

// ---------------------------------------------------------------------------
// TEST SUITE: ProxyData - create
// ---------------------------------------------------------------------------

TEST_SUITE("zerocopy-ProxyData - create") {
  TEST_CASE("create with all fields populates data correctly") {
    zerocopy::ProxyData pd;
    pd.set_control_id(1);
    pd.set_timestamp(5000LL);

    Bytes raw = Bytes::create(32);
    uint8_t* p = raw.data();

    for (size_t i = 0; i < 32; ++i) {
      p[i] = static_cast<uint8_t>(i);
    }

    pd.create(raw, "dds://my/topic", "protobuf", static_cast<uint32_t>(SchemaType::kProtobuf), "host01");

    CHECK(pd.is_valid());
    CHECK(pd.is_owner());
    CHECK(!pd.url().empty());
    CHECK(!pd.ser().empty());
    CHECK(!pd.hostname().empty());
    CHECK(pd.url() == "dds://my/topic");
    CHECK(pd.ser() == "protobuf");
    CHECK(pd.schema() == static_cast<uint32_t>(SchemaType::kProtobuf));
    CHECK(pd.hostname() == "host01");

    Bytes raw_view = pd.raw();
    CHECK(raw_view.size() == 32);
    CHECK(std::memcmp(raw_view.data(), raw.data(), 32) == 0);
  }

  TEST_CASE("create without hostname leaves hostname empty") {
    zerocopy::ProxyData pd;
    Bytes raw = Bytes::create(8);
    pd.create(raw, "intra://topic", "raw");

    CHECK(pd.is_valid());
    CHECK(pd.url() == "intra://topic");
    CHECK(pd.ser() == "raw");
    CHECK(pd.schema() == 0u);
    CHECK(pd.hostname().empty());
  }

  TEST_CASE("create with empty Bytes still sets url/ser/hostname") {
    zerocopy::ProxyData pd;
    Bytes empty;
    pd.create(empty, "shm://topic", "bytes", 0, "myhost");

    CHECK(pd.url() == "shm://topic");
    CHECK(pd.ser() == "bytes");
    CHECK(pd.schema() == 0u);
    CHECK(pd.hostname() == "myhost");
  }
}

// ---------------------------------------------------------------------------
// TEST SUITE: ProxyData - serialization
// ---------------------------------------------------------------------------

TEST_SUITE("zerocopy-ProxyData - serialization") {
  TEST_CASE("serialize and deserialize round-trip") {
    zerocopy::ProxyData src;
    src.set_control_id(10);
    src.set_mode(2);
    src.set_timestamp(9999LL);
    src.set_seq(42LL);

    const std::string payload_str = "hello proxy data test payload";
    Bytes raw = Bytes::create(payload_str.size());
    std::memcpy(raw.data(), payload_str.data(), payload_str.size());

    src.create(raw, "dds://sensor/lidar", "protobuf", static_cast<uint32_t>(SchemaType::kProtobuf), "vehicle01");

    Bytes wire;
    bool ser_ok = (src >> wire);
    CHECK(ser_ok);
    CHECK(!wire.empty());
    CHECK(zerocopy::ProxyData::check_valid(wire));
    CHECK(wire.size() == src.get_serialized_size());

    zerocopy::ProxyData dst;
    bool deser_ok = (dst << wire);
    CHECK(deser_ok);

    CHECK(dst.is_valid());
    CHECK(!dst.is_owner());
    CHECK(dst.control_id() == 10);
    CHECK(dst.mode() == 2);
    CHECK(dst.timestamp() == 9999LL);
    CHECK(dst.seq() == 42LL);
    CHECK(dst.url() == "dds://sensor/lidar");
    CHECK(dst.ser() == "protobuf");
    CHECK(dst.schema() == static_cast<uint32_t>(SchemaType::kProtobuf));
    CHECK(dst.hostname() == "vehicle01");

    Bytes raw_view = dst.raw();
    REQUIRE(raw_view.size() == payload_str.size());
    CHECK(std::memcmp(raw_view.data(), payload_str.data(), payload_str.size()) == 0);
  }

  TEST_CASE("check_valid on empty Bytes returns false") {
    Bytes empty;
    CHECK(!zerocopy::ProxyData::check_valid(empty));
  }

  TEST_CASE("check_valid on corrupted Bytes returns false") {
    zerocopy::ProxyData pd;
    Bytes raw = Bytes::create(16);
    pd.create(raw, "dds://topic", "raw");

    Bytes wire;
    pd >> wire;

    wire[0] ^= 0xFF;
    CHECK(!zerocopy::ProxyData::check_valid(wire));
  }

  TEST_CASE("get_serialized_size is magic(4) + struct(80) + size + magic(4)") {
    zerocopy::ProxyData pd;
    Bytes raw = Bytes::create(20);
    pd.create(raw, "url", "ser");

    size_t expected = sizeof(uint32_t) + sizeof(zerocopy::ProxyData) + pd.size() + sizeof(uint32_t);
    CHECK(pd.get_serialized_size() == expected);
  }

  TEST_CASE("schema field survives create + serialize + deserialize for every SchemaType family") {
    const uint32_t schema_values[] = {
        static_cast<uint32_t>(SchemaType::kUnknown), static_cast<uint32_t>(SchemaType::kRaw),
        static_cast<uint32_t>(SchemaType::kZeroCopy), static_cast<uint32_t>(SchemaType::kProtobuf),
        static_cast<uint32_t>(SchemaType::kFlatbuffers)};

    for (uint32_t schema : schema_values) {
      zerocopy::ProxyData src;
      Bytes raw = Bytes::create(4);
      raw[0] = 0x01;

      src.create(raw, "dds://roundtrip/schema", "ser_name", schema, "hostX");
      CHECK(src.schema() == schema);

      Bytes wire;
      bool ser_ok = (src >> wire);
      REQUIRE(ser_ok);

      zerocopy::ProxyData dst;
      bool deser_ok = (dst << wire);
      REQUIRE(deser_ok);
      CHECK(dst.schema() == schema);
      CHECK(dst.url() == "dds://roundtrip/schema");
      CHECK(dst.ser() == "ser_name");
      CHECK(dst.hostname() == "hostX");
    }
  }
}

// ---------------------------------------------------------------------------
// TEST SUITE: ProxyData - copy operations
// ---------------------------------------------------------------------------

TEST_SUITE("zerocopy-ProxyData - shallow copy") {
  TEST_CASE("shallow_copy makes a borrow") {
    zerocopy::ProxyData src;
    Bytes raw = Bytes::create(8);
    src.create(raw, "dds://a", "raw");

    zerocopy::ProxyData dst;
    CHECK(dst.shallow_copy(src));

    CHECK(!dst.is_owner());
    CHECK(dst.url() == "dds://a");
  }

  TEST_CASE("shallow_copy self returns false") {
    zerocopy::ProxyData pd;
    Bytes raw = Bytes::create(8);
    pd.create(raw, "dds://a", "raw");
    CHECK(!pd.shallow_copy(pd));
  }
}

TEST_SUITE("zerocopy-ProxyData - deep copy") {
  TEST_CASE("deep_copy creates owned copy") {
    zerocopy::ProxyData src;
    Bytes raw = Bytes::create(16);
    raw[0] = 0xAB;
    src.create(raw, "dds://b", "protobuf", static_cast<uint32_t>(SchemaType::kProtobuf), "host");

    zerocopy::ProxyData dst;
    CHECK(dst.deep_copy(src));

    CHECK(dst.is_owner());
    CHECK(dst.url() == "dds://b");
    CHECK(dst.ser() == "protobuf");
    CHECK(dst.schema() == static_cast<uint32_t>(SchemaType::kProtobuf));
    CHECK(dst.hostname() == "host");
  }

  TEST_CASE("deep_copy self returns false") {
    zerocopy::ProxyData pd;
    Bytes raw = Bytes::create(8);
    pd.create(raw, "dds://c", "raw");
    CHECK(!pd.deep_copy(pd));
  }
}

TEST_SUITE("zerocopy-ProxyData - move copy") {
  TEST_CASE("move_copy transfers ownership") {
    zerocopy::ProxyData src;
    Bytes raw = Bytes::create(16);
    src.create(raw, "dds://d", "bytes");

    zerocopy::ProxyData dst;
    CHECK(dst.move_copy(src));

    CHECK(dst.is_owner());
    CHECK(dst.url() == "dds://d");
    CHECK(!src.is_valid());
  }

  TEST_CASE("move_copy self returns false") {
    zerocopy::ProxyData pd;
    Bytes raw = Bytes::create(8);
    pd.create(raw, "dds://e", "raw");
    CHECK(!pd.move_copy(pd));
  }
}

// ---------------------------------------------------------------------------
// TEST SUITE: ProxyData - special members
// ---------------------------------------------------------------------------

TEST_SUITE("zerocopy-ProxyData - special members") {
  TEST_CASE("copy constructor makes deep copy") {
    zerocopy::ProxyData src;
    Bytes raw = Bytes::create(32);
    raw[0] = 0x42;
    src.create(raw, "dds://copy_test", "protobuf", static_cast<uint32_t>(SchemaType::kProtobuf), "host_copy");

    zerocopy::ProxyData copy(src);

    CHECK(copy.is_owner());
    CHECK(copy.url() == "dds://copy_test");
    CHECK(copy.hostname() == "host_copy");
  }

  TEST_CASE("move constructor transfers ownership") {
    zerocopy::ProxyData src;
    Bytes raw = Bytes::create(16);
    src.create(raw, "dds://move_test", "raw");

    zerocopy::ProxyData moved(std::move(src));

    CHECK(moved.is_owner());
    CHECK(moved.url() == "dds://move_test");
    CHECK(!src.is_valid());
  }

  TEST_CASE("copy assignment makes deep copy") {
    zerocopy::ProxyData src;
    Bytes raw = Bytes::create(16);
    src.create(raw, "dds://assign_test", "bytes");

    zerocopy::ProxyData dst;
    dst = src;

    CHECK(dst.is_owner());
    CHECK(dst.url() == "dds://assign_test");
  }

  TEST_CASE("move assignment transfers ownership") {
    zerocopy::ProxyData src;
    Bytes raw = Bytes::create(16);
    src.create(raw, "dds://move_assign_test", "raw");

    zerocopy::ProxyData dst;
    dst = std::move(src);

    CHECK(dst.is_owner());
    CHECK(dst.url() == "dds://move_assign_test");
    CHECK(!src.is_valid());
  }

  TEST_CASE("clear() resets to invalid/empty state") {
    zerocopy::ProxyData pd;
    Bytes raw = Bytes::create(64);
    pd.create(raw, "dds://clear_test", "protobuf");
    pd.set_control_id(99);

    pd.clear();

    CHECK(!pd.is_valid());
    CHECK(pd.size() == 0);
    CHECK(!pd.is_owner());
    CHECK(pd.control_id() == 0);
    CHECK(pd.url().empty());
  }
}

// NOLINTEND
