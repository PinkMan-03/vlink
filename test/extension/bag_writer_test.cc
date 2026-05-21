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

#include "./extension/bag_writer.h"

#include <doctest/doctest.h>

#include <chrono>
#include <filesystem>
#include <string>

#include "../common_test.h"

namespace {

class StubBagWriter : public BagWriter {
 public:
  explicit StubBagWriter(const std::string& path = (std::filesystem::temp_directory_path() / "stub.vdb").string(),
                         const BagWriter::Config& config = {})
      : BagWriter(path, config) {}

  ~StubBagWriter() override = default;

  void register_split_callback(SplitCallback&&, bool) override {}

  void register_schema_callback(SchemaCallback&&) override {}

  bool push_schema(const SchemaData&, bool) override { return true; }

  int64_t push(const std::string&, const std::string&, SchemaType, ActionType, const Bytes&, int64_t*, bool) override {
    return 0;
  }

  bool is_dumping() const override { return false; }

  bool is_split_mode() const override { return false; }

  int get_split_index() const override { return 0; }

  void set_url_loss(const std::string&, double) override {}

  using BagWriter::convert_action;
  using BagWriter::get_default_app_name;
  using BagWriter::get_default_tag_name;
  using BagWriter::get_default_timezone_diff;
  using BagWriter::get_format_date;
  using BagWriter::get_schema_interface;
  using BagWriter::get_url_meta;
};

}  // namespace

TEST_SUITE("extension-BagWriter") {
  TEST_CASE("stub construction yields false dumping/split state") {
    StubBagWriter writer;
    CHECK_FALSE(writer.is_dumping());
    CHECK_FALSE(writer.is_split_mode());
    CHECK_EQ(writer.get_split_index(), 0);
  }

  TEST_CASE("convert_action maps all known action types to strings") {
    CHECK_EQ(StubBagWriter::convert_action(ActionType::kPublish), "Pub");
    CHECK_EQ(StubBagWriter::convert_action(ActionType::kSubscribe), "Sub");
    CHECK_EQ(StubBagWriter::convert_action(ActionType::kClientRequest), "C/Req");
    CHECK_EQ(StubBagWriter::convert_action(ActionType::kClientResponse), "C/Resp");
    CHECK_EQ(StubBagWriter::convert_action(ActionType::kServerRequest), "S/Req");
    CHECK_EQ(StubBagWriter::convert_action(ActionType::kServerResponse), "S/Resp");
    CHECK_EQ(StubBagWriter::convert_action(ActionType::kSet), "Set");
    CHECK_EQ(StubBagWriter::convert_action(ActionType::kGet), "Get");
    CHECK_EQ(StubBagWriter::convert_action(ActionType::kUnknownAction), "Unknown");
  }

  TEST_CASE("create returns nullptr for unsupported file extension") {
    auto writer = BagWriter::create((std::filesystem::temp_directory_path() / "unsupported.xyz").string());
    CHECK_EQ(writer, nullptr);
  }

  TEST_CASE("filter_get returns nullptr for unsupported file extension") {
    auto writer = BagWriter::filter_get((std::filesystem::temp_directory_path() / "unsupported.xyz").string());
    CHECK_EQ(writer, nullptr);
  }

  TEST_CASE("get_url_meta assigns distinct positive indices for new url and ser") {
    StubBagWriter writer;
    int url_idx = -1;
    int ser_idx = -1;
    writer.get_url_meta("dds://topic1", "protobuf", url_idx, ser_idx);
    CHECK_GT(url_idx, 0);
    CHECK_GT(ser_idx, 0);
  }

  TEST_CASE("get_url_meta returns same indices for the same url and ser") {
    StubBagWriter writer;
    int u1 = -1;
    int s1 = -1;
    int u2 = -1;
    int s2 = -1;
    writer.get_url_meta("dds://topic1", "protobuf", u1, s1);
    writer.get_url_meta("dds://topic1", "protobuf", u2, s2);
    CHECK_EQ(u1, u2);
    CHECK_EQ(s1, s2);
  }

  TEST_CASE("get_url_meta assigns distinct url indices for different urls") {
    StubBagWriter writer;
    int u1 = -1;
    int s1 = -1;
    int u2 = -1;
    int s2 = -1;
    writer.get_url_meta("dds://topic1", "proto", u1, s1);
    writer.get_url_meta("dds://topic2", "proto", u2, s2);
    CHECK_NE(u1, u2);
    CHECK_EQ(s1, s2);
  }

  TEST_CASE("get_url_meta assigns distinct ser indices for different ser types") {
    StubBagWriter writer;
    int u1 = -1;
    int s1 = -1;
    int u2 = -1;
    int s2 = -1;
    writer.get_url_meta("dds://topic", "proto", u1, s1);
    writer.get_url_meta("dds://topic", "raw", u2, s2);
    CHECK_EQ(u1, u2);
    CHECK_NE(s1, s2);
  }

  TEST_CASE("reverse get_url_meta lookup returns original strings") {
    StubBagWriter writer;
    int url_idx = -1;
    int ser_idx = -1;
    writer.get_url_meta("intra://test_topic", "flatbuf", url_idx, ser_idx);

    std::string url_out;
    std::string ser_out;
    writer.get_url_meta(url_idx, ser_idx, url_out, ser_out);

    CHECK_EQ(url_out, "intra://test_topic");
    CHECK_EQ(ser_out, "flatbuf");
  }

  TEST_CASE("reverse get_url_meta lookup with invalid index returns empty strings") {
    StubBagWriter writer;
    std::string url_out;
    std::string ser_out;
    writer.get_url_meta(99999, 99999, url_out, ser_out);
    CHECK(url_out.empty());
    CHECK(ser_out.empty());
  }

  TEST_CASE("multiple urls and ser types all get unique indices and correct reverse lookups") {
    StubBagWriter writer;
    int u1 = -1;
    int s1 = -1;
    int u2 = -1;
    int s2 = -1;
    int u3 = -1;
    int s3 = -1;

    writer.get_url_meta("dds://a", "proto", u1, s1);
    writer.get_url_meta("shm://b", "raw", u2, s2);
    writer.get_url_meta("intra://c", "cdr", u3, s3);

    CHECK_NE(u1, u2);
    CHECK_NE(u2, u3);
    CHECK_NE(u1, u3);
    CHECK_NE(s1, s2);
    CHECK_NE(s2, s3);
    CHECK_NE(s1, s3);

    std::string u;
    std::string s;
    writer.get_url_meta(u2, s2, u, s);
    CHECK_EQ(u, "shm://b");
    CHECK_EQ(s, "raw");
  }

  TEST_CASE("get_default_tag_name returns a non-empty string") {
    CHECK_FALSE(StubBagWriter::get_default_tag_name().empty());
  }

  TEST_CASE("get_default_app_name returns a non-empty string") {
    CHECK_FALSE(StubBagWriter::get_default_app_name().empty());
  }

  TEST_CASE("get_default_timezone_diff returns a value within UTC offset range") {
    int32_t tz = StubBagWriter::get_default_timezone_diff();
    CHECK_GE(tz, -12 * 3600);
    CHECK_LE(tz, 14 * 3600);
  }

  TEST_CASE("get_format_date with no args returns a non-empty string") {
    CHECK_FALSE(StubBagWriter::get_format_date().empty());
  }

  TEST_CASE("get_format_date in file format contains hyphens") {
    std::string date = StubBagWriter::get_format_date(nullptr, true);
    CHECK_FALSE(date.empty());
    CHECK_NE(date.find('-'), std::string::npos);
  }

  TEST_CASE("get_format_date in display format contains slashes and colons") {
    std::string date = StubBagWriter::get_format_date(nullptr, false);
    CHECK_FALSE(date.empty());
    CHECK_NE(date.find('/'), std::string::npos);
    CHECK_NE(date.find(':'), std::string::npos);
  }

  TEST_CASE("get_format_date with fixed epoch produces deterministic year") {
    using SystemClock = BagWriter::SystemClock;
    SystemClock tp{std::chrono::milliseconds(1'000'000'000'000LL)};  // 2001-09-09
    std::string date = StubBagWriter::get_format_date(&tp, false);
    CHECK_NE(date.find("2001"), std::string::npos);
  }

  TEST_CASE("schema callback receives ser_type and schema family") {
    BagWriter::SchemaCallback cb = [](const std::string& ser_type, SchemaType schema_type) {
      SchemaData schema;
      schema.name = ser_type;
      schema.schema_type = schema_type;
      return schema;
    };

    SchemaData result = cb("demo.Message", SchemaType::kProtobuf);
    CHECK_EQ(result.name, "demo.Message");
    CHECK_EQ(result.schema_type, SchemaType::kProtobuf);
  }
}

// NOLINTEND
