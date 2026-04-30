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

#include "./extension/url_remap.h"

#include <doctest/doctest.h>

#include <filesystem>
#include <fstream>
#include <string>

//
#include "../common_test.h"

// ---------------------------------------------------------------------------
// Helper: write a temporary JSON remap file
// ---------------------------------------------------------------------------

namespace {

std::string write_temp_remap(const std::string& content) {
  std::string path = (std::filesystem::temp_directory_path() / "vlink_url_remap_test.json").string();
  std::ofstream f(path);
  f << content;
  return path;
}

}  // namespace

// ---------------------------------------------------------------------------
// TEST SUITE: UrlRemap - basic lifecycle
// ---------------------------------------------------------------------------

TEST_SUITE("extension-UrlRemap - lifecycle") {
  TEST_CASE("default construction: invalid") {
    UrlRemap remap;
    CHECK(remap.is_valid() == false);
  }

  TEST_CASE("default construction: error string empty") {
    UrlRemap remap;
    CHECK(remap.get_error_string().empty());
  }

  TEST_CASE("default construction: enable_log is false") {
    UrlRemap remap;
    CHECK(remap.is_enable_log() == false);
  }

  TEST_CASE("load non-existent file returns false and sets error") {
    UrlRemap remap;
    bool ok = remap.load((std::filesystem::temp_directory_path() / "definitely_does_not_exist_xyz.json").string());
    CHECK(ok == false);
    CHECK(remap.is_valid() == false);
    CHECK(!remap.get_error_string().empty());
  }

  TEST_CASE("load valid JSON file returns true") {
    const std::string json = R"({"intra://sensor/lidar": "dds://vehicle/lidar"})";
    std::string path = write_temp_remap(json);

    UrlRemap remap;
    bool ok = remap.load(path);
    CHECK(ok == true);
    CHECK(remap.is_valid() == true);
    CHECK(remap.get_error_string().empty());
  }

  TEST_CASE("load invalid JSON returns false") {
    std::string path = write_temp_remap("{ not valid json !!!");

    UrlRemap remap;
    bool ok = remap.load(path);
    CHECK(ok == false);
    CHECK(remap.is_valid() == false);
  }

  TEST_CASE("load twice without unload returns false") {
    const std::string json = R"({"intra://a": "dds://a"})";
    std::string path = write_temp_remap(json);

    UrlRemap remap;
    REQUIRE(remap.load(path));

    // Second load without unload must fail
    bool ok2 = remap.load(path);
    CHECK(ok2 == false);
  }

  TEST_CASE("unload returns true when loaded") {
    const std::string json = R"({"intra://a": "dds://a"})";
    std::string path = write_temp_remap(json);

    UrlRemap remap;
    REQUIRE(remap.load(path));

    bool ok = remap.unload();
    CHECK(ok == true);
    CHECK(remap.is_valid() == false);
  }

  TEST_CASE("unload when not loaded returns false") {
    UrlRemap remap;
    CHECK(remap.unload() == false);
  }

  TEST_CASE("reload succeeds after initial load") {
    const std::string json1 = R"({"intra://a": "dds://a"})";
    const std::string json2 = R"({"intra://b": "dds://b"})";

    std::string path1 = write_temp_remap(json1);

    UrlRemap remap;
    REQUIRE(remap.load(path1));

    std::string path2 = write_temp_remap(json2);
    bool ok = remap.reload(path2);
    CHECK(ok == true);
    CHECK(remap.is_valid() == true);
  }

  TEST_CASE("reload on non-existent file leaves remap invalid") {
    const std::string json = R"({"intra://a": "dds://a"})";
    std::string path = write_temp_remap(json);

    UrlRemap remap;
    REQUIRE(remap.load(path));

    bool ok = remap.reload((std::filesystem::temp_directory_path() / "no_such_file_for_reload_xyz.json").string());
    CHECK(ok == false);
    CHECK(remap.is_valid() == false);
  }
}

// ---------------------------------------------------------------------------
// TEST SUITE: UrlRemap - conversion
// ---------------------------------------------------------------------------

TEST_SUITE("extension-UrlRemap - conversion") {
  TEST_CASE("convert returns remapped URL when rule matches") {
    const std::string json = R"({
      "intra://sensor/lidar": "dds://vehicle/lidar",
      "shm://camera/front": "zenoh://camera/front"
    })";
    std::string path = write_temp_remap(json);

    UrlRemap remap;
    REQUIRE(remap.load(path));

    CHECK(remap.convert("intra://sensor/lidar") == "dds://vehicle/lidar");
    CHECK(remap.convert("shm://camera/front") == "zenoh://camera/front");
  }

  TEST_CASE("convert returns original URL when no match") {
    const std::string json = R"({"intra://sensor/lidar": "dds://vehicle/lidar"})";
    std::string path = write_temp_remap(json);

    UrlRemap remap;
    REQUIRE(remap.load(path));

    std::string url = "dds://no/match/here";
    CHECK(remap.convert(url) == url);
  }

  TEST_CASE("convert returns original URL when not loaded") {
    UrlRemap remap;
    std::string url = "intra://some/topic";
    CHECK(remap.convert(url) == url);
  }

  TEST_CASE("convert caches result: second call same as first") {
    const std::string json = R"({"intra://my/topic": "dds://my/topic"})";
    std::string path = write_temp_remap(json);

    UrlRemap remap;
    REQUIRE(remap.load(path));

    std::string r1 = remap.convert("intra://my/topic");
    std::string r2 = remap.convert("intra://my/topic");

    CHECK(r1 == r2);
    CHECK(r1 == "dds://my/topic");
  }

  TEST_CASE("convert: substring matching - key found in longer URL") {
    // The matching algorithm: first key that is a substring of the input
    const std::string json = R"({"sensor/lidar": "vehicle/lidar"})";
    std::string path = write_temp_remap(json);

    UrlRemap remap;
    REQUIRE(remap.load(path));

    // "sensor/lidar" is a substring of "intra://sensor/lidar"
    std::string result = remap.convert("intra://sensor/lidar");
    CHECK(result == "vehicle/lidar");
  }

  TEST_CASE("unload clears conversion cache") {
    const std::string json = R"({"intra://a": "dds://a"})";
    std::string path = write_temp_remap(json);

    UrlRemap remap;
    REQUIRE(remap.load(path));

    (void)remap.convert("intra://a");  // populate cache

    remap.unload();

    // After unload, convert should return original URL
    std::string url = "intra://a";
    CHECK(remap.convert(url) == url);
  }
}

// ---------------------------------------------------------------------------
// TEST SUITE: UrlRemap - enable_log
// ---------------------------------------------------------------------------

TEST_SUITE("extension-UrlRemap - enable_log") {
  TEST_CASE("set_enable_log / is_enable_log round-trip") {
    UrlRemap remap;

    CHECK(remap.is_enable_log() == false);

    remap.set_enable_log(true);
    CHECK(remap.is_enable_log() == true);

    remap.set_enable_log(false);
    CHECK(remap.is_enable_log() == false);
  }
}

// NOLINTEND
