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

#ifndef EXAMPLES_ZEROCOPY_ZEROCOPY_CAMERA_FRAME_FRAME_PRODUCER_H_
#define EXAMPLES_ZEROCOPY_ZEROCOPY_CAMERA_FRAME_FRAME_PRODUCER_H_

#include <vlink/zerocopy/camera_frame.h>

#include <cstddef>
#include <cstdint>

namespace frame_producer {

// Camera frame configuration (POD -- no default member initializers).
struct FrameConfig {
  uint32_t width;
  uint32_t height;
  uint8_t format;
  uint8_t stream;
  uint32_t freq;
  uint8_t channel;
};

// Create a CameraFrame from a configuration and fill it with test pattern data.
// The pattern cycles through 0..255 to simulate raw pixel data.
inline vlink::zerocopy::CameraFrame create_test_frame(const FrameConfig& cfg, uint32_t seq) {
  vlink::zerocopy::CameraFrame frame;
  frame.set_width(cfg.width);
  frame.set_height(cfg.height);
  frame.set_format(static_cast<vlink::zerocopy::CameraFrame::Format>(cfg.format));
  frame.set_stream(static_cast<vlink::zerocopy::CameraFrame::Stream>(cfg.stream));
  frame.set_freq(cfg.freq);
  frame.set_channel(cfg.channel);
  frame.header.seq = seq;

  // Calculate buffer size based on format
  size_t pixel_size = 0;
  switch (cfg.format) {
    case vlink::zerocopy::CameraFrame::kFormatNv12:
    case vlink::zerocopy::CameraFrame::kFormatNv21:
      pixel_size = cfg.width * cfg.height * 3 / 2;
      break;
    case vlink::zerocopy::CameraFrame::kFormatRgb888Packed:
    case vlink::zerocopy::CameraFrame::kFormatBgr888Packed:
      pixel_size = cfg.width * cfg.height * 3;
      break;
    case vlink::zerocopy::CameraFrame::kFormatJpeg:
    case vlink::zerocopy::CameraFrame::kFormatH264:
    case vlink::zerocopy::CameraFrame::kFormatH265:
      pixel_size = cfg.width * cfg.height;  // Compressed, approximate
      break;
    default:
      pixel_size = cfg.width * cfg.height * 2;  // Default YUV422-like
      break;
  }

  frame.create(pixel_size);

  // Fill with a test pattern
  for (size_t i = 0; i < pixel_size; ++i) {
    const_cast<uint8_t*>(frame.data())[i] = static_cast<uint8_t>((seq + i) & 0xFF);
  }

  return frame;
}

// Create a standard 1080p NV12 frame configuration.
inline FrameConfig make_1080p_nv12_config() {
  FrameConfig cfg;
  cfg.width = 1920;
  cfg.height = 1080;
  cfg.format = vlink::zerocopy::CameraFrame::kFormatNv12;
  cfg.stream = vlink::zerocopy::CameraFrame::kStreamI;
  cfg.freq = 30;
  cfg.channel = 0;
  return cfg;
}

// Create a standard 720p RGB frame configuration.
inline FrameConfig make_720p_rgb_config() {
  FrameConfig cfg;
  cfg.width = 1280;
  cfg.height = 720;
  cfg.format = vlink::zerocopy::CameraFrame::kFormatRgb888Packed;
  cfg.stream = vlink::zerocopy::CameraFrame::kStreamI;
  cfg.freq = 30;
  cfg.channel = 0;
  return cfg;
}

}  // namespace frame_producer

#endif  // EXAMPLES_ZEROCOPY_ZEROCOPY_CAMERA_FRAME_FRAME_PRODUCER_H_
