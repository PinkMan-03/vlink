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
 * @file bytes_basic.cc
 * @brief Demonstrates VLink vlink::Bytes basic operations: creation, construction, accessors.
 *
 * vlink::Bytes is VLink's primary binary data carrier.  It is a 128-byte value type
 * with Small Buffer Optimization (SBO): buffers <= 96 bytes use an inline
 * stack array, while larger buffers spill to the heap.
 *
 * This example covers:
 *   - vlink::Bytes::create() with SBO and heap paths
 *   - vlink::Bytes::from_string()
 *   - Construction from std::vector<uint8_t> and initializer_list
 *   - to_string(), size(), capacity(), offset(), is_owner(), empty(), clear()
 *   - Range-for iteration
 *   - Subscript operator
 *   - Equality comparison
 */

#include <vlink/base/logger.h>

#include "bytes_examples.h"

int main() {
  VLOG_I("=== Bytes Basic Example ===");

  bytes_examples::demo_sbo_path();
  bytes_examples::demo_heap_path();
  bytes_examples::demo_offset();
  bytes_examples::demo_from_string();
  bytes_examples::demo_from_vector();
  bytes_examples::demo_initializer_list();
  bytes_examples::demo_range_for();
  bytes_examples::demo_to_raw_data();
  bytes_examples::demo_empty_clear();
  bytes_examples::demo_equality();
  bytes_examples::demo_resize();
  bytes_examples::demo_endianness();

  VLOG_I("=== Bytes Basic Example Complete ===");
  return 0;
}
