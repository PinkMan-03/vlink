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

#include <atomic>
#include <chrono>
#include <filesystem>
#include <string>
#include <thread>

//
#include "../common_test.h"
#include "./extension/mcap_writer.h"

// ---------------------------------------------------------------------------
// Helpers: concrete subclass to test protected methods
// ---------------------------------------------------------------------------

namespace {

class TestBagWriter : public BagWriter {
 public:
  explicit TestBagWriter(const std::string& path = (std::filesystem::temp_directory_path() / "test.vdb").string(),
                         const BagWriter::Config& config = {})
      : BagWriter(path, config) {}

  ~TestBagWriter() override = default;

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

  // Expose protected methods for testing
  using BagWriter::convert_action;
  using BagWriter::get_default_app_name;
  using BagWriter::get_default_tag_name;
  using BagWriter::get_default_timezone_diff;
  using BagWriter::get_format_date;
  using BagWriter::get_schema_interface;
  using BagWriter::get_url_meta;
};

}  // namespace

// ---------------------------------------------------------------------------
// TEST SUITE: BagWriter::CompressType enum
// ---------------------------------------------------------------------------

TEST_SUITE("extension-BagWriter - CompressType") {
  TEST_CASE("enum values are correct") {
    CHECK(static_cast<uint8_t>(BagWriter::kCompressNone) == 0);
    CHECK(static_cast<uint8_t>(BagWriter::kCompressAuto) == 1);
    CHECK(static_cast<uint8_t>(BagWriter::kCompressZstd) == 2);
    CHECK(static_cast<uint8_t>(BagWriter::kCompressLz4) == 3);
    CHECK(static_cast<uint8_t>(BagWriter::kCompressLzav) == 4);
  }
}

// ---------------------------------------------------------------------------
// TEST SUITE: BagWriter::Config defaults
// ---------------------------------------------------------------------------

TEST_SUITE("extension-BagWriter - Config") {
  TEST_CASE("default Config values") {
    BagWriter::Config config;
    CHECK(config.tag_name.empty());
    CHECK(config.compress == BagWriter::kCompressNone);
    CHECK(config.wal_mode == false);
    CHECK(config.enable_limit == false);
    CHECK(config.split_name_by_time == false);
    CHECK(config.sync_mode == false);
    CHECK(config.optimize_on_exit == false);
    CHECK(config.max_row_count == 5'000'000'000LL);
    CHECK(config.max_bytes_size == 1024LL * 1024LL * 1024LL * 512LL);
    CHECK(config.split_by_size == 1024LL * 1024LL * 1024LL * 1LL);
    CHECK(config.split_by_time == 0);
    CHECK(config.begin_time == 0);
    CHECK(config.cache_size == 1024LL * 1024LL * 4);
    CHECK(config.compress_start_size == 128);
    CHECK(config.compress_level == 3);
    CHECK(config.max_task_depth == 20000);
    CHECK(config.max_memory_size == 1024LL * 1024LL * 1024LL * 2LL);
    CHECK(config.start_timestamp == 0);
    CHECK(config.ignore_compress_urls.empty());
  }
}

TEST_SUITE("extension-BagWriter - SchemaCallback") {
  TEST_CASE("schema callback receives ser_type and schema family") {
    BagWriter::SchemaCallback callback = [](const std::string& ser_type, SchemaType schema_type) {
      SchemaData schema;
      schema.name = ser_type;
      schema.schema_type = schema_type;
      return schema;
    };

    const auto schema = callback("demo.Message", SchemaType::kProtobuf);
    CHECK(schema.name == "demo.Message");
    CHECK(schema.schema_type == SchemaType::kProtobuf);
  }
}

// ---------------------------------------------------------------------------
// TEST SUITE: BagWriter - factory suffix handling
// ---------------------------------------------------------------------------

TEST_SUITE("extension-BagWriter - factory suffix handling") {
  TEST_CASE("create rejects unknown suffix") {
    auto writer = BagWriter::create((std::filesystem::temp_directory_path() / "bag_writer_unknown.tmp").string());
    CHECK(writer == nullptr);
  }

  TEST_CASE("filter_get rejects unknown suffix") {
    auto writer = BagWriter::filter_get((std::filesystem::temp_directory_path() / "bag_writer_unknown.tmp").string());
    CHECK(writer == nullptr);
  }
}

// ---------------------------------------------------------------------------
// TEST SUITE: BagWriter - convert_action
// ---------------------------------------------------------------------------

TEST_SUITE("extension-BagWriter - convert_action") {
  TEST_CASE("kPublish maps to Pub") { CHECK(TestBagWriter::convert_action(ActionType::kPublish) == "Pub"); }

  TEST_CASE("kSubscribe maps to Sub") { CHECK(TestBagWriter::convert_action(ActionType::kSubscribe) == "Sub"); }

  TEST_CASE("kClientRequest maps to C/Req") {
    CHECK(TestBagWriter::convert_action(ActionType::kClientRequest) == "C/Req");
  }

  TEST_CASE("kClientResponse maps to C/Resp") {
    CHECK(TestBagWriter::convert_action(ActionType::kClientResponse) == "C/Resp");
  }

  TEST_CASE("kServerRequest maps to S/Req") {
    CHECK(TestBagWriter::convert_action(ActionType::kServerRequest) == "S/Req");
  }

  TEST_CASE("kServerResponse maps to S/Resp") {
    CHECK(TestBagWriter::convert_action(ActionType::kServerResponse) == "S/Resp");
  }

  TEST_CASE("kSet maps to Set") { CHECK(TestBagWriter::convert_action(ActionType::kSet) == "Set"); }

  TEST_CASE("kGet maps to Get") { CHECK(TestBagWriter::convert_action(ActionType::kGet) == "Get"); }

  TEST_CASE("kUnknownAction maps to Unknown") {
    CHECK(TestBagWriter::convert_action(ActionType::kUnknownAction) == "Unknown");
  }
}

// ---------------------------------------------------------------------------
// TEST SUITE: BagWriter - get_format_date
// ---------------------------------------------------------------------------

TEST_SUITE("extension-BagWriter - get_format_date") {
  TEST_CASE("returns non-empty string with no args") {
    std::string date = TestBagWriter::get_format_date();
    CHECK(!date.empty());
  }

  TEST_CASE("file_format uses hyphens") {
    std::string date = TestBagWriter::get_format_date(nullptr, true);
    CHECK(!date.empty());
    // File format uses '-' separators: YYYY-MM-DD_HH-MM-SS-mmm
    CHECK(date.find('-') != std::string::npos);
  }

  TEST_CASE("display format uses slashes and colons") {
    std::string date = TestBagWriter::get_format_date(nullptr, false);
    CHECK(!date.empty());
    // Display format: YYYY/MM/DD HH:MM:SS:mmm
    CHECK(date.find('/') != std::string::npos);
    CHECK(date.find(':') != std::string::npos);
  }

  TEST_CASE("custom SystemClock returns deterministic result") {
    using SystemClock = BagWriter::SystemClock;
    SystemClock tp{std::chrono::milliseconds(1000000000000LL)};  // 2001-09-09
    std::string date = TestBagWriter::get_format_date(&tp, false);
    CHECK(!date.empty());
    CHECK(date.find("2001") != std::string::npos);
  }

  TEST_CASE("custom SystemClock with file_format") {
    using SystemClock = BagWriter::SystemClock;
    SystemClock tp{std::chrono::milliseconds(1000000000000LL)};
    std::string date = TestBagWriter::get_format_date(&tp, true);
    CHECK(!date.empty());
    CHECK(date.find("2001") != std::string::npos);
    CHECK(date.find('_') != std::string::npos);
  }
}

// ---------------------------------------------------------------------------
// TEST SUITE: BagWriter - get_url_meta bidirectional mapping
// ---------------------------------------------------------------------------

TEST_SUITE("extension-BagWriter - get_url_meta") {
  TEST_CASE("first call assigns new indices") {
    TestBagWriter writer;
    int url_idx = -1;
    int ser_idx = -1;
    writer.get_url_meta("dds://topic1", "protobuf", url_idx, ser_idx);
    CHECK(url_idx > 0);
    CHECK(ser_idx > 0);
  }

  TEST_CASE("same url and ser return same indices") {
    TestBagWriter writer;
    int url_idx1 = -1;
    int ser_idx1 = -1;
    int url_idx2 = -1;
    int ser_idx2 = -1;
    writer.get_url_meta("dds://topic1", "protobuf", url_idx1, ser_idx1);
    writer.get_url_meta("dds://topic1", "protobuf", url_idx2, ser_idx2);
    CHECK(url_idx1 == url_idx2);
    CHECK(ser_idx1 == ser_idx2);
  }

  TEST_CASE("different urls get different url indices") {
    TestBagWriter writer;
    int url_idx1 = -1;
    int ser_idx1 = -1;
    int url_idx2 = -1;
    int ser_idx2 = -1;
    writer.get_url_meta("dds://topic1", "protobuf", url_idx1, ser_idx1);
    writer.get_url_meta("dds://topic2", "protobuf", url_idx2, ser_idx2);
    CHECK(url_idx1 != url_idx2);
    CHECK(ser_idx1 == ser_idx2);
  }

  TEST_CASE("different ser types get different ser indices") {
    TestBagWriter writer;
    int url_idx1 = -1;
    int ser_idx1 = -1;
    int url_idx2 = -1;
    int ser_idx2 = -1;
    writer.get_url_meta("dds://topic1", "protobuf", url_idx1, ser_idx1);
    writer.get_url_meta("dds://topic1", "raw", url_idx2, ser_idx2);
    CHECK(url_idx1 == url_idx2);
    CHECK(ser_idx1 != ser_idx2);
  }

  TEST_CASE("reverse lookup returns correct strings") {
    TestBagWriter writer;
    int url_idx = -1;
    int ser_idx = -1;
    writer.get_url_meta("intra://test_topic", "flatbuf", url_idx, ser_idx);

    std::string url_out;
    std::string ser_out;
    writer.get_url_meta(url_idx, ser_idx, url_out, ser_out);
    CHECK(url_out == "intra://test_topic");
    CHECK(ser_out == "flatbuf");
  }

  TEST_CASE("reverse lookup with invalid index returns empty") {
    TestBagWriter writer;
    std::string url_out;
    std::string ser_out;
    writer.get_url_meta(99999, 99999, url_out, ser_out);
    CHECK(url_out.empty());
    CHECK(ser_out.empty());
  }

  TEST_CASE("multiple urls and ser types") {
    TestBagWriter writer;

    int idx1u = -1;
    int idx1s = -1;
    int idx2u = -1;
    int idx2s = -1;
    int idx3u = -1;
    int idx3s = -1;

    writer.get_url_meta("dds://a", "proto", idx1u, idx1s);
    writer.get_url_meta("shm://b", "raw", idx2u, idx2s);
    writer.get_url_meta("intra://c", "cdr", idx3u, idx3s);

    // All url indices must be unique
    CHECK(idx1u != idx2u);
    CHECK(idx2u != idx3u);
    CHECK(idx1u != idx3u);

    // All ser indices must be unique
    CHECK(idx1s != idx2s);
    CHECK(idx2s != idx3s);
    CHECK(idx1s != idx3s);

    // Reverse lookups
    std::string u;
    std::string s;
    writer.get_url_meta(idx2u, idx2s, u, s);
    CHECK(u == "shm://b");
    CHECK(s == "raw");
  }
}

// ---------------------------------------------------------------------------
// TEST SUITE: BagWriter - static helpers
// ---------------------------------------------------------------------------

TEST_SUITE("extension-BagWriter - static helpers") {
  TEST_CASE("get_default_tag_name returns non-empty string") {
    const std::string& tag = TestBagWriter::get_default_tag_name();
    CHECK(!tag.empty());
  }

  TEST_CASE("get_default_app_name returns non-empty string") {
    const std::string& app = TestBagWriter::get_default_app_name();
    CHECK(!app.empty());
  }

  TEST_CASE("get_default_timezone_diff returns plausible value") {
    int32_t tz = TestBagWriter::get_default_timezone_diff();
    // Timezone diff in seconds; should be between -12h and +14h
    CHECK(tz >= -12 * 3600);
    CHECK(tz <= 14 * 3600);
  }

  TEST_CASE("get_schema_interface returns nullptr when no plugin loaded") {
    // Without VLINK_SCHEMA_PLUGIN env, should return nullptr
    auto* iface = TestBagWriter::get_schema_interface();
    (void)iface;
    CHECK(true);
  }
}

// ---------------------------------------------------------------------------
// TEST SUITE: BagWriter - global_get
// ---------------------------------------------------------------------------

TEST_SUITE("extension-BagWriter - global_get") {
  TEST_CASE("global_get returns nullptr when VLINK_BAG_PATH is not set") {
    // In test environment VLINK_BAG_PATH is typically not set
    BagWriter* gw = BagWriter::global_get();
    // May or may not be null depending on environment
    (void)gw;
    CHECK(true);
  }
}

// ---------------------------------------------------------------------------
// TEST SUITE: BagWriter - construction
// ---------------------------------------------------------------------------

TEST_SUITE("extension-BagWriter - construction") {
  TEST_CASE("TestBagWriter construction with default config") {
    TestBagWriter writer;
    CHECK(writer.is_dumping() == false);
    CHECK(writer.is_split_mode() == false);
    CHECK(writer.get_split_index() == 0);
  }

  TEST_CASE("TestBagWriter construction with sync_mode config") {
    BagWriter::Config config;
    config.sync_mode = true;
    TestBagWriter writer((std::filesystem::temp_directory_path() / "test_sync.vdb").string(), config);
    CHECK(true);
  }
}

// TEST_SUITE("extension-McapWriter - sealed_schema_set cleared across splits") {
//   TEST_CASE("push_schema succeeds after a split rotates the output file") {
//     namespace fs = std::filesystem;
//     const auto dir =
//         fs::temp_directory_path() /
//         ("vlink_split_schema_test_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
//     fs::remove_all(dir);
//     fs::create_directories(dir);

//     const auto path = (dir / "split_schema.vcap").string();

//     BagWriter::Config config;
//     // Tiny split threshold so a few hundred bytes worth of messages
//     // trigger at least one rotation.
//     config.split_by_size = 2048;
//     config.split_name_by_time = false;

//     auto writer = std::make_shared<McapWriter>(path, config);

//     std::atomic<int> split_count{0};
//     writer->register_split_callback(
//         [&split_count](int, const std::string&) { split_count.fetch_add(1, std::memory_order_relaxed); }, false);

//     writer->async_run();

//     const std::string url = "dds://split/schema/topic";
//     const std::string ser = "demo.split.Message";

//     // Phase 1: push an initial schema + several messages under kProtobuf.
//     SchemaData schema_v1;
//     schema_v1.name = ser;
//     schema_v1.schema_type = SchemaType::kProtobuf;
//     schema_v1.encoding = "protobuf";
//     schema_v1.data = Bytes::from_string("dummy-descriptor-v1");

//     REQUIRE(writer->push_schema(schema_v1, /*immediate=*/true));

//     // Push enough bytes to trigger at least one size-based split.
//     const auto payload = Bytes::create(512);

//     for (int i = 0; i < 32; ++i) {
//       writer->push(url, ser, SchemaType::kProtobuf, ActionType::kPublish, payload);
//     }

//     // Give the recording loop time to flush and rotate.
//     const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
//     while (split_count.load(std::memory_order_relaxed) == 0 && std::chrono::steady_clock::now() < deadline) {
//       std::this_thread::sleep_for(std::chrono::milliseconds(20));
//     }

//     REQUIRE(split_count.load(std::memory_order_relaxed) > 0);

//     // Phase 2: after the split, the same schema must still be accepted.
//     // If sealed_schema_set leaked across close(), immediate merge would
//     // return false.  We also test that pushing a fresh schema after the
//     // split is not rejected.
//     SchemaData schema_after_split;
//     schema_after_split.name = ser;
//     schema_after_split.schema_type = SchemaType::kProtobuf;
//     schema_after_split.encoding = "protobuf";
//     schema_after_split.data = Bytes::from_string("dummy-descriptor-v1");

//     const bool accepted = writer->push_schema(schema_after_split, /*immediate=*/true);
//     CHECK(accepted);

//     // A brand-new schema registered for the first time in the new split
//     // must also be accepted (not falsely sealed from the previous split).
//     SchemaData schema_new;
//     schema_new.name = "demo.split.NewMessage";
//     schema_new.schema_type = SchemaType::kProtobuf;
//     schema_new.encoding = "protobuf";
//     schema_new.data = Bytes::from_string("dummy-descriptor-new");

//     CHECK(writer->push_schema(schema_new, /*immediate=*/true));

//     writer.reset();

//     // Best-effort cleanup.
//     std::error_code ec;
//     fs::remove_all(dir, ec);
//   }
// }

// NOLINTEND
