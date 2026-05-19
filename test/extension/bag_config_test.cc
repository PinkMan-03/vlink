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

#include <doctest/doctest.h>

#include <string>

#include "./extension/bag_reader.h"
#include "./extension/bag_writer.h"
#include "./impl/types.h"

//
#include "../common_test.h"

// ---------------------------------------------------------------------------
// TEST SUITE: BagWriter::CompressType enum
// ---------------------------------------------------------------------------

TEST_SUITE("extension-BagWriter::CompressType") {
  TEST_CASE("kCompressNone == 0") { CHECK(static_cast<int>(BagWriter::kCompressNone) == 0); }

  TEST_CASE("kCompressAuto == 1") { CHECK(static_cast<int>(BagWriter::kCompressAuto) == 1); }

  TEST_CASE("kCompressZstd == 2") { CHECK(static_cast<int>(BagWriter::kCompressZstd) == 2); }

  TEST_CASE("kCompressLz4 == 3") { CHECK(static_cast<int>(BagWriter::kCompressLz4) == 3); }

  TEST_CASE("kCompressLzav == 4") { CHECK(static_cast<int>(BagWriter::kCompressLzav) == 4); }

  TEST_CASE("all values are distinct") {
    CHECK(BagWriter::kCompressNone != BagWriter::kCompressAuto);
    CHECK(BagWriter::kCompressAuto != BagWriter::kCompressZstd);
    CHECK(BagWriter::kCompressZstd != BagWriter::kCompressLz4);
    CHECK(BagWriter::kCompressLz4 != BagWriter::kCompressLzav);
  }
}

// ---------------------------------------------------------------------------
// TEST SUITE: BagWriter::Config default values
// ---------------------------------------------------------------------------

TEST_SUITE("extension-BagWriter::Config - defaults") {
  TEST_CASE("tag_name is empty by default") {
    BagWriter::Config cfg;
    CHECK(cfg.tag_name.empty());
  }

  TEST_CASE("compress defaults to kCompressNone") {
    BagWriter::Config cfg;
    CHECK(cfg.compress == BagWriter::kCompressNone);
  }

  TEST_CASE("wal_mode is false by default") {
    BagWriter::Config cfg;
    CHECK(cfg.wal_mode == false);
  }

  TEST_CASE("enable_limit is false by default") {
    BagWriter::Config cfg;
    CHECK(cfg.enable_limit == false);
  }

  TEST_CASE("split_name_by_time is false by default") {
    BagWriter::Config cfg;
    CHECK(cfg.split_name_by_time == false);
  }

  TEST_CASE("sync_mode is false by default") {
    BagWriter::Config cfg;
    CHECK(cfg.sync_mode == false);
  }

  TEST_CASE("optimize_on_exit is false by default") {
    BagWriter::Config cfg;
    CHECK(cfg.optimize_on_exit == false);
  }

  TEST_CASE("max_row_count default is 5e9") {
    BagWriter::Config cfg;
    CHECK(cfg.max_row_count == 5'000'000'000LL);
  }

  TEST_CASE("max_bytes_size default is 512 GiB") {
    BagWriter::Config cfg;
    CHECK(cfg.max_bytes_size == 1024LL * 1024LL * 1024LL * 512LL);
  }

  TEST_CASE("split_by_size default is 1 GiB") {
    BagWriter::Config cfg;
    CHECK(cfg.split_by_size == 1024LL * 1024LL * 1024LL * 1LL);
  }

  TEST_CASE("split_by_time default is 0 (disabled)") {
    BagWriter::Config cfg;
    CHECK(cfg.split_by_time == 0);
  }

  TEST_CASE("begin_time default is 0") {
    BagWriter::Config cfg;
    CHECK(cfg.begin_time == 0);
  }

  TEST_CASE("cache_size default is 4 MiB") {
    BagWriter::Config cfg;
    CHECK(cfg.cache_size == 1024LL * 1024LL * 4);
  }

  TEST_CASE("compress_start_size default is 128") {
    BagWriter::Config cfg;
    CHECK(cfg.compress_start_size == 128);
  }

  TEST_CASE("compress_level default is 3") {
    BagWriter::Config cfg;
    CHECK(cfg.compress_level == 3);
  }

  TEST_CASE("max_task_depth default is 20000") {
    BagWriter::Config cfg;
    CHECK(cfg.max_task_depth == 20000);
  }

  TEST_CASE("max_memory_size default is 2 GiB") {
    BagWriter::Config cfg;
    CHECK(cfg.max_memory_size == 1024LL * 1024LL * 1024LL * 2LL);
  }

  TEST_CASE("start_timestamp default is 0") {
    BagWriter::Config cfg;
    CHECK(cfg.start_timestamp == 0);
  }

  TEST_CASE("ignore_compress_urls is empty by default") {
    BagWriter::Config cfg;
    CHECK(cfg.ignore_compress_urls.empty());
  }
}

// ---------------------------------------------------------------------------
// TEST SUITE: BagWriter::Config field mutation
// ---------------------------------------------------------------------------

TEST_SUITE("extension-BagWriter::Config - field mutation") {
  TEST_CASE("tag_name can be set") {
    BagWriter::Config cfg;
    cfg.tag_name = "my_test_bag";
    CHECK(cfg.tag_name == "my_test_bag");
  }

  TEST_CASE("compress can be changed") {
    BagWriter::Config cfg;
    cfg.compress = BagWriter::kCompressZstd;
    CHECK(cfg.compress == BagWriter::kCompressZstd);
  }

  TEST_CASE("wal_mode can be enabled") {
    BagWriter::Config cfg;
    cfg.wal_mode = true;
    CHECK(cfg.wal_mode == true);
  }

  TEST_CASE("split_by_size can be modified") {
    BagWriter::Config cfg;
    cfg.split_by_size = 512LL * 1024 * 1024;
    CHECK(cfg.split_by_size == 512LL * 1024 * 1024);
  }

  TEST_CASE("split_by_time can be enabled") {
    BagWriter::Config cfg;
    cfg.split_by_time = 60 * 1000000;  // 60 seconds in us
    CHECK(cfg.split_by_time == 60 * 1000000);
  }

  TEST_CASE("ignore_compress_urls can be populated") {
    BagWriter::Config cfg;
    cfg.ignore_compress_urls.insert("dds://sensor/lidar");
    cfg.ignore_compress_urls.insert("dds://sensor/camera");

    CHECK(cfg.ignore_compress_urls.size() == 2);
    CHECK(cfg.ignore_compress_urls.count("dds://sensor/lidar") == 1);
    CHECK(cfg.ignore_compress_urls.count("dds://sensor/camera") == 1);
  }
}

// ---------------------------------------------------------------------------
// TEST SUITE: BagReader::kInfinite
// ---------------------------------------------------------------------------

TEST_SUITE("extension-BagReader - kInfinite") {
  TEST_CASE("kInfinite equals -1") { CHECK(BagReader::kInfinite == -1); }
}

// ---------------------------------------------------------------------------
// TEST SUITE: BagReader::Status enum
// ---------------------------------------------------------------------------

TEST_SUITE("extension-BagReader::Status") {
  TEST_CASE("kStopped == 0") { CHECK(static_cast<int>(BagReader::kStopped) == 0); }

  TEST_CASE("kPaused == 1") { CHECK(static_cast<int>(BagReader::kPaused) == 1); }

  TEST_CASE("kPlaying == 2") { CHECK(static_cast<int>(BagReader::kPlaying) == 2); }

  TEST_CASE("all values are distinct") {
    CHECK(BagReader::kStopped != BagReader::kPaused);
    CHECK(BagReader::kPaused != BagReader::kPlaying);
    CHECK(BagReader::kStopped != BagReader::kPlaying);
  }
}

// ---------------------------------------------------------------------------
// TEST SUITE: BagReader::Config default values
// ---------------------------------------------------------------------------

TEST_SUITE("extension-BagReader::Config - defaults") {
  TEST_CASE("begin_time defaults to 0") {
    BagReader::Config cfg;
    CHECK(cfg.begin_time == 0);
  }

  TEST_CASE("end_time defaults to 0") {
    BagReader::Config cfg;
    CHECK(cfg.end_time == 0);
  }

  TEST_CASE("times defaults to 1") {
    BagReader::Config cfg;
    CHECK(cfg.times == 1);
  }

  TEST_CASE("rate defaults to 1.0") {
    BagReader::Config cfg;
    CHECK(cfg.rate == doctest::Approx(1.0));
  }

  TEST_CASE("skip_blank defaults to false") {
    BagReader::Config cfg;
    CHECK(cfg.skip_blank == false);
  }

  TEST_CASE("force_delay defaults to -1") {
    BagReader::Config cfg;
    CHECK(cfg.force_delay == -1);
  }

  TEST_CASE("auto_pause defaults to false") {
    BagReader::Config cfg;
    CHECK(cfg.auto_pause == false);
  }

  TEST_CASE("auto_quit defaults to false") {
    BagReader::Config cfg;
    CHECK(cfg.auto_quit == false);
  }

  TEST_CASE("filter_urls is empty by default") {
    BagReader::Config cfg;
    CHECK(cfg.filter_urls.empty());
  }
}

// ---------------------------------------------------------------------------
// TEST SUITE: BagReader::Config field mutation
// ---------------------------------------------------------------------------

TEST_SUITE("extension-BagReader::Config - field mutation") {
  TEST_CASE("times = kInfinite") {
    BagReader::Config cfg;
    cfg.times = BagReader::kInfinite;
    CHECK(cfg.times == -1);
  }

  TEST_CASE("rate can be changed") {
    BagReader::Config cfg;
    cfg.rate = 2.0;
    CHECK(cfg.rate == doctest::Approx(2.0));
  }

  TEST_CASE("skip_blank can be enabled") {
    BagReader::Config cfg;
    cfg.skip_blank = true;
    CHECK(cfg.skip_blank == true);
  }

  TEST_CASE("filter_urls can be populated") {
    BagReader::Config cfg;
    cfg.filter_urls.insert("dds://my/topic");
    CHECK(cfg.filter_urls.count("dds://my/topic") == 1);
  }

  TEST_CASE("begin_time and end_time can be set") {
    BagReader::Config cfg;
    cfg.begin_time = 1000000;
    cfg.end_time = 5000000;
    CHECK(cfg.begin_time == 1000000);
    CHECK(cfg.end_time == 5000000);
  }
}

// ---------------------------------------------------------------------------
// TEST SUITE: BagReader::Info and UrlMeta
// ---------------------------------------------------------------------------

TEST_SUITE("extension-BagReader::Info - default construction") {
  TEST_CASE("Info default fields") {
    BagReader::Info info;

    CHECK(info.file_name.empty());
    CHECK(info.tag_name.empty());
    CHECK(info.version.empty());
    CHECK(info.storage_type.empty());
    CHECK(info.has_completed == false);
    CHECK(info.has_idx_elapsed == false);
    CHECK(info.has_idx_url == false);
    CHECK(info.has_schema == false);
    CHECK(info.start_timestamp == 0);
    CHECK(info.total_duration == 0);
    CHECK(info.message_count == 0);
    CHECK(info.split_count == 0);
    CHECK(info.url_metas.empty());
  }
}

TEST_SUITE("extension-BagReader::Info::UrlMeta - default construction") {
  TEST_CASE("UrlMeta default fields") {
    BagReader::Info::UrlMeta meta;

    CHECK(meta.valid == false);
    CHECK(meta.index == 0);
    CHECK(meta.url.empty());
    CHECK(meta.url_type.empty());
    CHECK(meta.action_type == ActionType::kUnknownAction);
    CHECK(meta.ser_type.empty());
    CHECK(meta.count == 0);
    CHECK(meta.size == 0);
    CHECK(meta.freq == doctest::Approx(0.0));
    CHECK(meta.loss == doctest::Approx(0.0));
  }

  TEST_CASE("UrlMeta operator< compares by index") {
    BagReader::Info::UrlMeta a;
    BagReader::Info::UrlMeta b;
    a.index = 1;
    b.index = 2;

    CHECK(a < b);
    CHECK(!(b < a));
    CHECK(!(a < a));
  }

  TEST_CASE("UrlMeta fields are mutable") {
    BagReader::Info::UrlMeta meta;
    meta.valid = true;
    meta.index = 5;
    meta.url = "dds://sensor/lidar";
    meta.url_type = "pub";
    meta.action_type = ActionType::kPublish;
    meta.ser_type = "protobuf";
    meta.count = 1000;
    meta.size = 1024 * 1024;
    meta.freq = 10.0;
    meta.loss = 0.01;

    CHECK(meta.valid == true);
    CHECK(meta.index == 5);
    CHECK(meta.url == "dds://sensor/lidar");
    CHECK(meta.url_type == "pub");
    CHECK(meta.ser_type == "protobuf");
    CHECK(meta.count == 1000);
    CHECK(meta.size == 1024 * 1024);
    CHECK(meta.freq == doctest::Approx(10.0));
    CHECK(meta.loss == doctest::Approx(0.01));
  }
}

// NOLINTEND
