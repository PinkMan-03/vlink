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

#include "./extension/bag_reader_plugin_interface.h"

#include <doctest/doctest.h>

#include <cstdint>
#include <optional>
#include <string>
#include <type_traits>

//
#include "../common_test.h"

// ---------------------------------------------------------------------------
// Helper: a concrete subclass that records arguments passed to the interface
// methods so the contract can be exercised end-to-end without a real plugin
// shared library.
// ---------------------------------------------------------------------------

namespace {

class FakeBagReaderPlugin final : public BagReaderPluginInterface {
 public:
  FakeBagReaderPlugin() = default;

  [[nodiscard]] VersionInfo get_version_info() const override {
    return {"FakeBagReaderPlugin", "1.0.0", "2026-01-01", "v1.0.0", "deadbeef"};
  }

  bool convert_url_meta(std::string& url, std::string& ser_type, SchemaType& schema_type) override {
    convert_url_meta_called = true;
    last_url = url;
    last_ser_type = ser_type;
    last_schema_type = schema_type;

    if (rewrite_url_to.has_value()) {
      url = *rewrite_url_to;
    }

    return convert_url_meta_return_value;
  }

  void push(int64_t timestamp, const std::string& url, ActionType action_type, const Bytes& data) override {
    push_called = true;
    last_push_timestamp = timestamp;
    last_push_url = url;
    last_push_action = action_type;
    last_push_size = data.size();

    if (output_callback_) {
      output_callback_(timestamp, url, action_type, data);
    }
  }

  bool convert_url_meta_called{false};
  bool push_called{false};
  bool convert_url_meta_return_value{true};
  std::string last_url;
  std::string last_ser_type;
  SchemaType last_schema_type{SchemaType::kUnknown};
  std::optional<std::string> rewrite_url_to;
  int64_t last_push_timestamp{0};
  std::string last_push_url;
  ActionType last_push_action{ActionType::kUnknownAction};
  size_t last_push_size{0};
};

}  // namespace

// ---------------------------------------------------------------------------
// TEST SUITE: BagReaderPluginInterface - traits
// ---------------------------------------------------------------------------

TEST_SUITE("extension-BagReaderPluginInterface - traits") {
  TEST_CASE("interface is abstract") { CHECK(std::is_abstract_v<BagReaderPluginInterface>); }

  TEST_CASE("interface is not copy-constructible") {
    CHECK_FALSE(std::is_copy_constructible_v<BagReaderPluginInterface>);
  }

  TEST_CASE("interface is not copy-assignable") { CHECK_FALSE(std::is_copy_assignable_v<BagReaderPluginInterface>); }

  TEST_CASE("VersionInfo default-constructs to empty fields") {
    BagReaderPluginInterface::VersionInfo vi;
    CHECK(vi.name.empty());
    CHECK(vi.version.empty());
    CHECK(vi.timestamp.empty());
    CHECK(vi.tag.empty());
    CHECK(vi.commit_id.empty());
  }

  TEST_CASE("VersionInfo fields are settable") {
    BagReaderPluginInterface::VersionInfo vi;
    vi.name = "n";
    vi.version = "v";
    vi.timestamp = "t";
    vi.tag = "tag";
    vi.commit_id = "id";
    CHECK(vi.name == "n");
    CHECK(vi.version == "v");
    CHECK(vi.timestamp == "t");
    CHECK(vi.tag == "tag");
    CHECK(vi.commit_id == "id");
  }
}

// ---------------------------------------------------------------------------
// TEST SUITE: BagReaderPluginInterface - subclass behaviour
// ---------------------------------------------------------------------------

TEST_SUITE("extension-BagReaderPluginInterface - subclass behaviour") {
  TEST_CASE("get_version_info returns the subclass-provided values") {
    FakeBagReaderPlugin plugin;
    auto info = plugin.get_version_info();
    CHECK(info.name == "FakeBagReaderPlugin");
    CHECK(info.version == "1.0.0");
    CHECK(info.timestamp == "2026-01-01");
    CHECK(info.tag == "v1.0.0");
    CHECK(info.commit_id == "deadbeef");
  }

  TEST_CASE("convert_url_meta receives URL/ser_type/schema_type by reference") {
    FakeBagReaderPlugin plugin;
    std::string url = "intra://sensor/lidar";
    std::string ser_type = "proto.Lidar";
    SchemaType schema_type = SchemaType::kProtobuf;

    bool ok = plugin.convert_url_meta(url, ser_type, schema_type);

    CHECK(ok);
    CHECK(plugin.convert_url_meta_called);
    CHECK(plugin.last_url == "intra://sensor/lidar");
    CHECK(plugin.last_ser_type == "proto.Lidar");
    CHECK(plugin.last_schema_type == SchemaType::kProtobuf);
  }

  TEST_CASE("convert_url_meta may rewrite URL in-place") {
    FakeBagReaderPlugin plugin;
    plugin.rewrite_url_to = "dds://vehicle/lidar";

    std::string url = "intra://sensor/lidar";
    std::string ser_type = "proto.Lidar";
    SchemaType schema_type = SchemaType::kProtobuf;

    CHECK(plugin.convert_url_meta(url, ser_type, schema_type));
    CHECK(url == "dds://vehicle/lidar");
  }

  TEST_CASE("convert_url_meta may signal exclusion via false return") {
    FakeBagReaderPlugin plugin;
    plugin.convert_url_meta_return_value = false;

    std::string url = "intra://x";
    std::string ser_type = "y";
    SchemaType schema_type = SchemaType::kUnknown;

    CHECK_FALSE(plugin.convert_url_meta(url, ser_type, schema_type));
  }

  TEST_CASE("push records the message metadata") {
    FakeBagReaderPlugin plugin;
    Bytes data = Bytes::create(8U);

    plugin.push(1234, "dds://topic", ActionType::kPublish, data);

    CHECK(plugin.push_called);
    CHECK(plugin.last_push_timestamp == 1234);
    CHECK(plugin.last_push_url == "dds://topic");
    CHECK(plugin.last_push_action == ActionType::kPublish);
    CHECK(plugin.last_push_size == data.size());
  }

  TEST_CASE("register_output_callback wires the callback used by push") {
    FakeBagReaderPlugin plugin;
    int call_count = 0;
    int64_t observed_ts = -1;
    std::string observed_url;
    ActionType observed_action = ActionType::kUnknownAction;

    plugin.register_output_callback([&](int64_t ts, const std::string& url, ActionType action, const Bytes&) {
      call_count += 1;
      observed_ts = ts;
      observed_url = url;
      observed_action = action;
    });

    Bytes data = Bytes::create(4U);
    plugin.push(7777, "intra://x", ActionType::kSubscribe, data);

    CHECK(call_count == 1);
    CHECK(observed_ts == 7777);
    CHECK(observed_url == "intra://x");
    CHECK(observed_action == ActionType::kSubscribe);
  }

  TEST_CASE("register_output_callback replaces a previously registered callback") {
    FakeBagReaderPlugin plugin;
    int first_count = 0;
    int second_count = 0;

    plugin.register_output_callback([&](int64_t, const std::string&, ActionType, const Bytes&) { first_count += 1; });
    plugin.register_output_callback([&](int64_t, const std::string&, ActionType, const Bytes&) { second_count += 1; });

    Bytes data = Bytes::create(1U);
    plugin.push(0, "u", ActionType::kPublish, data);

    CHECK(first_count == 0);
    CHECK(second_count == 1);
  }

  TEST_CASE("virtual dispatch via base pointer") {
    FakeBagReaderPlugin concrete;
    BagReaderPluginInterface* base = &concrete;
    auto info = base->get_version_info();
    CHECK(info.name == "FakeBagReaderPlugin");
  }
}

// NOLINTEND
