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
#include <memory>
#include <string>
#include <type_traits>

//
#include "../common_test.h"

// ---------------------------------------------------------------------------
// Helper: a minimal MessageConvertPlugin that records each call.
// ---------------------------------------------------------------------------

namespace {

class FakeConvertPlugin final : public MessageConvertPlugin {
 public:
  FakeConvertPlugin() = default;

  bool init(const std::string& config) override {
    init_called = true;
    init_config = config;
    return init_return_value;
  }

  [[nodiscard]] bool can_convert(const std::string& vlink_ser, ConvertTarget target) override {
    return vlink_ser == handled_ser && (target == ConvertTarget::kFoxglove || target == ConvertTarget::kRerun);
  }

  [[nodiscard]] bool get_schema_info(const std::string& vlink_ser, ConvertTarget target, std::string& type_name,
                                     std::string& encoding, std::string& schema_encoding,
                                     std::string& schema_data) override {
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

  [[nodiscard]] bool convert(const std::string& vlink_ser, const Bytes& raw, ConvertTarget target,
                             Bytes& payload) override {
    if (vlink_ser != handled_ser) {
      return false;
    }
    last_target = target;
    last_input_size = raw.size();
    payload = Bytes::create(static_cast<size_t>(payload_size_));
    return true;
  }

  bool init_called{false};
  bool init_return_value{true};
  std::string init_config;
  std::string handled_ser{"my_pkg.MyMessage"};
  ConvertTarget last_target{ConvertTarget::kFoxglove};
  size_t last_input_size{0};
  size_t payload_size_{16};
};

}  // namespace

// ---------------------------------------------------------------------------
// TEST SUITE: ConvertTarget enum
// ---------------------------------------------------------------------------

TEST_SUITE("extension-MessageConvertPlugin - ConvertTarget enum") {
  TEST_CASE("kFoxglove == 0") { CHECK(static_cast<uint8_t>(ConvertTarget::kFoxglove) == 0); }

  TEST_CASE("kRerun == 1") { CHECK(static_cast<uint8_t>(ConvertTarget::kRerun) == 1); }
}

// ---------------------------------------------------------------------------
// TEST SUITE: WebChannel / VlinkPublish struct defaults
// ---------------------------------------------------------------------------

TEST_SUITE("extension-MessageConvertPlugin - WebChannel struct") {
  TEST_CASE("WebChannel default-constructs to empty fields") {
    WebChannel ch;
    CHECK(ch.topic.empty());
    CHECK(ch.encoding.empty());
    CHECK(ch.schema_name.empty());
    CHECK(ch.schema_encoding.empty());
    CHECK(ch.schema.empty());
  }

  TEST_CASE("WebChannel fields are settable") {
    WebChannel ch;
    ch.topic = "/cmd";
    ch.encoding = "json";
    ch.schema_name = "Cmd";
    ch.schema_encoding = "jsonschema";
    ch.schema = "{}";
    CHECK(ch.topic == "/cmd");
    CHECK(ch.encoding == "json");
    CHECK(ch.schema_name == "Cmd");
    CHECK(ch.schema_encoding == "jsonschema");
    CHECK(ch.schema == "{}");
  }

  TEST_CASE("VlinkPublish default-constructs to kUnknown schema_type") {
    VlinkPublish p;
    CHECK(p.url.empty());
    CHECK(p.serialization.empty());
    CHECK(p.schema_type == SchemaType::kUnknown);
  }
}

// ---------------------------------------------------------------------------
// TEST SUITE: MessageConvertPlugin - traits
// ---------------------------------------------------------------------------

TEST_SUITE("extension-MessageConvertPlugin - traits") {
  TEST_CASE("interface is abstract") { CHECK(std::is_abstract_v<MessageConvertPlugin>); }

  TEST_CASE("interface is not copy-constructible") { CHECK_FALSE(std::is_copy_constructible_v<MessageConvertPlugin>); }

  TEST_CASE("interface is not copy-assignable") { CHECK_FALSE(std::is_copy_assignable_v<MessageConvertPlugin>); }
}

// ---------------------------------------------------------------------------
// TEST SUITE: MessageConvertPlugin - subclass behaviour
// ---------------------------------------------------------------------------

TEST_SUITE("extension-MessageConvertPlugin - subclass behaviour") {
  TEST_CASE("init forwards config and return value") {
    FakeConvertPlugin plugin;
    bool ok = plugin.init("{\"key\":\"value\"}");
    CHECK(ok);
    CHECK(plugin.init_called);
    CHECK(plugin.init_config == "{\"key\":\"value\"}");
  }

  TEST_CASE("can_convert filters by handled type") {
    FakeConvertPlugin plugin;
    CHECK(plugin.can_convert("my_pkg.MyMessage", ConvertTarget::kFoxglove));
    CHECK(plugin.can_convert("my_pkg.MyMessage", ConvertTarget::kRerun));
    CHECK_FALSE(plugin.can_convert("other.Message", ConvertTarget::kFoxglove));
  }

  TEST_CASE("get_schema_info fills foxglove fields") {
    FakeConvertPlugin plugin;
    std::string type_name;
    std::string encoding;
    std::string schema_encoding;
    std::string schema_data;

    bool ok = plugin.get_schema_info("my_pkg.MyMessage", ConvertTarget::kFoxglove, type_name, encoding, schema_encoding,
                                     schema_data);
    CHECK(ok);
    CHECK(type_name == "foxglove.Test");
    CHECK(encoding == "flatbuffers");
    CHECK(schema_encoding == "flatbuffers");
    CHECK(schema_data == "BFBS_BYTES");
  }

  TEST_CASE("get_schema_info leaves schema fields empty for rerun") {
    FakeConvertPlugin plugin;
    std::string type_name;
    std::string encoding;
    std::string schema_encoding = "preset";
    std::string schema_data = "preset";

    bool ok = plugin.get_schema_info("my_pkg.MyMessage", ConvertTarget::kRerun, type_name, encoding, schema_encoding,
                                     schema_data);
    CHECK(ok);
    CHECK(type_name == "Points3D");
    CHECK(encoding == "json");
    CHECK(schema_encoding.empty());
    CHECK(schema_data.empty());
  }

  TEST_CASE("get_schema_info returns false for unhandled types") {
    FakeConvertPlugin plugin;
    std::string a;
    std::string b;
    std::string c;
    std::string d;
    CHECK_FALSE(plugin.get_schema_info("other.Type", ConvertTarget::kFoxglove, a, b, c, d));
  }

  TEST_CASE("convert produces a non-empty payload of the configured size") {
    FakeConvertPlugin plugin;
    Bytes raw = Bytes::create(32U);
    Bytes payload;

    bool ok = plugin.convert("my_pkg.MyMessage", raw, ConvertTarget::kFoxglove, payload);

    CHECK(ok);
    CHECK(plugin.last_input_size == 32U);
    CHECK(plugin.last_target == ConvertTarget::kFoxglove);
    CHECK(payload.size() == 16U);
  }

  TEST_CASE("convert returns false for unhandled types") {
    FakeConvertPlugin plugin;
    Bytes raw = Bytes::create(8U);
    Bytes payload;
    CHECK_FALSE(plugin.convert("other.Type", raw, ConvertTarget::kFoxglove, payload));
  }
}

// ---------------------------------------------------------------------------
// TEST SUITE: MessageConvertPlugin - default frontend hooks
// ---------------------------------------------------------------------------

TEST_SUITE("extension-MessageConvertPlugin - default frontend hooks") {
  TEST_CASE("default extract_timestamp returns -1") {
    FakeConvertPlugin plugin;
    Bytes raw = Bytes::create(8U);
    int64_t ts = plugin.extract_timestamp("my_pkg.MyMessage", raw, ConvertTarget::kFoxglove);
    CHECK(ts == -1);
  }

  TEST_CASE("default can_convert_frontend returns false") {
    FakeConvertPlugin plugin;
    WebChannel ch;
    ch.topic = "any";
    CHECK_FALSE(plugin.can_convert_frontend(ch, ConvertTarget::kFoxglove));
  }

  TEST_CASE("default get_publish_info returns false and leaves output unchanged") {
    FakeConvertPlugin plugin;
    WebChannel ch;
    VlinkPublish pub;
    pub.url = "preset";
    CHECK_FALSE(plugin.get_publish_info(ch, ConvertTarget::kFoxglove, pub));
    CHECK(pub.url == "preset");
  }

  TEST_CASE("default convert_frontend returns false") {
    FakeConvertPlugin plugin;
    WebChannel ch;
    Bytes raw = Bytes::create(4U);
    Bytes payload;
    CHECK_FALSE(plugin.convert_frontend(ch, raw, ConvertTarget::kFoxglove, payload));
  }
}

// NOLINTEND
