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

// MCAP format writer and reader
#include <vlink/extension/vcap_reader.h>
#include <vlink/extension/vcap_writer.h>
// Generic BagWriter/BagReader factory (auto-detects format by extension)
#include <vlink/extension/bag_reader.h>
#include <vlink/extension/bag_writer.h>
// VLink core
#include <vlink/base/logger.h>
#include <vlink/vlink.h>

#include <atomic>
#include <thread>

using namespace std::chrono_literals;  // NOLINT(build/namespaces, google-build-using-namespace)

/// MCAP format recording example
///
/// Demonstrates:
///   1. Creating an MCAP writer via BagWriter::create() with .vcap extension
///   2. Creating an VCAPWriter explicitly
///   3. Recording with compression in MCAP format
///   4. Reading back MCAP files with VCAPReader
///   5. Split mode with MCAP files
///
/// MCAP (Message Capture Archive Protocol) is a modular, indexed binary format
/// that supports channel-level schemas, checksums, and multiple compression codecs.
/// MCAP files can be opened by Foxglove Studio and other MCAP-compatible tools.
int main() {
  // ======== MCAP via BagWriter::create() ========
  // When the path ends with .vcap or .vcapx, create() returns an VCAPWriter
  {
    VLOG_I("=== MCAP via BagWriter::create() ===");

    auto writer = vlink::BagWriter::create("/tmp/mcap_auto.vcap");
    writer->async_run();

    // Push messages -- same API as VDBWriter
    for (int i = 0; i < 100; ++i) {
      std::string payload = "mcap_message_" + std::to_string(i);
      writer->push("dds://mcap/topic_a", "raw", vlink::SchemaType::kRaw, vlink::ActionType::kPublish,
                   vlink::Bytes::from_string(payload));
    }

    VLOG_I("Auto-detected VCAPWriter: 100 messages pushed to .vcap file.");
  }

  // ======== Explicit VCAPWriter Construction ========
  {
    VLOG_I("=== Explicit VCAPWriter ===");

    vlink::BagWriter::Config config;
    config.tag_name = "mcap_explicit_test";
    config.compress = vlink::BagWriter::CompressType::kCompressZstd;  // Zstandard compression
    config.compress_level = 5;

    // Directly construct VCAPWriter instead of using the factory
    auto writer = std::make_shared<vlink::VCAPWriter>("/tmp/mcap_explicit.vcap", config);
    writer->async_run();

    // Push messages from multiple topics
    int64_t ts = 2000000LL;  // 2 seconds in microseconds
    for (int i = 0; i < 50; ++i) {
      writer->push("dds://mcap/sensor_a", "raw", vlink::SchemaType::kRaw, vlink::ActionType::kPublish,
                   vlink::Bytes::create(256), &ts);
      ts += 50000LL;  // 50ms intervals

      writer->push("dds://mcap/sensor_b", "raw", vlink::SchemaType::kRaw, vlink::ActionType::kPublish,
                   vlink::Bytes::create(128), &ts);
      ts += 50000LL;
    }

    VLOG_I("Explicit VCAPWriter: is_dumping =", writer->is_dumping());
  }

  // ======== MCAP with Split Mode ========
  {
    VLOG_I("=== MCAP Split Mode ===");

    vlink::BagWriter::Config config;
    config.split_by_size = 1024 * 50;  // Split every 50 KB
    config.split_name_by_time = true;

    auto writer = vlink::BagWriter::create("/tmp/mcap_split.vcapx", config);

    writer->register_split_callback(
        [](int split_index, const std::string& filename) { VLOG_I("MCAP split:", split_index, "->", filename); },
        false);

    writer->async_run();

    // Push enough data to trigger splits
    for (int i = 0; i < 300; ++i) {
      vlink::Bytes data = vlink::Bytes::create(512);
      writer->push("dds://mcap/split_topic", "raw", vlink::SchemaType::kRaw, vlink::ActionType::kPublish, data);
    }

    VLOG_I("Split mode: is_split_mode =", writer->is_split_mode());
    VLOG_I("Split mode: split_index =", writer->get_split_index());
  }

  // Allow writes to flush
  std::this_thread::sleep_for(500ms);

  // ======== Read MCAP via BagReader::create() ========
  {
    VLOG_I("=== Read MCAP via BagReader::create() ===");

    auto reader = vlink::BagReader::create("/tmp/mcap_auto.vcap");

    // Inspect metadata
    const auto& info = reader->get_info();
    VLOG_I("Storage type:", info.storage_type);
    VLOG_I("Message count:", info.message_count);
    VLOG_I("Duration:", info.total_duration, "ms");
    VLOG_I("URLs recorded:", info.url_metas.size());

    for (const auto& meta : info.url_metas) {
      VLOG_I("  URL:", meta.url, "count:", meta.count, "action:", static_cast<int>(meta.action_type),
             "ser:", meta.ser_type);
    }

    // Detect embedded schemas
    auto schemas = reader->detect_schema();
    VLOG_I("Embedded schemas:", schemas.size());

    // Playback
    std::atomic<int> msg_count{0};
    reader->register_output_callback(
        [&msg_count](int64_t timestamp, const std::string& url, vlink::ActionType, const vlink::Bytes& data) {
          ++msg_count;

          if (msg_count <= 3) {
            VLOG_I("  Read: ts:", timestamp, "url:", url, "size:", data.size());
          }
        });

    reader->register_finish_callback(
        [](bool interrupted) { VLOG_I("MCAP playback finished. interrupted:", interrupted); });

    reader->async_run();

    vlink::BagReader::Config config;
    config.rate = 50.0;  // Fast playback
    reader->play(config);

    std::this_thread::sleep_for(3s);
    VLOG_I("Total MCAP messages read:", msg_count.load());
  }

  // ======== Explicit VCAPReader ========
  {
    VLOG_I("=== Explicit VCAPReader ===");

    auto reader = std::make_shared<vlink::VCAPReader>("/tmp/mcap_explicit.vcap");

    const auto& info = reader->get_info();
    VLOG_I("Tag:", info.tag_name);
    VLOG_I("Compression:", info.compression_type);
    VLOG_I("Messages:", info.message_count);

    // Query serialization type for a specific URL
    std::string ser = reader->get_ser_type("dds://mcap/sensor_a");
    VLOG_I("ser_type for sensor_a:", ser);
  }

  VLOG_I("All MCAP examples complete.");

  return 0;
}
