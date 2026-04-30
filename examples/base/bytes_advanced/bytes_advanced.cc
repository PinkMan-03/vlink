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
 * @file bytes_advanced.cc
 * @brief Advanced vlink::Bytes operations: compression, base64, CRC-32, hex, user input, reverse.
 *
 * Covers:
 *   - compress_data / uncompress_data / is_compress_data
 *   - encode_to_base64 / decode_from_base64
 *   - get_crc_32
 *   - convert_to_hex_str
 *   - from_user_input
 *   - reverse_order
 *   - resize / reserve / shrink_to
 */

#include <vlink/base/logger.h>

#include "compression_demo.h"
#include "encoding_demo.h"

int main() {
  VLOG_I("=== Bytes Advanced Example ===");

  // Compression and checksum demos.
  compression_demo::demo_compression();
  encoding_demo::demo_base64();
  compression_demo::demo_crc32();

  // Encoding and format demos.
  encoding_demo::demo_hex();
  encoding_demo::demo_user_input();
  encoding_demo::demo_reverse();
  encoding_demo::demo_resize();

  // Memory pool demo.
  compression_demo::demo_memory_pool();

  // Stream output demo.
  encoding_demo::demo_stream_output();

  VLOG_I("=== Bytes Advanced Example Complete ===");
  return 0;
}
