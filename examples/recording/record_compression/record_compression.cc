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

// Record Compression Comparison Example
// Compares None vs Zstd vs Lz4 vs Lzav compression for BagWriter.

#include <vlink/base/logger.h>
#include <vlink/extension/bag_writer.h>
#include <vlink/vlink.h>

#include <chrono>
#include <iostream>
#include <thread>

using namespace std::chrono_literals;  // NOLINT(build/namespaces, google-build-using-namespace)

// Helper to measure write throughput for a given compression type
void benchmark_compression(const std::string& label, const std::string& path,
                           vlink::BagWriter::CompressType compress_type, int compress_level, int message_count,
                           size_t message_size) {
  vlink::BagWriter::Config config;
  config.compress = compress_type;
  config.compress_level = compress_level;
  config.compress_start_size = 64;  // Only compress payloads >= 64 bytes

  auto writer = vlink::BagWriter::create(path, config);
  writer->async_run();

  // Generate test data with a repeating pattern (compressible)
  vlink::Bytes data = vlink::Bytes::create(message_size);
  for (size_t i = 0; i < message_size; ++i) {
    data[i] = static_cast<uint8_t>((i * 7 + 13) % 256);
  }

  auto start = std::chrono::steady_clock::now();

  for (int i = 0; i < message_count; ++i) {
    writer->push("dds://compression/bench", "raw", vlink::SchemaType::kRaw, vlink::ActionType::kPublish, data);
  }

  // Wait for async writes to complete
  std::this_thread::sleep_for(200ms);

  auto end = std::chrono::steady_clock::now();
  double elapsed_ms = std::chrono::duration<double, std::milli>(end - start).count();
  double throughput_mb = (static_cast<double>(message_count) * message_size) / (1024.0 * 1024.0);

  std::cout << "  " << label << ":" << std::endl;
  std::cout << "    Messages:   " << message_count << " x " << message_size << " bytes" << std::endl;
  std::cout << "    Time:       " << elapsed_ms << " ms" << std::endl;
  std::cout << "    Throughput: " << (throughput_mb / (elapsed_ms / 1000.0)) << " MB/s" << std::endl;
  std::cout << "    File:       " << path << std::endl;
}

int main() {
  constexpr int kMessageCount = 500;
  constexpr size_t kMessageSize = 4096;  // 4 KB per message

  // ======== Section 1: No Compression ========
  {
    std::cout << "\n[1] No Compression (kCompressNone)" << std::endl;
    benchmark_compression("None", "/tmp/record_compress_none.vdb", vlink::BagWriter::CompressType::kCompressNone, 0,
                          kMessageCount, kMessageSize);
  }

  // ======== Section 2: Zstd Compression ========
  {
    std::cout << "\n[2] Zstd Compression (kCompressZstd)" << std::endl;
    std::cout << "  Zstandard: good compression ratio, moderate speed." << std::endl;
    benchmark_compression("Zstd", "/tmp/record_compress_zstd.vdb", vlink::BagWriter::CompressType::kCompressZstd, 3,
                          kMessageCount, kMessageSize);
  }

  // ======== Section 3: LZ4 Compression ========
  {
    std::cout << "\n[3] LZ4 Compression (kCompressLz4)" << std::endl;
    std::cout << "  LZ4: fast compression/decompression, lower ratio." << std::endl;
    benchmark_compression("LZ4", "/tmp/record_compress_lz4.vdb", vlink::BagWriter::CompressType::kCompressLz4, 1,
                          kMessageCount, kMessageSize);
  }

  // ======== Section 4: LZAV Compression ========
  {
    std::cout << "\n[4] LZAV Compression (kCompressLzav)" << std::endl;
    std::cout << "  LZAV: fast, lightweight, built-in (no external dependency)." << std::endl;
    benchmark_compression("LZAV", "/tmp/record_compress_lzav.vdb", vlink::BagWriter::CompressType::kCompressLzav, 3,
                          kMessageCount, kMessageSize);
  }

  // ======== Section 5: Compression Configuration Details ========
  {
    std::cout << "\n[5] Compression Configuration" << std::endl;
    std::cout << "  BagWriter::Config fields:" << std::endl;
    std::cout << "    compress:            CompressType enum" << std::endl;
    std::cout << "    compress_level:      Algorithm-specific level (0-22 for Zstd)" << std::endl;
    std::cout << "    compress_start_size: Min payload size to compress (bytes)" << std::endl;
    std::cout << std::endl;
    std::cout << "  CompressType enum:" << std::endl;
    std::cout << "    kCompressNone = 0   No compression (raw bytes)" << std::endl;
    std::cout << "    kCompressAuto = 1   Auto-select best algorithm" << std::endl;
    std::cout << "    kCompressZstd = 2   Zstandard (good ratio)" << std::endl;
    std::cout << "    kCompressLz4  = 3   LZ4 (fast)" << std::endl;
    std::cout << "    kCompressLzav = 4   LZAV (built-in, lightweight)" << std::endl;
  }

  // ======== Section 6: Selection Guide ========
  {
    std::cout << "\n[6] Compression Selection Guide" << std::endl;
    std::cout << "  +----------+-------+--------+------------+-------------------------+" << std::endl;
    std::cout << "  | Type     | Ratio | Speed  | Dependency | Use Case                |" << std::endl;
    std::cout << "  +----------+-------+--------+------------+-------------------------+" << std::endl;
    std::cout << "  | None     | 1:1   | Max    | None       | Real-time, low CPU      |" << std::endl;
    std::cout << "  | LZAV     | Good  | Fast   | Built-in   | Default choice          |" << std::endl;
    std::cout << "  | LZ4      | Fair  | Fast   | liblz4     | High throughput data    |" << std::endl;
    std::cout << "  | Zstd     | Best  | Medium | libzstd    | Storage-constrained     |" << std::endl;
    std::cout << "  | Auto     | Varies| Varies | All        | Let VLink decide        |" << std::endl;
    std::cout << "  +----------+-------+--------+------------+-------------------------+" << std::endl;
  }

  VLOG_I("Record compression comparison complete.");
  VLOG_I("Check /tmp/record_compress_*.vdb for output files.");
  return 0;
}
