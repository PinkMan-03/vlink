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
 * @brief Plugin contract for rewriting URLs and intercepting frames during bag playback.
 *
 * @details
 * @c BagReaderPluginInterface is a dynamic plugin loaded through the VLink @c Plugin
 * framework and attached to a @c BagReader via @c BagReader::bind_plugin_interface().
 * It customises two well-defined extension points of the playback pipeline:
 *
 * 1. @b URL/type remapping -- @c convert_url_meta() is invoked once per URL stored in
 *    the bag and may rename topics, override serialisation type and schema family,
 *    or filter URLs out of playback altogether.
 * 2. @b Frame interception -- @c push() receives every replayed message after timing
 *    pacing.  Implementations forward (or transform) the frame by calling the stored
 *    @c output_callback_; dropping or delaying a frame is permitted.
 *
 * Plugin contract:
 *
 * | Hook                       | When called                             | Mandatory action                          |
 * | -------------------------- | --------------------------------------- | ----------------------------------------- |
 * | @c get_version_info()      | After load, before first use            | Return populated @c VersionInfo           |
 * | @c register_output_callback| At bind time (called by the host)       | Store the supplied sink                   |
 * | @c convert_url_meta()      | Once per URL discovered in the bag      | Return @c true to keep, @c false to drop  |
 * | @c push()                  | Every replayed frame                    | Optionally transform, then forward        |
 *
 * Lifecycle:
 *
 * @verbatim
 *   load .so  -->  register_output_callback  -->  convert_url_meta (per URL)
 *                                                       |
 *                                                       v
 *                                                  push (per frame)  -->  output_callback_  -->  BagReader
 *                                                       |
 *                                                       v
 *                                                  destructor (on unload)
 * @endverbatim
 *
 * @par Example
 * @code
 * class MyPlugin : public vlink::BagReaderPluginInterface {
 *  public:
 *   VersionInfo get_version_info() const override {
 *     return {"my-bag-plugin", "1.0.0", __DATE__, "release", "abcd1234"};
 *   }
 *
 *   bool convert_url_meta(std::string& url, std::string& ser_type,
 *                         vlink::SchemaType& schema_type) override {
 *     if (url.rfind("dds://legacy/", 0) == 0) {
 *       url.replace(0, 14, "dds://v2/");
 *     }
 *     (void)ser_type;
 *     (void)schema_type;
 *     return true;
 *   }
 *
 *   void push(int64_t ts, const std::string& url,
 *             vlink::ActionType action, const vlink::Bytes& data) override {
 *     if (output_callback_) {
 *       output_callback_(ts, url, action, data);
 *     }
 *   }
 * };
 * VLINK_PLUGIN_DECLARE(MyPlugin, 1, 0)
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
 * @brief Abstract plugin base for bag-playback URL and frame rewriting.
 *
 * @details
 * The host binds an instance through @c BagReader::bind_plugin_interface(); after binding
 * the host invokes @c register_output_callback() to supply the forwarding sink that the
 * plugin must call from @c push().  Implementations are expected to be thread-compatible
 * with the bag reader's loop thread.
 */
class BagReaderPluginInterface {
  VLINK_PLUGIN_REGISTER(BagReaderPluginInterface)

 protected:
  BagReaderPluginInterface() = default;

  virtual ~BagReaderPluginInterface() = default;

 public:
  /**
   * @struct VersionInfo
   * @brief Build identity returned by @c get_version_info().
   */
  struct VersionInfo final {
    std::string name;       ///< Human-readable plugin display name.
    std::string version;    ///< Semantic version (e.g. @c "1.2.3").
    std::string timestamp;  ///< Build timestamp string.
    std::string tag;        ///< Source-control tag or label.
    std::string commit_id;  ///< Source-control commit hash.
  };

  /**
   * @brief Callable type used by the plugin to forward processed frames back to the reader.
   *
   * @details
   * The host stores its sink in @c output_callback_ via @c register_output_callback().
   * Implementations must invoke it from @c push() to deliver a frame downstream.
   */
  using OutputCallback =
      MoveFunction<void(int64_t timestamp, const std::string& url, ActionType action_type, const Bytes& data)>;

  /**
   * @brief Returns the build identity used by the host for logging and diagnostics.
   *
   * @return Populated @c VersionInfo describing this plugin.
   */
  [[nodiscard]] virtual VersionInfo get_version_info() const = 0;

  /**
   * @brief Stores the forwarding callback used by @c push() to deliver frames.
   *
   * @details
   * Invoked by @c BagReader::bind_plugin_interface() at attach time.  The plugin keeps
   * @p output_callback in @c output_callback_ and must call it (or deliberately drop the
   * frame) from every @c push() invocation.
   *
   * @param output_callback Sink that ultimately reaches the user @c OutputCallback.
   */
  void register_output_callback(OutputCallback&& output_callback);

  /**
   * @brief Rewrites or filters a stored URL before playback begins.
   *
   * @details
   * Called exactly once per URL contained in the bag.  Implementations may modify any of
   * the three output parameters in place to remap topics or override schema metadata.
   *
   * @param url         URL string from the bag index; may be modified in place.
   * @param ser_type    Stored serialisation type; may be modified in place.
   * @param schema_type Stored coarse schema family; may be modified in place.
   * @return @c true to retain the URL in the playback set; @c false to exclude it.
   */
  virtual bool convert_url_meta(std::string& url, std::string& ser_type, SchemaType& schema_type) = 0;

  /**
   * @brief Intercepts a replayed frame on its way to the host @c OutputCallback.
   *
   * @details
   * Implementations are free to transform, drop or buffer the frame.  Forward it
   * downstream by calling @c output_callback_; failing to call the sink causes the
   * frame to be skipped.
   *
   * @param timestamp   Frame timestamp in microseconds.
   * @param url         Already-remapped playback URL.
   * @param action_type Recorded action kind.
   * @param data        Serialised payload bytes.
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
