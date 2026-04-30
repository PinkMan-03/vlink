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
 * @file consumer.cc
 * @brief CameraFrame Zero-Copy Consumer -- Receives and validates camera frames.
 *
 * Demonstrates:
 *   - Subscribing to CameraFrame messages
 *   - Validating received frame metadata
 *   - Zero-copy deserialization (is_owner=false after operator<<)
 *   - Shallow and deep copy patterns
 *
 * Usage:
 *   Terminal 1: ./example_camera_consumer
 *   Terminal 2: ./example_camera_producer
 */

#include <vlink/base/logger.h>
#include <vlink/vlink.h>
#include <vlink/zerocopy/camera_frame.h>

#include <atomic>
#include <iostream>
#include <thread>

#include "frame_consumer.h"

using namespace std::chrono_literals;  // NOLINT(build/namespaces, google-build-using-namespace)

int main() {
  VLOG_I("=== CameraFrame Consumer ===");
  VLOG_I("Waiting for producer... Run example_camera_producer in another terminal.");

  vlink::MessageLoop loop;
  loop.set_name("camera_loop");
  loop.async_run();

  std::atomic<int> received{0};

  vlink::Subscriber<vlink::zerocopy::CameraFrame> sub("dds://zerocopy/camera");
  sub.attach(&loop);
  sub.listen([&received](const vlink::zerocopy::CameraFrame& frame) {
    received++;

    // Validate the frame
    if (!frame_consumer::validate_frame(frame)) {
      return;
    }

    // Print frame info
    frame_consumer::print_frame_info(frame);

    // Compute a simple checksum for verification
    uint32_t checksum = frame_consumer::compute_checksum(frame);
    std::cout << "    checksum=" << checksum << std::endl;
  });

  // Wait for messages (timeout after 15 seconds)
  for (int i = 0; i < 150; ++i) {
    std::this_thread::sleep_for(100ms);
    if (received >= 10) break;
  }

  VLOG_I("Total frames received: ", received.load());

  loop.quit();
  loop.wait_for_quit();

  VLOG_I("=== Consumer Complete ===");
  return 0;
}
