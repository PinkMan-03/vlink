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

// BagWriter for creating test data, BagReader for playback
#include <vlink/extension/bag_reader.h>
#include <vlink/extension/bag_reader_processor.h>
#include <vlink/extension/bag_writer.h>
// VLink core
#include <vlink/base/logger.h>
#include <vlink/vlink.h>

#include <atomic>
#include <thread>

using namespace std::chrono_literals;  // NOLINT(build/namespaces, google-build-using-namespace)

/// BagReader playback example
///
/// First records test data to a bag file, then demonstrates:
///   1. Basic playback with output callback
///   2. Bag file info and per-URL metadata inspection
///   3. Playback configuration: rate, loop, time range, skip_blank
///   4. Status and finish callbacks
///   5. Seek/jump during playback
///   6. BagReaderProcessor for time-ordered merge from multiple readers

// Helper: create a test bag file with sample data
static void create_test_bag(const std::string& path) {
  auto writer = vlink::BagWriter::create(path);
  writer->async_run();

  int64_t ts = 1000000LL;  // Start at 1 second (microseconds)
  for (int i = 0; i < 50; ++i) {
    std::string payload = "msg_" + std::to_string(i);
    vlink::Bytes data = vlink::Bytes::from_string(payload);
    writer->push("dds://test/topic_a", "raw", vlink::SchemaType::kRaw, vlink::ActionType::kPublish, data, &ts);
    ts += 100000LL;  // 100ms intervals

    if (i % 2 == 0) {
      std::string payload_b = "data_" + std::to_string(i);
      writer->push("dds://test/topic_b", "text", vlink::SchemaType::kRaw, vlink::ActionType::kPublish,
                   vlink::Bytes::from_string(payload_b), &ts);
    }
  }

  // Wait for writes to flush, then let writer close
  std::this_thread::sleep_for(300ms);
}

int main() {
  // Step 1: Create test data
  const std::string bag_path = "/tmp/bag_reader_test.vdb";
  VLOG_I("Creating test bag file:", bag_path);
  create_test_bag(bag_path);

  // ======== Inspect Bag File Info ========
  {
    auto reader = vlink::BagReader::create(bag_path, true);  // true = read-only

    const vlink::BagReader::Info& info = reader->get_info();
    VLOG_I("=== Bag File Info ===");
    VLOG_I("File:", info.file_name);
    VLOG_I("Version:", info.version);
    VLOG_I("Storage type:", info.storage_type);
    VLOG_I("Compression:", info.compression_type);
    VLOG_I("Total duration:", info.total_duration, "ms");
    VLOG_I("Message count:", info.message_count);
    VLOG_I("File size:", info.file_size, "bytes");
    VLOG_I("Date/time:", info.date_time);
    VLOG_I("Has embedded schema:", info.has_schema);
    VLOG_I("Completed:", info.has_completed);

    // Inspect per-URL metadata
    VLOG_I("=== URL Metadata ===");
    for (const auto& url_meta : info.url_metas) {
      VLOG_I("  URL:", url_meta.url);
      VLOG_I("    action:", static_cast<int>(url_meta.action_type));
      VLOG_I("    ser_type:", url_meta.ser_type);
      VLOG_I("    count:", url_meta.count);
      VLOG_I("    size:", url_meta.size, "bytes");
      VLOG_I("    freq:", url_meta.freq, "Hz");
    }
  }

  // ======== Basic Playback ========
  {
    VLOG_I("=== Basic Playback ===");
    auto reader = vlink::BagReader::create(bag_path);
    std::atomic<int> msg_count{0};

    // Register the output callback -- invoked for each replayed message
    reader->register_output_callback([&msg_count](int64_t timestamp, const std::string& url,
                                                  vlink::ActionType action_type, const vlink::Bytes& data) {
      (void)action_type;
      ++msg_count;
      if (msg_count <= 5) {
        VLOG_I("  ts:", timestamp, "url:", url, "size:", data.size());
      }
    });

    // Register a finish callback
    reader->register_finish_callback(
        [](bool is_interrupted) { VLOG_I("Playback finished. interrupted:", is_interrupted); });

    reader->async_run();

    // Play with default configuration (1x speed, single pass)
    vlink::BagReader::Config config;
    config.rate = 1.0;
    config.times = 1;
    reader->play(config);

    // Wait for playback to complete
    std::this_thread::sleep_for(8s);
    VLOG_I("Total messages played:", msg_count.load());
  }

  // ======== Fast Playback with Rate Multiplier ========
  {
    VLOG_I("=== Fast Playback (10x speed) ===");
    auto reader = vlink::BagReader::create(bag_path);
    std::atomic<int> msg_count{0};

    reader->register_output_callback(
        [&msg_count](int64_t, const std::string&, vlink::ActionType, const vlink::Bytes&) { ++msg_count; });

    reader->async_run();

    vlink::BagReader::Config config;
    config.rate = 10.0;  // 10x playback speed
    config.times = 1;
    config.skip_blank = true;  // Skip silent gaps between messages
    reader->play(config);

    std::this_thread::sleep_for(2s);
    VLOG_I("Fast playback messages:", msg_count.load());
  }

  // ======== Loop Playback ========
  {
    VLOG_I("=== Loop Playback (3 loops at 20x) ===");
    auto reader = vlink::BagReader::create(bag_path);
    std::atomic<int> msg_count{0};

    reader->register_output_callback(
        [&msg_count](int64_t, const std::string&, vlink::ActionType, const vlink::Bytes&) { ++msg_count; });

    reader->async_run();

    vlink::BagReader::Config config;
    config.rate = 20.0;
    config.times = 3;  // Play 3 times. Use BagReader::kInfinite for endless loop.
    reader->play(config);

    std::this_thread::sleep_for(5s);
    VLOG_I("Loop playback messages:", msg_count.load());
  }

  // ======== Time Range Filtering ========
  {
    VLOG_I("=== Time Range Filtering ===");
    auto reader = vlink::BagReader::create(bag_path);
    std::atomic<int> msg_count{0};

    reader->register_output_callback(
        [&msg_count](int64_t timestamp, const std::string& url, vlink::ActionType, const vlink::Bytes&) {
          ++msg_count;
          if (msg_count <= 3) {
            VLOG_I("  ts:", timestamp, "url:", url);
          }
        });

    reader->async_run();

    vlink::BagReader::Config config;
    config.rate = 10.0;
    config.begin_time = 2000;  // Start from 2 seconds (in ms)
    config.end_time = 4000;    // End at 4 seconds (in ms)
    reader->play(config);

    std::this_thread::sleep_for(2s);
    VLOG_I("Time range messages:", msg_count.load());
  }

  // ======== URL Filtering ========
  {
    VLOG_I("=== URL Filtering ===");
    auto reader = vlink::BagReader::create(bag_path);
    std::atomic<int> msg_count{0};

    reader->register_output_callback(
        [&msg_count](int64_t, const std::string& url, vlink::ActionType, const vlink::Bytes&) {
          ++msg_count;
          if (msg_count <= 3) {
            VLOG_I("  url:", url);
          }
        });

    reader->async_run();

    vlink::BagReader::Config config;
    config.rate = 10.0;
    config.filter_urls.insert("dds://test/topic_b");  // Only play topic_b
    reader->play(config);

    std::this_thread::sleep_for(2s);
    VLOG_I("URL filtered messages:", msg_count.load());
  }

  // ======== BagReaderProcessor for Time-Ordered Merge ========
  {
    VLOG_I("=== BagReaderProcessor ===");

    vlink::BagReaderProcessor::Config proc_config;
    proc_config.min_cache_time = 200;  // Buffer 200ms before flushing
    vlink::BagReaderProcessor processor(proc_config);

    std::atomic<int> ordered_count{0};
    processor.register_output_callback(
        [&ordered_count](int64_t timestamp, const std::string& url, vlink::ActionType, const vlink::Bytes&) {
          ++ordered_count;
          if (ordered_count <= 3) {
            VLOG_I("  Processor output: ts:", timestamp, "url:", url);
          }
        });

    // Feed from a reader into the processor
    auto reader = vlink::BagReader::create(bag_path);
    reader->register_output_callback([&processor](int64_t ts, const std::string& url, vlink::ActionType action,
                                                  const vlink::Bytes& data) { processor.push(ts, url, action, data); });

    reader->async_run();

    vlink::BagReader::Config config;
    config.rate = 10.0;
    reader->play(config);

    std::this_thread::sleep_for(3s);
    VLOG_I("Processor ordered messages:", ordered_count.load());
  }

  VLOG_I("All BagReader examples complete.");

  return 0;
}
