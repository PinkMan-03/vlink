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
 * @file terminal_stream.h
 * @brief Buffered, thread-safe stdout stream with TTY detection and ANSI support.
 *
 * @details
 * Provides @c TerminalStream, a singleton stream with a 1-MiB internal buffer
 * that writes to @c stdout via direct POSIX @c write() / Win32 @c _write() calls,
 * avoiding @c std::cout and stdio overhead.
 *
 * The global convenience macro @c VLINK_TERM_OUT expands to @c vlink::TerminalStream::get().
 */

#pragma once

#include <atomic>
#include <cstdio>
#include <cstring>
#include <limits>
#include <mutex>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

#ifdef _WIN32
#include <Windows.h>
#include <io.h>
#undef min
#undef max
#undef GetMessage
#define VLINK_TERM_STDOUT_FILENO _fileno(stdout)
#define VLINK_TERM_WRITE _write
#define VLINK_TERM_ISATTY _isatty
#else
#include <termios.h>
#include <unistd.h>
#define VLINK_TERM_STDOUT_FILENO STDOUT_FILENO
#define VLINK_TERM_WRITE ::write
#define VLINK_TERM_ISATTY ::isatty
#endif

namespace vlink {

/**
 * @class TerminalStream
 * @brief Singleton buffered stdout writer with thread-safety and TTY detection.
 *
 * @details
 * Obtain the singleton with @c TerminalStream::get().  Copy and move are disabled.
 */
class TerminalStream final {
 public:
  /**
   * @brief Default internal buffer size (1 MiB).
   */
  static constexpr size_t kDefaultBufferSize{1024 * 1024 * 1};

  /**
   * @brief Type alias for stream manipulator functions.
   */
  using ManipType = TerminalStream& (*)(TerminalStream&);

  /**
   * @brief Returns the process-global @c TerminalStream singleton.
   *
   * @return Reference to the singleton instance.
   */
  static TerminalStream& get() noexcept;

  /**
   * @brief Stream manipulator that appends a newline and flushes the buffer.
   *
   * @details Usage: @c stream << TerminalStream::endl;
   *
   * @param stream  The @c TerminalStream to write to.
   * @return Reference to @p stream.
   */
  static TerminalStream& endl(TerminalStream& stream) noexcept;

  /**
   * @brief Stream manipulator that flushes the buffer without appending a newline.
   *
   * @details Usage: @c stream << TerminalStream::flush_manip;
   *
   * @param stream  The @c TerminalStream to flush.
   * @return Reference to @p stream.
   */
  static TerminalStream& flush_manip(TerminalStream& stream) noexcept;

  /**
   * @brief Constructs the stream with a @c kDefaultBufferSize internal buffer.
   */
  TerminalStream() noexcept;

  /**
   * @brief Destructor -- flushes any remaining buffered data to stdout.
   */
  ~TerminalStream() noexcept;

  TerminalStream(const TerminalStream&) noexcept = delete;

  TerminalStream& operator=(const TerminalStream&) noexcept = delete;

  TerminalStream(TerminalStream&&) noexcept = delete;

  TerminalStream& operator=(TerminalStream&&) noexcept = delete;

  /**
   * @brief Initialises the stream: detects the file descriptor and TTY status.
   *
   * @details
   * Must be called once before using @c is_tty().  On Windows, enables virtual terminal
   * processing (ANSI escape codes).  Subsequent calls are no-ops.
   */
  void init() noexcept;

  /**
   * @brief Returns @c true if stdout is connected to a terminal (TTY).
   *
   * @details
   * Only meaningful after @c init() has been called.
   *
   * @return @c true if stdout is a TTY.
   */
  [[nodiscard]] bool is_tty() const noexcept;

  /**
   * @brief Returns @c true if @c init() has been called.
   *
   * @return @c true after the first successful @c init() call.
   */
  [[nodiscard]] bool is_initialized() const noexcept;

  /**
   * @brief Flushes all buffered data to stdout immediately.
   *
   * @details
   * On non-Windows TTY outputs, also calls @c tcdrain() to wait for the terminal.
   *
   * @note This method acquires the internal mutex; safe to call concurrently.
   */
  void flush();

  /**
   * @brief Writes a raw byte array of @p len bytes to the buffer.
   *
   * @param data  Pointer to data to write.
   * @param len   Number of bytes to write.
   * @return Reference to @c *this for chaining.
   */
  TerminalStream& write_raw(const char* data, size_t len) noexcept;

  /**
   * @brief Writes a single character to the buffer.
   *
   * @param c  Character to write.
   * @return Reference to @c *this.
   */
  TerminalStream& operator<<(char c) noexcept;

  /**
   * @brief Writes a NUL-terminated C string to the buffer.
   *
   * @details Null-pointer check is performed; passing @c nullptr is a no-op.
   *
   * @param str  C string to write.
   * @return Reference to @c *this.
   */
  TerminalStream& operator<<(const char* str) noexcept;

  /**
   * @brief Writes a @c std::string to the buffer.
   *
   * @param str  String to write.
   * @return Reference to @c *this.
   */
  TerminalStream& operator<<(const std::string& str) noexcept;

  /**
   * @brief Writes a @c std::string_view to the buffer.
   *
   * @param str  String view to write.
   * @return Reference to @c *this.
   */
  TerminalStream& operator<<(std::string_view str) noexcept;

  // NOLINTBEGIN

  /**
   * @brief Writes @c "true" or @c "false".
   *
   * @return Reference to @c *this.
   */
  TerminalStream& operator<<(bool value) noexcept;

  /**
   * @brief Writes a short integer.
   *
   * @return Reference to @c *this.
   */
  TerminalStream& operator<<(short value) noexcept;

  /**
   * @brief Writes an unsigned short integer.
   *
   * @return Reference to @c *this.
   */
  TerminalStream& operator<<(unsigned short value) noexcept;

  /**
   * @brief Writes an int.
   *
   * @return Reference to @c *this.
   */
  TerminalStream& operator<<(int value) noexcept;

  /**
   * @brief Writes an unsigned int.
   *
   * @return Reference to @c *this.
   */
  TerminalStream& operator<<(unsigned int value) noexcept;

  /**
   * @brief Writes a long.
   *
   * @return Reference to @c *this.
   */
  TerminalStream& operator<<(long value) noexcept;

  /**
   * @brief Writes an unsigned long.
   *
   * @return Reference to @c *this.
   */
  TerminalStream& operator<<(unsigned long value) noexcept;

  /**
   * @brief Writes a long long.
   *
   * @return Reference to @c *this.
   */
  TerminalStream& operator<<(long long value) noexcept;

  /**
   * @brief Writes an unsigned long long.
   *
   * @return Reference to @c *this.
   */
  TerminalStream& operator<<(unsigned long long value) noexcept;

  /**
   * @brief Writes a float using @c "%g" format.
   *
   * @return Reference to @c *this.
   */
  TerminalStream& operator<<(float value) noexcept;

  /**
   * @brief Writes a double using @c "%g" format.
   *
   * @return Reference to @c *this.
   */
  TerminalStream& operator<<(double value) noexcept;

  /**
   * @brief Writes a long double using @c "%Lg" format.
   *
   * @return Reference to @c *this.
   */
  TerminalStream& operator<<(long double value) noexcept;

  /**
   * @brief Writes a pointer address as a hex string.
   *
   * @return Reference to @c *this.
   */
  TerminalStream& operator<<(const void* ptr) noexcept;

  // NOLINTEND

  /**
   * @brief Applies a @c ManipType manipulator function to the stream.
   *
   * @details
   * Compatible with @c TerminalStream::endl and @c TerminalStream::flush_manip.
   *
   * @param manip  Manipulator function pointer.
   * @return Reference to @c *this.
   */
  TerminalStream& operator<<(ManipType manip) noexcept;

  /**
   * @brief Accepts @c std::endl and similar @c std::ostream manipulators.
   *
   * @details
   * When @c std::endl is passed, writes a newline and flushes the buffer.
   *
   * @return Reference to @c *this.
   */
  TerminalStream& operator<<(std::ostream& (*)(std::ostream&)) noexcept;

 private:
  void flush_unlocked() noexcept;

  void write_to_buffer(const char* data, size_t len) noexcept;

  template <typename T>
  TerminalStream& write_integer(T value) noexcept;

  template <typename T>
  TerminalStream& write_float(T value) noexcept;

  std::atomic_bool initialized_{false};
  std::atomic_bool is_tty_{false};

  mutable std::mutex mutex_;
  std::vector<char> buffer_;
  size_t write_pos_{0};
  int fd_{VLINK_TERM_STDOUT_FILENO};
};

////////////////////////////////////////////////////////////////
/// Implementation Details
////////////////////////////////////////////////////////////////

inline TerminalStream& TerminalStream::get() noexcept {
  static TerminalStream instance;
  return instance;
}

inline TerminalStream& TerminalStream::endl(TerminalStream& stream) noexcept {
  std::lock_guard lock(stream.mutex_);
  stream.write_to_buffer("\n", 1);
  stream.flush_unlocked();
  return stream;
}

inline TerminalStream& TerminalStream::flush_manip(TerminalStream& stream) noexcept {
  std::lock_guard lock(stream.mutex_);
  stream.flush_unlocked();
  return stream;
}

inline TerminalStream::TerminalStream() noexcept : buffer_(kDefaultBufferSize) {}

inline TerminalStream::~TerminalStream() noexcept {
  std::lock_guard lock(mutex_);
  flush_unlocked();
}

inline void TerminalStream::init() noexcept {
  bool expected = false;

  if (!initialized_.compare_exchange_strong(expected, true)) {
    return;
  }

  std::lock_guard lock(mutex_);

  fd_ = VLINK_TERM_STDOUT_FILENO;

#ifdef _WIN32
  HANDLE h_out = GetStdHandle(STD_OUTPUT_HANDLE);

  if (h_out != INVALID_HANDLE_VALUE) {
    DWORD dw_mode = 0;

    if (GetConsoleMode(h_out, &dw_mode)) {
      dw_mode |= 0x0004;  // ENABLE_VIRTUAL_TERMINAL_PROCESSING
      SetConsoleMode(h_out, dw_mode);
    }
  }
#endif

  is_tty_.store(VLINK_TERM_ISATTY(fd_) != 0, std::memory_order_release);
}

inline bool TerminalStream::is_tty() const noexcept { return is_tty_.load(std::memory_order_acquire); }

inline bool TerminalStream::is_initialized() const noexcept { return initialized_.load(std::memory_order_acquire); }

inline void TerminalStream::flush() {
  std::lock_guard lock(mutex_);
  flush_unlocked();
}

inline TerminalStream& TerminalStream::write_raw(const char* data, size_t len) noexcept {
  std::lock_guard lock(mutex_);
  write_to_buffer(data, len);
  return *this;
}

inline TerminalStream& TerminalStream::operator<<(char c) noexcept {
  std::lock_guard lock(mutex_);
  write_to_buffer(&c, 1);
  return *this;
}

inline TerminalStream& TerminalStream::operator<<(const char* str) noexcept {
  if (str == nullptr) {
    return *this;
  }

  std::lock_guard lock(mutex_);
  write_to_buffer(str, std::strlen(str));
  return *this;
}

inline TerminalStream& TerminalStream::operator<<(const std::string& str) noexcept {
  std::lock_guard lock(mutex_);
  write_to_buffer(str.data(), str.size());
  return *this;
}

inline TerminalStream& TerminalStream::operator<<(std::string_view str) noexcept {
  std::lock_guard lock(mutex_);
  write_to_buffer(str.data(), str.size());
  return *this;
}

// NOLINTBEGIN

inline TerminalStream& TerminalStream::operator<<(bool value) noexcept { return *this << (value ? "true" : "false"); }

inline TerminalStream& TerminalStream::operator<<(short value) noexcept { return write_integer(value); }

inline TerminalStream& TerminalStream::operator<<(unsigned short value) noexcept { return write_integer(value); }

inline TerminalStream& TerminalStream::operator<<(int value) noexcept { return write_integer(value); }

inline TerminalStream& TerminalStream::operator<<(unsigned int value) noexcept { return write_integer(value); }

inline TerminalStream& TerminalStream::operator<<(long value) noexcept { return write_integer(value); }

inline TerminalStream& TerminalStream::operator<<(unsigned long value) noexcept { return write_integer(value); }

inline TerminalStream& TerminalStream::operator<<(long long value) noexcept { return write_integer(value); }

inline TerminalStream& TerminalStream::operator<<(unsigned long long value) noexcept { return write_integer(value); }

inline TerminalStream& TerminalStream::operator<<(float value) noexcept { return write_float(value); }

inline TerminalStream& TerminalStream::operator<<(double value) noexcept { return write_float(value); }

inline TerminalStream& TerminalStream::operator<<(long double value) noexcept { return write_float(value); }

inline TerminalStream& TerminalStream::operator<<(const void* ptr) noexcept {
  std::lock_guard lock(mutex_);

  char buf[32];

  int len = std::snprintf(buf, sizeof(buf), "%p", ptr);

  if (len > 0) {
    write_to_buffer(buf, static_cast<size_t>(len));
  }

  return *this;
}

// NOLINTEND

inline TerminalStream& TerminalStream::operator<<(ManipType manip) noexcept { return manip(*this); }

inline TerminalStream& TerminalStream::operator<<(std::ostream& (*)(std::ostream&)) noexcept {
  std::lock_guard lock(mutex_);

  write_to_buffer("\n", 1);
  flush_unlocked();

  return *this;
}

inline void TerminalStream::flush_unlocked() noexcept {
  if (write_pos_ == 0) {
    return;
  }

  const char* data = buffer_.data();
  size_t remaining = write_pos_;

  while (remaining > 0) {
    auto written = VLINK_TERM_WRITE(fd_, data, static_cast<unsigned int>(remaining));

    if (written <= 0) {
      break;
    }

    data += written;
    remaining -= static_cast<size_t>(written);
  }

  write_pos_ = 0;

#ifndef _WIN32
  if (is_tty_.load(std::memory_order_acquire)) {
    ::tcdrain(fd_);
  }
#endif
}

inline void TerminalStream::write_to_buffer(const char* data, size_t len) noexcept {
  if (len == 0) {
    return;
  }

  if (len >= buffer_.size()) {
    flush_unlocked();

    size_t remaining = len;
    while (remaining > 0) {
      auto written = VLINK_TERM_WRITE(fd_, data, static_cast<unsigned int>(remaining));

      if (written <= 0) {
        break;
      }

      data += written;
      remaining -= static_cast<size_t>(written);
    }

    return;
  }

  if (write_pos_ + len > buffer_.size()) {
    flush_unlocked();
  }

  std::memcpy(buffer_.data() + write_pos_, data, len);
  write_pos_ += len;
}

template <typename T>
inline TerminalStream& TerminalStream::write_integer(T value) noexcept {
  std::lock_guard lock(mutex_);

  char buf[32];
  char* ptr = buf + sizeof(buf);
  bool negative = false;

  if constexpr (std::is_signed_v<T>) {
    if (value < 0) {
      negative = true;

      if (value == std::numeric_limits<T>::min()) {
        int len = std::snprintf(buf, sizeof(buf), "%lld",
                                static_cast<long long>(value));  // NOLINT(runtime/int, google-runtime-int)

        if (len > 0) {
          write_to_buffer(buf, static_cast<size_t>(len));
        }

        return *this;
      }

      value = -value;
    }
  }

  if (value == 0) {
    *--ptr = '0';
  } else {
    while (value != 0) {
      *--ptr = '0' + static_cast<char>(value % 10);
      value /= 10;
    }
  }

  if (negative) {
    *--ptr = '-';
  }

  write_to_buffer(ptr, static_cast<size_t>(buf + sizeof(buf) - ptr));

  return *this;
}

template <typename T>
inline TerminalStream& TerminalStream::write_float(T value) noexcept {
  std::lock_guard lock(mutex_);

  char buf[64];
  int len;

  // NOLINTNEXTLINE(google-runtime-float)
  if constexpr (std::is_same_v<T, long double>) {
    len = std::snprintf(buf, sizeof(buf), "%Lg", value);
  } else {
    len = std::snprintf(buf, sizeof(buf), "%g", static_cast<double>(value));
  }

  if (len > 0) {
    write_to_buffer(buf, static_cast<size_t>(len));
  }

  return *this;
}

}  // namespace vlink

#ifndef VLINK_TERM_OUT
#define VLINK_TERM_OUT vlink::TerminalStream::get()
#endif
