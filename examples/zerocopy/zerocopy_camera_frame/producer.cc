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
 * @file producer.cc
 * @brief CameraFrame Zero-Copy Producer -- Publishes camera frames via SHM/DDS.
 *
 * Demonstrates:
 *   - Creating CameraFrame with metadata (resolution, format, stream type)
 *   - Filling pixel data and setting header timestamps
 *   - Publishing frames via zerocopy-capable transport
 *
 * Usage:
 *   Terminal 1: ./example_camera_consumer
 *   Terminal 2: ./example_camera_producer
 */

#include <vlink/base/logger.h>
#include <vlink/vlink.h>
#include <vlink/zerocopy/camera_frame.h>

#include <iostream>
#include <thread>

#include "frame_producer.h"

using namespace std::chrono_literals;  // NOLINT(build/namespaces, google-build-using-namespace)

int main() {
  // ======== Create a 320x240 NV12 frame publisher ========
  // Using small resolution for the example to keep memory usage low.
  VLOG_I("=== CameraFrame Producer ===");

  vlink::Publisher<vlink::zerocopy::CameraFrame> pub("dds://zerocopy/camera");
  pub.wait_for_subscribers(5s);

  frame_producer::FrameConfig cfg;
  cfg.width = 320;
  cfg.height = 240;
  cfg.format = vlink::zerocopy::CameraFrame::kFormatNv12;
  cfg.stream = vlink::zerocopy::CameraFrame::kStreamI;
  cfg.freq = 30;
  cfg.channel = 0;

  // Publish 10 frames at ~30fps simulation
  for (uint32_t seq = 1; seq <= 10; ++seq) {
    auto frame = frame_producer::create_test_frame(cfg, seq);

    VLOG_I("Publishing frame seq=", seq, " size=", frame.size(), " bytes");
    pub.publish(frame);

    std::this_thread::sleep_for(33ms);  // ~30fps
  }

  // ======== Format reference ========
  VLOG_I("Format Reference:");
  VLOG_I("  Uncompressed: YUV420(1) YUV422(2) YUV444(3) NV12(4) NV21(5)");
  VLOG_I("  Packed YUV:   YUYV(6) YVYU(7) UYVY(8) VYUY(9)");
  VLOG_I("  RGB:          BGR888(10) RGB888Packed(11) RGB888Planar(12)");
  VLOG_I("  Compressed:   JPEG(101) H264(102) H265(103)");
  VLOG_I("  Stream Types: I-frame(1) P-frame(2) B-frame(3)");

  std::this_thread::sleep_for(500ms);
  VLOG_I("=== Producer Complete ===");
  return 0;
}
