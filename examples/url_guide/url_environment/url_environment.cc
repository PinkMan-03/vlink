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

#include <string>
#include <thread>

using namespace std::chrono_literals;  // NOLINT(build/namespaces, google-build-using-namespace)

/// VLink Environment Variables Comprehensive Example
///
/// This example demonstrates ALL important VLINK_ environment variables,
/// grouped by category. Each variable is shown with:
///   - Its purpose
///   - Default value
///   - How to set it programmatically (Utils::set_env) and from shell
///   - Practical usage notes
///
/// Categories:
///   1. Transport configuration (DDS, SHM, MQTT)
///   2. URL remapping
///   3. Logging
///   4. Recording (bag files)
///   5. Security (SSL/TLS)
///   6. Profiling and diagnostics
///   7. Discovery
///   8. System paths and memory

/// Helper: display an env var's current value
static void show_env(const std::string& key, const std::string& description) {
  std::string val = vlink::Utils::get_env(key);
  if (val.empty()) {
    VLOG_I("  ", key, " = (not set)  --  ", description);
  } else {
    VLOG_I("  ", key, " = ", val, "  --  ", description);
  }
}

int main() {
  // ================================================================
  //  Category 1: Transport Configuration
  // ================================================================
  {
    VLOG_I("==================================================");
    VLOG_I("  Category 1: Transport Configuration");
    VLOG_I("==================================================");

    // --- DDS ---
    // VLINK_DDS_DOMAIN: Default DDS domain ID for all dds:// and ddsc:// URLs
    //   that do not specify ?domain= explicitly.
    //   Default: 0
    //   Shell: export VLINK_DDS_DOMAIN=42
    show_env("VLINK_DDS_DOMAIN", "Default DDS domain ID");

    vlink::Utils::set_env("VLINK_DDS_DOMAIN", "42");
    VLOG_I("  -> Set VLINK_DDS_DOMAIN=42");
    show_env("VLINK_DDS_DOMAIN", "Default DDS domain ID");
    vlink::Utils::unset_env("VLINK_DDS_DOMAIN");

    // VLINK_DDS_BIND: Bind DDS discovery and data to a specific network interface IP.
    //   Essential in multi-NIC systems to prevent DDS traffic on the wrong network.
    //   Default: (not set, DDS uses all available interfaces)
    //   Shell: export VLINK_DDS_BIND=192.168.1.100
    show_env("VLINK_DDS_BIND", "Bind DDS to specific NIC IP");

    // --- MQTT ---
    // VLINK_MQTT_BROKER: Default MQTT broker URI.
    //   Default: tcp://localhost:1883
    //   Shell: export VLINK_MQTT_BROKER=tcp://mqtt.example.com:1883
    show_env("VLINK_MQTT_BROKER", "MQTT broker URI");

    // VLINK_MQTT_CLIENT_ID: MQTT client ID prefix.
    //   Default: vlink_mqtt
    //   Shell: export VLINK_MQTT_CLIENT_ID=my_robot
    show_env("VLINK_MQTT_CLIENT_ID", "MQTT client ID prefix");

    // VLINK_MQTT_QOS: Default MQTT QoS level (0, 1, or 2).
    //   Default: 1
    //   Shell: export VLINK_MQTT_QOS=2
    show_env("VLINK_MQTT_QOS", "Default MQTT QoS level");

    // VLINK_MQTT_KEEPALIVE: MQTT keep-alive interval in seconds.
    //   Default: 60
    //   Shell: export VLINK_MQTT_KEEPALIVE=30
    show_env("VLINK_MQTT_KEEPALIVE", "MQTT keep-alive seconds");

    // VLINK_MQTT_DOMAIN: Default MQTT domain ID.
    //   Default: 0
    //   Shell: export VLINK_MQTT_DOMAIN=1
    show_env("VLINK_MQTT_DOMAIN", "Default MQTT domain ID");

    // --- Intra ---
    // VLINK_INTRA_BIND: Enable intra:// observation in proxy mode.
    //   When set to "1", intra:// topics are visible to the VLink proxy.
    //   Default: (not set, disabled)
    //   Shell: export VLINK_INTRA_BIND=1
    show_env("VLINK_INTRA_BIND", "Enable intra:// proxy observation");
  }

  // ================================================================
  //  Category 2: URL Remapping
  // ================================================================
  {
    VLOG_I("==================================================");
    VLOG_I("  Category 2: URL Remapping");
    VLOG_I("==================================================");

    // VLINK_URL_REMAP: Path to the URL remap JSON configuration file.
    //   When set along with VLINK_URL_USE_REMAP=1, every URL passed to
    //   VLink node constructors is automatically remapped according to the file.
    //   Default: (not set)
    //   Shell: export VLINK_URL_REMAP=/etc/vlink/remap.json
    show_env("VLINK_URL_REMAP", "Path to remap JSON file");

    // VLINK_URL_USE_REMAP: Enable automatic URL remapping.
    //   Set to "1" to activate. Requires VLINK_URL_REMAP to be set.
    //   Default: (not set, disabled)
    //   Shell: export VLINK_URL_USE_REMAP=1
    show_env("VLINK_URL_USE_REMAP", "Enable URL remapping (1=on)");

    // Demonstration: set both variables
    vlink::Utils::set_env("VLINK_URL_REMAP", "/etc/vlink/remap.json");
    vlink::Utils::set_env("VLINK_URL_USE_REMAP", "1");
    VLOG_I("  -> Enabled URL remapping:");
    show_env("VLINK_URL_REMAP", "Path to remap JSON");
    show_env("VLINK_URL_USE_REMAP", "Remapping enabled");
    vlink::Utils::unset_env("VLINK_URL_REMAP");
    vlink::Utils::unset_env("VLINK_URL_USE_REMAP");
  }

  // ================================================================
  //  Category 3: Logging
  // ================================================================
  {
    VLOG_I("==================================================");
    VLOG_I("  Category 3: Logging");
    VLOG_I("==================================================");

    // VLINK_LOG_CONSOLE_LEVEL: Console log verbosity level.
    //   Levels: 0=Trace, 1=Debug, 2=Info, 3=Warn, 4=Error, 5=Fatal, 6=Off
    //   Default: 2 (Info)
    //   Shell: export VLINK_LOG_CONSOLE_LEVEL=0  (show all logs)
    show_env("VLINK_LOG_CONSOLE_LEVEL", "Console log level (0-6)");

    vlink::Utils::set_env("VLINK_LOG_CONSOLE_LEVEL", "1");
    VLOG_I("  -> Set VLINK_LOG_CONSOLE_LEVEL=1 (Debug level)");
    vlink::Utils::unset_env("VLINK_LOG_CONSOLE_LEVEL");

    // VLINK_LOG_DIR: Directory for log files.
    //   When set, VLink writes log files to this directory in addition to console.
    //   Default: (not set, file logging disabled)
    //   Shell: export VLINK_LOG_DIR=/var/log/vlink
    show_env("VLINK_LOG_DIR", "Log file directory");
  }

  // ================================================================
  //  Category 4: Recording (Bag Files)
  // ================================================================
  {
    VLOG_I("==================================================");
    VLOG_I("  Category 4: Recording (Bag Files)");
    VLOG_I("==================================================");

    // VLINK_BAG_PATH: Enable global recording and set the output directory.
    //   When set, all published messages are recorded to bag files at this path.
    //   Default: (not set, recording disabled)
    //   Shell: export VLINK_BAG_PATH=/data/recordings
    show_env("VLINK_BAG_PATH", "Bag file output directory");

    // VLINK_BAG_TAG: Tag string appended to recorded bag file names.
    //   Useful for identifying recording sessions.
    //   Default: (empty)
    //   Shell: export VLINK_BAG_TAG=test_run_001
    show_env("VLINK_BAG_TAG", "Bag file name tag");

    // Demonstration
    vlink::Utils::set_env("VLINK_BAG_PATH", "/data/recordings");
    vlink::Utils::set_env("VLINK_BAG_TAG", "highway_test_001");
    VLOG_I("  -> Enabled recording:");
    show_env("VLINK_BAG_PATH", "Recording path");
    show_env("VLINK_BAG_TAG", "Recording tag");
    vlink::Utils::unset_env("VLINK_BAG_PATH");
    vlink::Utils::unset_env("VLINK_BAG_TAG");
  }

  // ================================================================
  //  Category 5: Security (SSL/TLS)
  // ================================================================
  {
    VLOG_I("==================================================");
    VLOG_I("  Category 5: Security (SSL/TLS)");
    VLOG_I("==================================================");

    // VLINK_SSL_CA: Path to the Certificate Authority (CA) certificate file.
    //   Used to verify the server's TLS certificate.
    //   Default: (not set)
    //   Shell: export VLINK_SSL_CA=/etc/vlink/certs/ca.pem
    show_env("VLINK_SSL_CA", "CA certificate file path");

    // VLINK_SSL_CERT: Path to the client/server TLS certificate file.
    //   Default: (not set)
    //   Shell: export VLINK_SSL_CERT=/etc/vlink/certs/client.pem
    show_env("VLINK_SSL_CERT", "TLS certificate file path");

    // VLINK_SSL_KEY: Path to the private key file for TLS.
    //   Default: (not set)
    //   Shell: export VLINK_SSL_KEY=/etc/vlink/certs/client.key
    show_env("VLINK_SSL_KEY", "TLS private key file path");

    VLOG_I("  TLS is used for secure MQTT (ssl://), DDS security plugins, etc.");
  }

  // ================================================================
  //  Category 6: Profiling and Diagnostics
  // ================================================================
  {
    VLOG_I("==================================================");
    VLOG_I("  Category 6: Profiling and Diagnostics");
    VLOG_I("==================================================");

    // VLINK_PROFILER_ENABLE: Enable the built-in CPU profiler.
    //   When set to "1", VLink instruments key code paths for performance analysis.
    //   Default: (not set, disabled)
    //   Shell: export VLINK_PROFILER_ENABLE=1
    show_env("VLINK_PROFILER_ENABLE", "Enable CPU profiler (1=on)");

    // VLINK_QOS_CONFIG: Path to a QoS profile JSON configuration file.
    //   Allows loading QoS profiles from a file rather than registering in code.
    //   Default: (not set)
    //   Shell: export VLINK_QOS_CONFIG=/etc/vlink/qos.json
    show_env("VLINK_QOS_CONFIG", "QoS profile config file path");
  }

  // ================================================================
  //  Category 7: Discovery
  // ================================================================
  {
    VLOG_I("==================================================");
    VLOG_I("  Category 7: Discovery");
    VLOG_I("==================================================");

    // VLINK_DISCOVER_DISABLE: Disable the VLink discovery subsystem.
    //   When set to "1", nodes do not advertise themselves and do not discover peers.
    //   Useful in scenarios where discovery is handled externally or is unnecessary.
    //   Default: (not set, discovery enabled)
    //   Shell: export VLINK_DISCOVER_DISABLE=1
    show_env("VLINK_DISCOVER_DISABLE", "Disable discovery (1=disable)");

    // VLINK_PLUGIN_DIR: Directory for VLink plugin shared libraries.
    //   VLink searches this directory for transport and extension plugins.
    //   Default: (not set, uses built-in transports only)
    //   Shell: export VLINK_PLUGIN_DIR=/usr/lib/vlink/plugins
    show_env("VLINK_PLUGIN_DIR", "Plugin search directory");
  }

  // ================================================================
  //  Category 8: System Paths and Memory
  // ================================================================
  {
    VLOG_I("==================================================");
    VLOG_I("  Category 8: System Paths and Memory");
    VLOG_I("==================================================");

    // VLINK_TMP_DIR: Override the platform temporary directory.
    //   By default, VLink uses /tmp on Linux and %TEMP% on Windows.
    //   Default: (platform-specific temp dir)
    //   Shell: export VLINK_TMP_DIR=/var/run/vlink
    show_env("VLINK_TMP_DIR", "Temporary directory override");

    // VLINK_LOCK_DIR: Directory for lock files (singleton guard, etc.).
    //   Default: (same as tmp dir)
    //   Shell: export VLINK_LOCK_DIR=/var/lock/vlink
    show_env("VLINK_LOCK_DIR", "Lock file directory");

    // VLINK_MEMORY_LEVEL: Tier configuration level for vlink::MemoryPool.
    //   Integer in [0, 9]; 0 = bypass mode (every allocation goes straight to
    //   ::operator new / delete). 1..9 select the built-in pyramid; higher
    //   levels reserve more blocks per tier, trading resident footprint for
    //   fewer upstream allocations. Out-of-range or non-numeric values clamp
    //   to [0, 9].
    //   Default: 3 (Balanced)
    //   Shell: export VLINK_MEMORY_LEVEL=4
    show_env("VLINK_MEMORY_LEVEL", "MemoryPool tier level (0..9, default 3; 0 = bypass)");

    // Show the current platform temp directory
    VLOG_I("  Platform tmp dir:", vlink::Utils::get_tmp_dir());
  }

  // ================================================================
  //  Programmatic Environment Variable Management
  // ================================================================
  {
    VLOG_I("==================================================");
    VLOG_I("  Utils::get_env / set_env / unset_env");
    VLOG_I("==================================================");

    // Read an environment variable with a default value
    std::string val = vlink::Utils::get_env("MY_CUSTOM_VAR", "default_value");
    VLOG_I("  get_env(\"MY_CUSTOM_VAR\", \"default_value\") =", val);

    // Set an environment variable
    vlink::Utils::set_env("MY_CUSTOM_VAR", "hello_vlink");
    VLOG_I("  After set_env:");
    VLOG_I("  get_env(\"MY_CUSTOM_VAR\") =", vlink::Utils::get_env("MY_CUSTOM_VAR"));

    // set_env with force=false will NOT overwrite existing values
    vlink::Utils::set_env("MY_CUSTOM_VAR", "overwritten", false);
    VLOG_I("  After set_env(force=false):");
    VLOG_I("  get_env(\"MY_CUSTOM_VAR\") =", vlink::Utils::get_env("MY_CUSTOM_VAR"), " (unchanged)");

    // Unset an environment variable
    vlink::Utils::unset_env("MY_CUSTOM_VAR");
    VLOG_I("  After unset_env:");
    VLOG_I("  get_env(\"MY_CUSTOM_VAR\") =", vlink::Utils::get_env("MY_CUSTOM_VAR", "(not set)"));
  }

  // ================================================================
  //  Shell Command Reference
  // ================================================================
  {
    VLOG_I("==================================================");
    VLOG_I("  Shell Command Reference");
    VLOG_I("==================================================");

    VLOG_I("  # Transport");
    VLOG_I("  export VLINK_DDS_DOMAIN=42");
    VLOG_I("  export VLINK_DDS_BIND=192.168.1.100");
    VLOG_I("  export VLINK_MQTT_BROKER=tcp://broker:1883");
    VLOG_I("  export VLINK_MQTT_CLIENT_ID=my_app");
    VLOG_I("  export VLINK_MQTT_QOS=1");
    VLOG_I("");
    VLOG_I("  # URL Remapping");
    VLOG_I("  export VLINK_URL_REMAP=/etc/vlink/remap.json");
    VLOG_I("  export VLINK_URL_USE_REMAP=1");
    VLOG_I("");
    VLOG_I("  # Logging");
    VLOG_I("  export VLINK_LOG_CONSOLE_LEVEL=0");
    VLOG_I("  export VLINK_LOG_DIR=/var/log/vlink");
    VLOG_I("");
    VLOG_I("  # Recording");
    VLOG_I("  export VLINK_BAG_PATH=/data/recordings");
    VLOG_I("  export VLINK_BAG_TAG=test_001");
    VLOG_I("");
    VLOG_I("  # Security");
    VLOG_I("  export VLINK_SSL_CA=/etc/vlink/certs/ca.pem");
    VLOG_I("  export VLINK_SSL_CERT=/etc/vlink/certs/client.pem");
    VLOG_I("  export VLINK_SSL_KEY=/etc/vlink/certs/client.key");
    VLOG_I("");
    VLOG_I("  # Profiling");
    VLOG_I("  export VLINK_PROFILER_ENABLE=1");
    VLOG_I("  export VLINK_QOS_CONFIG=/etc/vlink/qos.json");
    VLOG_I("");
    VLOG_I("  # Discovery");
    VLOG_I("  export VLINK_DISCOVER_DISABLE=1");
    VLOG_I("  export VLINK_PLUGIN_DIR=/usr/lib/vlink/plugins");
    VLOG_I("");
    VLOG_I("  # System");
    VLOG_I("  export VLINK_TMP_DIR=/var/run/vlink");
    VLOG_I("  export VLINK_LOCK_DIR=/var/lock/vlink");
    VLOG_I("  export VLINK_MEMORY_LEVEL=3");
    VLOG_I("  export VLINK_INTRA_BIND=1");
  }

  return 0;
}
