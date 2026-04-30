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

#include "./impl/setter_impl.h"

#include <doctest/doctest.h>

#include "./base/bytes.h"
#include "./impl/types.h"

//
#include "../common_test.h"

// ---------------------------------------------------------------------------
// Helpers: concrete subclass to test SetterImpl
// ---------------------------------------------------------------------------

namespace {

class TestSetterImpl : public SetterImpl {
 public:
  TestSetterImpl() = default;
  ~TestSetterImpl() override = default;

  void init() override {}
  void deinit() override {}

  void write(const Bytes& msg_data) override {
    last_written = msg_data;
    ++write_count;
  }

  void sync(SyncCallback&& callback) override {
    ++sync_count;
    if (callback) {
      callback();
    }
  }

  Bytes last_written;
  int write_count{0};
  int sync_count{0};
};

}  // namespace

// ---------------------------------------------------------------------------
// TEST SUITE: SetterImpl - construction
// ---------------------------------------------------------------------------

TEST_SUITE("impl-SetterImpl - construction") {
  TEST_CASE("constructor sets kSetter impl_type") {
    TestSetterImpl setter;
    CHECK(setter.impl_type == kSetter);
  }
}

// ---------------------------------------------------------------------------
// TEST SUITE: SetterImpl - write
// ---------------------------------------------------------------------------

TEST_SUITE("impl-SetterImpl - write") {
  TEST_CASE("write stores data") {
    TestSetterImpl setter;
    Bytes data = {1, 2, 3, 4};

    setter.write(data);

    CHECK(setter.write_count == 1);
    CHECK(setter.last_written.size() == 4);
  }

  TEST_CASE("multiple writes") {
    TestSetterImpl setter;
    Bytes data1;
    Bytes data2;

    setter.write(data1);
    setter.write(data2);

    CHECK(setter.write_count == 2);
  }
}

// ---------------------------------------------------------------------------
// TEST SUITE: SetterImpl - sync
// ---------------------------------------------------------------------------

TEST_SUITE("impl-SetterImpl - sync") {
  TEST_CASE("sync fires callback") {
    TestSetterImpl setter;

    bool called = false;
    setter.sync([&]() { called = true; });

    CHECK(called == true);
    CHECK(setter.sync_count == 1);
  }

  TEST_CASE("sync with nullptr callback does not crash") {
    TestSetterImpl setter;
    setter.sync(nullptr);
    CHECK(setter.sync_count == 1);
  }

  TEST_CASE("multiple syncs") {
    TestSetterImpl setter;
    int counter = 0;

    setter.sync([&]() { ++counter; });
    setter.sync([&]() { ++counter; });

    CHECK(counter == 2);
    CHECK(setter.sync_count == 2);
  }
}

// ---------------------------------------------------------------------------
// TEST SUITE: SetterImpl - inherited NodeImpl features
// ---------------------------------------------------------------------------

TEST_SUITE("impl-SetterImpl - inherited") {
  TEST_CASE("properties work through SetterImpl") {
    TestSetterImpl setter;
    setter.set_property("field_name", "temperature");
    CHECK(setter.get_property("field_name") == "temperature");
  }

  TEST_CASE("interrupt works through SetterImpl") {
    TestSetterImpl setter;
    CHECK(setter.is_interrupted() == false);
    setter.interrupt();
    CHECK(setter.is_interrupted() == true);
    setter.reset_interrupted();
    CHECK(setter.is_interrupted() == false);
  }
}

// NOLINTEND
