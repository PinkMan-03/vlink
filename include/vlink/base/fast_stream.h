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
 * @file fast_stream.h
 * @brief A high-performance @c std::ostream backed by a resizable @c std::string buffer.
 *
 * @details
 * @c FastStream is a specialised output stream designed for zero-copy, low-latency
 * log formatting inside the VLink Logger.  It inherits from @c std::ostream, so any
 * standard stream manipulator or @c operator<< overload works transparently.
 *
 * Key design points:
 * - Backed by an internal @c StringBuf that stores data in a @c std::string
 *   (avoids heap fragmentation for short messages via a pre-allocated capacity).
 * - @c take_view() returns a @c std::string_view into the internal buffer,
 *   enabling zero-copy hand-off to the logger sink without an extra copy.
 * - @c write_raw() bypasses @c std::ostream formatting and writes bytes directly
 *   into the underlying buffer, suitable for pre-formatted C-strings.
 * - Default capacity is 256 bytes; the buffer grows by doubling until it
 *   exceeds 8 KiB (@c kMaxExpandSize), after which growth is linear in
 *   @c kMaxExpandSize-sized increments.  @c shrink_to_fit() trims the
 *   vector's implementation capacity to its current backing size.
 *
 * @note
 * - @c FastStream is @b not thread-safe.  Each thread should own its own instance,
 *   which is the pattern used by the Logger (thread_local FastStream).
 * - Calling @c take_view() invalidates any previously taken view after the next
 *   write or explicit @c reset().  Do not hold views across multiple log calls.
 *
 * @par Example
 * @code
 * vlink::FastStream stream;
 * stream << "sensor_id=" << 42 << " value=" << 3.14;
 * std::string_view view = stream.take_view();  // view valid until next write
 * write_to_sink(view);
 * @endcode
 */

#pragma once

#include <ostream>
#include <string>
#include <string_view>

#include "./macros.h"

namespace vlink {

/**
 * @class FastStream
 * @brief High-performance @c std::ostream with an embedded resizable string buffer.
 *
 * @details
 * Used internally by @c Logger to accumulate a single log message and hand it
 * off to the sink as a @c std::string_view without an extra copy.  All
 * @c std::ostream formatting (hex, precision, fill, etc.) is supported.
 */
class VLINK_EXPORT FastStream : public std::ostream {
 public:
  /**
   * @brief Constructs a @c FastStream with a default initial buffer capacity.
   *
   * @details
   * The default capacity is 256 bytes.  The buffer grows automatically when
   * the message length exceeds the current capacity.
   */
  FastStream() noexcept;

  /**
   * @brief Destructor.  Releases the internal string buffer.
   */
  ~FastStream() noexcept override;

  /**
   * @brief Clears all buffered content and resets the stream error state.
   *
   * @details
   * After @c reset() the stream is ready to receive a new message.  The
   * allocated memory of the underlying buffer is retained (no deallocation).
   */
  void reset() noexcept;

  /**
   * @brief Appends the current buffered content to an existing @c std::string.
   *
   * @details
   * The internal buffer is @b not reset after this call.  Use this method when
   * the content needs to be collected into an external string incrementally.
   *
   * @param target  The string to which buffered content is appended.
   */
  void append_to(std::string& target) const noexcept;

  /**
   * @brief Returns a @c std::string_view of the current buffer contents.
   *
   * @details
   * The stream is not reset by this call.  The returned view remains valid only
   * until the next write to the stream or an explicit @c reset().  This is the
   * primary zero-copy hand-off mechanism used by the Logger.
   *
   * @warning
   * Do not store the returned view beyond the lifetime of the next stream
   * operation.  Treating it as a persistent string leads to undefined behaviour.
   *
   * @return A @c std::string_view over the internal buffer content.
   */
  std::string_view take_view();

  /**
   * @brief Returns the current number of bytes stored in the buffer.
   *
   * @return Number of bytes written since the last @c reset().
   */
  [[nodiscard]] size_t size() const noexcept;

  /**
   * @brief Returns the current allocated capacity of the internal buffer in bytes.
   *
   * @return Capacity in bytes.
   */
  [[nodiscard]] size_t capacity() const noexcept;

  /**
   * @brief Trims the internal vector capacity to the current backing size.
   *
   * @details
   * This does not shrink to the current formatted message length; the backing
   * size is kept and the write pointer is reset to the beginning.
   */
  void shrink_to_fit() noexcept;

  /**
   * @brief Writes raw bytes directly into the buffer, bypassing @c std::ostream formatting.
   *
   * @details
   * This is faster than @c std::ostream::write for pre-formatted C-strings
   * because it avoids locale and format-flag overhead.
   *
   * @param data  Pointer to the bytes to write.  Must not be null if @p len > 0.
   * @param len   Number of bytes to write.
   * @return A reference to @c *this to support chaining.
   */
  FastStream& write_raw(const char* data, size_t len);

 private:
  static constexpr size_t kDefaultCapacity{256};
  static constexpr size_t kMinCapacity{64};
  static constexpr size_t kMaxExpandSize{8192};

  class VLINK_EXPORT StringBuf final : public std::streambuf {
   public:
    explicit StringBuf(size_t initial_capacity = kDefaultCapacity);

    void reset() noexcept;

    void shrink_to_fit() noexcept;

    void append_to(std::string& target) const noexcept;

    [[nodiscard]] std::string_view take_view();

    [[nodiscard]] size_t size() const noexcept;

    [[nodiscard]] size_t capacity() const noexcept;

    [[nodiscard]] int_type overflow(int_type ch) override;

    std::streamsize xsputn(const char* s, std::streamsize n) override;

   private:
    void grow_buffer(size_t required_size);

    void advance_pptr(size_t count) noexcept;

    std::string buffer_;

    VLINK_DISALLOW_COPY_AND_ASSIGN(StringBuf)
  };

  StringBuf buf_;

  VLINK_DISALLOW_COPY_AND_ASSIGN(FastStream)
};

}  // namespace vlink
