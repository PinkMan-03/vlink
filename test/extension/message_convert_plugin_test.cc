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

#include "./extension/message_convert_plugin.h"

#include <doctest/doctest.h>

#include <cstdint>
#include <string>
#include <type_traits>

#include "../common_test.h"

namespace {

class FakeConvertPlugin final : public MessageConvertPlugin {
 public:
  FakeConvertPlugin() = default;

  bool init(const std::string& config) override {
    init_called = true;
    last_config = config;
    return init_result;
  }

  bool can_convert(const std::string& vlink_ser, ConvertTarget target) override {
    return vlink_ser == handled_ser && (target == ConvertTarget::kFoxglove || target == ConvertTarget::kRerun);
  }

  bool get_schema_info(const std::string& vlink_ser, ConvertTarget target, std::string& type_name,
                       std::string& encoding, std::string& schema_encoding, std::string& schema_data) override {
    if (vlink_ser != handled_ser) {
      return false;
    }

    if (target == ConvertTarget::kFoxglove) {
      type_name = "foxglove.Test";
      encoding = "flatbuffers";
      schema_encoding = "flatbuffers";
      schema_data = "BFBS_BYTES";
    } else {
      type_name = "Points3D";
      encoding = "json";
      schema_encoding.clear();
      schema_data.clear();
    }

    return true;
  }

  bool convert(const std::string& vlink_ser, const Bytes& raw, ConvertTarget target, Bytes& payload) override {
    if (vlink_ser != handled_ser) {
      return false;
    }

    last_target = target;
    last_input_size = raw.size();
    payload = Bytes::create(output_size);
    return true;
  }

  bool init_called{false};
  bool init_result{true};
  std::string last_config;
  std::string handled_ser{"my_pkg.MyMessage"};
  ConvertTarget last_target{ConvertTarget::kFoxglove};
  size_t last_input_size{0};
  size_t output_size{16u};
};

}  // namespace

TEST_SUITE("extension-MessageConvertPlugin") {
  TEST_CASE("convert target enum values are sequential and distinct") {
    CHECK_EQ(static_cast<uint8_t>(ConvertTarget::kFoxglove), 0u);
    CHECK_EQ(static_cast<uint8_t>(ConvertTarget::kRerun), 1u);
    CHECK_NE(ConvertTarget::kFoxglove, ConvertTarget::kRerun);
  }

  TEST_CASE("web channel default construction yields all empty fields") {
    WebChannel ch;
    CHECK(ch.topic.empty());
    CHECK(ch.encoding.empty());
    CHECK(ch.schema_name.empty());
    CHECK(ch.schema_encoding.empty());
    CHECK(ch.schema.empty());
  }

  TEST_CASE("web channel fields are independently mutable") {
    WebChannel ch;
    ch.topic = "/cmd";
    ch.encoding = "json";
    ch.schema_name = "Cmd";
    ch.schema_encoding = "jsonschema";
    ch.schema = "{}";
    CHECK_EQ(ch.topic, "/cmd");
    CHECK_EQ(ch.encoding, "json");
    CHECK_EQ(ch.schema_name, "Cmd");
    CHECK_EQ(ch.schema_encoding, "jsonschema");
    CHECK_EQ(ch.schema, "{}");
  }

  TEST_CASE("vlink publish default construction yields kUnknown schema type") {
    VlinkPublish p;
    CHECK(p.url.empty());
    CHECK(p.serialization.empty());
    CHECK_EQ(p.schema_type, SchemaType::kUnknown);
  }

  TEST_CASE("interface is abstract and non-copyable") {
    CHECK(std::is_abstract_v<MessageConvertPlugin>);
    CHECK_FALSE(std::is_copy_constructible_v<MessageConvertPlugin>);
    CHECK_FALSE(std::is_copy_assignable_v<MessageConvertPlugin>);
  }

  TEST_CASE("init forwards config string and returns plugin's return value") {
    FakeConvertPlugin plugin;
    CHECK(plugin.init("{\"key\":\"value\"}"));
    CHECK(plugin.init_called);
    CHECK_EQ(plugin.last_config, "{\"key\":\"value\"}");
  }

  TEST_CASE("init returns false when plugin signals failure") {
    FakeConvertPlugin plugin;
    plugin.init_result = false;
    CHECK_FALSE(plugin.init(""));
  }

  TEST_CASE("can_convert accepts handled type for both targets") {
    FakeConvertPlugin plugin;
    CHECK(plugin.can_convert("my_pkg.MyMessage", ConvertTarget::kFoxglove));
    CHECK(plugin.can_convert("my_pkg.MyMessage", ConvertTarget::kRerun));
    CHECK_FALSE(plugin.can_convert("other.Type", ConvertTarget::kFoxglove));
    CHECK_FALSE(plugin.can_convert("other.Type", ConvertTarget::kRerun));
  }

  TEST_CASE("get_schema_info fills foxglove schema fields for kFoxglove") {
    FakeConvertPlugin plugin;
    std::string type_name;
    std::string encoding;
    std::string schema_encoding;
    std::string schema_data;

    CHECK(plugin.get_schema_info("my_pkg.MyMessage", ConvertTarget::kFoxglove, type_name, encoding, schema_encoding,
                                 schema_data));
    CHECK_EQ(type_name, "foxglove.Test");
    CHECK_EQ(encoding, "flatbuffers");
    CHECK_EQ(schema_encoding, "flatbuffers");
    CHECK_EQ(schema_data, "BFBS_BYTES");
  }

  TEST_CASE("get_schema_info clears schema fields for kRerun") {
    FakeConvertPlugin plugin;
    std::string type_name;
    std::string encoding;
    std::string schema_encoding = "preset";
    std::string schema_data = "preset";

    CHECK(plugin.get_schema_info("my_pkg.MyMessage", ConvertTarget::kRerun, type_name, encoding, schema_encoding,
                                 schema_data));
    CHECK_EQ(type_name, "Points3D");
    CHECK_EQ(encoding, "json");
    CHECK(schema_encoding.empty());
    CHECK(schema_data.empty());
  }

  TEST_CASE("get_schema_info returns false for unhandled type") {
    FakeConvertPlugin plugin;
    std::string a;
    std::string b;
    std::string c;
    std::string d;
    CHECK_FALSE(plugin.get_schema_info("other.Type", ConvertTarget::kFoxglove, a, b, c, d));
  }

  TEST_CASE("convert produces payload of expected size and records input info") {
    FakeConvertPlugin plugin;
    Bytes raw = Bytes::create(32u);
    Bytes payload;

    CHECK(plugin.convert("my_pkg.MyMessage", raw, ConvertTarget::kFoxglove, payload));
    CHECK_EQ(plugin.last_input_size, 32u);
    CHECK_EQ(plugin.last_target, ConvertTarget::kFoxglove);
    CHECK_EQ(payload.size(), 16u);
  }

  TEST_CASE("convert returns false for unhandled type") {
    FakeConvertPlugin plugin;
    Bytes raw = Bytes::create(8u);
    Bytes payload;
    CHECK_FALSE(plugin.convert("other.Type", raw, ConvertTarget::kFoxglove, payload));
  }

  TEST_CASE("extract_timestamp default implementation returns -1") {
    FakeConvertPlugin plugin;
    Bytes raw = Bytes::create(8u);
    int64_t ts = plugin.extract_timestamp("my_pkg.MyMessage", raw, ConvertTarget::kFoxglove);
    CHECK_EQ(ts, -1);
  }

  TEST_CASE("can_convert_frontend default implementation returns false") {
    FakeConvertPlugin plugin;
    WebChannel ch;
    ch.topic = "any";
    CHECK_FALSE(plugin.can_convert_frontend(ch, ConvertTarget::kFoxglove));
  }

  TEST_CASE("get_publish_info default implementation returns false and leaves output unchanged") {
    FakeConvertPlugin plugin;
    WebChannel ch;
    VlinkPublish pub;
    pub.url = "preset";
    CHECK_FALSE(plugin.get_publish_info(ch, ConvertTarget::kFoxglove, pub));
    CHECK_EQ(pub.url, "preset");
  }

  TEST_CASE("convert_frontend default implementation returns false") {
    FakeConvertPlugin plugin;
    WebChannel ch;
    Bytes raw = Bytes::create(4u);
    Bytes payload;
    CHECK_FALSE(plugin.convert_frontend(ch, raw, ConvertTarget::kFoxglove, payload));
  }
}

// NOLINTEND
