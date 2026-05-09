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

#include "./impl/conf.h"

#include <doctest/doctest.h>

#include <map>
#include <string>
#include <type_traits>

//
#include "../common_test.h"

// ---------------------------------------------------------------------------
// Helper: a public subclass that exposes the protected default constructor.
// The base class is intentionally protected-constructible so that only the
// six Node template specialisations and Url can build it directly. For tests
// we only need to verify the default behaviour of the public virtual API.
// ---------------------------------------------------------------------------

namespace {

struct TrivialConf final : public Conf {
  TrivialConf() = default;
};

}  // namespace

// ---------------------------------------------------------------------------
// TEST SUITE: Conf - default virtual behaviour
// ---------------------------------------------------------------------------

TEST_SUITE("impl-Conf - default virtual behaviour") {
  TEST_CASE("default is_valid is false") {
    TrivialConf conf;
    CHECK_FALSE(conf.is_valid());
  }

  TEST_CASE("default get_transport_type is TransportType::kUnknown") {
    TrivialConf conf;
    CHECK(conf.get_transport_type() == TransportType::kUnknown);
  }

  TEST_CASE("default get_impl_type is kUnknownImplType before parse()") {
    TrivialConf conf;
    CHECK(conf.get_impl_type() == kUnknownImplType);
  }

  TEST_CASE("default hash_code is zero") {
    TrivialConf conf;
    CHECK(conf.hash_code == 0);
  }

  TEST_CASE("hash_code is settable") {
    TrivialConf conf;
    conf.hash_code = 0xCAFEBABE;
    CHECK(conf.hash_code == 0xCAFEBABE);
  }
}

// ---------------------------------------------------------------------------
// TEST SUITE: Conf::parse - caches impl_type
// ---------------------------------------------------------------------------

TEST_SUITE("impl-Conf - parse() caches impl_type") {
  TEST_CASE("parse(kPublisher) returns true and updates get_impl_type") {
    TrivialConf conf;
    REQUIRE(conf.parse(kPublisher));
    CHECK(conf.get_impl_type() == kPublisher);
  }

  TEST_CASE("parse(kSubscriber) updates the cached impl_type") {
    TrivialConf conf;
    REQUIRE(conf.parse(kSubscriber));
    CHECK(conf.get_impl_type() == kSubscriber);
  }

  TEST_CASE("parse can be called more than once and the latest value wins") {
    TrivialConf conf;
    REQUIRE(conf.parse(kPublisher));
    REQUIRE(conf.parse(kServer));
    CHECK(conf.get_impl_type() == kServer);
  }

  TEST_CASE("parse accepts every concrete ImplType bit") {
    TrivialConf conf;
    CHECK(conf.parse(kPublisher));
    CHECK(conf.parse(kSubscriber));
    CHECK(conf.parse(kSetter));
    CHECK(conf.parse(kGetter));
    CHECK(conf.parse(kServer));
    CHECK(conf.parse(kClient));
  }
}

// ---------------------------------------------------------------------------
// TEST SUITE: Conf - PropertiesMap alias
// ---------------------------------------------------------------------------

TEST_SUITE("impl-Conf - PropertiesMap alias") {
  TEST_CASE("PropertiesMap is std::map<string,string>") {
    CHECK((std::is_same_v<Conf::PropertiesMap, std::map<std::string, std::string>>));
  }

  TEST_CASE("PropertiesMap can hold key/value pairs") {
    Conf::PropertiesMap m;
    m["dds.ip"] = "127.0.0.1";
    m["zenoh.mode"] = "peer";
    CHECK(m.at("dds.ip") == "127.0.0.1");
    CHECK(m.at("zenoh.mode") == "peer");
  }
}

// NOLINTEND
