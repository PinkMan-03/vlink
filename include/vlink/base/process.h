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
 * @file process.h
 * @brief Cross-platform child process management with I/O piping and async callbacks.
 *
 * @details
 * @c Process provides a Qt-QProcess-inspired API for launching, monitoring and communicating
 * with child processes.  It supports:
 * - Asynchronous state change, I/O ready-read and exit callbacks.
 * - Multiple channel modes (separate stdout/stderr, merged, or forwarded to parent).
 * - Line-by-line and bulk I/O reading from stdout and stderr.
 * - Writing to stdin with a configurable timeout.
 * - Synchronous helpers (@c execute, @c start_detached) for fire-and-forget use cases.
 *
 * Channel modes:
 *
 * | Mode                   | stdout              | stderr              |
 * | ---------------------- | ------------------- | ------------------- |
 * | @c kSeparateMode       | Buffered pipe       | Buffered pipe       |
 * | @c kMergedMode         | Buffered pipe       | Merged into stdout  |
 * | @c kForwardedMode      | Forwarded to parent | Forwarded to parent |
 * | @c kForwardedOutputMode| Forwarded to parent | Buffered pipe       |
 * | @c kForwardedErrorMode | Buffered pipe       | Forwarded to parent |
 *
 * @note
 * - All callbacks are invoked from an internal monitor thread; access shared state with care.
 * - @c close() requests termination and optionally force-kills after a timeout.
 * - @c Process objects are non-moveable and non-copyable.
 * - The destructor waits @c kDestructorWaitTimeoutMs (5 s) for the process to exit.
 *
 * @par Example
 * @code
 * vlink::Process proc;
 * proc.set_process_mode(vlink::Process::kSeparateMode);
 * proc.register_ready_read_stdout_callback([&proc]() {
 *   std::string line;
 *   while (proc.can_read_line_stdout()) {
 *     proc.read_line_stdout(line);
 *     // handle line
 *   }
 * });
 * proc.register_finished_callback([](int code, vlink::Process::ExitStatus status) {
 *   // handle exit
 * });
 * proc.start("/usr/bin/ls", {"-la"});
 * proc.wait_for_finished();
 * @endcode
 */

#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "./functional.h"
#include "./macros.h"

namespace vlink {

/**
 * @class Process
 * @brief Cross-platform child process with async I/O and state notification.
 *
 * @details
 * A single @c Process object manages one child process at a time.
 * It is non-copyable and non-moveable.
 */
class VLINK_EXPORT Process {
 public:
  /**
   * @brief Lifecycle state of the child process.
   */
  enum State : uint8_t {
    kNotRunningState = 0,  ///< Not started or has exited
    kStartingState = 1,    ///< @c start() called; waiting for exec to complete
    kRunningState = 2,     ///< Successfully running
  };

  /**
   * @brief How the child process exited.
   */
  enum ExitStatus : uint8_t {
    kNormalExitStatus = 0,  ///< Exited normally (via exit() or return from main)
    kCrashExitStatus = 1,   ///< Killed by a signal or crashed
  };

  /**
   * @brief Error codes set on failure.
   */
  enum Error : uint8_t {
    kNoError = 0,              ///< No error
    kUnknownError = 1,         ///< Unknown error
    kStartError = 2,           ///< Failed to start the process (e.g., file not found)
    kCrashedError = 3,         ///< Process crashed
    kTimedOutError = 4,        ///< A wait operation timed out
    kWriteError = 5,           ///< Write to stdin failed
    kReadError = 6,            ///< Read from stdout/stderr failed
    kBufferOverflowError = 7,  ///< Output exceeded @c max_buffer_size
  };

  /**
   * @brief I/O channel routing mode.
   */
  enum Mode : uint8_t {
    kSeparateMode = 0,         ///< stdout and stderr buffered as separate pipes
    kMergedMode = 1,           ///< stderr merged into stdout pipe
    kForwardedMode = 2,        ///< Both stdout and stderr forwarded to the parent process
    kForwardedOutputMode = 3,  ///< stdout forwarded; stderr buffered separately
    kForwardedErrorMode = 4,   ///< stderr forwarded; stdout buffered separately
  };

  /**
   * @brief Environment variable map type for @c set_environment().
   */
  using EnvironmentMap = std::unordered_map<std::string, std::string>;

  /**
   * @brief Callback type invoked when an error occurs.
   */
  using ErrorCallback = Function<void(Error)>;

  /**
   * @brief Callback type invoked when the process exits.
   *
   * @details
   * First argument is the exit code; second is @c kNormalExitStatus or @c kCrashExitStatus.
   */
  using FinishedCallback = Function<void(int, ExitStatus)>;

  /**
   * @brief Callback type invoked when new data is available on a pipe.
   */
  using ReadyReadCallback = Function<void()>;

  /**
   * @brief Callback type invoked when the process state changes.
   */
  using StateChangedCallback = Function<void(State)>;

  /**
   * @brief Sentinel wait timeout meaning wait indefinitely.
   */
  static constexpr int kInfinite{-1};

  /**
   * @brief Default timeout for @c wait_for_started() and @c wait_for_finished() in milliseconds.
   */
  static constexpr int kDefaultWaitTimeoutMs{3000};

  /**
   * @brief Default timeout for @c write() in milliseconds.
   */
  static constexpr int kDefaultWriteTimeoutMs{5000};

  /**
   * @brief Default timeout for the synchronous @c execute() helper in milliseconds.
   */
  static constexpr int kDefaultExecuteTimeoutMs{30000};

  /**
   * @brief Time the destructor waits for the child to exit before force-killing in milliseconds.
   */
  static constexpr int kDestructorWaitTimeoutMs{5000};

  /**
   * @brief Constructs a @c Process object.  No child is started yet.
   */
  Process();

  /**
   * @brief Destructor.  Closes the process and waits up to @c kDestructorWaitTimeoutMs.
   */
  ~Process();

  Process(Process&& other) noexcept = delete;

  Process& operator=(Process&& other) noexcept = delete;

  /**
   * @brief Returns the current state of the child process.
   *
   * @return Current @c State value.
   */
  [[nodiscard]] State get_state() const;

  /**
   * @brief Returns the last error code.
   *
   * @return @c kNoError if no error has occurred.
   */
  [[nodiscard]] Error get_error() const;

  /**
   * @brief Returns the exit code of the child process.
   *
   * @details
   * Valid only after @c get_state() returns @c kNotRunningState.
   *
   * @return Exit code.
   */
  [[nodiscard]] int get_exit_code() const;

  /**
   * @brief Returns how the child process exited.
   *
   * @return @c kNormalExitStatus or @c kCrashExitStatus.
   */
  [[nodiscard]] ExitStatus get_exit_status() const;

  /**
   * @brief Returns @c true if the child process is currently running.
   *
   * @return @c true if state is @c kRunningState.
   */
  [[nodiscard]] bool is_running() const;

  /**
   * @brief Returns the operating-system process ID of the child.
   *
   * @return PID, or -1 if not running.
   */
  [[nodiscard]] int64_t get_process_id() const;

  /**
   * @brief Sets the maximum buffer size for stdout and stderr capture.
   *
   * @details
   * If the child produces more output than @p size bytes, @c kBufferOverflowError is set.
   * Passing 0 resets the limit to the default (16 MB).
   *
   * @param size  Maximum buffer size in bytes.
   */
  void set_max_buffer_size(size_t size);

  /**
   * @brief Returns the configured maximum buffer size.
   *
   * @return Buffer size in bytes.  Default is 16 MB (16 * 1024 * 1024).
   */
  [[nodiscard]] size_t get_max_buffer_size() const;

  /**
   * @brief Sets the environment variables for the child process.
   *
   * @details
   * Replaces (or supplements, depending on @c set_inherit_environment) the child's
   * environment with @p env_map.
   *
   * @param env_map  Map of variable name to value.
   */
  void set_environment(const EnvironmentMap& env_map);

  /**
   * @brief Returns the configured environment map.
   *
   * @return Environment variable map.
   */
  [[nodiscard]] EnvironmentMap get_environment() const;

  /**
   * @brief Sets the I/O channel routing mode.
   *
   * @param mode  Channel mode.  Must be set before @c start().
   */
  void set_process_mode(Mode mode);

  /**
   * @brief Returns the configured I/O channel mode.
   *
   * @return Current mode.
   */
  [[nodiscard]] Mode get_process_mode() const;

  /**
   * @brief Controls whether the child inherits the parent's environment.
   *
   * @param inherit  If @c true, the child inherits all parent environment variables.
   *                 If @c false, only the variables in the @c EnvironmentMap are set.
   */
  void set_inherit_environment(bool inherit);

  /**
   * @brief Returns whether the child inherits the parent environment.
   *
   * @return @c true if inheriting.
   */
  [[nodiscard]] bool get_inherit_environment() const;

  /**
   * @brief Sets the working directory for the child process.
   *
   * @param dir  Absolute path to the working directory.
   */
  void set_working_directory(const std::string& dir);

  /**
   * @brief Returns the configured working directory.
   *
   * @return Working directory path.
   */
  [[nodiscard]] std::string get_working_directory() const;

  /**
   * @brief Registers a callback for error events.
   *
   * @param callback  Invoked with the @c Error code when an error occurs.
   */
  void register_error_callback(ErrorCallback&& callback);

  /**
   * @brief Registers a callback invoked when the child exits.
   *
   * @param callback  Invoked with @c (exit_code, exit_status).
   */
  void register_finished_callback(FinishedCallback&& callback);

  /**
   * @brief Registers a callback invoked when new stdout data is available.
   *
   * @param callback  Invoked from the monitor thread when stdout has data.
   */
  void register_ready_read_stdout_callback(ReadyReadCallback&& callback);

  /**
   * @brief Registers a callback invoked when new stderr data is available.
   *
   * @param callback  Invoked from the monitor thread when stderr has data.
   */
  void register_ready_read_stderr_callback(ReadyReadCallback&& callback);

  /**
   * @brief Registers a callback invoked when the process state changes.
   *
   * @param callback  Invoked with the new @c State value.
   */
  void register_state_changed_callback(StateChangedCallback&& callback);

  /**
   * @brief Launches the child process.
   *
   * @details
   * The child is started asynchronously.  Call @c wait_for_started() to block until
   * the child is in @c kRunningState.
   *
   * @param program    Path to the executable.
   * @param arguments  Command-line arguments.  Default: empty.
   */
  void start(const std::string& program, const std::vector<std::string>& arguments = {});

  /**
   * @brief Parses and launches a shell command string.
   *
   * @details
   * Splits @p command by whitespace into a program and argument list, then calls @c start().
   *
   * @param command  Shell command string.
   */
  void start_command(const std::string& command);

  /**
   * @brief Blocks until the child process enters @c kRunningState.
   *
   * @param msecs  Timeout in milliseconds.  Default: @c kDefaultWaitTimeoutMs (3000 ms).
   * @return @c true if the process is running within the timeout.
   */
  bool wait_for_started(int msecs = kDefaultWaitTimeoutMs);

  /**
   * @brief Blocks until the child process exits.
   *
   * @param msecs  Timeout in milliseconds.  Default: @c kDefaultWaitTimeoutMs (3000 ms).
   * @return @c true if the process exited within the timeout.
   */
  bool wait_for_finished(int msecs = kDefaultWaitTimeoutMs);

  /**
   * @brief Blocks until new data is available on any pipe.
   *
   * @param msecs  Timeout in milliseconds.  Default: @c kDefaultWaitTimeoutMs (3000 ms).
   * @return @c true if data became available within the timeout.
   */
  bool wait_for_ready_read(int msecs = kDefaultWaitTimeoutMs);

  /**
   * @brief Requests child termination.
   *
   * @details
   * On POSIX this sends @c SIGTERM, which the child may handle or ignore.
   * On Windows this uses @c TerminateProcess, so termination is immediate.
   * Use @c kill() for an explicit forceful stop.
   */
  void terminate();

  /**
   * @brief Forcefully kills the child process (SIGKILL / TerminateProcess).
   */
  void kill();

  /**
   * @brief Calls @c terminate() and optionally force-kills after a timeout.
   *
   * @param force_kill_on_timeout  If @c true, calls @c kill() if the process has not exited
   *                               after @c kDestructorWaitTimeoutMs. If @c false and the child
   *                               does not exit in time, the process remains running. On Windows,
   *                               @c terminate() is already forceful. Default: @c false.
   */
  void close(bool force_kill_on_timeout = false);

  /**
   * @brief Returns the number of bytes available to read from stdout.
   *
   * @return Bytes available in the stdout buffer.
   */
  [[nodiscard]] size_t bytes_available_stdout() const;

  /**
   * @brief Returns the number of bytes available to read from stderr.
   *
   * @return Bytes available in the stderr buffer.
   */
  [[nodiscard]] size_t bytes_available_stderr() const;

  /**
   * @brief Returns @c true if a complete newline-terminated line is available on stdout.
   *
   * @return @c true if at least one line can be read.
   */
  [[nodiscard]] bool can_read_line_stdout() const;

  /**
   * @brief Returns @c true if a complete newline-terminated line is available on stderr.
   *
   * @return @c true if at least one line can be read.
   */
  [[nodiscard]] bool can_read_line_stderr() const;

  /**
   * @brief Reads one line from stdout into @p line.
   *
   * @param line  Output string. Includes the trailing newline when one is buffered.
   * @return @c true if a line was read.
   */
  bool read_line_stdout(std::string& line);

  /**
   * @brief Reads one line from stderr into @p line.
   *
   * @param line  Output string. Includes the trailing newline when one is buffered.
   * @return @c true if a line was read.
   */
  bool read_line_stderr(std::string& line);

  /**
   * @brief Reads up to @p max_size bytes from stdout into @p buffer.
   *
   * @param buffer    Destination vector.
   * @param max_size  Maximum bytes to read.
   * @return Number of bytes actually read.
   */
  size_t read_stdout(std::vector<uint8_t>& buffer, size_t max_size);

  /**
   * @brief Reads up to @p max_size bytes from stderr into @p buffer.
   *
   * @param buffer    Destination vector.
   * @param max_size  Maximum bytes to read.
   * @return Number of bytes actually read.
   */
  size_t read_stderr(std::vector<uint8_t>& buffer, size_t max_size);

  /**
   * @brief Reads all available stdout data into @p buffer (byte vector overload).
   *
   * @param buffer  Destination vector; existing content is replaced.
   * @return @c true if any data was available.
   */
  bool read_all_output(std::vector<uint8_t>& buffer);

  /**
   * @brief Reads all available stderr data into @p buffer (byte vector overload).
   *
   * @param buffer  Destination vector; existing content is replaced.
   * @return @c true if any data was available.
   */
  bool read_all_error(std::vector<uint8_t>& buffer);

  /**
   * @brief Reads all available stdout and stderr data into @p buffer (byte vector overload).
   *
   * @param buffer  Destination vector; existing content is replaced.
   * @return @c true if any data was available.
   */
  bool read_all(std::vector<uint8_t>& buffer);

  /**
   * @brief Reads all available stdout data into @p str.
   *
   * @param str  Destination string.
   * @return @c true if any data was available.
   */
  bool read_all_output(std::string& str);

  /**
   * @brief Reads all available stderr data into @p str.
   *
   * @param str  Destination string.
   * @return @c true if any data was available.
   */
  bool read_all_error(std::string& str);

  /**
   * @brief Reads all available stdout and stderr data into @p str.
   *
   * @param str  Destination string.
   * @return @c true if any data was available.
   */
  bool read_all(std::string& str);

  /**
   * @brief Writes @p buffer to the child's stdin.
   *
   * @param buffer     Data to write.
   * @param timeout_ms Maximum time to wait for the write to complete.  Default: @c kDefaultWriteTimeoutMs.
   * @return Number of bytes actually written.
   */
  size_t write(const std::vector<uint8_t>& buffer, int timeout_ms = kDefaultWriteTimeoutMs);

  /**
   * @brief Writes a string to the child's stdin.
   *
   * @param str        String to write.
   * @param timeout_ms Maximum time to wait.  Default: @c kDefaultWriteTimeoutMs.
   * @return Number of bytes actually written.
   */
  size_t write(const std::string& str, int timeout_ms = kDefaultWriteTimeoutMs);

  /**
   * @brief Closes the write channel (stdin pipe), signalling EOF to the child.
   */
  void close_write_channel();

  /**
   * @brief Synchronously executes a program and waits for it to finish.
   *
   * @details
   * Blocks until the program exits or @p timeout_ms elapses.  Returns the exit code.
   * Stdout and stderr are discarded.
   *
   * @param program     Path to the executable.
   * @param arguments   Command-line arguments.  Default: empty.
   * @param timeout_ms  Maximum wait time.  Default: @c kDefaultExecuteTimeoutMs (30 s).
   * @return Exit code, or -1 on timeout or start failure.
   */
  static int execute(const std::string& program, const std::vector<std::string>& arguments = {},
                     int timeout_ms = kDefaultExecuteTimeoutMs);

  /**
   * @brief Starts a program in the background and returns immediately.
   *
   * @details
   * The started process is completely detached from the calling process.
   * No handle is returned; the process runs until it exits on its own.
   *
   * @param program    Path to the executable.
   * @param arguments  Command-line arguments.  Default: empty.
   * @return @c true if the process was successfully started.
   */
  static bool start_detached(const std::string& program, const std::vector<std::string>& arguments = {});

 private:
  struct ReadResult final {
    bool has_stdout_data{false};
    bool has_stderr_data{false};
    bool truncated{false};
    bool read_error{false};
  };

  void set_state(State state);

  void set_error(Error error);

  void cleanup();

  ReadResult read_from_pipes();

  void report_read_result(const ReadResult& result);

  void read_from_pipes_with_lock();

  bool setup_pipes();

  bool start_program(const std::string& program, const std::vector<std::string>& arguments);

  void monitor_thread();

  void start_monitor_thread();

  void stop_monitor_thread();

  void handle_process_exit(int exit_code, ExitStatus status);

  void invoke_callbacks_outside_lock(Error error_to_report, bool has_finished, int exit_code_to_report,
                                     ExitStatus exit_status_to_report, State state_to_report, bool has_state_changed,
                                     bool has_stdout_data, bool has_stderr_data);

  struct Impl;
  std::unique_ptr<Impl> impl_;

  VLINK_DISALLOW_COPY_AND_ASSIGN(Process)
};

}  // namespace vlink
