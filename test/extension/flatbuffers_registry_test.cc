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

#include "./extension/flatbuffers_registry.h"

#include <doctest/doctest.h>

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

//
#include "../common_test.h"

#ifdef VLINK_HAS_SCHEMA_PLUGIN_FLATBUFFERS

// ---------------------------------------------------------------------------
// TEST SUITE: FlatbuffersRegistry - singleton lifecycle
// ---------------------------------------------------------------------------

TEST_SUITE("extension-FlatbuffersRegistry - singleton") {
  TEST_CASE("get() returns the same instance on repeat calls") {
    FlatbuffersRegistry& a = FlatbuffersRegistry::get();
    FlatbuffersRegistry& b = FlatbuffersRegistry::get();
    CHECK(&a == &b);
  }

  TEST_CASE("get_all_schemas returns a vector (possibly empty, never throws)") {
    auto schemas = FlatbuffersRegistry::get().get_all_schemas();
    (void)schemas;
    CHECK(true);
  }
}

// ---------------------------------------------------------------------------
// TEST SUITE: FlatbuffersRegistry - register_schema input validation
// ---------------------------------------------------------------------------
//
// The registry validates inputs before parsing. Any of: empty name,
// null pointer, zero size, or invalid BFBS bytes must return false and leave
// the registry in a consistent state.
// ---------------------------------------------------------------------------

TEST_SUITE("extension-FlatbuffersRegistry - register_schema validation") {
  TEST_CASE("empty name is rejected") {
    static const uint8_t kData[]{'d', 'a', 't', 'a'};
    bool ok = FlatbuffersRegistry::register_schema("", kData, sizeof(kData));
    CHECK_FALSE(ok);
  }

  TEST_CASE("nullptr data is rejected") {
    bool ok = FlatbuffersRegistry::register_schema("schema.Test", nullptr, 0U);
    CHECK_FALSE(ok);
  }

  TEST_CASE("zero-size buffer is rejected") {
    static const uint8_t kBuf[]{0};
    bool ok = FlatbuffersRegistry::register_schema("schema.Test", kBuf, 0U);
    CHECK_FALSE(ok);
  }

  TEST_CASE("garbage bytes that do not parse as BFBS are rejected") {
    static const uint8_t kGarbage[]{0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07};
    bool ok = FlatbuffersRegistry::register_schema("schema.GarbageOnly", kGarbage, sizeof(kGarbage));
    CHECK_FALSE(ok);
  }

  TEST_CASE("template overload forwards through to validation path") {
    struct EmptyBinarySchema {
      [[nodiscard]] static const uint8_t* data() {
        static const uint8_t kBuf[]{0};
        return kBuf;
      }

      [[nodiscard]] static size_t size() { return 1U; }
    };

    bool ok = FlatbuffersRegistry::register_schema<EmptyBinarySchema>("schema.TemplateGarbage");
    CHECK_FALSE(ok);
  }
}

// ---------------------------------------------------------------------------
// TEST SUITE: FlatbuffersRegistry - search_schema for unknown names
// ---------------------------------------------------------------------------

TEST_SUITE("extension-FlatbuffersRegistry - search_schema") {
  TEST_CASE("unknown name returns an empty SchemaData") {
    auto schema = FlatbuffersRegistry::get().search_schema("definitely_not_registered.Type_xyz");
    CHECK(schema.name.empty());
    CHECK(schema.encoding.empty());
    CHECK(schema.schema_type == SchemaType::kUnknown);
    CHECK(schema.data.empty());
  }
}

#else

TEST_SUITE("extension-FlatbuffersRegistry - flatbuffers unavailable") {
  TEST_CASE("VLINK_HAS_SCHEMA_PLUGIN_FLATBUFFERS is undefined and header compiles") { CHECK(true); }
}

#endif  // VLINK_HAS_SCHEMA_PLUGIN_FLATBUFFERS

// NOLINTEND
