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

#include <cstdint>
#include <type_traits>

/// A simple POD struct representing a 2D point.
/// No default initializers -- required for VLink POD serialization (kStandardType).
struct Point2D {
  float x;  // X coordinate
  float y;  // Y coordinate
};

// Verify at compile time that Point2D satisfies POD requirements
static_assert(std::is_trivial_v<Point2D>, "Point2D must be trivial");
static_assert(std::is_standard_layout_v<Point2D>, "Point2D must be standard-layout");
static_assert(sizeof(Point2D) == 8, "Point2D should be 8 bytes (2 x float)");

/// A more complex POD struct with multiple field types.
/// No default initializers -- required for VLink POD serialization (kStandardType).
struct SensorReading {
  uint32_t sensor_id;    // Unique sensor identifier
  double temperature;    // Temperature in Celsius
  double humidity;       // Relative humidity percentage
  int64_t timestamp_us;  // Timestamp in microseconds since epoch
  uint8_t status;        // Sensor status code
  uint8_t reserved[7];   // Padding to maintain alignment
};

static_assert(std::is_trivial_v<SensorReading>, "SensorReading must be trivial");
static_assert(std::is_standard_layout_v<SensorReading>, "SensorReading must be standard-layout");

/// A packed struct with fixed-size arrays (still POD).
/// No default initializers -- required for VLink POD serialization (kStandardType).
struct CanFrame {
  uint32_t id;      // CAN bus message identifier
  uint8_t dlc;      // Data Length Code (0-8)
  uint8_t flags;    // Frame flags (e.g., extended, RTR)
  uint8_t pad[2];   // Padding for alignment
  uint8_t data[8];  // CAN frame payload (up to 8 bytes)
};

static_assert(std::is_trivial_v<CanFrame>, "CanFrame must be trivial");
static_assert(std::is_standard_layout_v<CanFrame>, "CanFrame must be standard-layout");
