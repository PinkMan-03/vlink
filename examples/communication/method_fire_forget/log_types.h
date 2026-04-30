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

/// POD log entry for the method_fire_forget example.
/// No default initializers -- required for VLink POD serialization (kStandardType).
struct LogEntry {
  int level;          // Log level: 0=debug, 1=info, 2=warn, 3=error
  int source_id;      // Originating module ID
  int64_t timestamp;  // Timestamp in milliseconds since epoch
};

/// POD notification command for the method_fire_forget example.
/// No default initializers -- required for VLink POD serialization (kStandardType).
struct NotifyCommand {
  int command_id;  // Unique command identifier
  int target_id;   // Target device or module ID
  int payload;     // Command-specific payload value
};
