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

/// @file plugin_schema.cc
/// @brief Conceptual demonstration of the SchemaPluginInterface and SchemaPluginManager API.
///
/// This example shows how VLink's schema-plugin system works:
///
///   1. SchemaPluginInterface -- abstract interface for descriptor lookup,
///      schema serialisation, and dynamic message creation.
///
///   2. SchemaPluginManager -- process-global singleton that loads and holds
///      the schema plugin, accessed via SchemaPluginManager::get().
///
/// The actual plugin .so requires a protobuf dependency, so this demo loads
/// the plugin only when the .so is available.  When it is not, the program
/// prints a clear explanation of the API without crashing.

#include <vlink/base/logger.h>
#include <vlink/base/plugin.h>
#include <vlink/extension/schema_plugin_interface.h>
#include <vlink/extension/schema_plugin_manager.h>

#include <cstdlib>
#include <string>

/// Helper: attempt to load a SchemaPluginInterface .so and exercise its API.
static void demo_direct_load() {
  VLOG_I("=== Direct Plugin::load<SchemaPluginInterface> demo ===");

  vlink::Plugin plugin;
  plugin.set_log_level(vlink::Logger::kInfo);

  // The plugin .so name is typically set by the build system or environment.
  // Common names depend on the actual shared library target name.
  const char* env_name = std::getenv("VLINK_SCHEMA_PLUGIN");
  std::string lib_name = env_name ? env_name : "vlink_schema_plugin";

  VLOG_I("Attempting to load SchemaPluginInterface from: ", lib_name);
  VLOG_I("SchemaPluginInterface plugin_id: ", std::string(vlink::SchemaPluginInterface::get_plugin_id()));

  auto schema_plugin = plugin.load<vlink::SchemaPluginInterface>(lib_name, 1, 0);
  if (!schema_plugin) {
    VLOG_W("Schema plugin not available (protobuf dependency required).");
    VLOG_I("Set VLINK_SCHEMA_PLUGIN=<path_to_so> to enable this demo.");
    VLOG_I("Skipping direct-load demo.");
    return;
  }

  // --- VersionInfo ---
  vlink::SchemaPluginInterface::VersionInfo ver = schema_plugin->get_version_info();
  VLOG_I("  version_info.name:      ", ver.name);
  VLOG_I("  version_info.version:   ", ver.version);
  VLOG_I("  version_info.timestamp: ", ver.timestamp);
  VLOG_I("  version_info.tag:       ", ver.tag);
  VLOG_I("  version_info.commit_id: ", ver.commit_id);

  // --- Descriptor lookup ---
  std::string type_name = "example.SensorData";
  VLOG_I("  search_protobuf_descriptor(\"", type_name, "\")...");
  vlink::SchemaPluginInterface::ProtobufDescriptorPtr desc = schema_plugin->search_protobuf_descriptor(type_name);
  if (desc) {
    VLOG_I("    found descriptor at ", desc);
  } else {
    VLOG_I("    descriptor not found (type not registered in this plugin)");
  }

  // --- Schema serialisation ---
  VLOG_I("  search_schema(\"", type_name, "\", kProtobuf)...");
  vlink::SchemaData schema = schema_plugin->search_schema(type_name, vlink::SchemaType::kProtobuf);
  if (!schema.data.empty()) {
    VLOG_I("    schema.name: ", schema.name);
    VLOG_I("    schema.encoding: ", schema.encoding);
    VLOG_I("    schema.schema_type: ", static_cast<int>(schema.schema_type));
    VLOG_I("    schema.data size: ", schema.data.size(), " bytes");
  } else {
    VLOG_I("    schema not found for this type");
  }

  // --- Dynamic message creation ---
  VLOG_I("  create_protobuf_message(\"", type_name, "\")...");
  vlink::SchemaPluginInterface::ProtobufMessagePtr msg = schema_plugin->create_protobuf_message(type_name);
  if (msg) {
    VLOG_I("    created message prototype at ", msg);
  } else {
    VLOG_I("    message creation failed (type not registered)");
  }

  auto* fbs_schema = schema_plugin->search_flatbuffers_schema("example.SensorFrame");
  VLOG_I("  search_flatbuffers_schema(\"example.SensorFrame\"): ", fbs_schema ? "found" : "not found");

  auto* fbs_parser = schema_plugin->create_flatbuffers_parser("example.SensorFrame");
  VLOG_I("  create_flatbuffers_parser(\"example.SensorFrame\"): ", fbs_parser ? "ready" : "not found");

  schema_plugin.reset();
  plugin.clear();
}

/// Helper: demonstrate the SchemaPluginManager singleton API.
static void demo_manager() {
  VLOG_I("=== SchemaPluginManager singleton demo ===");

  // SchemaPluginManager::get() loads the plugin on first call.
  // It reads VLINK_SCHEMA_PLUGIN env var, or accepts an explicit path.
  vlink::SchemaPluginManager& mgr = vlink::SchemaPluginManager::get();

  if (!mgr.is_valid()) {
    VLOG_W("SchemaPluginManager: no plugin loaded.");
    VLOG_I("Set VLINK_SCHEMA_PLUGIN environment variable to the .so path.");
    VLOG_I("Skipping manager demo.");
    return;
  }

  std::shared_ptr<vlink::SchemaPluginInterface> iface = mgr.get_interface();
  if (!iface) {
    VLOG_E("SchemaPluginManager::get_interface() returned nullptr.");
    return;
  }

  vlink::SchemaPluginInterface::VersionInfo ver = iface->get_version_info();
  VLOG_I("  Manager loaded plugin: ", ver.name, " v", ver.version);

  // Demonstrate schema lookup through the manager.
  std::string type_name = "example.SensorData";
  vlink::SchemaData schema = iface->search_schema(type_name, vlink::SchemaType::kProtobuf);
  VLOG_I("  search_schema(\"", type_name, "\", kProtobuf",
         "\"): ", schema.data.empty() ? "not found" : "found (" + std::to_string(schema.data.size()) + " bytes)");
}

int main() {
  VLOG_I("=== SchemaPluginInterface conceptual demo ===");
  VLOG_I("");
  VLOG_I("SchemaPluginInterface provides six capabilities:");
  VLOG_I("  1. search_protobuf_descriptor(name) -- lookup a Protobuf Descriptor by type name");
  VLOG_I("  2. search_schema(name, schema_type) -- return protobuf/flatbuffers schema payloads");
  VLOG_I("  3. get_all_schemas(schema_type)     -- enumerate cached/imported schema payloads");
  VLOG_I("  4. create_protobuf_message(name)    -- create a dynamic Protobuf Message prototype");
  VLOG_I("  5. search_flatbuffers_schema(name)  -- lookup a BFBS reflection schema");
  VLOG_I("  6. create_flatbuffers_parser(name)  -- create a runtime FlatBuffers parser");
  VLOG_I("");

  demo_direct_load();
  VLOG_I("");
  demo_manager();

  VLOG_I("");
  VLOG_I("Schema plugin example complete.");
  return 0;
}
