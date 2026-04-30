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

/// POD translate RPC request for the method_async example.
/// No default initializers -- required for VLink POD serialization (kStandardType).
struct TranslateRequest {
  int word_id;      // Identifier of the word to translate
  int target_lang;  // Target language: 0=English, 1=Chinese, 2=Japanese
};

/// POD translate RPC response for the method_async example.
/// No default initializers -- required for VLink POD serialization (kStandardType).
struct TranslateResponse {
  int word_id;      // Identifier of the translated word
  int target_lang;  // Target language that was requested
  int result_code;  // Result: 0=success, 1=not_found
};
