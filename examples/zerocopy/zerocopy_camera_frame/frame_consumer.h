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

#ifndef EXAMPLES_ZEROCOPY_ZEROCOPY_CAMERA_FRAME_FRAME_CONSUMER_H_
#define EXAMPLES_ZEROCOPY_ZEROCOPY_CAMERA_FRAME_FRAME_CONSUMER_H_

#include <vlink/zerocopy/camera_frame.h>

#include <iostream>

namespace frame_consumer {

// Print a summary of the received CameraFrame metadata.
inline void print_frame_info(const vlink::zerocopy::CameraFrame& frame) {
  std::cout << "  [Frame] seq=" << frame.header.seq << " " << frame.width() << "x" << frame.height()
            << " format=" << static_cast<int>(frame.format()) << " stream=" << static_cast<int>(frame.stream())
            << " size=" << frame.size() << " bytes" << " is_owner=" << std::boolalpha << frame.is_owner() << std::endl;
}

// Validate basic CameraFrame integrity.
// Returns true if the frame has valid dimensions and data.
inline bool validate_frame(const vlink::zerocopy::CameraFrame& frame) {
  if (!frame.is_valid()) {
    std::cout << "  [Consumer] Frame is invalid" << std::endl;
    return false;
  }
  if (frame.width() == 0 || frame.height() == 0) {
    std::cout << "  [Consumer] Frame has zero dimensions" << std::endl;
    return false;
  }
  if (frame.size() == 0) {
    std::cout << "  [Consumer] Frame has no pixel data" << std::endl;
    return false;
  }
  return true;
}

// Compute a simple checksum of the frame data (for verification).
inline uint32_t compute_checksum(const vlink::zerocopy::CameraFrame& frame) {
  uint32_t sum = 0;
  const uint8_t* data = frame.data();
  for (size_t i = 0; i < frame.size(); ++i) {
    sum += data[i];
  }
  return sum;
}

}  // namespace frame_consumer

#endif  // EXAMPLES_ZEROCOPY_ZEROCOPY_CAMERA_FRAME_FRAME_CONSUMER_H_
