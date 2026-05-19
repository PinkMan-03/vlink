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
 * @file schema_plugin_interface.h
 * @brief Runtime schema plugin interface for Protobuf and FlatBuffers metadata loading.
 *
 * @details
 * @c SchemaPluginInterface provides a unified runtime schema registry for
 * Protobuf and FlatBuffers. It keeps explicit Protobuf-only reflection hooks
 * used by @c cli/eproto and the webviz converters, and adds generic schema
 * listing plus FlatBuffers parser creation so the same plugin
 * can serve:
 *
 * 1. Protobuf @c FileDescriptorSet blobs.
 * 2. FlatBuffers BFBS blobs.
 * 3. Mixed schema registries for MCAP/bag embedding.
 * 4. Reusable FlatBuffers parsers for @c cli/efbs and other runtime tooling.
 *
 */

#pragma once

#include <string>
#include <vector>

#include "../base/plugin.h"
#include "../impl/types.h"

namespace vlink {

/**
 * @class SchemaPluginInterface
 * @brief Abstract interface for runtime schema lookup and dynamic object creation.
 *
 * @details
 * Loaded as a dynamic plugin via @c Plugin::load<SchemaPluginInterface>().
 * @c SchemaPluginManager provides a convenient singleton accessor.
 *
 * The interface intentionally exposes both generic schema APIs and encoding-specific
 * runtime hooks:
 * - Generic APIs such as @c search_schema() and @c get_all_schemas() are used by
 *   bag/database/MCAP embedding logic.
 * - Protobuf-specific hooks are consumed by @c cli/eproto and webviz protobuf decoders.
 * - FlatBuffers-specific hooks are consumed by @c cli/efbs and runtime BFBS-based decoders.
 */
class SchemaPluginInterface {
  VLINK_PLUGIN_REGISTER(SchemaPluginInterface)

 protected:
  SchemaPluginInterface() = default;

  virtual ~SchemaPluginInterface() = default;

 public:
  /**
   * @brief Opaque pointer type for a @c google::protobuf::Descriptor.
   *
   * @details
   * Callers that know the concrete plugin implementation may cast this to
   * @c google::protobuf::Descriptor* directly.
   */
  using ProtobufDescriptorPtr = void*;

  /**
   * @brief Opaque pointer type for a @c google::protobuf::Message instance.
   *
   * @details
   * Callers may cast this to @c google::protobuf::Message* for dynamic access.
   */
  using ProtobufMessagePtr = void*;

  /**
   * @brief Opaque pointer type for a FlatBuffers @c reflection::Schema instance.
   *
   * @details
   * Callers that include FlatBuffers reflection headers may cast this to
   * @c reflection::Schema* directly.
   */
  using FlatbuffersSchemaPtr = void*;

  /**
   * @brief Opaque pointer type for a runtime FlatBuffers @c Parser instance.
   *
   * @details
   * Callers may cast this to @c flatbuffers::Parser* when FlatBuffers headers are available.
   */
  using FlatbuffersParserPtr = void*;

  /**
   * @struct VersionInfo
   * @brief Plugin version and build metadata.
   */
  struct VersionInfo final {
    std::string name;       ///< Plugin display name.
    std::string version;    ///< Semantic version string.
    std::string timestamp;  ///< Build timestamp.
    std::string tag;        ///< Source control tag.
    std::string commit_id;  ///< Source control commit hash.
  };

  /**
   * @brief Returns version and build metadata for this plugin.
   *
   * @return @c VersionInfo with name, version, timestamp, tag, and commit ID.
   */
  [[nodiscard]] virtual VersionInfo get_version_info() const = 0;

  /**
   * @brief Finds one schema constrained by schema family.
   *
   * @details
   * Callers that already know the expected schema family (for example derived from
   * compile-time traits or discovery metadata) should supply @p schema_type to
   * skip family-agnostic probing. Passing @c SchemaType::kUnknown still performs
   * a best-effort lookup across every registered family.
   *
   * @param name         Serialization type / message type name.
   * @param schema_type  Coarse schema family hint, or @c SchemaType::kUnknown for
   *                     family-agnostic lookup.
   * @return Matching @c SchemaData, or an empty schema when not found.
   */
  [[nodiscard]] virtual SchemaData search_schema(const std::string& name,
                                                 SchemaType schema_type = SchemaType::kUnknown) = 0;

  /**
   * @brief Returns all imported or cached schemas filtered by schema family.
   *
   * @param schema_type  Schema family to filter on; @c SchemaType::kUnknown returns
   *                     every cached schema.
   * @return List of schemas belonging to @p schema_type.
   */
  [[nodiscard]] virtual std::vector<SchemaData> get_all_schemas(SchemaType schema_type = SchemaType::kUnknown) = 0;

  /**
   * @brief Looks up a Protobuf descriptor by fully-qualified message name.
   *
   * @details
   * For non-Protobuf types this returns @c nullptr.
   *
   * @param name  Fully-qualified Protobuf message type name.
   * @return Opaque @c Descriptor pointer, or @c nullptr if not found.
   */
  [[nodiscard]] virtual ProtobufDescriptorPtr search_protobuf_descriptor(const std::string& name) = 0;

  /**
   * @brief Creates a Protobuf dynamic message prototype for the named type.
   *
   * @details
   * For non-Protobuf types this returns @c nullptr.
   *
   * @param name  Fully-qualified Protobuf message type name.
   * @return Opaque @c Message pointer, or @c nullptr if not found.
   */
  [[nodiscard]] virtual ProtobufMessagePtr create_protobuf_message(const std::string& name) = 0;

  /**
   * @brief Looks up a FlatBuffers binary-schema reflection handle by type name.
   *
   * @details
   * The returned handle points at the BFBS-backed @c reflection::Schema for the
   * requested root type. For non-FlatBuffers types this returns @c nullptr.
   *
   * @param name  Fully-qualified FlatBuffers root type name.
   * @return Opaque BFBS reflection schema handle, or @c nullptr if not found.
   */
  [[nodiscard]] virtual FlatbuffersSchemaPtr search_flatbuffers_schema(const std::string& name) = 0;

  /**
   * @brief Creates a FlatBuffers parser preloaded with the named schema.
   *
   * @details
   * The returned parser is backed by the plugin lifetime and already contains
   * the BFBS schema plus the requested root type. Each successful call returns
   * a distinct parser instance. For non-FlatBuffers types this returns @c nullptr.
   *
   * @param name  Fully-qualified FlatBuffers root type name.
   * @return Opaque runtime parser handle, or @c nullptr if not found.
   */
  [[nodiscard]] virtual FlatbuffersParserPtr create_flatbuffers_parser(const std::string& name) = 0;

 private:
  VLINK_DISALLOW_COPY_AND_ASSIGN(SchemaPluginInterface)
};

}  // namespace vlink
