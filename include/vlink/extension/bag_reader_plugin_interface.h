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
 * @file bag_reader_plugin_interface.h
 * @brief Plugin interface for custom bag reader URL/type transformation and message processing.
 *
 * @details
 * @c BagReaderPluginInterface is loaded dynamically via the @c Plugin system and bound to a
 * @c BagReader instance with @c BagReader::bind_plugin_interface().  It allows a plugin to:
 *
 * 1. **Remap URLs and serialisation types** -- @c convert_url_meta() is called for every URL
 *    found in the bag, enabling renaming or type overriding before playback begins.
 * 2. **Intercept messages** -- @c push() receives every replayed message, allowing the plugin
 *    to transform, filter, or republish them before forwarding via @c output_callback_.
 *
 * @par Example plugin implementation
 * @code
 * class MyPlugin : public vlink::BagReaderPluginInterface {
 * public:
 *     VersionInfo get_version_info() const override { return {"MyPlugin", "1.0.0", ...}; }
 *
 *     bool convert_url_meta(std::string& url, std::string& ser_type, vlink::SchemaType& schema_type) override {
 *         // Optionally remap url / ser_type / schema_type here
 *         return true;
 *     }
 *
 *     void push(int64_t ts, const std::string& url,
 *               vlink::ActionType action, const vlink::Bytes& data) override {
 *         if (output_callback_) output_callback_(ts, url, action, data);
 *     }
 * };
 * VLINK_PLUGIN_DECLARE(MyPlugin)
 * @endcode
 */

#pragma once

#include <cstdint>
#include <string>
#include <utility>

#include "../base/functional.h"
#include "../base/plugin.h"
#include "../impl/types.h"

namespace vlink {

/**
 * @class BagReaderPluginInterface
 * @brief Abstract plugin interface for custom bag reading, URL conversion, and message relay.
 *
 * @details
 * Loaded as a dynamic plugin via @c Plugin::load<BagReaderPluginInterface>().
 * Bind to a reader with @c BagReader::bind_plugin_interface().
 */
class BagReaderPluginInterface {
  VLINK_PLUGIN_REGISTER(BagReaderPluginInterface)

 protected:
  BagReaderPluginInterface() = default;

  virtual ~BagReaderPluginInterface() = default;

 public:
  /**
   * @struct VersionInfo
   * @brief Plugin version and build metadata returned by @c get_version_info().
   */
  struct VersionInfo final {
    std::string name;       ///< Plugin display name.
    std::string version;    ///< Semantic version string.
    std::string timestamp;  ///< Build timestamp.
    std::string tag;        ///< Source control tag.
    std::string commit_id;  ///< Source control commit hash.
  };

  /**
   * @brief Callback type used to forward processed messages to the @c BagReader output.
   *
   * @details
   * Stored in @c output_callback_.  Call this inside @c push() to forward a message.
   */
  using OutputCallback =
      vlink::Function<void(int64_t timestamp, const std::string& url, ActionType action_type, const Bytes& data)>;

  /**
   * @brief Returns version and build metadata for this plugin.
   *
   * @return @c VersionInfo struct with name, version, timestamp, tag, and commit ID.
   */
  [[nodiscard]] virtual VersionInfo get_version_info() const = 0;

  /**
   * @brief Registers the output callback used to forward messages after processing.
   *
   * @details
   * Called by @c BagReader::bind_plugin_interface() to inject its output pipeline.
   * The plugin must call @p output_callback_ from @c push() to deliver messages.
   *
   * @param output_callback  Callback to store in @c output_callback_.
   */
  void register_output_callback(OutputCallback&& output_callback);

  /**
   * @brief Called for each URL in the bag to allow remapping of address, serialisation type, and schema family.
   *
   * @details
   * Implementations may modify @p url, @p ser_type, and/or @p schema_type in-place.
   * Return @c true to accept the URL; return @c false to exclude it from playback.
   *
   * @param url       URL string from the bag index (may be modified).
   * @param ser_type     Serialisation type string (may be modified).
   * @param schema_type  Coarse schema family (may be modified).
   * @return @c true to include this URL in playback; @c false to exclude it.
   */
  virtual bool convert_url_meta(std::string& url, std::string& ser_type, SchemaType& schema_type) = 0;

  /**
   * @brief Called for each replayed message, allowing interception and transformation.
   *
   * @details
   * Implementations should process the message and call @c output_callback_ to forward it.
   * Dropping or delaying messages is permitted.
   *
   * @param timestamp    Message timestamp in microseconds.
   * @param url          Topic URL string.
   * @param action_type  Action type.
   * @param data         Serialized payload bytes.
   */
  virtual void push(int64_t timestamp, const std::string& url, ActionType action_type, const Bytes& data) = 0;

 protected:
  OutputCallback output_callback_;

 private:
  VLINK_DISALLOW_COPY_AND_ASSIGN(BagReaderPluginInterface)
};

////////////////////////////////////////////////////////////////
/// Details
////////////////////////////////////////////////////////////////

inline void BagReaderPluginInterface::register_output_callback(OutputCallback&& output_callback) {
  output_callback_ = std::move(output_callback);
}

}  // namespace vlink
