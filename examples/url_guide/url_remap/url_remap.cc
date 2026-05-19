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

// VLink core communication API
#include <vlink/base/logger.h>
#include <vlink/vlink.h>

// UrlRemap: JSON-driven URL remapping for transport switching
#include <vlink/extension/url_remap.h>

#include <fstream>
#include <string>
#include <thread>

using namespace std::chrono_literals;  // NOLINT(build/namespaces, google-build-using-namespace)

/// UrlRemap Example
///
/// UrlRemap enables runtime URL translation without recompiling.
/// A JSON file maps source URL patterns to target URLs, allowing:
///   - Transport switching (intra:// -> dds://)
///   - Topic renaming
///   - Environment-specific configuration (dev/staging/production)
///
/// JSON format:
///   {
///     "source_url_1": "target_url_1",
///     "source_url_2": "target_url_2"
///   }
///
/// The matching algorithm:
///   For each convert() call, the remap list is searched in order.
///   The first entry whose key is found as a substring of the input URL
///   is selected, and its value is returned as the remapped URL.
///   Results are cached internally for O(1) repeated lookups.
///
/// Environment variables:
///   - VLINK_URL_REMAP: path to the remap JSON file. In builds with URL remap enabled,
///     this is read once when the global remapper is first used.

/// Helper: write a JSON remap file for demonstration
static void write_json_file(const std::string& path, const std::string& content) {
  std::ofstream ofs(path);
  ofs << content;
  ofs.close();
}

int main() {
  // Create temporary directory for demo JSON files
  std::string tmp_dir = vlink::Utils::get_tmp_dir();
  std::string dev_json = tmp_dir + "/vlink_remap_dev.json";
  std::string staging_json = tmp_dir + "/vlink_remap_staging.json";
  std::string prod_json = tmp_dir + "/vlink_remap_prod.json";

  // ======== Example 1: Basic UrlRemap usage ========
  // Create a JSON file, load it, and convert URLs
  {
    VLOG_I("=== Example 1: Basic UrlRemap usage ===");

    // Write a remap JSON file
    write_json_file(dev_json, R"({
  "intra://sensor/lidar": "dds://sensor/lidar",
  "intra://sensor/camera": "shm://sensor/camera",
  "intra://vehicle/speed": "zenoh://vehicle/speed"
})");

    // Create a UrlRemap instance and load the file
    vlink::UrlRemap remap;
    bool ok = remap.load(dev_json);
    VLOG_I("  load():", ok);
    VLOG_I("  is_valid():", remap.is_valid());

    // Convert URLs: if the input matches a key (substring match), return the mapped value
    const std::string& result1 = remap.convert("intra://sensor/lidar");
    VLOG_I("  intra://sensor/lidar -> ", result1);

    const std::string& result2 = remap.convert("intra://sensor/camera");
    VLOG_I("  intra://sensor/camera -> ", result2);

    const std::string& result3 = remap.convert("intra://vehicle/speed");
    VLOG_I("  intra://vehicle/speed -> ", result3);

    // Unmatched URLs are returned unchanged
    const std::string& result4 = remap.convert("intra://unknown/topic");
    VLOG_I("  intra://unknown/topic -> ", result4, " (unchanged)");
  }

  // ======== Example 2: Enable logging ========
  // set_enable_log(true) logs each conversion at INFO level
  {
    VLOG_I("=== Example 2: Enable logging ===");

    vlink::UrlRemap remap;
    remap.load(dev_json);

    remap.set_enable_log(true);
    VLOG_I("  is_enable_log():", remap.is_enable_log());

    // This conversion will also emit a log message
    remap.convert("intra://sensor/lidar");
  }

  // ======== Example 3: unload() and reload() ========
  // unload() clears the remap table; reload() atomically replaces it
  {
    VLOG_I("=== Example 3: unload() and reload() ===");

    vlink::UrlRemap remap;
    remap.load(dev_json);
    VLOG_I("  After load - is_valid():", remap.is_valid());

    // unload() clears the table
    remap.unload();
    VLOG_I("  After unload - is_valid():", remap.is_valid());

    // convert() returns the original URL when not loaded
    const std::string& result = remap.convert("intra://sensor/lidar");
    VLOG_I("  convert after unload:", result, " (unchanged)");

    // reload() unloads then loads a new file
    write_json_file(staging_json, R"({
  "intra://sensor/lidar": "zenoh://staging/sensor/lidar",
  "intra://sensor/camera": "zenoh://staging/sensor/camera"
})");

    bool ok = remap.reload(staging_json);
    VLOG_I("  After reload - is_valid():", remap.is_valid(), ", ok:", ok);

    const std::string& result2 = remap.convert("intra://sensor/lidar");
    VLOG_I("  intra://sensor/lidar -> ", result2);
  }

  // ======== Example 4: Error handling ========
  // load() returns false on error; get_error_string() provides details
  {
    VLOG_I("=== Example 4: Error handling ===");

    vlink::UrlRemap remap;

    // Try to load a non-existent file
    bool ok = remap.load("/nonexistent/path/remap.json");
    VLOG_I("  load non-existent:", ok);
    VLOG_I("  error:", remap.get_error_string());

    // Try to load again without unloading first (after a successful load)
    remap.load(dev_json);
    bool ok2 = remap.load(dev_json);  // Will fail because already loaded
    VLOG_I("  double load:", ok2);
    VLOG_I("  error:", remap.get_error_string());
  }

  // ======== Example 5: Dev/Staging/Production URL switching ========
  // A practical scenario: use different remap files for each deployment environment
  {
    VLOG_I("=== Example 5: Environment-based URL switching ===");

    // Development: use intra:// for fast local testing
    write_json_file(dev_json, R"({
  "app://sensor/lidar": "intra://sensor/lidar",
  "app://sensor/camera": "intra://sensor/camera",
  "app://vehicle/control": "intra://vehicle/control"
})");

    // Staging: use DDS for same-LAN testing
    write_json_file(staging_json, R"({
  "app://sensor/lidar": "dds://sensor/lidar?domain=10",
  "app://sensor/camera": "dds://sensor/camera?domain=10",
  "app://vehicle/control": "dds://vehicle/control?domain=10"
})");

    // Production: use Zenoh for WAN-capable deployment
    write_json_file(prod_json, R"({
  "app://sensor/lidar": "zenoh://prod/sensor/lidar?domain=0",
  "app://sensor/camera": "zenoh://prod/sensor/camera?domain=0",
  "app://vehicle/control": "zenoh://prod/vehicle/control?domain=0"
})");

    // The application always uses app:// URLs in source code.
    // The deployment environment determines which remap file is loaded.
    std::string env = "dev";  // Would come from DEPLOY_ENV or similar

    vlink::UrlRemap remap;
    if (env == "dev") {
      remap.load(dev_json);
    } else if (env == "staging") {
      remap.load(staging_json);
    } else {
      remap.load(prod_json);
    }

    const std::string& url = remap.convert("app://sensor/lidar");
    VLOG_I("  [", env, "] app://sensor/lidar -> ", url);
  }

  // ======== Example 6: VLINK_URL_REMAP env var ========
  // In builds with URL remap enabled, this environment variable enables
  // automatic URL remapping at the framework level.
  {
    VLOG_I("=== Example 6: VLINK_URL_REMAP env vars ===");

    // Enable automatic remapping by setting the remap file before constructing nodes:
    //   export VLINK_URL_REMAP=/path/to/remap.json

    vlink::Utils::set_env("VLINK_URL_REMAP", dev_json);

    VLOG_I("  VLINK_URL_REMAP=", vlink::Utils::get_env("VLINK_URL_REMAP"));

    // Now every VLink node constructor automatically remaps:
    //   Publisher<Msg> pub("intra://sensor/lidar");
    //   // internally remapped to dds://sensor/lidar if matched

    // Clean up
    vlink::Utils::unset_env("VLINK_URL_REMAP");
  }

  // ======== Example 7: Substring matching behavior ========
  // The remap uses substring matching: the first key that is a substring of the
  // input URL wins. Order matters!
  {
    VLOG_I("=== Example 7: Substring matching ===");

    std::string json_path = tmp_dir + "/vlink_remap_substring.json";
    write_json_file(json_path, R"({
  "intra://sensor": "dds://sensor_default",
  "intra://sensor/lidar": "dds://sensor_lidar"
})");

    vlink::UrlRemap remap;
    remap.load(json_path);

    // "intra://sensor" is a substring of "intra://sensor/lidar"
    // Since it appears first in the list, it matches first!
    const std::string& result = remap.convert("intra://sensor/lidar");
    VLOG_I("  intra://sensor/lidar -> ", result);
    VLOG_I("  Note: 'intra://sensor' matched first because it appears earlier in the JSON");
    VLOG_I("  To match specific URLs, place more specific rules before general ones");
  }

  // Clean up temporary files
  std::remove(dev_json.c_str());
  std::remove(staging_json.c_str());
  std::remove(prod_json.c_str());

  return 0;
}
