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
 * Provides @c TerminalStream, a process-singleton output stream backed by a
 * 1-MiB internal buffer that writes to @c stdout via direct POSIX @c write() /
 * Win32 @c _write() calls, bypassing @c std::cout / stdio buffering and locale
 * overhead.  Designed for hot-path logging where @c iostream is too expensive.
 *
 * @par Buffering model
 * - Writes are appended to a fixed-size internal buffer (default @c kDefaultBufferSize = 1 MiB).
 * - When the buffer is full (or @c flush() / a flushing manipulator is invoked), the
 *   accumulated bytes are written in one or more @c write() syscalls.
 * - A single write whose length is >= buffer size flushes the existing buffer first,
 *   writes directly to stdout, and buffers any unwritten suffix if the direct write
 *   only partially completes.
 *
 * @par Thread safety
 * Every public method takes the same internal @c std::mutex, so concurrent
 * writes from multiple threads do not interleave within a single @c << call.
 * Note that long chains like @c (stream << a << b << c) acquire and release
 * the mutex per @c << operation, so other threads may interleave between them
 * unless callers wrap the chain in an external lock.
 *
 * @par TTY detection and ANSI
 * @c init() detects whether @c stdout is connected to a terminal and, on
 * Windows, enables virtual-terminal processing so ANSI colour escape codes
 * are interpreted.  Call @c init() once at program start before relying on
 * @c is_tty() or before emitting colour codes.
 *
 * @par Singleton access
 * @c TerminalStream::get() returns the per-process singleton.  The convenience
 * macro @c VLINK_TERM_OUT expands to @c vlink::TerminalStream::get().
 *
 * @par Example
 * @code
 * vlink::TerminalStream::get().init();
 *
 * VLINK_TERM_OUT << "hello " << 42 << " " << 3.14 << vlink::TerminalStream::endl;
 *
 * // Equivalent:
 * auto& ts = vlink::TerminalStream::get();
 * ts << "x=" << static_cast<long long>(123456789) << '\n';
 * ts.flush();
 * @endcode
 */

#pragma once

#include <atomic>
#include <cstddef>
#include <iosfwd>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

#include "../base/macros.h"

namespace vlink {

/**
 * @class TerminalStream
 * @brief Singleton buffered stdout writer with thread-safety and TTY detection.
 *
 * @details
 * Obtain the singleton with @c TerminalStream::get() (or the @c VLINK_TERM_OUT
 * macro).  Copy and move are deleted -- the class is non-instantiable outside
 * of the singleton path.
 *
 * Internally holds:
 * - A fixed-size @c std::vector<char> buffer (@c kDefaultBufferSize bytes by default).
 * - A write cursor @c write_pos_ into the buffer.
 * - A @c std::mutex guarding the buffer and cursor.
 * - Two @c std::atomic_bool flags: @c initialized_ (set by @c init()) and @c is_tty_
 *   (set when @c init() detects a TTY).
 * - The underlying file descriptor (stdout by default).
 */
class VLINK_EXPORT TerminalStream final {
 public:
  /**
   * @brief Default internal buffer size (1 MiB).
   *
   * @details
   * Writes shorter than this fit into the buffer and are batched until @c flush() / a
   * flushing manipulator runs or the buffer fills up.  Writes longer than this bypass
   * the buffer and go directly to @c stdout.
   */
  static constexpr size_t kDefaultBufferSize{1024 * 1024 * 1};

  /**
   * @brief Type alias for stream manipulator functions.
   *
   * @details
   * Any free / static function with signature
   * @c TerminalStream& fn(TerminalStream&) may be passed to @c operator<<.
   * Built-in manipulators include @c TerminalStream::endl and
   * @c TerminalStream::flush_manip.
   */
  using ManipType = TerminalStream& (*)(TerminalStream&);

  /**
   * @brief Returns the process-global @c TerminalStream singleton.
   *
   * @details
   * The singleton is constructed on first call (Meyers singleton).  Its
   * destructor runs at program exit and flushes any unwritten buffered
   * data to @c stdout.  Safe to call concurrently.
   *
   * @return Reference to the singleton instance.
   */
  static TerminalStream& get() noexcept;

  /**
   * @brief Stream manipulator that appends a newline and flushes the buffer.
   *
   * @details
   * Equivalent to writing @c "\n" followed by @c flush().  Used with
   * @c operator<<(ManipType) to terminate a line and force immediate output.
   *
   * @par Example
   * @code
   * stream << "ready" << TerminalStream::endl;
   * @endcode
   *
   * @param stream  The @c TerminalStream to write to.
   * @return Reference to @p stream for chaining.
   */
  static TerminalStream& endl(TerminalStream& stream) noexcept;

  /**
   * @brief Stream manipulator that flushes the buffer without appending a newline.
   *
   * @details
   * Forces immediate write of all currently buffered bytes to @c stdout.
   * Unlike @c endl, no newline is added.
   *
   * @par Example
   * @code
   * stream << "[progress] " << TerminalStream::flush_manip;
   * @endcode
   *
   * @param stream  The @c TerminalStream to flush.
   * @return Reference to @p stream for chaining.
   */
  static TerminalStream& flush_manip(TerminalStream& stream) noexcept;

  /**
   * @brief Constructs the stream with a @c kDefaultBufferSize internal buffer.
   *
   * @details
   * Allocates 1 MiB for the buffer up-front; no further allocation occurs
   * during normal operation.  The file descriptor is initialised to the
   * platform's @c stdout fd (@c STDOUT_FILENO on POSIX, @c _fileno(stdout)
   * on Windows).  @c init() must be called separately to detect TTY status
   * and to enable Windows ANSI processing.
   */
  TerminalStream() noexcept;

  /**
   * @brief Destructor -- flushes any remaining buffered data to stdout.
   *
   * @details
   * Acquires the internal mutex and calls the unlocked flush path so any
   * pending bytes are written out before the process exits.  Called
   * automatically at program shutdown for the singleton instance.
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
   * Performs three actions, guarded by an atomic flag so only the first call
   * has effect:
   * - Sets @c fd_ to the platform stdout descriptor.
   * - On Windows, fetches @c STD_OUTPUT_HANDLE and enables the
   *   @c ENABLE_VIRTUAL_TERMINAL_PROCESSING console mode so ANSI escape codes
   *   (colours, cursor movement) are interpreted by the console host.
   * - Caches @c isatty() result in @c is_tty_ for fast queries.
   *
   * Idempotent: second and later calls return immediately without re-running
   * setup.  Safe to call from multiple threads concurrently; only one thread
   * will execute the body.
   *
   * Must be called once before relying on @c is_tty() or emitting ANSI
   * sequences on Windows.  Plain ASCII writes work without calling @c init().
   */
  void init() noexcept;

  /**
   * @brief Returns @c true if stdout is connected to a terminal (TTY).
   *
   * @details
   * Reads the cached @c is_tty_ flag set by @c init().  Returns @c false if
   * @c init() has not been called yet.  Useful to gate colour / cursor
   * escape sequences (e.g. only emit @c "\x1b[31m" when output is a terminal,
   * not a redirected file).
   *
   * @return @c true if stdout was a TTY at the time @c init() ran.
   */
  [[nodiscard]] bool is_tty() const noexcept;

  /**
   * @brief Returns @c true if @c init() has been called.
   *
   * @details
   * Reads the @c initialized_ flag.  Useful to lazy-initialise in code paths
   * that may be reached before any explicit @c init() call.
   *
   * @return @c true after the first successful @c init() call; @c false otherwise.
   */
  [[nodiscard]] bool is_initialized() const noexcept;

  /**
   * @brief Flushes all buffered data to stdout immediately.
   *
   * @details
   * Acquires the internal mutex and forwards to the unlocked flush path:
   * - Writes the buffered bytes to @c stdout using @c write() / @c _write()
   *   in a loop until either all bytes are written or @c write() returns
   *   <= 0.
   * - Resets the internal write cursor to 0 on full success.  If only part of
   *   the buffer was written, the unwritten bytes remain buffered.
   * - On non-Windows TTY outputs, additionally calls @c tcdrain() so that
   *   the kernel-level terminal output queue is drained before returning.
   *   This produces the strongest "user can see it now" guarantee on POSIX.
   *
   * @note Safe to call concurrently; the internal mutex serialises with
   *       active writers.  Blocking time is proportional to the number of
   *       bytes pending and (on TTYs) the terminal's drain latency.
   */
  void flush();

  /**
   * @brief Writes a raw byte array of @p len bytes to the buffer.
   *
   * @details
   * Bypasses any character-class formatting (no escaping, no NUL termination
   * stripping).  The @p data bytes are appended verbatim and may contain
   * arbitrary binary values, including embedded NULs.
   *
   * Internally, if @p len is >= the buffer size, the current buffer is flushed
   * first and the data is written directly to @c stdout in one or more @c write()
   * calls.  If the direct write only partially completes, the unwritten suffix is
   * buffered for a later @c flush(); otherwise the bytes are appended to the buffer.
   *
   * @param data  Pointer to the bytes to write.  Must be valid for at least
   *              @p len bytes; not retained beyond the call.
   * @param len   Number of bytes to write.  Zero is a no-op.
   * @return Reference to @c *this for chaining.
   */
  TerminalStream& write_raw(const char* data, size_t len) noexcept;

  /**
   * @brief Writes a single character to the buffer.
   *
   * @param c  The character to append.
   * @return Reference to @c *this for chaining.
   */
  TerminalStream& operator<<(char c) noexcept;

  /**
   * @brief Writes a NUL-terminated C string to the buffer.
   *
   * @details
   * Calls @c std::strlen on @p str to determine length, then appends the
   * bytes (excluding the trailing NUL).  Passing @c nullptr is a no-op --
   * no fault is raised.
   *
   * @param str  NUL-terminated source string, or @c nullptr.
   * @return Reference to @c *this for chaining.
   */
  TerminalStream& operator<<(const char* str) noexcept;

  /**
   * @brief Writes a @c std::string to the buffer.
   *
   * @details
   * Appends @c str.size() bytes from @c str.data().  Empty strings are a no-op.
   * Embedded NULs in the string are written verbatim.
   *
   * @param str  Source string.
   * @return Reference to @c *this for chaining.
   */
  TerminalStream& operator<<(const std::string& str) noexcept;

  /**
   * @brief Writes a @c std::string_view to the buffer.
   *
   * @details
   * Appends @c str.size() bytes from @c str.data().  Empty views are a no-op.
   * Embedded NULs in the view are written verbatim.
   *
   * @param str  Source view.
   * @return Reference to @c *this for chaining.
   */
  TerminalStream& operator<<(std::string_view str) noexcept;

  /**
   * @brief Writes the literal @c "true" or @c "false".
   *
   * @param value  Boolean to print.
   * @return Reference to @c *this for chaining.
   */
  TerminalStream& operator<<(bool value) noexcept;

  /**
   * @name Integer overloads
   * @brief Formats and writes the decimal representation of @p value.
   *
   * @details
   * Negative values are prefixed with @c '-'.  The minimum value of each
   * signed type (e.g. @c INT_MIN, @c LLONG_MIN) is handled correctly without
   * undefined behaviour.  All overloads forward to a non-template helper that
   * promotes the input to @c long long / @c unsigned long long internally.
   *
   * @param value  Integer to print.
   * @return Reference to @c *this for chaining.
   * @{
   */
  TerminalStream& operator<<(short value) noexcept;           // NOLINT(runtime/int,google-runtime-int)
  TerminalStream& operator<<(unsigned short value) noexcept;  // NOLINT(runtime/int,google-runtime-int)
  TerminalStream& operator<<(int value) noexcept;
  TerminalStream& operator<<(unsigned int value) noexcept;
  TerminalStream& operator<<(long value) noexcept;                // NOLINT(runtime/int,google-runtime-int)
  TerminalStream& operator<<(unsigned long value) noexcept;       // NOLINT(runtime/int,google-runtime-int)
  TerminalStream& operator<<(long long value) noexcept;           // NOLINT(runtime/int,google-runtime-int)
  TerminalStream& operator<<(unsigned long long value) noexcept;  // NOLINT(runtime/int,google-runtime-int)
  /** @} */

  /**
   * @brief Writes a @c float using @c snprintf with the @c "%g" format.
   *
   * @details
   * The value is promoted to @c double before formatting.  Output uses the
   * shortest of decimal or scientific notation per @c %g semantics.  No
   * precision specifier; the system default precision is used.
   *
   * @param value  Float to print.
   * @return Reference to @c *this for chaining.
   */
  TerminalStream& operator<<(float value) noexcept;

  /**
   * @brief Writes a @c double using @c snprintf with the @c "%g" format.
   *
   * @param value  Double to print.
   * @return Reference to @c *this for chaining.
   */
  TerminalStream& operator<<(double value) noexcept;

  /**
   * @brief Writes a @c long double using @c snprintf with the @c "%Lg" format.
   *
   * @param value  Long double to print.
   * @return Reference to @c *this for chaining.
   */
  TerminalStream& operator<<(long double value) noexcept;  // NOLINT(google-runtime-float)

  /**
   * @brief Writes a pointer address using @c snprintf with the @c "%p" format.
   *
   * @details
   * Output format is implementation-defined; typically a hexadecimal address
   * with an @c "0x" prefix on POSIX, or upper-case hex with no prefix on
   * MSVC.  @c nullptr is rendered as the platform's @c %p representation of
   * a null pointer.
   *
   * @param ptr  Pointer to format.
   * @return Reference to @c *this for chaining.
   */
  TerminalStream& operator<<(const void* ptr) noexcept;

  /**
   * @brief Applies a @c ManipType manipulator function to the stream.
   *
   * @details
   * The manipulator is invoked with @c *this as argument.  Built-in
   * manipulators are @c TerminalStream::endl (newline + flush) and
   * @c TerminalStream::flush_manip (flush only).  User code may pass any
   * static or free function with signature
   * @c TerminalStream& fn(TerminalStream&).
   *
   * @param manip  Function pointer to invoke with @c *this.
   * @return The reference returned by @p manip (normally @c *this).
   */
  TerminalStream& operator<<(ManipType manip) noexcept;

  /**
   * @brief Accepts @c std::endl and similar @c std::ostream manipulators.
   *
   * @details
   * This overload exists so users can write @c stream << std::endl without a
   * compilation error.  The standard manipulator's behaviour cannot be
   * invoked on a non-@c std::ostream object, so this overload simply writes
   * a newline (@c "\n") and flushes the buffer -- the same effect as
   * @c TerminalStream::endl.  The function pointer parameter itself is
   * ignored.
   *
   * @return Reference to @c *this for chaining.
   */
  TerminalStream& operator<<(std::ostream& (*)(std::ostream&)) noexcept;

 private:
  static int default_stdout_fd() noexcept;

  void flush_unlocked() noexcept;

  void write_to_buffer(const char* data, size_t len) noexcept;

  TerminalStream& write_signed(long long value) noexcept;             // NOLINT(runtime/int,google-runtime-int)
  TerminalStream& write_unsigned(unsigned long long value) noexcept;  // NOLINT(runtime/int,google-runtime-int)

  TerminalStream& write_double(double value) noexcept;
  TerminalStream& write_long_double(long double value) noexcept;  // NOLINT(google-runtime-float)

  std::atomic_bool initialized_{false};
  std::atomic_bool is_tty_{false};

  mutable std::mutex mutex_;
  std::vector<char> buffer_;
  size_t write_pos_{0};
  int fd_{default_stdout_fd()};
};

}  // namespace vlink

#ifndef VLINK_TERM_OUT
#define VLINK_TERM_OUT vlink::TerminalStream::get()
#endif
