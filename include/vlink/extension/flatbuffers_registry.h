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

/**
 * @file flatbuffers_registry.h
 * @brief Process-local registry for BFBS blobs compiled into the current library.
 *
 * @details
 * Protobuf already provides @c google::protobuf::DescriptorPool::generated_pool()
 * as the global registry of generated descriptors. FlatBuffers has no equivalent
 * runtime pool for BFBS reflection data, so VLink keeps a small process-local
 * registry that stores BFBS blobs embedded into the current binary/library.
 *
 * The registry is intentionally limited to:
 * - registering compiled-in BFBS blobs
 * - looking up one schema by fully-qualified root type
 * - enumerating all registered BFBS schemas
 *
 * It does not read schema files from disk and does not depend on
 * @c VLINK_PROTO_DIR / @c VLINK_FBS_DIR.
 */

#pragma once

#include <cstddef>
#include <mutex>
#include <shared_mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include "../impl/types.h"

#if __has_include(<flatbuffers/idl.h>)
#include <flatbuffers/flatbuffers.h>
#include <flatbuffers/idl.h>
#define VLINK_HAS_SCHEMA_PLUGIN_FLATBUFFERS
#endif

#ifdef VLINK_HAS_SCHEMA_PLUGIN_FLATBUFFERS

namespace vlink {

/**
 * @class FlatbuffersRegistry
 * @brief Global BFBS registry shared by schema-plugin instances in one process.
 *
 * @details
 * The registry owns copied BFBS blobs so callers may register data from
 * generated @c *BinarySchema helpers without managing the lifetime manually.
 * Each BFBS blob is keyed by its fully-qualified FlatBuffers root type name.
 */
class FlatbuffersRegistry final {
 public:
  /**
   * @brief Registers one BFBS blob from a generated @c *BinarySchema helper type.
   *
   * @tparam BinarySchema  Generated type exposing @c data() and @c size().
   * @param name           Fully-qualified FlatBuffers root type name.
   * @return @c true if the BFBS blob is valid and was inserted successfully.
   */
  template <typename BinarySchema>
  static bool register_schema(const std::string& name);

  /**
   * @brief Registers one BFBS blob into the global registry.
   *
   * @param name       Fully-qualified FlatBuffers root type name.
   * @param bfbs_data  Pointer to BFBS bytes.
   * @param bfbs_size  Size of @p bfbs_data.
   * @return @c true if the BFBS blob is valid and was inserted successfully.
   */
  static bool register_schema(const std::string& name, const uint8_t* bfbs_data, size_t bfbs_size);

  /**
   * @brief Finds one registered BFBS schema by root type name.
   *
   * @param name  Fully-qualified FlatBuffers root type name.
   * @return Copy of the registered @c SchemaData, or an empty schema when absent.
   */
  [[nodiscard]] SchemaData search_schema(const std::string& name);

  /**
   * @brief Returns all BFBS schemas currently registered in this process.
   *
   * @return Vector of registered FlatBuffers schema blobs.
   */
  [[nodiscard]] std::vector<SchemaData> get_all_schemas();

 private:
  FlatbuffersRegistry() = default;

  ~FlatbuffersRegistry() = default;

  static SchemaData build_data(const std::string& name, const uint8_t* bfbs_data, size_t bfbs_size);

  std::unordered_map<std::string, SchemaData> map_;
  std::shared_mutex mtx_;

  VLINK_SINGLETON_DECLARE(FlatbuffersRegistry)
};

////////////////////////////////////////////////////////////////
// Details
////////////////////////////////////////////////////////////////

template <typename BinarySchema>
inline bool FlatbuffersRegistry::register_schema(const std::string& name) {
  return register_schema(name, BinarySchema::data(), BinarySchema::size());
}

inline bool FlatbuffersRegistry::register_schema(const std::string& name, const uint8_t* bfbs_data, size_t bfbs_size) {
  auto schema = build_data(name, bfbs_data, bfbs_size);

  if VUNLIKELY (schema.encoding != "flatbuffers" || schema.data.empty()) {
    return false;
  }

  auto& registry = get();

  std::lock_guard lock(registry.mtx_);
  registry.map_[schema.name] = std::move(schema);

  return true;
}

inline SchemaData FlatbuffersRegistry::search_schema(const std::string& name) {
  std::shared_lock lock(mtx_);

  auto iter = map_.find(name);

  if (iter == map_.end()) {
    return {};
  }

  return iter->second;
}

inline std::vector<SchemaData> FlatbuffersRegistry::get_all_schemas() {
  std::shared_lock lock(mtx_);

  std::vector<SchemaData> schemas;
  schemas.reserve(map_.size());

  for (const auto& [name, schema] : map_) {
    (void)name;
    schemas.emplace_back(schema);
  }

  return schemas;
}

inline SchemaData FlatbuffersRegistry::build_data(const std::string& name, const uint8_t* bfbs_data, size_t bfbs_size) {
  SchemaData schema;
  Bytes::init_memory_pool();

  if VUNLIKELY (name.empty() || bfbs_data == nullptr || bfbs_size == 0) {
    return schema;
  }

  schema.name = name;
  schema.encoding = "flatbuffers";
  schema.schema_type = SchemaType::kFlatbuffers;
  schema.data = Bytes::shallow_copy(bfbs_data, bfbs_size);

  flatbuffers::Verifier verifier(schema.data.data(), schema.data.size());

  if VUNLIKELY (!reflection::VerifySchemaBuffer(verifier)) {
    schema.encoding.clear();
    schema.schema_type = SchemaType::kUnknown;
    schema.data.clear();
  }

  return schema;
}

}  // namespace vlink

/**
 * @def VLINK_REGISTER_FLATBUFFERS_NOW(schema_name, binary_schema_type)
 * @brief Immediately registers a compiled FlatBuffers BFBS schema.
 *
 * @details
 * Expands to a direct call to
 * ::vlink::FlatbuffersRegistry::register_schema<binary_schema_type>().
 * Registration is performed immediately at the point of expansion —
 * no static objects are created, and no automatic initialization is involved.
 */
#define VLINK_REGISTER_FLATBUFFERS_NOW(schema_name, binary_schema_type) \
  ::vlink::FlatbuffersRegistry::register_schema<binary_schema_type>(schema_name)

/**
 * @def VLINK_REGISTER_FLATBUFFERS(schema_name, binary_schema_type)
 * @brief Automatically registers an embedded FlatBuffers BFBS schema
 *        during static initialization.
 *
 * @details
 * Defines one translation-unit-local helper type and one translation-unit-local
 * static object whose constructor registers the schema. The helper names are
 * made unique for every expansion, so the macro may be used multiple times in
 * the same translation unit and still works for namespaced or templated
 * @p binary_schema_type values.
 *
 * Example:
 * @code
 * VLINK_REGISTER_FLATBUFFERS("Helloworld.fbs.User",
 *                            Helloworld::fbs::UserBinarySchema);
 * @endcode
 */
#define VLINK_REGISTER_FLATBUFFERS(schema_name, binary_schema_type)    \
  /* NOLINTBEGIN */                                                    \
  namespace {                                                          \
  template <int Id>                                                    \
  struct VlinkAutoRegisterFlatbuffersHelper;                           \
                                                                       \
  template <>                                                          \
  struct VlinkAutoRegisterFlatbuffersHelper<__COUNTER__> {             \
    struct Init {                                                      \
      Init() noexcept {                                                \
        using SchemaType = binary_schema_type;                         \
        (void)VLINK_REGISTER_FLATBUFFERS_NOW(schema_name, SchemaType); \
      }                                                                \
    };                                                                 \
                                                                       \
    [[maybe_unused]] inline static const Init instance{};              \
  };                                                                   \
  }                                                                    \
  /* NOLINTEND */

#endif  // VLINK_HAS_SCHEMA_PLUGIN_FLATBUFFERS
