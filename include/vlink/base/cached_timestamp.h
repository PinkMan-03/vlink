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
 * @file cached_timestamp.h
 * @brief A low-overhead, thread-safe, formatted timestamp generator for log output.
 *
 * @details
 * @c CachedTimestamp generates a human-readable timestamp string (e.g.
 * @c "03-18 14:30:01.042") without formatting the full string every call.
 * Instead it caches the date-and-second part and only updates the millisecond
 * field on sub-second increments, significantly reducing the number of expensive
 * @c strftime / @c localtime_r calls under high log throughput.
 *
 * The caching strategy:
 * - The full timestamp (up to seconds) is formatted only once per second.
 *   The second boundary is detected via an @c std::atomic<int64_t> storing the
 *   last formatted Unix second.  Access is serialized so only one thread
 *   updates the shared cache at a time.
 * - The millisecond field is patched in-place by @c update_milliseconds()
 *   without reformatting the entire string.
 * - A @c std::mutex protects the shared buffer while formatting or patching
 *   the millisecond field.
 *
 * @note
 * - The internal buffer is 32 bytes, which is sufficient for most strftime
 *   format strings.  Using a format that produces a longer string results in
 *   truncated output.
 * - The default format @c "%02d-%02d %02d:%02d:%02d.%03d" produces
 *   @c "MM-DD HH:MM:SS.mmm" (18 characters).  Do not include year if character
 *   count matters.
 * - This class is used internally by the VLink Logger.  There is normally no need
 *   to instantiate it directly in application code.
 *
 * @par Example
 * @code
 * vlink::CachedTimestamp ts;
 * for (int i = 0; i < 1000; ++i) {
 *   std::string_view sv = ts.get();
 *   write_to_log(sv);  // sv points to internal buffer; copy if needed
 * }
 * @endcode
 */

#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#include <mutex>
#include <string_view>

#include "./macros.h"

namespace vlink {

/**
 * @class CachedTimestamp
 * @brief Cached, thread-safe formatted timestamp generator.
 *
 * @details
 * Reformats only the seconds portion of the timestamp once per second,
 * patching the milliseconds in-place for all other calls.  Used internally
 * by the Logger to stamp each log line.
 */
class VLINK_EXPORT CachedTimestamp final {
 public:
  /**
   * @brief Constructs a @c CachedTimestamp with an empty internal cache.
   *
   * @details The cache is populated on the first call to @c get().
   */
  CachedTimestamp();

  /**
   * @brief Destructor.
   */
  ~CachedTimestamp();

  /**
   * @brief Returns a @c std::string_view of the current formatted timestamp.
   *
   * @details
   * The view points into the internal 32-byte buffer.  It is valid until the
   * next call to @c get() from any thread that shares this instance.  Copy the
   * string if a persistent value is needed.
   *
   * The default format produces strings of the form @c "MM-DD HH:MM:SS.mmm".
   *
   * @param format   A @c snprintf-compatible format string consuming exactly six
   *                 @c int arguments in this order: month (1-12), day, hour, minute,
   *                 second, milliseconds (0-999).  Must end with a 3-digit milliseconds
   *                 field (e.g., @c %03d) because the cached buffer is patched
   *                 in-place at the last three characters of the formatted string.
   *                 The resulting string must fit within 31 characters.
   *                 Default: @c "%02d-%02d %02d:%02d:%02d.%03d"
   * @param use_utc  If @c true, formats in UTC; if @c false (default), in local time.
   * @return A @c std::string_view into the internal buffer with the current timestamp.
   *
   * @note
   * The returned view is invalidated on the next call from any thread that
   * holds a reference to this @c CachedTimestamp instance.
   */
  [[nodiscard]] std::string_view get(const char* format = "%02d-%02d %02d:%02d:%02d.%03d", bool use_utc = false);

 private:
  void format_full_timestamp(const char* format, std::chrono::system_clock::time_point now, bool use_utc, int ms);

  void update_milliseconds(int ms);

  alignas(64) std::atomic<int64_t> last_sec_{0};
  std::mutex mtx_;
  char buffer_[32]{};
  size_t buffer_len_{0};
  size_t ms_offset_{0};
  bool is_utc_{false};
};

}  // namespace vlink
