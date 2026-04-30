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

#include "./base/plugin.h"

#include <doctest/doctest.h>

#include <string>

//
#include "../common_test.h"

// ---------------------------------------------------------------------------
// Minimal abstract interface for plugin loading tests.
// The actual shared library does not exist in the test environment, so we
// test only the behaviours that do NOT require a real .so file.
// ---------------------------------------------------------------------------

class MyTestInterface {
  VLINK_PLUGIN_REGISTER_BY_ID(MyTestInterface, "test.my_interface")

 public:
  virtual ~MyTestInterface() = default;

  virtual int compute(int x) = 0;
};

class AnotherInterface {
  VLINK_PLUGIN_REGISTER_BY_ID(AnotherInterface, "test.another_interface")

 public:
  virtual ~AnotherInterface() = default;

  virtual void run() = 0;
};

// ---------------------------------------------------------------------------
// TEST SUITE: Plugin - construction and log level
// ---------------------------------------------------------------------------

TEST_SUITE("base-Plugin - lifecycle") {
  TEST_CASE("Plugin can be constructed and destroyed") {
    Plugin plugin;
    CHECK(true);  // must not crash
  }

  TEST_CASE("set_log_level and get_log_level are consistent") {
    Plugin plugin;
    plugin.set_log_level(Logger::Level::kWarn);
    CHECK(plugin.get_log_level() == Logger::Level::kWarn);

    plugin.set_log_level(Logger::Level::kDebug);
    CHECK(plugin.get_log_level() == Logger::Level::kDebug);
  }

  TEST_CASE("clear on an empty plugin does not crash") {
    Plugin plugin;
    plugin.clear();
    CHECK(true);
  }
}

// ---------------------------------------------------------------------------
// TEST SUITE: Plugin - default_search_path
// ---------------------------------------------------------------------------

TEST_SUITE("base-Plugin - default_search_path") {
  TEST_CASE("default_search_path returns a non-empty deque") {
    auto paths = Plugin::default_search_path();
    CHECK(!paths.empty());
  }

  TEST_CASE("all paths in default_search_path are non-empty strings") {
    auto paths = Plugin::default_search_path();

    for (const auto& p : paths) {
      CHECK(!p.empty());
    }
  }
}

// ---------------------------------------------------------------------------
// TEST SUITE: Plugin - get_plugin_complex_id
// ---------------------------------------------------------------------------

TEST_SUITE("base-Plugin - get_plugin_complex_id") {
  TEST_CASE("complex id contains the lib name") {
    Plugin plugin;
    std::string id = plugin.get_plugin_complex_id<MyTestInterface>("my_lib");
    CHECK(id.find("my_lib") != std::string::npos);
  }

  TEST_CASE("complex id contains the interface id") {
    Plugin plugin;
    std::string id = plugin.get_plugin_complex_id<MyTestInterface>("my_lib");
    CHECK(id.find("test.my_interface") != std::string::npos);
  }

  TEST_CASE("complex id format is lib_name@plugin_id") {
    Plugin plugin;
    std::string id = plugin.get_plugin_complex_id<MyTestInterface>("my_lib");
    CHECK(id == "my_lib@test.my_interface");
  }

  TEST_CASE("different interface types produce different complex ids") {
    Plugin plugin;
    std::string id1 = plugin.get_plugin_complex_id<MyTestInterface>("lib");
    std::string id2 = plugin.get_plugin_complex_id<AnotherInterface>("lib");
    CHECK(id1 != id2);
  }
}

// ---------------------------------------------------------------------------
// TEST SUITE: Plugin - has_loaded (before loading)
// ---------------------------------------------------------------------------

TEST_SUITE("base-Plugin - has_loaded before loading") {
  TEST_CASE("has_loaded returns false for unloaded library") {
    Plugin plugin;
    CHECK(!plugin.has_loaded<MyTestInterface>("nonexistent_lib"));
  }
}

// ---------------------------------------------------------------------------
// TEST SUITE: Plugin - unload (before loading)
// ---------------------------------------------------------------------------

TEST_SUITE("base-Plugin - unload before loading") {
  TEST_CASE("unload returns false for library that was never loaded") {
    Plugin plugin;
    bool result = plugin.unload<MyTestInterface>("nonexistent_lib");
    CHECK(!result);
  }
}

// ---------------------------------------------------------------------------
// TEST SUITE: Plugin - load with missing library returns nullptr
// ---------------------------------------------------------------------------

TEST_SUITE("base-Plugin - load missing library") {
  TEST_CASE("load returns nullptr when library does not exist") {
    Plugin plugin;
    auto result = plugin.load<MyTestInterface>("__definitely_does_not_exist__", 1, 0);
    CHECK(result == nullptr);
  }

  TEST_CASE("has_loaded returns false after failed load") {
    Plugin plugin;
    (void)plugin.load<MyTestInterface>("__definitely_does_not_exist__", 1, 0);
    CHECK(!plugin.has_loaded<MyTestInterface>("__definitely_does_not_exist__"));
  }
}

// ---------------------------------------------------------------------------
// TEST SUITE: Plugin - process_plugin_internal
// ---------------------------------------------------------------------------

TEST_SUITE("base-Plugin - process_plugin_internal") {
  TEST_CASE("matching id and version returns true") {
    bool ok = Plugin::process_plugin_internal("my_lib", "test.my_interface", 1, 0, "test.my_interface", 1, 0,
                                              static_cast<uint8_t>(Logger::Level::kWarn));
    CHECK(ok);
  }

  TEST_CASE("mismatched plugin id returns false") {
    bool ok = Plugin::process_plugin_internal("my_lib", "test.my_interface", 1, 0, "test.other_interface", 1, 0,
                                              static_cast<uint8_t>(Logger::Level::kWarn));
    CHECK(!ok);
  }

  TEST_CASE("mismatched major version returns false") {
    bool ok = Plugin::process_plugin_internal("my_lib", "test.my_interface", 2, 0, "test.my_interface", 1, 0,
                                              static_cast<uint8_t>(Logger::Level::kWarn));
    CHECK(!ok);
  }

  TEST_CASE("target minor version newer than local minor version returns false") {
    // Check: target_version_minor > local_version_minor fails.
    // local=0, target=1: 1 > 0 is true -> fail
    bool ok = Plugin::process_plugin_internal("my_lib", "test.my_interface", 1, 0, "test.my_interface", 1, 1,
                                              static_cast<uint8_t>(Logger::Level::kWarn));
    CHECK(!ok);
  }

  TEST_CASE("empty lib_name is accepted when ids and versions match") {
    bool ok = Plugin::process_plugin_internal("", "test.my_interface", 1, 0, "test.my_interface", 1, 0,
                                              static_cast<uint8_t>(Logger::Level::kWarn));
    CHECK(ok);
  }
}

// ---------------------------------------------------------------------------
// TEST SUITE: Plugin - VLINK_PLUGIN_REGISTER_BY_ID macro
// ---------------------------------------------------------------------------

TEST_SUITE("base-Plugin - VLINK_PLUGIN_REGISTER_BY_ID macro") {
  TEST_CASE("get_plugin_id returns the supplied literal") {
    // NOLINTNEXTLINE(readability-identifier-naming)
    constexpr std::string_view id = MyTestInterface::get_plugin_id();
    CHECK(id == "test.my_interface");
  }

  TEST_CASE("get_plugin_id is non-empty") {
    // NOLINTNEXTLINE(readability-identifier-naming)
    constexpr std::string_view id = MyTestInterface::get_plugin_id();
    CHECK(!id.empty());
  }

  TEST_CASE("two interfaces have distinct ids") {
    // NOLINTNEXTLINE(readability-identifier-naming)
    constexpr std::string_view id1 = MyTestInterface::get_plugin_id();
    // NOLINTNEXTLINE(readability-identifier-naming)
    constexpr std::string_view id2 = AnotherInterface::get_plugin_id();
    CHECK(id1 != id2);
  }
}

// NOLINTEND
