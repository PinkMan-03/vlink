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

#include "./extension/protobuf_registry.h"

#include <doctest/doctest.h>

//
#include "../common_test.h"

// ---------------------------------------------------------------------------
// TEST SUITE: protobuf_registry - feature probe
// ---------------------------------------------------------------------------
//
// The header is intentionally lightweight: it only forwards the protobuf
// reflection runtime headers when they are available on the build host and
// defines VLINK_HAS_SCHEMA_PLUGIN_PROTOBUF as the compile-time feature gate.
// Whether the macro is defined depends entirely on the build environment, so
// these tests assert the contract: "when the feature is detected, the runtime
// headers compile and are usable; otherwise the header still includes cleanly".
// ---------------------------------------------------------------------------

#ifdef VLINK_HAS_SCHEMA_PLUGIN_PROTOBUF

TEST_SUITE("extension-protobuf_registry - protobuf available") {
  TEST_CASE("VLINK_HAS_SCHEMA_PLUGIN_PROTOBUF is defined when protobuf is present") { CHECK(true); }

  TEST_CASE("DescriptorPool::generated_pool() returns a non-null instance") {
    const auto* pool = google::protobuf::DescriptorPool::generated_pool();
    CHECK(pool != nullptr);
  }

  TEST_CASE("MessageFactory::generated_factory() returns a non-null instance") {
    auto* factory = google::protobuf::MessageFactory::generated_factory();
    CHECK(factory != nullptr);
  }
}

#else

TEST_SUITE("extension-protobuf_registry - protobuf unavailable") {
  TEST_CASE("VLINK_HAS_SCHEMA_PLUGIN_PROTOBUF is undefined and header compiles") { CHECK(true); }
}

#endif

// NOLINTEND
