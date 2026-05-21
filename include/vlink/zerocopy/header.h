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
 * @file header.h
 * @brief Common timestamp and sequencing metadata header for VLink zero-copy data types.
 *
 * @details
 * @c Header is a fixed-size (40-byte) POD structure that is embedded as the first public
 * member in all VLink zero-copy containers (@c RawData, @c CameraFrame, @c PointCloud).
 * It carries per-message timing and identification fields that survive binary serialisation
 * intact because the entire container struct is @c memcpy'd into the wire buffer.
 *
 * The layout is verified at compile time:
 * @code
 * static_assert(sizeof(Header) == 40, "Sizeof must be 40 bytes.");
 * @endcode
 *
 * @par Memory layout
 * @code
 * Offset  Size  Field
 * ------  ----  ----------
 *  0      16    frame_id
 * 16       4    seq
 * 20       4    reserved
 * 24       8    time_meas
 * 32       8    time_pub
 * @endcode
 *
 * @note
 * - All fields are plain data; no virtual functions, no dynamic allocation.
 * - The struct is 8-byte aligned (@c VLINK_EXPORT_AND_ALIGNED(8)) to ensure
 *   consistent padding across compilers and architectures.
 * - Other zero-copy containers emit 32-bit architecture warnings; @c Header
 *   itself only verifies its fixed size with @c static_assert.
 */

#pragma once

#include <cstdint>
#include <cstring>
#include <string_view>

#include "../base/macros.h"

namespace vlink {

namespace zerocopy {

/**
 * @struct Header
 * @brief Fixed-size (40-byte) metadata header embedded in all VLink zero-copy data types.
 *
 * @details
 * Provides per-frame sequencing and dual-timestamp fields.  Two timestamps allow
 * end-to-end latency to be measured by comparing @c time_meas (when the sensor
 * acquired the data) with @c time_pub (when the publisher dispatched it).
 */
struct VLINK_EXPORT_AND_ALIGNED(8) Header final {
  char frame_id[16]{"unknown"};  ///< Frame identifier; semantics defined by the producing sensor.
  uint32_t seq{0};               ///< Monotonically increasing sequence number, wraps at UINT32_MAX.
  uint32_t reserved{0};          ///< Reserved for future use.
  uint64_t time_meas{0};         ///< Measurement timestamp in nanoseconds since epoch.
  uint64_t time_pub{0};          ///< Publish timestamp in nanoseconds since epoch.

  /**
   * @brief Default constructor.
   *
   * @details
   * Verifies via @c static_assert that the struct is exactly 40 bytes.
   */
  Header() noexcept;

  /**
   * @brief NUL-safe view of @c frame_id.
   *
   * @details
   * @c frame_id is a fixed 16-byte buffer; producers and serialised wire data
   * may fill the entire 16 bytes without a NUL terminator.  This accessor
   * returns a @c string_view whose length is bounded by @c strnlen so that
   * consumers cannot read past the buffer end.
   */
  [[nodiscard]] std::string_view frame_id_view() const noexcept {
    return {frame_id, ::strnlen(frame_id, sizeof(frame_id))};
  }
};

}  // namespace zerocopy

}  // namespace vlink
