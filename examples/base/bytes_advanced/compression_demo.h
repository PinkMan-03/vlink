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

#pragma once

/**
 * @file compression_demo.h
 * @brief Demonstrates Bytes compression and CRC-32 checksum operations.
 *
 * VLink uses LZAV compression with magic-byte headers/footers so that
 * is_compress_data() can detect compressed buffers at runtime.
 */

#include <vlink/base/bytes.h>
#include <vlink/base/logger.h>

#include <cstring>
#include <string>

namespace compression_demo {

// Demonstrate compress_data / uncompress_data / is_compress_data.
inline void demo_compression() {
  VLOG_I("--- 1. Compression ---");

  // Create a buffer with repetitive content (compresses well).
  std::string original(500, 'A');
  for (size_t i = 0; i < original.size(); ++i) {
    original[i] = static_cast<char>('A' + (i % 26));
  }

  auto src = vlink::Bytes::from_string(original);
  VLOG_I("Original size: ", src.size());

  auto compressed = vlink::Bytes::compress_data(src.data(), src.size());
  VLOG_I("Compressed size: ", compressed.size());
  VLOG_I("Compression ratio: ", static_cast<double>(compressed.size()) / static_cast<double>(src.size()));

  VLOG_I("is_compress_data: ", vlink::Bytes::is_compress_data(compressed.data(), compressed.size()));

  auto decompressed = vlink::Bytes::uncompress_data(compressed.data(), compressed.size());
  VLOG_I("Decompressed size: ", decompressed.size());
  VLOG_I("Round-trip match: ", (decompressed.to_string() == original));

  // High-ratio compression mode (slower but better ratio).
  auto hi_compressed = vlink::Bytes::compress_data(src.data(), src.size(), true);
  VLOG_I("High-ratio compressed size: ", hi_compressed.size());
}

// Demonstrate CRC-32 checksum.
inline void demo_crc32() {
  VLOG_I("--- 3. CRC-32 ---");
  auto data = vlink::Bytes::from_string("VLink CRC test");
  uint32_t crc = vlink::Bytes::get_crc_32(data);
  MLOG_I("CRC-32: 0x{:08X}", crc);

  auto data2 = vlink::Bytes::from_string("VLink CRC test");
  VLOG_I("CRC consistency: ", (vlink::Bytes::get_crc_32(data2) == crc));

  auto data3 = vlink::Bytes::from_string("VLink CRC test!");
  VLOG_I("CRC differs: ", (vlink::Bytes::get_crc_32(data3) != crc));
}

// Demonstrate memory pool initialization.
// init_memory_pool() triggers MemoryPool::global_instance(true), so the
// shared pool's tier configuration follows VLINK_MEMORY_LEVEL (1..9).
// The pool lives for the duration of the process.
inline void demo_memory_pool() {
  VLOG_I("--- 8. Memory pool ---");

  vlink::Bytes::init_memory_pool();
  VLOG_I("Memory pool initialized (VLINK_MEMORY_LEVEL governs tier sizes)");

  auto pooled = vlink::Bytes::create(200);
  VLOG_I("Pooled allocation: size=", pooled.size());

  pooled.clear();
}

}  // namespace compression_demo
