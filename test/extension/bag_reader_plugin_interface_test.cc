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

#include <optional>
#include <string>
#include <type_traits>

#include "../common_test.h"

namespace {

class FakePlugin final : public BagReaderPluginInterface {
 public:
  FakePlugin() = default;

  VersionInfo get_version_info() const override { return {"FakePlugin", "2.0.0", "2026-01-01", "v2.0.0", "cafebabe"}; }

  bool convert_url_meta(std::string& url, std::string& ser_type, SchemaType& schema_type) override {
    called_convert = true;
    captured_url = url;
    captured_ser_type = ser_type;
    captured_schema_type = schema_type;

    if (remap_to.has_value()) {
      url = *remap_to;
    }

    return accept;
  }

  void push(int64_t ts, const std::string& url, ActionType action, const Bytes& data) override {
    called_push = true;
    captured_ts = ts;
    captured_push_url = url;
    captured_action = action;
    captured_size = data.size();

    if (output_callback_) {
      output_callback_(ts, url, action, data);
    }
  }

  bool called_convert{false};
  bool called_push{false};
  bool accept{true};
  std::string captured_url;
  std::string captured_ser_type;
  SchemaType captured_schema_type{SchemaType::kUnknown};
  std::optional<std::string> remap_to;
  int64_t captured_ts{0};
  std::string captured_push_url;
  ActionType captured_action{ActionType::kUnknownAction};
  size_t captured_size{0};
};

}  // namespace

TEST_SUITE("extension-BagReaderPluginInterface") {
  TEST_CASE("interface is abstract and non-copyable") {
    CHECK(std::is_abstract_v<BagReaderPluginInterface>);
    CHECK_FALSE(std::is_copy_constructible_v<BagReaderPluginInterface>);
    CHECK_FALSE(std::is_copy_assignable_v<BagReaderPluginInterface>);
  }

  TEST_CASE("version info default constructs with empty fields") {
    BagReaderPluginInterface::VersionInfo vi;
    CHECK(vi.name.empty());
    CHECK(vi.version.empty());
    CHECK(vi.timestamp.empty());
    CHECK(vi.tag.empty());
    CHECK(vi.commit_id.empty());
  }

  TEST_CASE("version info fields are independently mutable") {
    BagReaderPluginInterface::VersionInfo vi;
    vi.name = "MyPlugin";
    vi.version = "1.2.3";
    vi.timestamp = "2026-01-01";
    vi.tag = "v1.2.3";
    vi.commit_id = "abc123";

    CHECK_EQ(vi.name, "MyPlugin");
    CHECK_EQ(vi.version, "1.2.3");
    CHECK_EQ(vi.timestamp, "2026-01-01");
    CHECK_EQ(vi.tag, "v1.2.3");
    CHECK_EQ(vi.commit_id, "abc123");
  }

  TEST_CASE("get_version_info returns subclass values") {
    FakePlugin plugin;
    auto info = plugin.get_version_info();
    CHECK_EQ(info.name, "FakePlugin");
    CHECK_EQ(info.version, "2.0.0");
    CHECK_EQ(info.timestamp, "2026-01-01");
    CHECK_EQ(info.tag, "v2.0.0");
    CHECK_EQ(info.commit_id, "cafebabe");
  }

  TEST_CASE("virtual dispatch via base pointer works") {
    FakePlugin concrete;
    BagReaderPluginInterface* base = &concrete;
    auto info = base->get_version_info();
    CHECK_EQ(info.name, "FakePlugin");
  }

  TEST_CASE("convert_url_meta receives arguments by reference") {
    FakePlugin plugin;
    std::string url = "intra://sensor/lidar";
    std::string ser_type = "proto.Lidar";
    SchemaType schema_type = SchemaType::kProtobuf;

    bool ok = plugin.convert_url_meta(url, ser_type, schema_type);

    CHECK(ok);
    CHECK(plugin.called_convert);
    CHECK_EQ(plugin.captured_url, "intra://sensor/lidar");
    CHECK_EQ(plugin.captured_ser_type, "proto.Lidar");
    CHECK_EQ(plugin.captured_schema_type, SchemaType::kProtobuf);
  }

  TEST_CASE("convert_url_meta can rewrite url in-place") {
    FakePlugin plugin;
    plugin.remap_to = "dds://vehicle/lidar";

    std::string url = "intra://sensor/lidar";
    std::string ser_type = "proto.Lidar";
    SchemaType schema_type = SchemaType::kProtobuf;

    CHECK(plugin.convert_url_meta(url, ser_type, schema_type));
    CHECK_EQ(url, "dds://vehicle/lidar");
  }

  TEST_CASE("convert_url_meta can exclude a url by returning false") {
    FakePlugin plugin;
    plugin.accept = false;

    std::string url = "intra://x";
    std::string ser_type = "raw";
    SchemaType schema_type = SchemaType::kUnknown;

    CHECK_FALSE(plugin.convert_url_meta(url, ser_type, schema_type));
  }

  TEST_CASE("push records message metadata") {
    FakePlugin plugin;
    Bytes data = Bytes::create(8u);

    plugin.push(9999, "dds://topic", ActionType::kPublish, data);

    CHECK(plugin.called_push);
    CHECK_EQ(plugin.captured_ts, 9999);
    CHECK_EQ(plugin.captured_push_url, "dds://topic");
    CHECK_EQ(plugin.captured_action, ActionType::kPublish);
    CHECK_EQ(plugin.captured_size, data.size());
  }

  TEST_CASE("register_output_callback is invoked from push") {
    FakePlugin plugin;
    int call_count = 0;
    int64_t observed_ts = -1;
    std::string observed_url;

    plugin.register_output_callback([&](int64_t ts, const std::string& url, ActionType, const Bytes&) {
      ++call_count;
      observed_ts = ts;
      observed_url = url;
    });

    Bytes data = Bytes::create(4u);
    plugin.push(7777, "intra://x", ActionType::kSubscribe, data);

    CHECK_EQ(call_count, 1);
    CHECK_EQ(observed_ts, 7777);
    CHECK_EQ(observed_url, "intra://x");
  }

  TEST_CASE("register_output_callback replaces previously registered callback") {
    FakePlugin plugin;
    int first_count = 0;
    int second_count = 0;

    plugin.register_output_callback([&](int64_t, const std::string&, ActionType, const Bytes&) { ++first_count; });
    plugin.register_output_callback([&](int64_t, const std::string&, ActionType, const Bytes&) { ++second_count; });

    Bytes data = Bytes::create(1u);
    plugin.push(0, "u", ActionType::kPublish, data);

    CHECK_EQ(first_count, 0);
    CHECK_EQ(second_count, 1);
  }

  TEST_CASE("push without registered callback does not crash") {
    FakePlugin plugin;
    Bytes data = Bytes::create(4u);
    plugin.push(1, "intra://x", ActionType::kPublish, data);
    CHECK(plugin.called_push);
  }
}

// NOLINTEND
