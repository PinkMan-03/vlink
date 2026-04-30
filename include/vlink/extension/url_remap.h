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
 * @file url_remap.h
 * @brief JSON-driven URL remapping for VLink topic address translation.
 *
 * @details
 * @c UrlRemap loads a JSON configuration file that maps source URL patterns to
 * target URL strings, enabling topic renaming at runtime without recompiling.
 *
 * The JSON format is a flat object of key/value pairs:
 * @code
 * {
 *     "intra://sensor/lidar": "dds://vehicle/lidar",
 *     "shm://camera/front":   "zenoh://camera/front"
 * }
 * @endcode
 *
 * @par Matching algorithm
 * For each call to @c convert(), the remap list is searched in order.  The first
 * entry whose key is found as a substring of the input URL is selected, and its
 * value is returned as the remapped URL.  Results are cached in an internal
 * @c unordered_map to avoid repeated linear scans.
 *
 * @par Lifecycle
 * @code
 * vlink::UrlRemap remap;
 * remap.load("/etc/vlink/remap.json");
 *
 * std::string topic = remap.convert("intra://sensor/lidar");
 * // topic == "dds://vehicle/lidar"
 *
 * remap.reload("/etc/vlink/new_remap.json");
 * @endcode
 *
 * @note
 * - @c load() returns @c false (and sets the error string) if the file does not
 *   exist, cannot be parsed, or is already loaded.
 * - @c unload() clears the remap table and the result cache.
 * - @c convert() returns the original URL unchanged if @c is_valid() is @c false
 *   or no matching rule is found.
 */

#pragma once

#include <string>
#include <unordered_map>
#include <vector>

#include "../base/macros.h"

namespace vlink {

/**
 * @class UrlRemap
 * @brief Loads a JSON remap file and translates VLink URL strings at runtime.
 *
 * @details
 * Not thread-safe.  All methods should be called from a single thread, or
 * the caller must provide external synchronisation.
 */
class VLINK_EXPORT UrlRemap {
 public:
  /**
   * @brief Constructs an empty, invalid @c UrlRemap.  No file is loaded.
   */
  UrlRemap() noexcept;

  /**
   * @brief Destructor.
   */
  ~UrlRemap() noexcept;

  /**
   * @brief Loads and parses a JSON remap configuration from @p file_path.
   *
   * @details
   * The file must be a flat JSON object with string keys and string values.
   * Calling @c load() on an already-loaded instance returns @c false without
   * modifying state (call @c unload() or @c reload() first).
   *
   * @param file_path  Absolute or relative path to the JSON file.
   * @return @c true on success; @c false if the file is missing, unreadable,
   *         or contains invalid JSON.  The error description is accessible via
   *         @c get_error_string().
   */
  bool load(const std::string& file_path) noexcept;

  /**
   * @brief Clears the remap table and marks the instance as invalid.
   *
   * @details
   * Also clears the conversion result cache.  Does nothing and returns @c false
   * if the instance is not currently loaded.
   *
   * @return @c true if the table was cleared; @c false if already unloaded.
   */
  bool unload() noexcept;

  /**
   * @brief Unloads the current configuration and loads a new one atomically.
   *
   * @details
   * Equivalent to calling @c unload() followed by @c load(@p file_path).
   *
   * @param file_path  Path to the new JSON remap file.
   * @return @c true if the new file was loaded successfully.
   */
  bool reload(const std::string& file_path) noexcept;

  /**
   * @brief Translates @p url according to the loaded remap rules.
   *
   * @details
   * Checks an internal cache first for O(1) repeated lookups.  If not cached,
   * iterates the ordered remap list and returns the first matching target.
   * The result is cached before returning.
   *
   * Returns @p url unchanged if:
   * - The remap table is not loaded (@c is_valid() is @c false).
   * - No rule matches.
   *
   * @param url  Input URL string.
   * @return Remapped URL, or the original @p url if no rule matches.
   */
  const std::string& convert(const std::string& url) noexcept;

  /**
   * @brief Enables or disables logging of each URL conversion.
   *
   * @param enable_log  If @c true, each successful remap is logged at INFO level.
   */
  void set_enable_log(bool enable_log) noexcept;

  /**
   * @brief Returns whether conversion logging is currently enabled.
   *
   * @return @c true if logging is enabled.
   */
  [[nodiscard]] bool is_enable_log() const noexcept;

  /**
   * @brief Returns whether a remap file has been successfully loaded.
   *
   * @return @c true if @c load() or @c reload() succeeded.
   */
  [[nodiscard]] bool is_valid() const noexcept;

  /**
   * @brief Returns the human-readable error description from the last failed operation.
   *
   * @details
   * Populated by @c load() on failure.  Cleared on @c unload() and on successful @c load().
   *
   * @return Error string, or an empty string if no error has occurred.
   */
  [[nodiscard]] const std::string& get_error_string() const noexcept;

 private:
  bool is_enable_log_{false};
  bool is_valid_{false};

  std::string error_string_;

  std::vector<std::pair<std::string, std::string>> remap_list_;
  std::unordered_map<std::string, std::string> cache_map_;

  VLINK_DISALLOW_COPY_AND_ASSIGN(UrlRemap)
};

////////////////////////////////////////////////////////////////
/// Details
////////////////////////////////////////////////////////////////

inline void UrlRemap::set_enable_log(bool enable_log) noexcept { is_enable_log_ = enable_log; }

inline bool UrlRemap::is_enable_log() const noexcept { return is_enable_log_; }

inline bool UrlRemap::is_valid() const noexcept { return is_valid_; }

inline const std::string& UrlRemap::get_error_string() const noexcept { return error_string_; }

}  // namespace vlink
