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

// BagWriter: abstract base class for recording messages to bag files
#include <vlink/extension/bag_writer.h>
// VLink core communication API
#include <vlink/base/logger.h>
#include <vlink/vlink.h>

#include <chrono>
#include <thread>

using namespace std::chrono_literals;  // NOLINT(build/namespaces, google-build-using-namespace)

/// BagWriter manual recording example
///
/// Demonstrates direct usage of the BagWriter API:
///   1. Creating a BagWriter with Config (compression, split, WAL mode)
///   2. Manually pushing messages via push()
///   3. Split-by-size mode with a callback
///   4. Using filter_get() for shared writer access
///
/// BagWriter::create() selects the concrete implementation based on file extension:
///   - .vdb / .vdbx    -> VDBWriter (SQLite)
///   - .vcap / .vcapx  -> VCAPWriter
///   - unknown suffixes return nullptr
int main() {
  // ======== Basic BagWriter Creation ========
  // Create a BagWriter with default configuration (no compression, no splitting)
  {
    auto writer = vlink::BagWriter::create("/tmp/bag_writer_basic.vdb");
    writer->async_run();  // Start the recording event loop thread

    // Manually push messages into the bag
    for (int i = 0; i < 100; ++i) {
      std::string payload = "event_message_" + std::to_string(i);
      vlink::Bytes data = vlink::Bytes::from_string(payload);

      // push() parameters: url, ser_type, schema_type, action_type, data
      writer->push("dds://example/event", "raw", vlink::SchemaType::kRaw, vlink::ActionType::kPublish, data);
    }

    VLOG_I("Basic writer: 100 messages pushed.");
    // writer destructor flushes pending writes and stops the loop
  }

  // ======== BagWriter with Compression ========
  {
    vlink::BagWriter::Config config;
    config.compress = vlink::BagWriter::CompressType::kCompressLzav;  // Use LZAV compression
    config.compress_level = 3;                                        // Compression level
    config.compress_start_size = 64;                                  // Only compress payloads >= 64 bytes

    auto writer = vlink::BagWriter::create("/tmp/bag_writer_compressed.vdb", config);
    writer->async_run();

    // Push larger messages that will benefit from compression
    for (int i = 0; i < 50; ++i) {
      vlink::Bytes data = vlink::Bytes::create(1024);  // 1KB message
      // NOLINTNEXTLINE(modernize-loop-convert)
      for (size_t j = 0; j < data.size(); ++j) {
        data[j] = static_cast<uint8_t>(i & 0xFF);
      }
      writer->push("dds://example/large_event", "raw", vlink::SchemaType::kRaw, vlink::ActionType::kPublish, data);
    }

    VLOG_I("Compressed writer: 50 messages pushed with LZAV compression.");
  }

  // ======== BagWriter with Split-by-Size ========
  {
    vlink::BagWriter::Config config;
    config.split_by_size = 1024 * 100;  // Split every 100 KB
    config.split_name_by_time = true;   // Append timestamp to split file names
    config.compress = vlink::BagWriter::CompressType::kCompressNone;

    auto writer = vlink::BagWriter::create("/tmp/bag_writer_split.vdb", config);

    // Register a callback that fires on each file split
    writer->register_split_callback(
        [](int split_index, const std::string& split_filename) {
          VLOG_I("Split occurred! index:", split_index, "file:", split_filename);
        },
        false);  // false = callback fires AFTER the new file is opened

    writer->async_run();

    // Push enough data to trigger multiple splits
    for (int i = 0; i < 500; ++i) {
      vlink::Bytes data = vlink::Bytes::create(512);  // 512 bytes each
      writer->push("dds://example/split_event", "raw", vlink::SchemaType::kRaw, vlink::ActionType::kPublish, data);
    }

    VLOG_I("Split writer: is_split_mode =", writer->is_split_mode());
    VLOG_I("Split writer: current split_index =", writer->get_split_index());
  }

  // ======== BagWriter with WAL Mode ========
  {
    vlink::BagWriter::Config config;
    config.wal_mode = true;          // Enable SQLite WAL mode for crash resilience
    config.sync_mode = false;        // Async writes for better performance
    config.optimize_on_exit = true;  // Run VACUUM on close

    auto writer = vlink::BagWriter::create("/tmp/bag_writer_wal.vdb", config);
    writer->async_run();

    for (int i = 0; i < 200; ++i) {
      std::string payload = "wal_message_" + std::to_string(i);
      writer->push("dds://example/wal_event", "raw", vlink::SchemaType::kRaw, vlink::ActionType::kPublish,
                   vlink::Bytes::from_string(payload));
    }

    VLOG_I("WAL mode writer: 200 messages pushed.");
  }

  // ======== BagWriter with Tag Name ========
  {
    vlink::BagWriter::Config config;
    config.tag_name = "regression_test_v2";  // Tag embedded in the bag header

    auto writer = vlink::BagWriter::create("/tmp/bag_writer_tagged.vdb", config);
    writer->async_run();

    writer->push("dds://example/tagged", "text", vlink::SchemaType::kRaw, vlink::ActionType::kPublish,
                 vlink::Bytes::from_string("tagged_data"));

    VLOG_I("Tagged writer: tag_name = regression_test_v2");
  }

  // ======== BagWriter with Custom Timestamp ========
  {
    auto writer = vlink::BagWriter::create("/tmp/bag_writer_timestamp.vdb");
    writer->async_run();

    // Push a message with a custom timestamp (in microseconds)
    int64_t custom_timestamp = 1700000000000000LL;  // 2023-11-14 in microseconds
    writer->push("dds://example/timestamped", "raw", vlink::SchemaType::kRaw, vlink::ActionType::kPublish,
                 vlink::Bytes::from_string("custom_ts_data"), &custom_timestamp);

    VLOG_I("Custom timestamp writer: message pushed with explicit timestamp.");
  }

  // ======== filter_get() for Shared Access ========
  {
    // filter_get() returns an existing writer for the path, or creates and starts a new one.
    // Multiple callers sharing the same path get the same writer instance.
    auto writer1 = vlink::BagWriter::filter_get("/tmp/bag_writer_shared.vdb");
    auto writer2 = vlink::BagWriter::filter_get("/tmp/bag_writer_shared.vdb");

    // Both pointers refer to the same underlying writer
    VLOG_I("Shared writer: same instance =", (writer1.get() == writer2.get()));

    writer1->push("dds://example/shared_a", "raw", vlink::SchemaType::kRaw, vlink::ActionType::kPublish,
                  vlink::Bytes::from_string("from_writer1"));
    writer2->push("dds://example/shared_b", "raw", vlink::SchemaType::kRaw, vlink::ActionType::kPublish,
                  vlink::Bytes::from_string("from_writer2"));
  }

  // Allow final async writes to flush
  std::this_thread::sleep_for(500ms);

  VLOG_I("All BagWriter examples complete.");

  return 0;
}
