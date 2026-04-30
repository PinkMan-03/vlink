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
 * @file message_convert_plugin.h
 * @brief Plugin interface for VLink/webviz message conversion across visualization backends.
 *
 * @details
 * @c MessageConvertPlugin allows users to implement custom VLink/webviz message conversion
 * logic in a shared library plugin. The plugin receives raw VLink message bytes
 * (Protobuf, FlatBuffers, CDR, POD, or any custom serialisation) and produces a
 * backend-specific payload along with schema/type metadata.
 *
 * This interface has **zero third-party dependencies** -- it only uses VLink base
 * types (@c Bytes) and standard C++ types, making it easy to implement in external
 * projects without linking against Protobuf, FlatBuffers, Rerun SDK, or JSON libraries.
 *
 * A single plugin can support multiple visualization backends by checking the
 * @c ConvertTarget parameter in each method. The plugin coexists with JSON-based
 * VLink-to-webviz mapping files. When both are present, the plugin is tried
 * first; if it returns @c false for a given type, the JSON mapping pipeline takes over.
 *
 * @par Supported targets
 * | Target       | Payload format                    | type_name meaning              |
 * |------------- |-----------------------------------|--------------------------------|
 * | @c kFoxglove | FlatBuffer / Protobuf binary      | Foxglove schema name           |
 * | @c kRerun    | JSON string describing components | Rerun archetype name           |
 *
 * @par Rerun JSON payload format
 * When targeting Rerun, the plugin should produce a UTF-8 JSON string as the payload.
 * The JSON object describes the Rerun archetype components. Each archetype has its own
 * expected fields:
 *
 * @code{.json}
 * // Points3D example:
 * {
 *   "positions": [[1.0, 2.0, 3.0], [4.0, 5.0, 6.0]],
 *   "colors": [[255, 0, 0, 255], [0, 255, 0, 255]],
 *   "radii": [0.1, 0.2]
 * }
 *
 * // EncodedImage example (binary data is base64-encoded):
 * {
 *   "media_type": "image/jpeg",
 *   "data_base64": "<base64-encoded image bytes>"
 * }
 *
 * // GeoPoints example:
 * {
 *   "lat_deg": [37.7749, 37.7750],
 *   "lon_deg": [-122.4194, -122.4195]
 * }
 *
 * // TextLog example:
 * {
 *   "text": "Hello world",
 *   "level": "INFO"
 * }
 *
 * // Scalars example:
 * {
 *   "value": 3.14
 * }
 *
 * // Transform3D example:
 * {
 *   "translation": [1.0, 2.0, 3.0],
 *   "rotation_quat": [0.0, 0.0, 0.0, 1.0]
 * }
 *
 * // Boxes3D example:
 * {
 *   "half_sizes": [[1.0, 2.0, 3.0]],
 *   "centers": [[0.0, 0.0, 0.0]],
 *   "quaternions": [[0.0, 0.0, 0.0, 1.0]],
 *   "colors": [[255, 0, 0, 255]],
 *   "labels": ["box1"]
 * }
 *
 * // Pinhole example:
 * {
 *   "image_from_camera": [[fx, 0, cx], [0, fy, cy], [0, 0, 1]],
 *   "resolution": [1920, 1080]
 * }
 * @endcode
 *
 * Binary archetypes should carry their raw bytes in a @c data_base64 string. Currently the
 * built-in Rerun JSON bridge supports this for @c EncodedImage, @c Image, @c DepthImage,
 * @c SegmentationImage, @c EncodedDepthImage, @c Asset3D, @c AssetVideo, and @c Tensor.
 * @c Image, @c DepthImage, and @c SegmentationImage also require @c width/@c height (or
 * @c resolution). @c Tensor additionally requires @c shape and may provide @c dim_names.
 * Direct VLink-to-Rerun mappings still cover a broader set of archetypes than the plugin
 * JSON bridge.
 *
 * @par Plugin lifecycle
 * 1. @c init() is called once after loading, with an optional config string.
 * 2. @c can_convert() is called for each discovered VLink serialisation type
 *    (with the target backend) to determine if the plugin handles it.
 * 3. @c get_schema_info() is called once per type to register the channel/archetype.
 * 4. @c convert() is called for every incoming message on matched types.
 * 5. The plugin is destroyed when the server shuts down.
 *
 * @par Example implementation (supports both Foxglove and Rerun)
 * @code
 * #include <vlink/extension/message_convert_plugin.h>
 *
 * class MyConvertPlugin : public vlink::MessageConvertPlugin {
 *   VLINK_PLUGIN_REGISTER(MessageConvertPlugin)
 *
 *  public:
 *   bool init(const std::string& config) override {
 *     // Parse config, load schemas, etc.
 *     return true;
 *   }
 *
 *   bool can_convert(const std::string& vlink_ser, ConvertTarget target) override {
 *     if (vlink_ser != "my_pkg.MyMessage") return false;
 *     // Support both backends
 *     return target == ConvertTarget::kFoxglove || target == ConvertTarget::kRerun;
 *   }
 *
 *   bool get_schema_info(const std::string& vlink_ser, ConvertTarget target,
 *                        std::string& type_name, std::string& encoding,
 *                        std::string& schema_encoding,
 *                        std::string& schema_data) override {
 *     if (target == ConvertTarget::kFoxglove) {
 *       type_name = "foxglove.LocationFix";
 *       encoding = "flatbuffers";
 *       schema_encoding = "flatbuffers";
 *       // schema_data = binary FBS schema bytes
 *     } else if (target == ConvertTarget::kRerun) {
 *       type_name = "GeoPoints";
 *       encoding = "json";
 *       // schema_encoding and schema_data are unused for Rerun
 *     }
 *     return true;
 *   }
 *
 *   bool convert(const std::string& vlink_ser, const vlink::Bytes& raw,
 *                ConvertTarget target, vlink::Bytes& payload) override {
 *     if (target == ConvertTarget::kFoxglove) {
 *       // Build Foxglove FlatBuffer payload
 *     } else if (target == ConvertTarget::kRerun) {
 *       // Build JSON: {"lat_deg":[37.77], "lon_deg":[-122.41]}
 *       std::string json = R"({"lat_deg":[37.77],"lon_deg":[-122.41]})";
 *       payload = vlink::Bytes::deep_copy(json.data(), json.size());
 *     }
 *     return true;
 *   }
 * };
 * VLINK_PLUGIN_DECLARE(MyConvertPlugin, 4, 0)
 * @endcode
 */

#pragma once

#include <string>

#include "../base/bytes.h"
#include "../base/plugin.h"
#include "../impl/types.h"

namespace vlink {

/**
 * @enum ConvertTarget
 * @brief Identifies the visualization backend that the plugin is converting for.
 *
 * @details
 * Passed to @c can_convert(), @c get_schema_info(), and @c convert() so that
 * a single plugin implementation can produce the appropriate output format
 * for each backend.
 */
enum class ConvertTarget : uint8_t {
  kFoxglove = 0,  ///< Foxglove Studio (WebSocket + FlatBuffers/Protobuf)
  kRerun = 1,     ///< Rerun Viewer (gRPC + Arrow IPC, plugin outputs JSON)
};

/**
 * @struct WebChannel
 * @brief Describes a frontend-advertised publish channel.
 *
 * @details
 * Used by inbound/web command conversion hooks so plugins can route Foxglove
 * webviz-published payloads to the correct VLink topic and serialisation type.
 */
struct WebChannel final {
  std::string topic;            ///< Frontend channel topic as advertised by the client.
  std::string encoding;         ///< Frontend payload encoding (e.g. json/protobuf/flatbuffers).
  std::string schema_name;      ///< Frontend schema/type name.
  std::string schema_encoding;  ///< Encoding of @c schema (if provided by the client).
  std::string schema;           ///< Raw schema string/binary payload (transport-specific).
};

/**
 * @struct VlinkPublish
 * @brief VLink publish destination resolved for an inbound frontend message.
 */
struct VlinkPublish final {
  std::string url;                               ///< Destination VLink URL (e.g. @c "dds://vehicle/cmd").
  std::string serialization;                     ///< Destination VLink serialisation type.
  SchemaType schema_type{SchemaType::kUnknown};  ///< Coarse backend schema family for the published payload.
};

/**
 * @class MessageConvertPlugin
 * @brief Abstract interface for VLink/webviz message conversion plugins supporting
 *        multiple visualization backends.
 *
 * @details
 * Loaded as a dynamic plugin via @c Plugin::load<MessageConvertPlugin>().
 *
 * The plugin must be thread-safe: @c convert() may be called concurrently from
 * multiple ProxyAPI data callback threads.
 *
 * A plugin may support only one target or both. Return @c false from
 * @c can_convert() for unsupported targets.
 */
class MessageConvertPlugin {
  VLINK_PLUGIN_REGISTER(MessageConvertPlugin)

 protected:
  MessageConvertPlugin() = default;

  virtual ~MessageConvertPlugin() = default;

 public:
  /**
   * @brief Initialises the plugin with an optional configuration string.
   *
   * @details
   * Called once after the plugin is loaded. The @p config parameter may contain
   * a file path, JSON string, or any format the plugin understands.
   * Returning @c false causes the plugin to be unloaded.
   *
   * @param config  Configuration string (may be empty).
   * @return @c true on success, @c false if initialisation failed.
   */
  virtual bool init(const std::string& config) = 0;

  /**
   * @brief Tests whether this plugin can handle the given VLink serialisation type
   *        for the specified target backend.
   *
   * @details
   * Called during channel/topic discovery for each new VLink type. If this returns
   * @c true, @c get_schema_info() and @c convert() will be used for that type.
   * The result may be cached by the caller.
   *
   * @param vlink_ser  VLink serialisation type name (e.g. @c "proto.VehiclePose").
   * @param target     The visualization backend requesting the conversion.
   * @return @c true if this plugin handles the type for the given target.
   */
  [[nodiscard]] virtual bool can_convert(const std::string& vlink_ser, ConvertTarget target) = 0;

  /**
   * @brief Returns schema/type metadata for the given VLink type and target backend.
   *
   * @details
   * Called once per type during channel/topic registration.
   *
   * For @c ConvertTarget::kFoxglove, the plugin must fill in the Foxglove schema name,
   * encoding type, schema encoding, and the binary schema data (typically a serialised
   * FlatBuffers binary schema @c .bfbs file content).
   *
   * For @c ConvertTarget::kRerun, the plugin should fill in:
   * - @p type_name: Rerun archetype name (e.g. @c "Points3D", @c "EncodedImage")
   * - @p encoding: @c "json" (the payload format)
   * - @p schema_encoding and @p schema_data may be left empty.
   *
   * @param[in]  vlink_ser        VLink serialisation type name.
   * @param[in]  target           The visualization backend requesting the metadata.
   * @param[out] type_name        Schema/archetype name.
   * @param[out] encoding         Wire encoding (e.g. @c "flatbuffers", @c "json").
   * @param[out] schema_encoding  Schema encoding (Foxglove only, e.g. @c "flatbuffers").
   * @param[out] schema_data      Binary schema data bytes (Foxglove only).
   * @return @c true on success.
   */
  [[nodiscard]] virtual bool get_schema_info(const std::string& vlink_ser, ConvertTarget target, std::string& type_name,
                                             std::string& encoding, std::string& schema_encoding,
                                             std::string& schema_data) = 0;

  /**
   * @brief Converts a raw VLink message to a backend-specific payload.
   *
   * @details
   * Called for every incoming message whose type was accepted by @c can_convert().
   *
   * For @c ConvertTarget::kFoxglove, the plugin should build the target Foxglove
   * FlatBuffer (or Protobuf) and write the result to @p payload using
   * @c Bytes::deep_copy() or @c Bytes::create().
   *
   * For @c ConvertTarget::kRerun, the plugin should produce a UTF-8 JSON string
   * describing the Rerun archetype components. The JSON format is documented in the
   * file-level documentation. Write the JSON bytes to @p payload.
   *
   * This method must be thread-safe.
   *
   * @param[in]  vlink_ser  VLink serialisation type name.
   * @param[in]  raw        Raw serialised message bytes from VLink.
   * @param[in]  target     The visualization backend requesting the conversion.
   * @param[out] payload    Output buffer for the converted payload.
   * @return @c true on success.
   */
  [[nodiscard]] virtual bool convert(const std::string& vlink_ser, const Bytes& raw, ConvertTarget target,
                                     Bytes& payload) = 0;

  /**
   * @brief Extracts a message-level timestamp from the raw message, in nanoseconds.
   *
   * @details
   * Called after @c convert() to obtain a per-message timestamp derived from the
   * message content (e.g. a sensor timestamp field). This allows the visualization
   * frontend to use the actual data timestamp rather than the proxy transport timestamp.
   *
   * The default implementation returns @c -1 (not available), meaning the server
   * falls back to the proxy-provided timestamp. Override this method to extract
   * timestamps from your message format.
   *
   * @param[in] vlink_ser  VLink serialisation type name.
   * @param[in] raw        Raw serialised message bytes from VLink.
   * @param[in] target     The visualization backend.
   * @return Timestamp in nanoseconds since epoch, or @c -1 if not available.
   */
  [[nodiscard]] virtual int64_t extract_timestamp(const std::string& vlink_ser, const Bytes& raw,
                                                  ConvertTarget target) {
    (void)vlink_ser;
    (void)raw;
    (void)target;
    return -1;
  }

  /**
   * @brief Tests whether this plugin handles a frontend-published channel.
   *
   * @details
   * This hook is used for inbound command/control flows such as Foxglove
   * clientPublish. The default implementation returns @c false.
   */
  [[nodiscard]] virtual bool can_convert_frontend(const WebChannel& channel, ConvertTarget target) {
    (void)channel;
    (void)target;
    return false;
  }

  /**
   * @brief Resolves the destination VLink publish URL and serialisation type for
   *        an inbound frontend channel.
   *
   * @details
   * Called when a frontend channel is advertised. Returning @c true enables the
   * host to provision the required VLink publisher(s) ahead of time.
   *
   * The default implementation returns @c false.
   */
  [[nodiscard]] virtual bool get_publish_info(const WebChannel& channel, ConvertTarget target,
                                              VlinkPublish& publish_info) {
    (void)channel;
    (void)target;
    (void)publish_info;
    return false;
  }

  /**
   * @brief Converts a frontend-published payload into a raw VLink message payload.
   *
   * @details
   * Called for each inbound frontend message after @c get_publish_info()
   * resolved the destination topic. The plugin should write the destination raw
   * VLink payload to @p payload.
   *
   * The default implementation returns @c false.
   */
  [[nodiscard]] virtual bool convert_frontend(const WebChannel& channel, const Bytes& raw, ConvertTarget target,
                                              Bytes& payload) {
    (void)channel;
    (void)raw;
    (void)target;
    (void)payload;
    return false;
  }

 private:
  VLINK_DISALLOW_COPY_AND_ASSIGN(MessageConvertPlugin)
};

}  // namespace vlink
