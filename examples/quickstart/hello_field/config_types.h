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
 * @file config_types.h
 * @brief POD configuration data types shared between config_setter and config_getter.
 *
 * These types use VLink's kStandardType serialization (memcpy-based).
 * IMPORTANT: POD structs must NOT have default member initializers
 * (e.g. use `int x;` not `int x{0};`) to guarantee trivially-copyable
 * and standard-layout properties across all compilers.
 */

#pragma once

#include <cstdint>

namespace example {

/// Sensor configuration parameters -- written by Setter, read by Getter.
struct SensorConfig {
  int sample_rate_hz;      ///< Sampling rate in Hz
  int filter_window_size;  ///< Moving-average filter window
  float threshold_low;     ///< Low alarm threshold (Celsius)
  float threshold_high;    ///< High alarm threshold (Celsius)
  int64_t updated_at_ms;   ///< Timestamp of last update (steady_clock ms)
};

/// Topic URL shared by both setter and getter programs.
/// Change the transport to switch transport (e.g. "shm://", "dds://").
static const char* const kConfigUrl = "intra://sensor/config";

}  // namespace example
