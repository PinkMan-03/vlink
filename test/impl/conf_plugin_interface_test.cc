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

#include "./impl/conf_plugin_interface.h"

#include <doctest/doctest.h>

#include <memory>
#include <type_traits>

//
#include "../common_test.h"

// ---------------------------------------------------------------------------
// Helper concrete subclasses representing two fictional transports.  We use
// reserved enumerator values from TransportType (kIntra / kZenoh) so that
// the contract testing is deterministic without inventing new values.
// ---------------------------------------------------------------------------

namespace {

struct StubConf final : public Conf {
  TransportType transport{TransportType::kIntra};

  StubConf() = default;

  [[nodiscard]] bool is_valid() const override { return true; }

  [[nodiscard]] TransportType get_transport_type() const override { return transport; }
};

class FakeIntraPlugin final : public ConfPluginInterface {
 public:
  FakeIntraPlugin() = default;

  [[nodiscard]] TransportType get_transport_type() const override { return TransportType::kIntra; }

  [[nodiscard]] std::unique_ptr<Conf> create() const override {
    auto c = std::make_unique<StubConf>();
    c->transport = TransportType::kIntra;
    return c;
  }
};

class FakeZenohPlugin final : public ConfPluginInterface {
 public:
  FakeZenohPlugin() = default;

  [[nodiscard]] TransportType get_transport_type() const override { return TransportType::kZenoh; }

  [[nodiscard]] std::unique_ptr<Conf> create() const override {
    auto c = std::make_unique<StubConf>();
    c->transport = TransportType::kZenoh;
    return c;
  }
};

}  // namespace

// ---------------------------------------------------------------------------
// TEST SUITE: ConfPluginInterface - traits
// ---------------------------------------------------------------------------

TEST_SUITE("impl-ConfPluginInterface - traits") {
  TEST_CASE("interface is abstract") { CHECK(std::is_abstract_v<ConfPluginInterface>); }

  TEST_CASE("interface is not copy-constructible") { CHECK_FALSE(std::is_copy_constructible_v<ConfPluginInterface>); }

  TEST_CASE("interface is not copy-assignable") { CHECK_FALSE(std::is_copy_assignable_v<ConfPluginInterface>); }
}

// ---------------------------------------------------------------------------
// TEST SUITE: ConfPluginInterface - subclass behaviour
// ---------------------------------------------------------------------------

TEST_SUITE("impl-ConfPluginInterface - subclass behaviour") {
  TEST_CASE("get_transport_type returns the subclass value") {
    FakeIntraPlugin intra;
    FakeZenohPlugin zenoh;
    CHECK(intra.get_transport_type() == TransportType::kIntra);
    CHECK(zenoh.get_transport_type() == TransportType::kZenoh);
  }

  TEST_CASE("create returns a fresh independent Conf each call") {
    FakeIntraPlugin plugin;
    auto a = plugin.create();
    auto b = plugin.create();
    REQUIRE(a != nullptr);
    REQUIRE(b != nullptr);
    CHECK(a.get() != b.get());
    CHECK(a->get_transport_type() == TransportType::kIntra);
    CHECK(b->get_transport_type() == TransportType::kIntra);
  }

  TEST_CASE("created Conf is valid for our stub") {
    FakeZenohPlugin plugin;
    auto conf = plugin.create();
    REQUIRE(conf != nullptr);
    CHECK(conf->is_valid());
    CHECK(conf->get_transport_type() == TransportType::kZenoh);
  }

  TEST_CASE("virtual dispatch via base pointer") {
    FakeZenohPlugin concrete;
    ConfPluginInterface* base = &concrete;
    auto conf = base->create();
    REQUIRE(conf != nullptr);
    CHECK(conf->get_transport_type() == TransportType::kZenoh);
  }
}

// NOLINTEND
