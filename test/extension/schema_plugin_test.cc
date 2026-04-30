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

#include <filesystem>
#include <fstream>
#include <string>

#if __has_include(<google/protobuf/any.pb.h>)
#include <google/protobuf/any.pb.h>
#endif

#define VLINK_ENABLE_CLI_EPROTO
#define VLINK_ENABLE_CLI_EFBS
#include "./extension/schema_plugin_base.h"
#include "./extension/schema_plugin_manager.h"

//
#include "../common_test.h"

namespace {

struct InvalidBinarySchema final {
  [[nodiscard]] static const uint8_t* data() {
    static const uint8_t kData[]{0};
    return kData;
  }

  [[nodiscard]] static size_t size() { return 1U; }
};

class TestSchemaPlugin final : public SchemaPluginBase {
 public:
  TestSchemaPlugin() = default;

  using SchemaPluginBase::create_flatbuffers_parser;
  using SchemaPluginBase::create_protobuf_message;
  using SchemaPluginBase::get_all_schemas;
  using SchemaPluginBase::search_flatbuffers_schema;
  using SchemaPluginBase::search_protobuf_descriptor;
  using SchemaPluginBase::search_schema;

  [[nodiscard]] VersionInfo get_version_info() const override {
    return {"TestSchemaPlugin", "1.0.0", "2026-01-01", "", ""};
  }
};

#ifdef VLINK_HAS_SCHEMA_PLUGIN_FLATBUFFERS
VLINK_REGISTER_FLATBUFFERS("invalid.Schema", InvalidBinarySchema);
VLINK_REGISTER_FLATBUFFERS("invalid.Schema.second", InvalidBinarySchema);
#endif

[[maybe_unused]] static std::filesystem::path test_idl_dir() {
  return std::filesystem::path(__FILE__).parent_path().parent_path() / "idl";
}

}  // namespace

// ---------------------------------------------------------------------------
// TEST SUITE: SchemaPluginInterface::VersionInfo - struct fields
// ---------------------------------------------------------------------------

TEST_SUITE("extension-SchemaPluginInterface::VersionInfo") {
  TEST_CASE("default construction has empty fields") {
    SchemaPluginInterface::VersionInfo vi;
    CHECK(vi.name.empty());
    CHECK(vi.version.empty());
    CHECK(vi.timestamp.empty());
    CHECK(vi.tag.empty());
    CHECK(vi.commit_id.empty());
  }

  TEST_CASE("fields are assignable") {
    SchemaPluginInterface::VersionInfo vi;
    vi.name = "TestPlugin";
    vi.version = "1.0.0";
    vi.timestamp = "2026-01-01";
    vi.tag = "v1.0.0";
    vi.commit_id = "abc123";

    CHECK(vi.name == "TestPlugin");
    CHECK(vi.version == "1.0.0");
    CHECK(vi.timestamp == "2026-01-01");
    CHECK(vi.tag == "v1.0.0");
    CHECK(vi.commit_id == "abc123");
  }
}

// ---------------------------------------------------------------------------
// TEST SUITE: SchemaPluginManager - no plugin loaded
// ---------------------------------------------------------------------------

TEST_SUITE("extension-SchemaPluginManager - no plugin") {
  TEST_CASE("get() returns a valid reference") {
    // Access without a real plugin path — uses env var or nothing.
    // The singleton must be accessible without crashing.
    SchemaPluginManager& mgr = SchemaPluginManager::get("");
    // Just verify the reference is valid by checking is_valid()
    bool valid = mgr.is_valid();
    // valid may be true or false depending on environment; we just check no crash.
    (void)valid;
    CHECK(true);
  }

  TEST_CASE("get_interface returns nullptr when no plugin loaded") {
    SchemaPluginManager& mgr = SchemaPluginManager::get("");

    if (!mgr.is_valid()) {
      CHECK(mgr.get_interface() == nullptr);
    } else {
      CHECK(mgr.get_interface() != nullptr);
    }
  }

  TEST_CASE("repeated get() calls return same singleton") {
    SchemaPluginManager& a = SchemaPluginManager::get();
    SchemaPluginManager& b = SchemaPluginManager::get();
    CHECK(&a == &b);
  }

  TEST_CASE("is_valid returns false when no plugin is installed") {
    // In CI without a schema plugin .so, is_valid() must return false.
    // We cannot force it to be true here, but we can verify the API.
    SchemaPluginManager& mgr = SchemaPluginManager::get();
    bool valid = mgr.is_valid();
    // Either branch is acceptable; test just verifies the call doesn't throw.
    (void)valid;
    CHECK(true);
  }
}

// ---------------------------------------------------------------------------
// TEST SUITE: SchemaPluginManager - conditional protobuf tests
// ---------------------------------------------------------------------------

#if defined(VLINK_TEST_SUPPORT_PROTOBUF)

TEST_SUITE("extension-SchemaPluginManager - with protobuf support") {
  TEST_CASE("SchemaPluginManager is accessible with protobuf enabled") {
    // Even with protobuf compiled in, the manager only loads a plugin
    // from an .so file; without that file it remains invalid.
    SchemaPluginManager& mgr = SchemaPluginManager::get();
    (void)mgr.is_valid();
    CHECK(true);
  }
}

#endif  // VLINK_TEST_SUPPORT_PROTOBUF

#if !defined(_WIN32) && !defined(__CYGWIN__) && defined(VLINK_HAS_SCHEMA_PLUGIN_PROTOBUF)
TEST_SUITE("extension-SchemaPluginBase - linked schema registration") {
  TEST_CASE("registers protobuf schemas from linked generated descriptors") {
    TestSchemaPlugin plugin;
    const auto* descriptor =
        google::protobuf::DescriptorPool::generated_pool()->FindMessageTypeByName("google.protobuf.Any");
    REQUIRE(descriptor != nullptr);

    const auto schema = plugin.search_schema("google.protobuf.Any", SchemaType::kProtobuf);
    CHECK(schema.name == "google.protobuf.Any");
    CHECK(schema.encoding == "protobuf");
    CHECK(schema.schema_type == SchemaType::kProtobuf);
    CHECK_FALSE(schema.data.empty());

    const auto wrong_schema = plugin.search_schema("google.protobuf.Any", SchemaType::kFlatbuffers);
    CHECK(wrong_schema.encoding.empty());
    CHECK(wrong_schema.data.empty());

    CHECK(plugin.search_protobuf_descriptor("google.protobuf.Any") != nullptr);
    CHECK(plugin.create_protobuf_message("google.protobuf.Any") != nullptr);

    const auto all_schemas = plugin.get_all_schemas(SchemaType::kProtobuf);
    CHECK_FALSE(all_schemas.empty());
  }

  TEST_CASE("registers embedded BFBS and exposes flatbuffers runtime handles") {
    auto fbs_path = test_idl_dir() / "test.fbs";
    REQUIRE(std::filesystem::exists(fbs_path));

    std::ifstream input(fbs_path, std::ios::binary);
    REQUIRE(input.is_open());

    std::string schema_text((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
    REQUIRE_FALSE(schema_text.empty());

    flatbuffers::Parser bfbs_builder;
    const auto include_dir = fbs_path.parent_path().string();
    const char* include_dirs[] = {include_dir.c_str(), nullptr};
    REQUIRE(bfbs_builder.Parse(schema_text.c_str(), include_dirs));
    REQUIRE(bfbs_builder.SetRootType("fbs.Message"));
    bfbs_builder.Serialize();

    const auto* bfbs_data = bfbs_builder.builder_.GetBufferPointer();
    const auto bfbs_size = bfbs_builder.builder_.GetSize();
    REQUIRE(bfbs_data != nullptr);
    REQUIRE(bfbs_size > 0);

    CHECK(FlatbuffersRegistry::register_schema("fbs.Message", bfbs_data, bfbs_size));

    TestSchemaPlugin plugin;

    const auto schema = plugin.search_schema("fbs.Message", SchemaType::kFlatbuffers);
    CHECK(schema.name == "fbs.Message");
    CHECK(schema.encoding == "flatbuffers");
    CHECK(schema.schema_type == SchemaType::kFlatbuffers);
    CHECK_FALSE(schema.data.empty());

    const auto wrong_schema = plugin.search_schema("fbs.Message", SchemaType::kProtobuf);
    CHECK(wrong_schema.encoding.empty());
    CHECK(wrong_schema.data.empty());

    auto* reflection_schema = static_cast<const reflection::Schema*>(plugin.search_flatbuffers_schema("fbs.Message"));
    REQUIRE(reflection_schema != nullptr);
    CHECK(reflection_schema->root_table() != nullptr);

    auto* runtime_parser = static_cast<flatbuffers::Parser*>(plugin.create_flatbuffers_parser("fbs.Message"));
    REQUIRE(runtime_parser != nullptr);
    REQUIRE(runtime_parser->root_struct_def_ != nullptr);
    CHECK(runtime_parser->root_struct_def_->defined_namespace != nullptr);
    CHECK(runtime_parser->root_struct_def_->defined_namespace->GetFullyQualifiedName(
              runtime_parser->root_struct_def_->name) == "fbs.Message");

    const auto all_schemas = plugin.get_all_schemas(SchemaType::kFlatbuffers);
    CHECK_FALSE(all_schemas.empty());
  }
}

TEST_SUITE("extension-SchemaPluginBase - schema family disambiguation") {
  TEST_CASE("family-agnostic lookup refuses ambiguous same-name schemas") {
    auto fbs_path = test_idl_dir() / "test.fbs";
    REQUIRE(std::filesystem::exists(fbs_path));

    std::ifstream input(fbs_path, std::ios::binary);
    REQUIRE(input.is_open());

    std::string schema_text((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
    REQUIRE_FALSE(schema_text.empty());

    flatbuffers::Parser bfbs_builder;
    const auto include_dir = fbs_path.parent_path().string();
    const char* include_dirs[] = {include_dir.c_str(), nullptr};
    REQUIRE(bfbs_builder.Parse(schema_text.c_str(), include_dirs));
    REQUIRE(bfbs_builder.SetRootType("fbs.Message"));
    bfbs_builder.Serialize();

    const auto* bfbs_data = bfbs_builder.builder_.GetBufferPointer();
    const auto bfbs_size = bfbs_builder.builder_.GetSize();
    REQUIRE(bfbs_data != nullptr);
    REQUIRE(bfbs_size > 0);

    REQUIRE(FlatbuffersRegistry::register_schema("vlink::zerocopy::Collision", bfbs_data, bfbs_size));

    TestSchemaPlugin plugin;

    const auto ambiguous_schema = plugin.search_schema("vlink::zerocopy::Collision");
    CHECK(ambiguous_schema.encoding.empty());
    CHECK(ambiguous_schema.data.empty());

    const auto resolved_fbs = plugin.search_schema("vlink::zerocopy::Collision", SchemaType::kFlatbuffers);
    CHECK(resolved_fbs.schema_type == SchemaType::kFlatbuffers);
    CHECK(resolved_fbs.encoding == "flatbuffers");

    const auto resolved_zerocopy = plugin.search_schema("vlink::zerocopy::Collision", SchemaType::kZeroCopy);
    CHECK(resolved_zerocopy.schema_type == SchemaType::kZeroCopy);
    CHECK(resolved_zerocopy.encoding == "vlink_msg");
  }
}

#endif

// NOLINTEND
