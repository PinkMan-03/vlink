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

#include "./extension/bag_reader.h"

#include <doctest/doctest.h>

#include <atomic>
#include <filesystem>
#include <future>
#include <string>
#include <unordered_map>
#include <vector>

#include "../common_test.h"

namespace {

class StubBagReader : public BagReader {
 public:
  explicit StubBagReader(const std::string& path = (std::filesystem::temp_directory_path() / "stub.vdb").string())
      : BagReader(path, true, false) {}

  ~StubBagReader() override = default;

  void play(const Config&) override {}

  void stop() override {}

  void pause() override {}

  void resume() override {}

  void pause_to_next() override {}

  void jump(int64_t, double, int, bool) override {}

  std::future<bool> check() override {
    return std::async(std::launch::deferred, [] { return true; });
  }

  std::future<bool> reindex() override {
    return std::async(std::launch::deferred, [] { return true; });
  }

  std::future<bool> fix(bool) override {
    return std::async(std::launch::deferred, [] { return true; });
  }

  void tag(const std::string&) override {}

  int64_t get_timestamp() const override { return 0; }

  int64_t get_real_timestamp() const override { return 0; }

  Status get_status() const override { return kStopped; }

  const Info& get_info() const override { return info_; }

  std::vector<SchemaData> detect_schema() override { return {}; }

  std::string get_ser_type(const std::string&) const override { return {}; }

  SchemaType get_schema_type(const std::string&) const override { return SchemaType::kUnknown; }

  bool is_split_mode() const override { return false; }

  int get_split_index() const override { return 0; }

  bool is_jumping() const override { return false; }

  using BagReader::convert_action;
  using BagReader::match_playback_url_filter;
  using BagReader::process_output;
  using BagReader::process_url_metas;
  using BagReader::rebuild_url_meta_maps;

 private:
  Info info_;
};

class RemapPlugin final : public BagReaderPluginInterface {
 public:
  VersionInfo get_version_info() const override { return {"Remap", "1.0.0", "", "", ""}; }

  bool convert_url_meta(std::string& url, std::string& ser_type, SchemaType& schema_type) override {
    (void)ser_type;
    (void)schema_type;

    if (url == "intra://drop") {
      return false;
    }

    if (url == "intra://old") {
      url = "intra://new";
    }

    return true;
  }

  void push(int64_t ts, const std::string& url, ActionType action, const Bytes& data) override {
    if (output_callback_) {
      output_callback_(ts, url, action, data);
    }
  }
};

}  // namespace

TEST_SUITE("extension-BagReader") {
  TEST_CASE("stub construction yields stopped/zero/false state") {
    StubBagReader reader;

    CHECK_EQ(reader.get_status(), BagReader::kStopped);
    CHECK_EQ(reader.get_timestamp(), 0);
    CHECK_EQ(reader.get_real_timestamp(), 0);
    CHECK_FALSE(reader.is_split_mode());
    CHECK_EQ(reader.get_split_index(), 0);
    CHECK_FALSE(reader.is_jumping());
    CHECK(reader.detect_schema().empty());
    CHECK(reader.get_ser_type("any").empty());
    CHECK_EQ(reader.get_schema_type("any"), SchemaType::kUnknown);
  }

  TEST_CASE("output callback is invoked by process_output") {
    StubBagReader reader;
    std::atomic_bool called{false};
    std::string received_url;

    reader.register_output_callback([&](int64_t, const std::string& url, ActionType, const Bytes&) {
      called = true;
      received_url = url;
    });

    Bytes data;
    reader.process_output(100, "dds://test", ActionType::kPublish, data);

    CHECK(called.load());
    CHECK_EQ(received_url, "dds://test");
  }

  TEST_CASE("process_output without callback does not crash") {
    StubBagReader reader;
    Bytes data;
    reader.process_output(0, "dds://test", ActionType::kPublish, data);
  }

  TEST_CASE("registering status/ready/finish callbacks does not crash") {
    StubBagReader reader;
    reader.register_status_callback([](BagReader::Status) {});
    reader.register_ready_callback([] {});
    reader.register_finish_callback([](bool) {});
  }

  TEST_CASE("process_url_metas without plugin leaves metas unchanged") {
    StubBagReader reader;
    std::vector<BagReader::Info::UrlMeta> metas;

    BagReader::Info::UrlMeta m;
    m.url = "dds://topic";
    m.ser_type = "proto";
    metas.push_back(m);

    reader.process_url_metas(metas);

    REQUIRE_EQ(metas.size(), 1u);
    CHECK_EQ(metas[0].url, "dds://topic");
  }

  TEST_CASE("plugin remaps and excludes urls during process_url_metas") {
    StubBagReader reader;
    auto plugin = std::make_shared<RemapPlugin>();
    reader.bind_plugin_interface(plugin);

    std::vector<BagReader::Info::UrlMeta> metas;
    BagReader::Info::UrlMeta a;
    a.url = "intra://old";
    metas.push_back(a);

    BagReader::Info::UrlMeta b;
    b.url = "intra://drop";
    metas.push_back(b);

    reader.process_url_metas(metas);

    REQUIRE_EQ(metas.size(), 1u);
    CHECK_EQ(metas[0].url, "intra://new");
  }

  TEST_CASE("process_output forwards remapped url derived from process_url_metas") {
    StubBagReader reader;
    auto plugin = std::make_shared<RemapPlugin>();
    reader.bind_plugin_interface(plugin);

    std::vector<BagReader::Info::UrlMeta> metas;
    BagReader::Info::UrlMeta m;
    m.url = "intra://old";
    metas.push_back(m);
    reader.process_url_metas(metas);

    std::string observed_url;
    int call_count = 0;

    reader.register_output_callback([&](int64_t, const std::string& url, ActionType, const Bytes&) {
      observed_url = url;
      ++call_count;
    });

    Bytes data = Bytes::create(1u);
    reader.process_output(1, "intra://old", ActionType::kPublish, data);

    CHECK_EQ(call_count, 1);
    CHECK_EQ(observed_url, "intra://new");
  }

  TEST_CASE("match_playback_url_filter uses remapped url for filter matching") {
    StubBagReader reader;
    auto plugin = std::make_shared<RemapPlugin>();
    reader.bind_plugin_interface(plugin);

    std::vector<BagReader::Info::UrlMeta> metas;
    BagReader::Info::UrlMeta m;
    m.url = "intra://old";
    metas.push_back(m);
    reader.process_url_metas(metas);

    std::unordered_set<std::string> filter_urls;
    filter_urls.emplace("intra://new");

    CHECK(reader.match_playback_url_filter("intra://old", filter_urls));
    CHECK_FALSE(reader.match_playback_url_filter("intra://drop", filter_urls));
  }

  TEST_CASE("rebinding plugin disconnects the old plugin output callback") {
    StubBagReader reader;
    auto old_plugin = std::make_shared<RemapPlugin>();
    auto new_plugin = std::make_shared<RemapPlugin>();

    int call_count = 0;
    reader.register_output_callback([&](int64_t, const std::string&, ActionType, const Bytes&) { ++call_count; });

    reader.bind_plugin_interface(old_plugin);
    reader.bind_plugin_interface(new_plugin);

    Bytes data = Bytes::create(1u);
    old_plugin->push(1, "intra://old", ActionType::kPublish, data);
    CHECK_EQ(call_count, 0);

    new_plugin->push(2, "intra://old", ActionType::kPublish, data);
    CHECK_EQ(call_count, 1);
  }

  TEST_CASE("convert_action maps known tokens to action types") {
    CHECK_EQ(StubBagReader::convert_action("Pub"), ActionType::kPublish);
    CHECK_EQ(StubBagReader::convert_action("Sub"), ActionType::kSubscribe);
    CHECK_EQ(StubBagReader::convert_action("C/Req"), ActionType::kClientRequest);
    CHECK_EQ(StubBagReader::convert_action("C/Resp"), ActionType::kClientResponse);
    CHECK_EQ(StubBagReader::convert_action("S/Req"), ActionType::kServerRequest);
    CHECK_EQ(StubBagReader::convert_action("S/Resp"), ActionType::kServerResponse);
    CHECK_EQ(StubBagReader::convert_action("Set"), ActionType::kSet);
    CHECK_EQ(StubBagReader::convert_action("Get"), ActionType::kGet);
  }

  TEST_CASE("convert_action returns kUnknownAction for unknown tokens") {
    CHECK_EQ(StubBagReader::convert_action("XYZ"), ActionType::kUnknownAction);
    CHECK_EQ(StubBagReader::convert_action(""), ActionType::kUnknownAction);
    CHECK_EQ(StubBagReader::convert_action("Unknown"), ActionType::kUnknownAction);
  }

  TEST_CASE("rebuild_url_meta_maps preserves known schema type over unknown duplicate") {
    std::vector<BagReader::Info::UrlMeta> metas;

    BagReader::Info::UrlMeta known;
    known.url = "intra://test";
    known.ser_type = "demo.Type";
    known.schema_type = SchemaType::kProtobuf;
    metas.emplace_back(known);

    BagReader::Info::UrlMeta unknown;
    unknown.url = "intra://test";
    unknown.schema_type = SchemaType::kUnknown;
    metas.emplace_back(unknown);

    std::unordered_map<std::string, std::string> ser_map;
    std::unordered_map<std::string, SchemaType> schema_type_map;
    StubBagReader::rebuild_url_meta_maps(metas, ser_map, schema_type_map);

    REQUIRE_EQ(ser_map.count("intra://test"), 1u);
    REQUIRE_EQ(schema_type_map.count("intra://test"), 1u);
    CHECK_EQ(ser_map["intra://test"], "demo.Type");
    CHECK_EQ(schema_type_map["intra://test"], SchemaType::kProtobuf);
  }

  TEST_CASE("create returns nullptr for unsupported file extension") {
    auto reader = BagReader::create("/tmp/unsupported.xyz");

    CHECK(reader == nullptr);
  }
}

// NOLINTEND
