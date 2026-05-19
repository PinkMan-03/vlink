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
 * @file logger.h
 * @brief Global singleton logger with three output styles and pluggable backends.
 *
 * @details
 * @c Logger is the central logging facility in VLink.  It is a singleton accessed via
 * @c Logger::get() and initialised once with @c Logger::init().  Log messages can be
 * written to a console sink and/or a file sink, each with an independently configured
 * minimum level.
 *
 * Output styles:
 *
 * | Style                | Syntax                         | Notes                                          |
 * | -------------------- | ------------------------------ | ---------------------------------------------- |
 * | Stream style (LOG)   | @c VLOG_I("x=", x)            | Uses @c FastStream operator<<, zero allocation  |
 * | Format style (MLOG)  | @c MLOG_I("x={}", x)          | Uses VLink @c format::format_to_n               |
 * | C style (CLOG)       | @c CLOG_I("x=%d", x)          | Uses @c std::snprintf                           |
 * | RAII stream (SLOG)   | @c SLOG_I << "x=" << x        | WrapperStream, flushed on destruction           |
 *
 * Log levels:
 *
 * | Value | Name    | Use case                                   |
 * | ----- | ------- | ------------------------------------------ |
 * | 0     | kTrace  | Verbose internals                          |
 * | 1     | kDebug  | Developer diagnostics                      |
 * | 2     | kInfo   | Normal operational messages                |
 * | 3     | kWarn   | Unusual but recoverable conditions         |
 * | 4     | kError  | Errors that may affect operation           |
 * | 5     | kFatal  | Unrecoverable errors; throws RuntimeError  |
 * | 6     | kOff    | Disables all output                        |
 *
 * Detail annotation:
 * When the log level is >= @c kDetailLevel (default: @c kWarn), the macro automatically
 * prepends @c {filename:line} to the message to aid in debugging.
 *
 * Compile-time filtering:
 * - Define @c VLINK_LOG_LEVEL=N to strip levels below @c N at compile time (zero overhead).
 * - Define @c VLINK_LOG_DETAIL_LEVEL=N to change the threshold at which file/line is shown.
 * - Define @c VLINK_LOG_DISABLE_SHORT to suppress the short @c VLOG_* aliases.
 *
 * Backtrace support:
 * When enabled via @c enable_backtrace(n), the last @p n log messages are kept in a
 * ring buffer and can be flushed on demand with @c dump_backtrace().
 *
 * Backend support:
 * The logger dispatches to spdlog, quill, DLT (automotive), Android logcat, QNX slog2,
 * or kmsg depending on compile-time configuration.  Custom backends can be registered
 * with @c register_console_handler() / @c register_file_handler().
 *
 * @note
 * - @c kFatal logs flush the logger and then throw @c Exception::RuntimeError.
 * - The @c WrapperStream class is template-based; unused log levels are compiled away.
 *
 * @par Example
 * @code
 * vlink::Logger::init("my_app", "/var/log/my_app.log");
 * vlink::Logger::set_console_level(vlink::Logger::kInfo);
 *
 * VLOG_I("node started, id=", node_id);
 * MLOG_W("temperature is {} C", temp);
 * CLOG_E("errno=%d", errno);
 * SLOG_D << "values: " << a << " " << b;
 * @endcode
 */

#pragma once

#include <cstdio>
#include <iomanip>
#include <iostream>
#include <memory>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

#include "./exception.h"
#include "./fast_stream.h"
#include "./format.h"
#include "./functional.h"
#include "./macros.h"

namespace vlink {

/**
 * @class Logger
 * @brief Global singleton logger supporting three output styles and configurable log levels.
 *
 * @details
 * Construct the singleton with @c Logger::init() once at application startup.
 * Use the @c VLOG_*, @c MLOG_*, @c CLOG_* or @c SLOG_* macros for logging.
 */
class VLINK_EXPORT Logger final {
 public:
  /**
   * @brief Output style selector (used internally by the print_* family).
   *
   * @details
   * Determines how format arguments are converted to a string.
   * Users interact with styles through macros rather than this enum directly.
   */
  enum Style : uint8_t {
    kStreamStyle = 0,  ///< operator<< stream composition via FastStream
    kFormatStyle = 1,  ///< Python-style {} placeholders via vlink::format
    kCStyle = 2,       ///< printf-style %d/%s via std::snprintf
  };

  /**
   * @brief Severity level for log messages.
   *
   * @details
   * Levels are ordered from least severe (kTrace) to most severe (kFatal).
   * kOff disables the corresponding sink entirely.
   *
   * | Value | Name    | Meaning                                      |
   * | ----- | ------- | -------------------------------------------- |
   * | 0     | kTrace  | Verbose tracing                              |
   * | 1     | kDebug  | Developer diagnostics                        |
   * | 2     | kInfo   | Normal operational messages                  |
   * | 3     | kWarn   | Unusual but recoverable conditions           |
   * | 4     | kError  | Recoverable errors                           |
   * | 5     | kFatal  | Throws RuntimeError after logging            |
   * | 6     | kOff    | Disable sink                                 |
   */
  enum Level : uint8_t {
    kTrace = 0,
    kDebug = 1,
    kInfo = 2,
    kWarn = 3,
    kError = 4,
    kFatal = 5,
    kOff = 6,
  };

  /**
   * @brief Compile-time minimum log level.
   *
   * @details
   * Messages with level < @c kMinimumLevel are compiled away completely.
   * Override by defining @c VLINK_LOG_LEVEL before including this header.
   * Defaults to @c kTrace (all messages enabled).
   */
#ifdef VLINK_LOG_LEVEL
  static constexpr uint8_t kMinimumLevel = VLINK_LOG_LEVEL;
#else
  static constexpr uint8_t kMinimumLevel = kTrace;
#endif

  /**
   * @brief Threshold above which file and line information is appended to messages.
   *
   * @details
   * When the message level >= @c kDetailLevel, the macro prepends @c {file:line}
   * to the log string.  Override by defining @c VLINK_LOG_DETAIL_LEVEL.
   * Defaults to @c kWarn.
   */
#ifdef VLINK_LOG_DETAIL_LEVEL
  static constexpr uint8_t kDetailLevel = VLINK_LOG_DETAIL_LEVEL;
#else
  static constexpr uint8_t kDetailLevel = kWarn;
#endif

  /**
   * @brief Size of the thread-local C-style format buffer in bytes.
   *
   * @details
   * Used by @c print_c_style() and @c print_format_style().  Messages longer than
   * @c kLocalBufferSize - 1 characters are silently truncated.
   */
  static constexpr int kLocalBufferSize = 4096;

  /**
   * @brief Callback type for custom console or file log handlers.
   *
   * @details
   * Registered handlers are called synchronously from the logging thread.
   * The @c std::string_view is valid only for the duration of the call.
   */
  using Callback = MoveFunction<void(Level, std::string_view)>;

  /**
   * @brief Carries the source file name and line number for detail annotation.
   *
   * @details
   * Created automatically by @c VLINK_LOG_GET_DETAIL when the message level is
   * >= @c kDetailLevel.
   */
  using DetailInfo = std::pair<std::string_view, int>;

  /**
   * @brief Sentinel type indicating that no file/line detail is attached.
   *
   * @details
   * Used for levels below @c kDetailLevel to avoid capturing __FILE__ and __LINE__.
   */
  struct NoDetail {};

  /**
   * @brief Initialises the logger singleton.
   *
   * @details
   * Must be called before any log macros are invoked.  Safe to call multiple times;
   * subsequent calls reconfigure the logger.  If @p log_path is non-empty, the file
   * sink is opened at that path.
   *
   * @param app_name  Application name embedded in log output.  Default: empty string.
   * @param log_path  Absolute path for the log file.  Default: no file sink.
   */
  static void init(const std::string& app_name = "", const std::string& log_path = "") noexcept;

  /**
   * @brief Returns the logger singleton instance.
   *
   * @details
   * The singleton is created on first use.  It is safe to call @c get() from any thread
   * after @c init() has been called.
   *
   * @return Reference to the global @c Logger instance.
   */
  static Logger& get() noexcept;

  /**
   * @brief Flushes all buffered log messages to the active sinks.
   *
   * @details
   * Useful before abnormal termination to ensure messages are not lost.
   * Called automatically for @c kFatal messages.
   */
  static void flush() noexcept;

  /**
   * @brief Registers a custom handler for console log output.
   *
   * @details
   * When set, the provided callback is invoked instead of the built-in console
   * sink.  Replaces any previously registered console handler.
   *
   * @param callback  Handler called with (level, message_view) for each log record.
   */
  static void register_console_handler(Callback&& callback) noexcept;

  /**
   * @brief Registers a custom handler for file log output.
   *
   * @details
   * When set, the provided callback is invoked instead of the built-in file
   * sink.  Replaces any previously registered file handler.
   *
   * @param callback  Handler called with (level, message_view) for each log record.
   */
  static void register_file_handler(Callback&& callback) noexcept;

  /**
   * @brief Sets the minimum level for the console sink.
   *
   * @details
   * Messages with level < @p level are not written to the console.
   * Pass @c kOff to disable the console sink entirely.
   *
   * @param level  New minimum console output level.
   */
  static void set_console_level(Level level) noexcept;

  /**
   * @brief Sets the minimum level for the file sink.
   *
   * @details
   * Messages with level < @p level are not written to the log file.
   * Pass @c kOff to disable the file sink entirely.
   *
   * @param level  New minimum file output level.
   */
  static void set_file_level(Level level) noexcept;

  /**
   * @brief Enables or disables ANSI colour/format codes in console output.
   *
   * @param enable  @c true to enable formatting codes (default), @c false for plain text.
   */
  static void set_console_fmt_enable(bool enable) noexcept;

  /**
   * @brief Returns the current minimum level for the console sink.
   *
   * @return Current console log level.
   */
  [[nodiscard]] static Level get_console_level() noexcept;

  /**
   * @brief Returns the current minimum level for the file sink.
   *
   * @return Current file log level.
   */
  [[nodiscard]] static Level get_file_level() noexcept;

  /**
   * @brief Returns whether ANSI colour/format codes are enabled for console output.
   *
   * @return @c true if formatting is enabled.
   */
  [[nodiscard]] static bool get_console_fmt_enable() noexcept;

  /**
   * @brief Sets @c std::ios_base format flags applied to stream-style messages.
   *
   * @details
   * The flags are applied to the thread-local @c FastStream before each message.
   * For example, pass @c std::ios::hex to print integers in hexadecimal.
   *
   * @param flags  Format flags to set.
   */
  static void set_stream_flag(std::ios_base::fmtflags flags) noexcept;

  /**
   * @brief Sets the floating-point precision for stream-style messages.
   *
   * @param precision  Number of significant digits (or decimal places depending on format flag).
   */
  static void set_stream_precision(int precision) noexcept;

  /**
   * @brief Sets the output field width for stream-style messages.
   *
   * @param width  Minimum field width in characters.
   */
  static void set_stream_width(int width) noexcept;

  /**
   * @brief Returns the @c std::ios_base format flags currently applied to stream output.
   *
   * @return Current stream format flags.
   */
  [[nodiscard]] static std::ios_base::fmtflags get_stream_flag() noexcept;

  /**
   * @brief Returns the floating-point precision currently used for stream output.
   *
   * @return Current precision value.
   */
  [[nodiscard]] static int get_stream_precision() noexcept;

  /**
   * @brief Returns the field width currently used for stream output.
   *
   * @return Current field width.
   */
  [[nodiscard]] static int get_stream_width() noexcept;

  /**
   * @brief Enables a ring-buffer backtrace of the last @p size log messages.
   *
   * @details
   * When enabled, the last @p size messages are retained in memory regardless of
   * the current log level, and can be flushed with @c dump_backtrace().
   *
   * @param size  Number of messages to retain in the backtrace ring buffer.
   */
  static void enable_backtrace(size_t size) noexcept;

  /**
   * @brief Disables backtrace collection and clears the ring buffer.
   */
  static void disable_backtrace() noexcept;

  /**
   * @brief Flushes the backtrace ring buffer to the active sinks.
   *
   * @details
   * Emits all retained backtrace messages (if any) to the console and file sinks
   * at their original log levels.  Useful just before an abort or fatal handler.
   */
  static void dump_backtrace() noexcept;

  /**
   * @brief Returns @c true if the logger is currently busy writing a message.
   *
   * @details
   * Useful for asynchronous backends to check whether the logger can accept more work.
   *
   * @return @c true if a write is in progress.
   */
  [[nodiscard]] static bool is_busy() noexcept;

  /**
   * @brief Returns @c true if a message at @p level would be written to at least one sink.
   *
   * @details
   * Returns @c true iff @p level is >= the console sink level or >= the file sink level.
   * Call this before constructing expensive log arguments to avoid unnecessary work.
   *
   * @param level  Level to test.
   * @return @c true if the message would be emitted.
   */
  [[nodiscard]] static bool is_writable(Level level) noexcept;

  /**
   * @brief Extracts the file name component from a full path at compile time.
   *
   * @details
   * Searches for the last @c '/' or @c '\\' and returns the substring after it.
   * Used by the @c VLINK_LOG_GET_DETAIL macro to trim __FILE__.
   *
   * @param path  Full source file path (typically @c __FILE__).
   * @return @c string_view of just the filename portion.
   */
  [[nodiscard]] static constexpr std::string_view extract_filename(std::string_view path) noexcept;

  /**
   * @brief Logs using stream-style composition (operator<<).
   *
   * @details
   * Called by the @c VLOG_* macros.  Checks @c should_log() first; returns immediately
   * if the level is disabled.  Uses a thread-local @c FastStream to avoid heap allocations.
   *
   * @tparam LevelT   Compile-time log level.
   * @tparam DetailT  Either @c DetailInfo (with file/line) or @c NoDetail.
   * @tparam ArgsT    Types of the stream arguments.
   * @param detail    File/line info or @c NoDetail{}.
   * @param args      Values streamed into the message via @c operator<<.
   */
  template <Level LevelT, typename DetailT, typename... ArgsT>
  static void print_stream_style(DetailT&& detail, ArgsT&&... args);

  /**
   * @brief Logs using @c {} placeholder format style.
   *
   * @details
   * Called by the @c MLOG_* macros.  Formats the message using @c format::format_to_n
   * into a 4096-byte thread-local buffer.  Messages exceeding the buffer are truncated.
   *
   * @tparam LevelT   Compile-time log level.
   * @tparam DetailT  Either @c DetailInfo or @c NoDetail.
   * @tparam ArgsT    Types of the format arguments.
   * @param detail    File/line info or @c NoDetail{}.
   * @param format    Format string with @c {} placeholders.
   * @param args      Format arguments.
   */
  template <Level LevelT, typename DetailT, typename... ArgsT>
  static void print_format_style(DetailT&& detail, format::format_string<ArgsT...> format, ArgsT&&... args);

  /**
   * @brief Logs using C-style printf format string.
   *
   * @details
   * Called by the @c CLOG_* macros.  Formats using @c std::snprintf into a 4096-byte
   * thread-local buffer.  Messages exceeding the buffer are truncated.
   *
   * @tparam LevelT   Compile-time log level.
   * @tparam DetailT  Either @c DetailInfo or @c NoDetail.
   * @tparam FormatT  Type of the format string (typically @c const char*).
   * @tparam ArgsT    Types of the printf arguments.
   * @param detail    File/line info or @c NoDetail{}.
   * @param format    printf-style format string.
   * @param args      printf arguments.
   */
  template <Logger::Level LevelT, typename DetailT, typename FormatT, typename... ArgsT>
  static void print_c_style(DetailT&& detail, FormatT&& format, ArgsT&&... args);

  /**
   * @brief Logs using stream style without file/line detail.
   *
   * @details
   * Convenience wrapper that passes @c NoDetail{} to @c print_stream_style.
   *
   * @tparam LevelT  Compile-time log level.
   * @tparam ArgsT   Types of the stream arguments.
   * @param args     Values to stream.
   */
  template <Level LevelT, typename... ArgsT>
  static void print(ArgsT&&... args);

  /**
   * @class WrapperStream
   * @brief RAII stream wrapper that accumulates tokens and flushes on destruction.
   *
   * @details
   * Used by the @c SLOG_* macros to allow natural @c << chaining.  The message is
   * emitted when the temporary @c WrapperStream object goes out of scope.
   * If the log level is disabled at compile time (@c kIsEnabled == false), all
   * methods are compiled away and the object has zero runtime cost.
   *
   * @tparam LevelT  Compile-time log level.
   */
  template <Logger::Level LevelT>
  class WrapperStream final {
   public:
    /**
     * @brief Indicates whether this level is enabled at compile time.
     *
     * @details
     * If @c false, all @c operator<< calls and the destructor flush are no-ops.
     */
    static constexpr bool kIsEnabled = (LevelT >= kMinimumLevel && LevelT < Logger::kOff);

    explicit WrapperStream(Logger::NoDetail) noexcept {
      if constexpr (kIsEnabled) {
        if (should_log<LevelT>()) {
          enabled_ = true;
          stream_ = &Logger::get_local_stream();
        }
      }
    }

    explicit WrapperStream(DetailInfo&& detail) noexcept {
      if constexpr (kIsEnabled) {
        if (should_log<LevelT>()) {
          enabled_ = true;
          stream_ = &Logger::get_local_stream();

          push_detail_to_stream(detail, *stream_);
        }
      }
    }

    WrapperStream(WrapperStream&& other) noexcept : stream_(other.stream_), enabled_(other.enabled_) {
      other.stream_ = nullptr;
      other.enabled_ = false;
    }

    WrapperStream& operator=(WrapperStream&&) = delete;

    ~WrapperStream() noexcept(LevelT != Level::kFatal) {
      if constexpr (kIsEnabled) {
        if (enabled_) {
          finalize_log<LevelT>(stream_->take_view());
        }
      }
    }

    template <typename T>
    WrapperStream& operator<<(T&& t) noexcept {
      if constexpr (kIsEnabled) {
        if (enabled_) {
          *stream_ << std::forward<T>(t);
        }
      }

      return *this;
    }

   private:
    VLINK_DISALLOW_COPY_AND_ASSIGN(WrapperStream)

    FastStream* stream_{nullptr};
    bool enabled_{false};
  };

 private:
  Logger() noexcept;

  ~Logger() noexcept;

  template <Level LevelT>
  static bool should_log() noexcept;

  template <Level LevelT>
  static void finalize_log(std::string_view log_view);

  template <typename DetailT>
  static void push_detail_to_stream(DetailT&& detail, FastStream& stream) noexcept;

  template <typename DetailT>
  static std::string_view format_with_detail(DetailT&& detail, const char* msg, int len) noexcept;

  static char* get_local_buffer() noexcept;

  static FastStream& get_local_stream() noexcept;

  void write_to_console(Level level, std::string_view log) noexcept;

  void write_to_file(Level level, std::string_view log) noexcept;

  struct Impl;
  std::unique_ptr<Impl> impl_;

  template <Logger::Level LevelT>
  friend class WrapperStream;

  VLINK_SINGLETON_CHECK(Logger)

  VLINK_DISALLOW_COPY_AND_ASSIGN(Logger)
};

////////////////////////////////////////////////////////////////
/// Details
////////////////////////////////////////////////////////////////

inline constexpr std::string_view Logger::extract_filename(std::string_view path) noexcept {
  auto pos = path.find_last_of("/\\");
  return (pos == std::string_view::npos) ? path : path.substr(pos + 1);
}

template <Logger::Level LevelT, typename DetailT, typename... ArgsT>
inline void Logger::print_stream_style([[maybe_unused]] DetailT&& detail, [[maybe_unused]] ArgsT&&... args) {
  if (!should_log<LevelT>()) {
    return;
  }

  auto& stream = get_local_stream();

  if constexpr (std::is_same_v<std::decay_t<DetailT>, DetailInfo>) {
    push_detail_to_stream(detail, stream);
  }

  (void)(stream << ... << args);

  finalize_log<LevelT>(stream.take_view());
}

template <Logger::Level LevelT, typename DetailT, typename... ArgsT>
inline void Logger::print_format_style([[maybe_unused]] DetailT&& detail,
                                       [[maybe_unused]] format::format_string<ArgsT...> format,
                                       [[maybe_unused]] ArgsT&&... args) {
  if (!should_log<LevelT>()) {
    return;
  }

  std::string_view log_view;

  auto* local_buffer = get_local_buffer();
  auto result = format::format_to_n(local_buffer, kLocalBufferSize - 1, format, std::forward<ArgsT>(args)...);
  auto written = static_cast<int>(result.out - local_buffer);

  local_buffer[written] = '\0';

  log_view = format_with_detail(detail, local_buffer, written);

  finalize_log<LevelT>(log_view);
}

template <Logger::Level LevelT, typename DetailT, typename FormatT, typename... ArgsT>
inline void Logger::print_c_style([[maybe_unused]] DetailT&& detail, [[maybe_unused]] FormatT&& format,
                                  [[maybe_unused]] ArgsT&&... args) {
  if (!should_log<LevelT>()) {
    return;
  }

  std::string_view log_view;

  if constexpr (sizeof...(ArgsT) == 0) {
    // static_assert(Traits::ExpectFalse<DetailT>(), "VLINK_CLOG_* Format error.");

    auto& stream = get_local_stream();

    if constexpr (std::is_same_v<std::decay_t<DetailT>, DetailInfo>) {
      push_detail_to_stream(detail, stream);
    }

    stream << format;
    log_view = stream.take_view();
  } else {
    auto* local_buffer = get_local_buffer();
    auto written = std::snprintf(local_buffer, kLocalBufferSize - 1, format, args...);

    if VUNLIKELY (written < 0) {
      written = 0;
    } else if VUNLIKELY (written > kLocalBufferSize - 1) {
      written = kLocalBufferSize - 1;
    }

    log_view = format_with_detail(detail, local_buffer, written);
  }

  finalize_log<LevelT>(log_view);
}

template <Logger::Level LevelT, typename... ArgsT>
inline void Logger::print([[maybe_unused]] ArgsT&&... args) {
  print_stream_style<LevelT>(NoDetail{}, args...);
}

template <Logger::Level LevelT>
inline bool Logger::should_log() noexcept {
  if constexpr (LevelT < Logger::kMinimumLevel || LevelT >= Logger::kOff) {
    return false;
  } else if constexpr (LevelT == Logger::kFatal) {
    return true;
  } else {
    return Logger::is_writable(LevelT);
  }
}

template <Logger::Level LevelT>
inline void Logger::finalize_log(std::string_view log_view) {
  Logger& instance = Logger::get();

  instance.write_to_console(LevelT, log_view);
  instance.write_to_file(LevelT, log_view);

  if constexpr (LevelT == Logger::kFatal) {
    Logger::flush();
    throw Exception::RuntimeError(std::string(log_view));
  }
}

template <typename DetailT>
inline void Logger::push_detail_to_stream(DetailT&& detail, FastStream& stream) noexcept {
  auto& [file, line] = detail;
  stream << "{" << file << ":" << line << "} ";
}

template <typename DetailT>
inline std::string_view Logger::format_with_detail(DetailT&& detail, const char* msg, int len) noexcept {
  if constexpr (std::is_same_v<std::decay_t<DetailT>, Logger::DetailInfo>) {
    auto& stream = Logger::get_local_stream();

    push_detail_to_stream(detail, stream);

    if VLIKELY (len > 0) {
      stream.write_raw(msg, static_cast<size_t>(len));
    }

    return stream.take_view();
  } else {
    if VLIKELY (len > 0) {
      return std::string_view(msg, static_cast<size_t>(len));
    }

    return {};
  }
}

}  // namespace vlink

using VLinkLogger = vlink::Logger;

////////////////////////////////////////////////////////////////
/// Macro Definitions
////////////////////////////////////////////////////////////////

#define VLINK_LOG_GET_DETAIL(level)                                                      \
  ([]() -> auto {                                                                        \
    if constexpr ((level) >= VLinkLogger::kDetailLevel) {                                \
      return VLinkLogger::DetailInfo{VLinkLogger::extract_filename(__FILE__), __LINE__}; \
    } else {                                                                             \
      return VLinkLogger::NoDetail{};                                                    \
    }                                                                                    \
  })()

#define VLINK_LOG_HEX(offset) std::hex, std::uppercase, std::setw(offset), std::setfill('0')

#define VLINK_LOG_HEXSS(offset) std::hex << std::uppercase << std::setw(offset) << std::setfill('0')

#define VLINK_LOG_IF_T VLinkLogger::is_writable(VLinkLogger::kTrace)

#define VLINK_LOG_IF_D VLinkLogger::is_writable(VLinkLogger::kDebug)

#define VLINK_LOG_IF_I VLinkLogger::is_writable(VLinkLogger::kInfo)

#define VLINK_LOG_IF_W VLinkLogger::is_writable(VLinkLogger::kWarn)

#define VLINK_LOG_IF_E VLinkLogger::is_writable(VLinkLogger::kError)

#define VLINK_LOG_IF_F VLinkLogger::is_writable(VLinkLogger::kFatal)

#define VLINK_LOG_T(...) \
  VLinkLogger::print_stream_style<VLinkLogger::kTrace>(VLINK_LOG_GET_DETAIL(VLinkLogger::kTrace), __VA_ARGS__)

#define VLINK_LOG_D(...) \
  VLinkLogger::print_stream_style<VLinkLogger::kDebug>(VLINK_LOG_GET_DETAIL(VLinkLogger::kDebug), __VA_ARGS__)

#define VLINK_LOG_I(...) \
  VLinkLogger::print_stream_style<VLinkLogger::kInfo>(VLINK_LOG_GET_DETAIL(VLinkLogger::kInfo), __VA_ARGS__)

#define VLINK_LOG_W(...) \
  VLinkLogger::print_stream_style<VLinkLogger::kWarn>(VLINK_LOG_GET_DETAIL(VLinkLogger::kWarn), __VA_ARGS__)

#define VLINK_LOG_E(...) \
  VLinkLogger::print_stream_style<VLinkLogger::kError>(VLINK_LOG_GET_DETAIL(VLinkLogger::kError), __VA_ARGS__)

#define VLINK_LOG_F(...) \
  VLinkLogger::print_stream_style<VLinkLogger::kFatal>(VLINK_LOG_GET_DETAIL(VLinkLogger::kFatal), __VA_ARGS__)

#define VLINK_MLOG_T(...) \
  VLinkLogger::print_format_style<VLinkLogger::kTrace>(VLINK_LOG_GET_DETAIL(VLinkLogger::kTrace), __VA_ARGS__)

#define VLINK_MLOG_D(...) \
  VLinkLogger::print_format_style<VLinkLogger::kDebug>(VLINK_LOG_GET_DETAIL(VLinkLogger::kDebug), __VA_ARGS__)

#define VLINK_MLOG_I(...) \
  VLinkLogger::print_format_style<VLinkLogger::kInfo>(VLINK_LOG_GET_DETAIL(VLinkLogger::kInfo), __VA_ARGS__)

#define VLINK_MLOG_W(...) \
  VLinkLogger::print_format_style<VLinkLogger::kWarn>(VLINK_LOG_GET_DETAIL(VLinkLogger::kWarn), __VA_ARGS__)

#define VLINK_MLOG_E(...) \
  VLinkLogger::print_format_style<VLinkLogger::kError>(VLINK_LOG_GET_DETAIL(VLinkLogger::kError), __VA_ARGS__)

#define VLINK_MLOG_F(...) \
  VLinkLogger::print_format_style<VLinkLogger::kFatal>(VLINK_LOG_GET_DETAIL(VLinkLogger::kFatal), __VA_ARGS__)

#define VLINK_CLOG_T(...) \
  VLinkLogger::print_c_style<VLinkLogger::kTrace>(VLINK_LOG_GET_DETAIL(VLinkLogger::kTrace), __VA_ARGS__)

#define VLINK_CLOG_D(...) \
  VLinkLogger::print_c_style<VLinkLogger::kDebug>(VLINK_LOG_GET_DETAIL(VLinkLogger::kDebug), __VA_ARGS__)

#define VLINK_CLOG_I(...) \
  VLinkLogger::print_c_style<VLinkLogger::kInfo>(VLINK_LOG_GET_DETAIL(VLinkLogger::kInfo), __VA_ARGS__)

#define VLINK_CLOG_W(...) \
  VLinkLogger::print_c_style<VLinkLogger::kWarn>(VLINK_LOG_GET_DETAIL(VLinkLogger::kWarn), __VA_ARGS__)

#define VLINK_CLOG_E(...) \
  VLinkLogger::print_c_style<VLinkLogger::kError>(VLINK_LOG_GET_DETAIL(VLinkLogger::kError), __VA_ARGS__)

#define VLINK_CLOG_F(...) \
  VLinkLogger::print_c_style<VLinkLogger::kFatal>(VLINK_LOG_GET_DETAIL(VLinkLogger::kFatal), __VA_ARGS__)

#define VLINK_SLOG_T VLinkLogger::WrapperStream<VLinkLogger::kTrace>(VLINK_LOG_GET_DETAIL(VLinkLogger::kTrace))

#define VLINK_SLOG_D VLinkLogger::WrapperStream<VLinkLogger::kDebug>(VLINK_LOG_GET_DETAIL(VLinkLogger::kDebug))

#define VLINK_SLOG_I VLinkLogger::WrapperStream<VLinkLogger::kInfo>(VLINK_LOG_GET_DETAIL(VLinkLogger::kInfo))

#define VLINK_SLOG_W VLinkLogger::WrapperStream<VLinkLogger::kWarn>(VLINK_LOG_GET_DETAIL(VLinkLogger::kWarn))

#define VLINK_SLOG_E VLinkLogger::WrapperStream<VLinkLogger::kError>(VLINK_LOG_GET_DETAIL(VLinkLogger::kError))

#define VLINK_SLOG_F VLinkLogger::WrapperStream<VLinkLogger::kFatal>(VLINK_LOG_GET_DETAIL(VLinkLogger::kFatal))

#ifndef VLINK_LOG_DISABLE_SHORT

#define VLOG_T(...) VLINK_LOG_T(__VA_ARGS__)

#define VLOG_D(...) VLINK_LOG_D(__VA_ARGS__)

#define VLOG_I(...) VLINK_LOG_I(__VA_ARGS__)

#define VLOG_W(...) VLINK_LOG_W(__VA_ARGS__)

#define VLOG_E(...) VLINK_LOG_E(__VA_ARGS__)

#define VLOG_F(...) VLINK_LOG_F(__VA_ARGS__)

#define CLOG_T(...) VLINK_CLOG_T(__VA_ARGS__)

#define CLOG_D(...) VLINK_CLOG_D(__VA_ARGS__)

#define CLOG_I(...) VLINK_CLOG_I(__VA_ARGS__)

#define CLOG_W(...) VLINK_CLOG_W(__VA_ARGS__)

#define CLOG_E(...) VLINK_CLOG_E(__VA_ARGS__)

#define CLOG_F(...) VLINK_CLOG_F(__VA_ARGS__)

#define MLOG_T(...) VLINK_MLOG_T(__VA_ARGS__)

#define MLOG_D(...) VLINK_MLOG_D(__VA_ARGS__)

#define MLOG_I(...) VLINK_MLOG_I(__VA_ARGS__)

#define MLOG_W(...) VLINK_MLOG_W(__VA_ARGS__)

#define MLOG_E(...) VLINK_MLOG_E(__VA_ARGS__)

#define MLOG_F(...) VLINK_MLOG_F(__VA_ARGS__)

#define SLOG_T VLINK_SLOG_T

#define SLOG_D VLINK_SLOG_D

#define SLOG_I VLINK_SLOG_I

#define SLOG_W VLINK_SLOG_W

#define SLOG_E VLINK_SLOG_E

#define SLOG_F VLINK_SLOG_F

#endif
