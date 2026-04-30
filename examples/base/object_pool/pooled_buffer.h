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
 * @file pooled_buffer.h
 * @brief A sample resource type managed by ObjectPool.
 *
 * Buffer represents a pre-allocated byte array with a "used" watermark.
 * It demonstrates the reset pattern expected by ObjectPool's reset callback.
 */

#include <vlink/base/logger.h>

#include <cstddef>
#include <cstdint>
#include <vector>

namespace pooled_buffer {

// A sample resource type managed by the pool.
// The data vector is allocated once at construction; reset() merely
// clears the watermark without releasing memory.
struct Buffer {
  std::vector<uint8_t> data;
  size_t used{0};

  explicit Buffer(size_t capacity = 1024) : data(capacity, 0) { MLOG_D("  Buffer created (capacity={})", capacity); }

  // Reset the buffer state without deallocating.
  // This is called by ObjectPool's reset callback.
  void reset() {
    used = 0;
    VLOG_D("  Buffer reset");
  }
};

}  // namespace pooled_buffer
