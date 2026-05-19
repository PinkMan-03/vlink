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
 * @file helpers.h
 * @brief String, number, hash and formatting utility functions.
 *
 * @details
 * The @c Helpers namespace groups portable, stateless helper functions used throughout
 * VLink for common tasks such as string splitting, type conversion, human-readable size
 * formatting, and compile-time string matching.
 *
 * @note
 * - All functions are @c noexcept; errors are indicated by default-constructed return values.
 * - Encoding conversion functions (@c string_local_to_utf8, @c string_utf8_to_local)
 *   use the Windows ANSI code page on Windows and return the input unchanged on POSIX.
 *
 * @par Example
 * @code
 * auto parts = vlink::Helpers::get_split_string("a,b,c", ',');
 * // parts == {"a", "b", "c"}
 *
 * std::string s = vlink::Helpers::format_file_size(1536);
 * // s == "1.50KB"
 * @endcode
 */

#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "./macros.h"

namespace vlink {

/**
 * @namespace vlink::Helpers
 * @brief Stateless string, number, hash and formatting helper functions.
 */
namespace Helpers {  // NOLINT(readability-identifier-naming)

/**
 * @brief Converts a decimal string to @c int.
 *
 * @param str  Decimal integer string.
 * @param dv   Default value returned when conversion fails.  Default: 0.
 * @return Parsed integer, or @p dv on failure.
 */
[[nodiscard]] VLINK_EXPORT int to_int(const std::string& str, int dv = 0) noexcept;

/**
 * @brief Converts a string to @c int64_t, optionally ignoring trailing characters.
 *
 * @details
 * Parses via @c std::stoll with auto base detection (@c 0x prefix → hex,
 * leading @c 0 → octal, otherwise decimal).  Parsing always starts at index
 * @c 0; @p offset specifies how many @b trailing characters of @p str the
 * parser is allowed to leave unconsumed (e.g., to skip a unit suffix like
 * @c "100s" by passing @p offset @c = @c 1).  If the parser consumes a
 * different number of characters than @c str.size() @c - @p offset, the
 * function returns @p dv.
 *
 * @param str     Integer string (decimal / @c 0x-hex / leading-@c 0-octal).
 * @param dv      Default value returned when conversion fails.  Default: 0.
 * @param offset  Number of @b trailing characters of @p str to ignore.
 *                Default: 0 (the whole string must be consumed).
 * @return Parsed 64-bit integer, or @p dv on failure.
 */
[[nodiscard]] VLINK_EXPORT int64_t to_long(const std::string& str, int64_t dv = 0, int offset = 0) noexcept;

/**
 * @brief Converts a @c double to a string with a specified number of decimal places.
 *
 * @param value      Value to convert.
 * @param precision  Number of decimal places.  Default: 2.
 * @return Formatted string representation.
 */
[[nodiscard]] VLINK_EXPORT std::string double_to_string(double value, int precision = 2) noexcept;

/**
 * @brief Combines two 64-bit hash values into one using a mixing function.
 *
 * @details
 * Based on a Murmur-inspired mix to spread entropy across both inputs.
 * Suitable for building composite hash keys.
 *
 * @param a  First hash value.
 * @param b  Second hash value.
 * @return Combined hash value.
 */
[[nodiscard]] VLINK_EXPORT uint64_t hash_combine(uint64_t a, uint64_t b) noexcept;

/**
 * @brief Replaces all occurrences of @p from in @p str with @p to in-place.
 *
 * @param str   String to modify.
 * @param from  Substring to search for.
 * @param to    Replacement substring.
 */
VLINK_EXPORT void replace_string(std::string& str, const std::string& from, const std::string& to) noexcept;

/**
 * @brief Strips leading and trailing whitespace from a string.
 *
 * @param str  Input string.
 * @return Trimmed copy.
 */
[[nodiscard]] VLINK_EXPORT std::string trim_string(const std::string& str) noexcept;

/**
 * @brief Converts a UTF-8 @c std::string to a @c std::wstring.
 *
 * @param input  UTF-8 encoded string.
 * @return Wide-character string, or empty on failure.
 */
[[nodiscard]] VLINK_EXPORT std::wstring string_to_wstring(const std::string& input) noexcept;

/**
 * @brief Converts a @c std::wstring to a UTF-8 @c std::string.
 *
 * @param input  Wide-character string.
 * @return UTF-8 encoded string, or empty on failure.
 */
[[nodiscard]] VLINK_EXPORT std::string wstring_to_string(const std::wstring& input) noexcept;

/**
 * @brief Converts a locally-encoded string to UTF-8.
 *
 * @details
 * On Windows, reads the active ANSI code page and converts via Win32 APIs.
 * On POSIX, returns the input unchanged.
 *
 * @param local_str  Locally-encoded (system locale) string.
 * @return UTF-8 encoded string, or the original string if conversion is not needed.
 */
[[nodiscard]] VLINK_EXPORT std::string string_local_to_utf8(const std::string& local_str) noexcept;

/**
 * @brief Converts a UTF-8 string to the locally-encoded system string on Windows.
 *
 * @details
 * On Windows, converts via Win32 APIs to the active ANSI code page.  On POSIX,
 * returns the input unchanged.
 *
 * @param utf8_str  UTF-8 encoded string.
 * @return Locally-encoded (system locale) string.
 */
[[nodiscard]] VLINK_EXPORT std::string string_utf8_to_local(const std::string& utf8_str) noexcept;

/**
 * @brief Converts a @c std::filesystem::path to a UTF-8 @c std::string portably.
 *
 * @details
 * On Windows, @c path::string() returns the ANSI path.  This function always
 * returns a UTF-8 string regardless of platform.
 *
 * @param path  File-system path.
 * @return UTF-8 path string.
 */
[[nodiscard]] VLINK_EXPORT std::string path_to_string(const std::filesystem::path& path) noexcept;

/**
 * @brief Splits a string by a delimiter character and returns the parts as a vector.
 *
 * @param str  Input string.
 * @param f    Delimiter character.
 * @return Vector of non-empty substrings; empty fields from consecutive delimiters are skipped.
 */
[[nodiscard]] VLINK_EXPORT std::vector<std::string> get_split_string(const std::string& str, char f) noexcept;

/**
 * @brief Splits a @c string_view by a delimiter character returning non-owning views.
 *
 * @details
 * Each element is a @c string_view into the original @p str.  The caller must ensure
 * @p str outlives all returned views.
 *
 * @param str  Input string view.
 * @param f    Delimiter character.
 * @return Vector of non-empty @c string_view elements over the substrings.
 */
[[nodiscard]] VLINK_EXPORT std::vector<std::string_view> get_split_string_view(std::string_view str, char f) noexcept;

/**
 * @brief Splits a string at the first occurrence of a delimiter and returns a pair.
 *
 * @details
 * Both halves are trimmed via @c trim_string.
 * Returns @c {"", ""} if @p str is empty.
 * If the delimiter is not found, returns @c {trim(str), ""}.
 *
 * @param str  Input string.
 * @param f    Delimiter character.
 * @return Pair of the left and right substrings around the first delimiter.
 */
[[nodiscard]] VLINK_EXPORT std::pair<std::string, std::string> get_pair_string(const std::string& str, char f) noexcept;

/**
 * @brief Splits a @c string_view at the first occurrence of a delimiter returning non-owning views.
 *
 * @details
 * Each element is a @c string_view into the original @p str.  The caller must ensure
 * @p str outlives all returned views.
 * Returns @c {"", ""} if @p str is empty.
 * If the delimiter is not found, the entire string is returned as @c first with an empty @c second.
 *
 * @param str  Input string view.
 * @param f    Delimiter character.
 * @return Pair of @c string_view over the left and right substrings around the first delimiter.
 */
[[nodiscard]] VLINK_EXPORT std::pair<std::string_view, std::string_view> get_pair_string_view(std::string_view str,
                                                                                              char f) noexcept;

/**
 * @brief Escapes one text-protocol field with percent encoding.
 *
 * @details
 * Used by discovery-style colon/space delimited text frames.  The function
 * escapes @c %, space, @c :, LF and CR as uppercase @c %XX sequences while
 * leaving other bytes unchanged.
 *
 * @param value  Field value to encode.
 * @return Encoded field value safe for delimiter-based text frames.
 */
[[nodiscard]] VLINK_EXPORT std::string escape_field(std::string_view value) noexcept;

/**
 * @brief Decodes fields produced by @c escape_field().
 *
 * @details
 * Valid @c %XX sequences are decoded byte-for-byte.  Invalid or incomplete
 * percent sequences are preserved literally so older or malformed text frames
 * can still be parsed conservatively.
 *
 * @param value  Encoded field value.
 * @return Decoded field value.
 */
[[nodiscard]] VLINK_EXPORT std::string unescape_field(std::string_view value) noexcept;

/**
 * @brief Computes a 32-bit FNV-1a-style hash code for a string.
 *
 * @param str  Input string.
 * @return 32-bit hash value.
 */
[[nodiscard]] VLINK_EXPORT uint32_t get_hash_code(const std::string& str) noexcept;

/**
 * @brief Formats a duration in milliseconds as a human-readable time string.
 *
 * @details
 * Example: 125000 ms -> @c "00:02:05" or @c "00:02:05:000" when @p show_millis is @c true.
 *
 * @param milliseconds  Duration in milliseconds.
 * @param show_millis   If @c true, append the sub-second milliseconds component.
 * @return Human-readable time string.
 */
[[nodiscard]] VLINK_EXPORT std::string format_milliseconds(int64_t milliseconds, bool show_millis) noexcept;

/**
 * @brief Formats a nanosecond-precision epoch timestamp as a UTC date-time string.
 *
 * @details
 * Output format: @c "YYYY-MM-DD HH:MM:SS.mmm".
 *
 * @param nanoseconds_since_epoch  Unix timestamp in nanoseconds.
 * @return Formatted date-time string.
 */
[[nodiscard]] VLINK_EXPORT std::string format_date(int64_t nanoseconds_since_epoch) noexcept;

/**
 * @brief Formats a time difference in milliseconds as a human-readable string.
 *
 * @details
 * Produces strings such as @c "01:23:45:000" or @c "00:00:00:250".
 *
 * @param milliseconds  Time difference in milliseconds.
 * @return Human-readable time-diff string.
 */
[[nodiscard]] VLINK_EXPORT std::string format_time_diff(int32_t milliseconds) noexcept;

/**
 * @brief Formats a signed 64-bit integer as a @c "0x..." hexadecimal string.
 *
 * @param hex_number  Value to format.
 * @return Hexadecimal string prefixed with @c "0x".
 */
[[nodiscard]] VLINK_EXPORT std::string format_hex_number(int64_t hex_number) noexcept;

/**
 * @brief Formats an unsigned 64-bit integer as a @c "0x..." hexadecimal string.
 *
 * @param hex_number  Value to format.
 * @return Hexadecimal string prefixed with @c "0x".
 */
[[nodiscard]] VLINK_EXPORT std::string format_hex_number(uint64_t hex_number) noexcept;

/**
 * @brief Formats a byte count as a human-readable size string.
 *
 * @details
 * Selects the appropriate unit and formats to 2 decimal places.  Three units
 * are emitted: @c KB (always for @p size @c < @c 1 @c MiB, including small
 * sub-KiB byte counts which are rendered as fractional KB), @c MB
 * (@p size @c < @c 1 @c GiB), @c GB (everything else, no @c TB step).
 * Examples: @c 1536 → @c "1.50KB", @c 100 → @c "0.10KB", @c 5 @c GiB →
 * @c "5.00GB".
 *
 * @param size  Size in bytes.
 * @return Human-readable size string (KB / MB / GB only).
 */
[[nodiscard]] VLINK_EXPORT std::string format_file_size(size_t size) noexcept;

/**
 * @brief Formats a byte-per-second rate as a human-readable string.
 *
 * @details
 * Selects the appropriate unit (B/s, KB/s, MB/s, GB/s) and formats to 2 decimal places.
 * Example: @c 1048576 -> @c "1.00MB/s".
 *
 * @param size  Rate in bytes per second.
 * @return Human-readable rate string.
 */
[[nodiscard]] VLINK_EXPORT std::string format_rate_size(size_t size) noexcept;

/**
 * @brief Converts a date string to a Unix nanosecond timestamp.
 *
 * @details
 * Parses with the format @c "%Y/%m/%d @c %H:%M:%S" (slash-separated date,
 * colon-separated time) plus an optional @c ":<ms>" trailing field for
 * milliseconds, e.g. @c "2026/03/18 12:00:00" or
 * @c "2026/03/18 12:00:00:250".  ISO-8601 dashes (@c "2026-03-18") are
 * **not** accepted and will return @c -1.
 *
 * The string is interpreted in the **local timezone** of the calling
 * process (it is fed through @c std::mktime), so the same input yields
 * different timestamps depending on @c $TZ / system locale.
 *
 * @param date  Date string to parse.
 * @return Unix timestamp in **nanoseconds** (ns since 1970-01-01 00:00:00
 *         UTC), or @c -1 on parse failure.
 */
[[nodiscard]] VLINK_EXPORT int64_t convert_date_to_timestamp(const std::string& date) noexcept;

/**
 * @brief Compile-time check whether @p str starts with the literal @p target.
 *
 * @tparam SizeT  Size of the string literal @p target (deduced; includes null terminator).
 * @param str     String to test.
 * @param target  String literal prefix to check for.
 * @return @c true if @p str begins with @p target.
 */
template <uint8_t SizeT>
[[nodiscard]] bool has_startwith(const std::string& str, const char (&target)[SizeT]) noexcept;

/**
 * @brief Compile-time check whether @p str ends with the literal @p target.
 *
 * @tparam SizeT  Size of the string literal @p target (deduced; includes null terminator).
 * @param str     String to test.
 * @param target  String literal suffix to check for.
 * @return @c true if @p str ends with @p target.
 */
template <uint8_t SizeT>
[[nodiscard]] bool has_endwith(const std::string& str, const char (&target)[SizeT]) noexcept;

/**
 * @brief Checks whether a @c string_view starts with a given prefix (constexpr-friendly).
 *
 * @details
 * Falls back to @c std::string_view::starts_with on C++20; otherwise uses manual comparison.
 *
 * @param str     String to test.
 * @param target  Prefix to check for.
 * @return @c true if @p str starts with @p target.
 */
[[nodiscard]] constexpr bool has_startwith(std::string_view str, std::string_view target) noexcept;

/**
 * @brief Checks whether a @c string_view ends with a given suffix (constexpr-friendly).
 *
 * @details
 * Falls back to @c std::string_view::ends_with on C++20; otherwise uses manual comparison.
 *
 * @param str     String to test.
 * @param target  Suffix to check for.
 * @return @c true if @p str ends with @p target.
 */
[[nodiscard]] constexpr bool has_endwith(std::string_view str, std::string_view target) noexcept;

/**
 * @brief Checks whether a @c string_view contains a given substring.
 *
 * @param sv      String to search.
 * @param needle  Substring to find.
 * @return @c true if @p sv contains @p needle.  Returns @c true if @p needle is empty.
 */
[[nodiscard]] constexpr bool contains_substring(std::string_view sv, std::string_view needle) noexcept;

////////////////////////////////////////////////////////////////
/// Details
////////////////////////////////////////////////////////////////

template <uint8_t SizeT>
inline bool has_startwith(const std::string& str, const char (&target)[SizeT]) noexcept {
  if constexpr (SizeT == 0) {
    (void)str;
    return false;
  }

  return str.compare(0, SizeT - 1, target) == 0;
}

template <uint8_t SizeT>
inline bool has_endwith(const std::string& str, const char (&target)[SizeT]) noexcept {
  if constexpr (SizeT == 0) {
    (void)str;
    return false;
  }

  if VUNLIKELY (str.size() < SizeT - 1) {
    return false;
  }

  return str.compare(str.size() - (SizeT - 1), SizeT - 1, target) == 0;
}

inline constexpr bool has_startwith(std::string_view str, std::string_view target) noexcept {
#if __cplusplus >= 202002L
  return str.starts_with(target);
#else
  return target.size() <= str.size() && str.substr(0, target.size()) == target;
#endif
}

inline constexpr bool has_endwith(std::string_view str, std::string_view target) noexcept {
#if __cplusplus >= 202002L
  return str.ends_with(target);
#else
  return target.size() <= str.size() && str.substr(str.size() - target.size(), target.size()) == target;
#endif
}

inline constexpr bool contains_substring(std::string_view sv, std::string_view needle) noexcept {
  if (needle.empty()) {
    return true;
  }

  if (sv.size() < needle.size()) {
    return false;
  }

  for (size_t i = 0; i <= sv.size() - needle.size(); ++i) {
    bool match = true;

    for (size_t j = 0; j < needle.size(); ++j) {
      if (sv[i + j] != needle[j]) {
        match = false;

        break;
      }
    }

    if (match) {
      return true;
    }
  }

  return false;
}

}  // namespace Helpers

}  // namespace vlink
