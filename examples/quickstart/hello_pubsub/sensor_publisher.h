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
 * @file sensor_publisher.h
 * @brief Reusable component that wraps a VLink Publisher and Timer to
 *        periodically publish temperature sensor readings.
 *
 * This header demonstrates how to encapsulate VLink primitives into a
 * self-contained class that can be composed in different applications.
 *
 * Design notes:
 *   - SensorReading is a POD struct (no default member initializers) so that
 *     VLink can use kStandardType (memcpy) serialization.
 *   - SensorPublisher owns a Publisher<SensorReading> and a Timer.
 *   - The Timer is attached to a caller-provided MessageLoop so that
 *     publish calls happen on a known thread.
 */

#pragma once

#include <vlink/base/logger.h>
#include <vlink/base/timer.h>
#include <vlink/vlink.h>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <string>

namespace example {

/// POD sensor reading -- no default member initializers for VLink kStandardType compatibility.
struct SensorReading {
  int sensor_id;         ///< Unique identifier for the sensor
  float temperature;     ///< Temperature in degrees Celsius
  int64_t timestamp_ms;  ///< Milliseconds since epoch (steady_clock)
  int sequence;          ///< Monotonically increasing sequence number
};

/**
 * @class SensorPublisher
 * @brief Encapsulates a Publisher<SensorReading> + Timer to periodically
 *        stream simulated temperature data.
 *
 * Usage:
 * @code
 *   MessageLoop loop;
 *   loop.async_run();
 *
 *   SensorPublisher sensor(1, "intra://sensor/temperature", &loop, 500);
 *   sensor.start();
 *   // ... later ...
 *   sensor.stop();
 * @endcode
 */
class SensorPublisher {
 public:
  /**
   * @brief Constructs a SensorPublisher.
   *
   * @param sensor_id    Numeric ID embedded in every published reading.
   * @param url          VLink URL (e.g. "intra://sensor/temperature").
   * @param loop         MessageLoop on which the Timer callback runs.
   * @param interval_ms  Publish interval in milliseconds.
   */
  inline SensorPublisher(int sensor_id, const std::string& url, vlink::MessageLoop* loop, int interval_ms)
      : sensor_id_(sensor_id),
        pub_(url),
        timer_(loop, interval_ms, vlink::Timer::kInfinite, [this]() { on_timer(); }) {}

  /// Starts periodic publishing.
  inline void start() {
    VLOG_I("[SensorPublisher] sensor_id=", sensor_id_, " start publishing");
    timer_.start();
  }

  /// Stops periodic publishing.
  inline void stop() {
    timer_.stop();
    VLOG_I("[SensorPublisher] sensor_id=", sensor_id_, " stopped, total published=", sequence_.load());
  }

  /// Returns the number of messages published so far.
  [[nodiscard]] inline int published_count() const { return sequence_.load(); }

 private:
  /// Timer callback -- builds a SensorReading and publishes it.
  inline void on_timer() {
    int seq = sequence_.fetch_add(1) + 1;

    SensorReading reading{};
    reading.sensor_id = sensor_id_;
    reading.temperature = base_temp_ + static_cast<float>(seq % 10) * 0.3f;
    reading.timestamp_ms = static_cast<int64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch())
            .count());
    reading.sequence = seq;

    bool ok = pub_.publish(reading);
    VLOG_I("[SensorPublisher] #", seq, " temp=", reading.temperature, " ok=", ok);
  }

  int sensor_id_;
  float base_temp_ = 22.5f;
  std::atomic<int> sequence_{0};
  vlink::Publisher<SensorReading> pub_;
  vlink::Timer timer_;
};

}  // namespace example
