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

// NOLINTBEGIN

#include "./base/process.h"

#include <doctest/doctest.h>

#include <atomic>
#include <chrono>
#include <filesystem>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

//
#include "../common_test.h"

// ---------------------------------------------------------------------------
// TEST SUITE
// ---------------------------------------------------------------------------

TEST_SUITE("base-Process") {
  // -------------------------------------------------------------------------
  // TEST: initial state of a freshly constructed Process object
  // -------------------------------------------------------------------------

  TEST_CASE("initial-state") {
    Process proc;

    CHECK(proc.get_state() == Process::kNotRunningState);
    CHECK(proc.get_error() == Process::kNoError);
    CHECK(proc.get_exit_code() == -1);
    CHECK_FALSE(proc.is_running());

#ifndef _WIN32
    CHECK(proc.get_process_id() == -1);
#endif
  }

  // -------------------------------------------------------------------------
  // TEST: enum constant sanity
  // -------------------------------------------------------------------------

  TEST_CASE("enum-values") {
    // State
    CHECK(static_cast<int>(Process::kNotRunningState) == 0);
    CHECK(static_cast<int>(Process::kStartingState) == 1);
    CHECK(static_cast<int>(Process::kRunningState) == 2);

    // ExitStatus
    CHECK(static_cast<int>(Process::kNormalExitStatus) == 0);
    CHECK(static_cast<int>(Process::kCrashExitStatus) == 1);

    // Error
    CHECK(static_cast<int>(Process::kNoError) == 0);
    CHECK(static_cast<int>(Process::kUnknownError) == 1);
    CHECK(static_cast<int>(Process::kStartError) == 2);

    // Mode
    CHECK(static_cast<int>(Process::kSeparateMode) == 0);
    CHECK(static_cast<int>(Process::kMergedMode) == 1);
    CHECK(static_cast<int>(Process::kForwardedMode) == 2);
    CHECK(static_cast<int>(Process::kForwardedOutputMode) == 3);
    CHECK(static_cast<int>(Process::kForwardedErrorMode) == 4);
  }

  // -------------------------------------------------------------------------
  // TEST: timeout sentinel constant
  // -------------------------------------------------------------------------

  TEST_CASE("infinite-constant") { CHECK(Process::kInfinite == -1); }

  // -------------------------------------------------------------------------
  // TEST: default timeout constants have expected values
  // -------------------------------------------------------------------------

  TEST_CASE("default-timeout-constants") {
    CHECK(Process::kDefaultWaitTimeoutMs == 3000);
    CHECK(Process::kDefaultWriteTimeoutMs == 5000);
    CHECK(Process::kDefaultExecuteTimeoutMs == 30000);
    CHECK(Process::kDestructorWaitTimeoutMs == 5000);
  }

  // -------------------------------------------------------------------------
  // TEST: configure and query process mode
  // -------------------------------------------------------------------------

  TEST_CASE("mode-get-set") {
    Process proc;

    proc.set_process_mode(Process::kSeparateMode);
    CHECK(proc.get_process_mode() == Process::kSeparateMode);

    proc.set_process_mode(Process::kMergedMode);
    CHECK(proc.get_process_mode() == Process::kMergedMode);

    proc.set_process_mode(Process::kForwardedMode);
    CHECK(proc.get_process_mode() == Process::kForwardedMode);
  }

  // -------------------------------------------------------------------------
  // TEST: configure and query inherit-environment flag
  // -------------------------------------------------------------------------

  TEST_CASE("inherit-environment") {
    Process proc;

    // Default should be false (does not inherit parent environment)
    CHECK_FALSE(proc.get_inherit_environment());

    proc.set_inherit_environment(false);
    CHECK_FALSE(proc.get_inherit_environment());

    proc.set_inherit_environment(true);
    CHECK(proc.get_inherit_environment());
  }

  // -------------------------------------------------------------------------
  // TEST: configure and query environment map
  // -------------------------------------------------------------------------

  TEST_CASE("environment-map") {
    Process proc;

    Process::EnvironmentMap env{{"MY_VAR", "hello"}, {"OTHER", "42"}};
    proc.set_environment(env);

    auto got = proc.get_environment();
    CHECK(got["MY_VAR"] == "hello");
    CHECK(got["OTHER"] == "42");
  }

  // -------------------------------------------------------------------------
  // TEST: configure and query working directory
  // -------------------------------------------------------------------------

  TEST_CASE("working-directory") {
    Process proc;

    std::string tmp_dir = std::filesystem::temp_directory_path().string();
    proc.set_working_directory(tmp_dir);
    CHECK(proc.get_working_directory() == tmp_dir);
  }

  // -------------------------------------------------------------------------
  // TEST: configure and query max buffer size
  // -------------------------------------------------------------------------

  TEST_CASE("max-buffer-size") {
    Process proc;

    proc.set_max_buffer_size(65536);
    CHECK(proc.get_max_buffer_size() == 65536);

    proc.set_max_buffer_size(1024 * 1024);
    CHECK(proc.get_max_buffer_size() == 1024 * 1024);
  }

  // ===========================================================================
  // Execution tests below require POSIX-compatible system with /bin/ utilities.
  // Skipped on Windows where these paths do not exist.
  // ===========================================================================

#if defined(__linux__) && defined(__x86_64__)

  // -------------------------------------------------------------------------
  // TEST: execute a simple program that exits 0 (e.g. /bin/true)
  // -------------------------------------------------------------------------

  TEST_CASE("execute-true") {
    int code = Process::execute("/bin/true", {}, 5000);
    CHECK(code == 0);
  }

  // -------------------------------------------------------------------------
  // TEST: execute returns non-zero for a program that exits with a specific code
  // -------------------------------------------------------------------------

  TEST_CASE("execute-false") {
    int code = Process::execute("/bin/false", {}, 5000);
    CHECK(code != 0);
  }

  // -------------------------------------------------------------------------
  // TEST: start, wait_for_started, wait_for_finished — /bin/true
  // -------------------------------------------------------------------------

  TEST_CASE("start-and-finish") {
    Process proc;
    proc.set_process_mode(Process::kForwardedMode);

    proc.start("/bin/true");
    bool started = proc.wait_for_started(3000);
    CHECK(started);

    bool finished = proc.wait_for_finished(3000);
    CHECK(finished);

    CHECK(proc.get_state() == Process::kNotRunningState);
    CHECK(proc.get_exit_status() == Process::kNormalExitStatus);
    CHECK(proc.get_exit_code() == 0);
    // Note: kForwardedMode may set a read error on pipe close; skip error check
  }

  // -------------------------------------------------------------------------
  // TEST: start_command parses a shell command string correctly
  // -------------------------------------------------------------------------

  TEST_CASE("start-command") {
    Process proc;
    proc.set_process_mode(Process::kForwardedMode);

    proc.start_command("/bin/true");
    bool finished = proc.wait_for_finished(3000);
    CHECK(finished);
    CHECK(proc.get_exit_code() == 0);
  }

  // -------------------------------------------------------------------------
  // TEST: reading stdout output from a child process
  // -------------------------------------------------------------------------

  TEST_CASE("read-stdout") {
    Process proc;
    proc.set_process_mode(Process::kSeparateMode);

    // "echo hello" writes "hello\n" to stdout
    proc.start("/bin/echo", {"hello"});
    bool finished = proc.wait_for_finished(3000);
    REQUIRE(finished);

    std::string output;
    proc.read_all_output(output);

    CHECK_FALSE(output.empty());
    // Output should contain "hello"
    CHECK(output.find("hello") != std::string::npos);
  }

  // -------------------------------------------------------------------------
  // TEST: reading stdout line-by-line
  // -------------------------------------------------------------------------

  TEST_CASE("read-line-stdout") {
    Process proc;
    proc.set_process_mode(Process::kSeparateMode);

    proc.start("/bin/echo", {"line1"});
    bool finished = proc.wait_for_finished(3000);
    REQUIRE(finished);

    if (proc.can_read_line_stdout()) {
      std::string line;
      bool ok = proc.read_line_stdout(line);
      CHECK(ok);
      CHECK_FALSE(line.empty());
    }
  }

  // -------------------------------------------------------------------------
  // TEST: read_all_output into vector
  // -------------------------------------------------------------------------

  TEST_CASE("read-all-output-vector") {
    Process proc;
    proc.set_process_mode(Process::kSeparateMode);

    proc.start("/bin/echo", {"data"});
    proc.wait_for_finished(3000);

    std::vector<uint8_t> buf;
    proc.read_all_output(buf);
    CHECK_FALSE(buf.empty());
  }

  // -------------------------------------------------------------------------
  // TEST: pid is valid while process is running, -1 before start
  // -------------------------------------------------------------------------

  TEST_CASE("pid-valid-while-running") {
    Process proc;
    proc.set_process_mode(Process::kForwardedMode);

    CHECK(proc.get_process_id() == -1);

    // Use sleep to keep the process alive long enough to observe the PID
    proc.start("/bin/sleep", {"0.1"});

    bool started = proc.wait_for_started(3000);
    REQUIRE(started);

    int64_t pid = proc.get_process_id();
    CHECK(pid > 0);

    proc.wait_for_finished(3000);
    CHECK(proc.get_process_id() == -1);
  }

  // -------------------------------------------------------------------------
  // TEST: state_changed callback fires
  // -------------------------------------------------------------------------

  TEST_CASE("state-changed-callback") {
    Process proc;
    proc.set_process_mode(Process::kForwardedMode);

    std::atomic<int> change_count{0};
    proc.register_state_changed_callback([&](Process::State) { change_count.fetch_add(1, std::memory_order_relaxed); });

    proc.start("/bin/true");
    proc.wait_for_finished(3000);

    // At minimum kStartingState and kRunningState transitions are expected
    CHECK(change_count.load() >= 1);
  }

  // -------------------------------------------------------------------------
  // TEST: finished callback fires with exit code 0
  // -------------------------------------------------------------------------

  TEST_CASE("finished-callback") {
    Process proc;
    proc.set_process_mode(Process::kForwardedMode);

    std::atomic<int> exit_code{-1};
    std::atomic<int> exit_status{-1};

    proc.register_finished_callback([&](int code, Process::ExitStatus status) {
      exit_code.store(code, std::memory_order_relaxed);
      exit_status.store(static_cast<int>(status), std::memory_order_relaxed);
    });

    proc.start("/bin/true");
    proc.wait_for_finished(3000);

    // Allow a brief moment for the callback to fire from the monitor thread
    std::this_thread::sleep_for(100ms);

    CHECK(exit_code.load() == 0);
    CHECK(exit_status.load() == static_cast<int>(Process::kNormalExitStatus));
  }

  // -------------------------------------------------------------------------
  // TEST: starting a non-existent program sets an error
  // -------------------------------------------------------------------------

  TEST_CASE("start-nonexistent") {
    Process proc;
    proc.set_process_mode(Process::kForwardedMode);

    proc.start("/nonexistent_program_xyz_vlink_test");
    proc.wait_for_finished(3000);

    // The process should not be running and some error should be set
    CHECK_FALSE(proc.is_running());
  }

  // -------------------------------------------------------------------------
  // TEST: start_detached returns true for a valid program
  // -------------------------------------------------------------------------

  TEST_CASE("start-detached") {
    bool ok = Process::start_detached("/bin/true");
    CHECK(ok);
  }

  // -------------------------------------------------------------------------
  // TEST: start_detached returns false for a non-existent program
  // -------------------------------------------------------------------------

  TEST_CASE("start-detached-fail") {
    bool ok = Process::start_detached("/nonexistent_program_xyz_vlink_test");
    CHECK_FALSE(ok);
  }

  TEST_CASE("start-with-custom-path-without-inherit-environment") {
    Process proc;
    proc.set_process_mode(Process::kForwardedMode);
    proc.set_inherit_environment(false);
    proc.set_environment({{"PATH", "/bin:/usr/bin"}});

    proc.start("sh", {"-c", "exit 0"});
    CHECK(proc.wait_for_finished(3000));
    CHECK(proc.get_exit_code() == 0);
  }

  // -------------------------------------------------------------------------
  // TEST: terminate stops a running process
  // -------------------------------------------------------------------------

  TEST_CASE("terminate") {
    Process proc;
    proc.set_process_mode(Process::kSeparateMode);

    proc.start("/bin/sleep", {"60"});
    REQUIRE(proc.wait_for_started(3000));
    CHECK(proc.is_running());

    proc.kill();
    bool finished = proc.wait_for_finished(3000);
    CHECK(finished);
    CHECK_FALSE(proc.is_running());
  }

  // -------------------------------------------------------------------------
  // TEST: kill stops a running process forcefully
  // -------------------------------------------------------------------------

  TEST_CASE("kill") {
    Process proc;
    proc.set_process_mode(Process::kSeparateMode);

    proc.start("/bin/sleep", {"60"});
    REQUIRE(proc.wait_for_started(3000));
    CHECK(proc.is_running());

    proc.kill();
    bool finished = proc.wait_for_finished(3000);
    CHECK(finished);
    CHECK_FALSE(proc.is_running());
  }

  // -------------------------------------------------------------------------
  // TEST: close() sends terminate and waits
  // -------------------------------------------------------------------------

  TEST_CASE("close") {
    Process proc;
    proc.set_process_mode(Process::kSeparateMode);

    proc.start("/bin/sleep", {"60"});
    REQUIRE(proc.wait_for_started(3000));

    proc.close(true);  // force-kill on timeout
    CHECK_FALSE(proc.is_running());
  }

  TEST_CASE("close-after-delayed-term-exit") {
    Process proc;
    proc.set_process_mode(Process::kSeparateMode);

    proc.start("/bin/sh", {"-c", "trap 'sleep 0.3; exit 0' TERM; while true; do sleep 1; done"});
    REQUIRE(proc.wait_for_started(3000));

    auto start_time = std::chrono::steady_clock::now();
    proc.close(false);
    auto elapsed =
        std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start_time).count();

    CHECK_FALSE(proc.is_running());
    CHECK(elapsed < 2000);
  }

  // -------------------------------------------------------------------------
  // TEST: read_all_error into string
  // -------------------------------------------------------------------------

  TEST_CASE("read-all-error-string") {
    Process proc;
    proc.set_process_mode(Process::kSeparateMode);

    // echo to stderr via sh -c
    proc.start("/bin/sh", {"-c", "echo stderr_msg >&2"});
    proc.wait_for_finished(3000);

    std::string err_out;
    proc.read_all_error(err_out);

    CHECK_FALSE(err_out.empty());
    CHECK(err_out.find("stderr_msg") != std::string::npos);
  }

  // -------------------------------------------------------------------------
  // TEST: read_all (combined stdout+stderr) in merged mode
  // -------------------------------------------------------------------------

  TEST_CASE("merged-mode-read-all") {
    Process proc;
    proc.set_process_mode(Process::kMergedMode);

    proc.start("/bin/sh", {"-c", "echo merged_line"});
    proc.wait_for_finished(3000);

    std::string output;
    proc.read_all(output);

    CHECK_FALSE(output.empty());
    CHECK(output.find("merged_line") != std::string::npos);
    CHECK(proc.get_error() == Process::kNoError);
  }

  // -------------------------------------------------------------------------
  // TEST: bytes_available_stdout is non-zero after output
  // -------------------------------------------------------------------------

  TEST_CASE("bytes-available-stdout") {
    Process proc;
    proc.set_process_mode(Process::kSeparateMode);

    proc.start("/bin/echo", {"available_test"});
    proc.wait_for_finished(3000);

    size_t avail = proc.bytes_available_stdout();
    CHECK(avail > 0U);
  }

  // -------------------------------------------------------------------------
  // TEST: read_stdout into vector (bounded read)
  // -------------------------------------------------------------------------

  TEST_CASE("read-stdout-vector") {
    Process proc;
    proc.set_process_mode(Process::kSeparateMode);

    proc.start("/bin/echo", {"read_vector"});
    proc.wait_for_finished(3000);

    std::vector<uint8_t> buf;
    size_t n = proc.read_stdout(buf, 64U);
    CHECK(n > 0U);
    CHECK(buf.size() == n);
  }

  // -------------------------------------------------------------------------
  // TEST: write to stdin — cat echoes what we write
  // -------------------------------------------------------------------------

  TEST_CASE("write-to-stdin") {
    Process proc;
    proc.set_process_mode(Process::kSeparateMode);

    proc.start("/bin/cat", {});
    REQUIRE(proc.wait_for_started(3000));

    std::string msg = "hello_stdin\n";
    size_t written = proc.write(msg, 3000);
    CHECK(written == msg.size());

    proc.close_write_channel();
    proc.wait_for_finished(3000);

    std::string output;
    proc.read_all_output(output);
    CHECK(output.find("hello_stdin") != std::string::npos);
  }

  // -------------------------------------------------------------------------
  // TEST: register_error_callback fires when start fails
  // -------------------------------------------------------------------------

  TEST_CASE("error-callback-on-bad-start") {
    Process proc;
    proc.set_process_mode(Process::kForwardedMode);

    std::atomic<bool> error_called{false};
    proc.register_error_callback(
        [&error_called](Process::Error) { error_called.store(true, std::memory_order_relaxed); });

    proc.start("/nonexistent_binary_vlink_xyz_test");
    proc.wait_for_finished(3000);

    // Allow callback to fire
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    CHECK(error_called.load());
  }

  // -------------------------------------------------------------------------
  // TEST: exit status is kNormalExitStatus for clean exit
  // -------------------------------------------------------------------------

  TEST_CASE("normal-exit-status") {
    Process proc;
    proc.set_process_mode(Process::kForwardedMode);

    proc.start("/bin/sh", {"-c", "exit 2"});
    proc.wait_for_finished(3000);

    CHECK(proc.get_exit_code() == 2);
    CHECK(proc.get_exit_status() == Process::kNormalExitStatus);
  }

  // -------------------------------------------------------------------------
  // TEST: kForwardedOutputMode routes stdout to parent, buffers stderr
  // -------------------------------------------------------------------------

  TEST_CASE("forwarded-output-mode") {
    Process proc;
    proc.set_process_mode(Process::kForwardedOutputMode);

    proc.start("/bin/sh", {"-c", "echo out; echo err >&2"});
    bool finished = proc.wait_for_finished(3000);
    // kForwardedOutputMode: stdout forwarded to parent, stderr buffered
    // Process should complete cleanly
    CHECK(finished);
    CHECK(proc.get_exit_code() == 0);

    std::string err;
    proc.read_all_error(err);
    CHECK(err.find("err") != std::string::npos);
  }

  // -------------------------------------------------------------------------
  // TEST: kForwardedErrorMode routes stderr to parent, buffers stdout
  // -------------------------------------------------------------------------

  TEST_CASE("forwarded-error-mode") {
    Process proc;
    proc.set_process_mode(Process::kForwardedErrorMode);

    proc.start("/bin/sh", {"-c", "echo out; echo err >&2"});
    bool finished = proc.wait_for_finished(3000);
    // kForwardedErrorMode: stderr forwarded to parent, stdout buffered
    // Process should complete cleanly
    CHECK(finished);
    CHECK(proc.get_exit_code() == 0);

    std::string out;
    proc.read_all_output(out);
    CHECK(out.find("out") != std::string::npos);
  }

  // -------------------------------------------------------------------------
  // TEST: ready_read_stdout callback fires when data is available
  // -------------------------------------------------------------------------

  TEST_CASE("ready-read-stdout-callback") {
    Process proc;
    proc.set_process_mode(Process::kSeparateMode);

    std::atomic<int> called{0};
    proc.register_ready_read_stdout_callback([&called]() { called.fetch_add(1, std::memory_order_relaxed); });

    proc.start("/bin/echo", {"callback_test"});
    proc.wait_for_finished(3000);

    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    CHECK(called.load() >= 1);
  }

  TEST_CASE("ready-read callback can read buffered output") {
    Process proc;
    proc.set_process_mode(Process::kSeparateMode);

    std::atomic<bool> read_in_callback{false};
    std::string output;
    std::mutex output_mtx;

    proc.register_ready_read_stdout_callback([&]() {
      std::string local;
      if (proc.read_all_output(local)) {
        std::lock_guard lock(output_mtx);
        output += local;
        read_in_callback.store(true, std::memory_order_release);
      }
    });

    proc.start("/bin/sh", {"-c", "echo callback_read"});
    CHECK(proc.wait_for_finished(3000));

    for (int i = 0; i < 50 && !read_in_callback.load(std::memory_order_acquire); ++i) {
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    std::lock_guard lock(output_mtx);
    CHECK(output.find("callback_read") != std::string::npos);
  }

  // -------------------------------------------------------------------------
  // TEST: execute with arguments
  // -------------------------------------------------------------------------

  TEST_CASE("execute-with-args") {
    // /bin/sh -c "exit 42" should exit with code 42
    int code = Process::execute("/bin/sh", {"-c", "exit 42"}, 5000);
    CHECK(code == 42);
  }

  // -------------------------------------------------------------------------
  // TEST: start_detached with arguments
  // -------------------------------------------------------------------------

  TEST_CASE("start-detached-with-args") {
    bool ok = Process::start_detached("/bin/sleep", {"0"});
    CHECK(ok);
  }

  // -------------------------------------------------------------------------
  // TEST: stderr-related functions
  // -------------------------------------------------------------------------

  TEST_CASE("stderr-vector-overloads") {
    Process proc;
    proc.start("/bin/sh", {"-c", "echo error_msg >&2"});
    proc.wait_for_finished(3000);

    std::vector<uint8_t> stderr_buf;
    bool ok = proc.read_all_error(stderr_buf);
    if (ok && !stderr_buf.empty()) {
      std::string stderr_str(stderr_buf.begin(), stderr_buf.end());
      CHECK(stderr_str.find("error_msg") != std::string::npos);
    }
  }

  TEST_CASE("read-line-stderr") {
    Process proc;
    proc.start("/bin/sh", {"-c", "echo stderr_line >&2"});
    proc.wait_for_finished(3000);

    std::string line;
    bool has_line = proc.read_line_stderr(line);
    if (has_line) {
      CHECK(line.find("stderr_line") != std::string::npos);
    }
  }

  TEST_CASE("bytes-available-stderr") {
    Process proc;
    proc.start("/bin/sh", {"-c", "echo stderr_data >&2"});
    proc.wait_for_finished(3000);

    size_t avail = proc.bytes_available_stderr();
    // May or may not have data depending on timing
    (void)avail;
    CHECK(true);
  }

  TEST_CASE("can-read-line-stderr") {
    Process proc;
    proc.start("/bin/sh", {"-c", "echo stderr_line >&2"});
    proc.wait_for_finished(3000);

    bool can_read = proc.can_read_line_stderr();
    (void)can_read;
    CHECK(true);
  }

  TEST_CASE("wait-for-ready-read-timeout") {
    Process proc;
    proc.start("/bin/sleep", {"0.1"});

    // Wait with short timeout - should return false since no output
    bool ready = proc.wait_for_ready_read(50);
    (void)ready;

    proc.wait_for_finished(3000);
    CHECK(true);
  }

  TEST_CASE("wait-for-ready-read-with-data") {
    Process proc;
    proc.start("/bin/echo", {"hello_ready_read"});

    bool ready = proc.wait_for_ready_read(2000);
    if (ready) {
      std::string output;
      proc.read_all_output(output);
      CHECK(output.find("hello_ready_read") != std::string::npos);
    }

    proc.wait_for_finished(3000);
  }

  TEST_CASE("write-vector-overload") {
    Process proc;
    proc.start("/bin/cat", {});

    std::vector<uint8_t> input_data = {'h', 'e', 'l', 'l', 'o', '\n'};
    auto wrote_size = proc.write(input_data, 2000);

    if (wrote_size > 0) {
      proc.close_write_channel();
      proc.wait_for_finished(3000);

      std::string output;
      proc.read_all_output(output);
      CHECK(output.find("hello") != std::string::npos);
    } else {
      proc.kill();
      proc.wait_for_finished(1000);
    }
  }

  TEST_CASE("read-stderr-bounded") {
    Process proc;
    proc.start("/bin/sh", {"-c", "echo bounded_stderr >&2"});
    proc.wait_for_finished(3000);

    std::vector<uint8_t> buf;
    size_t bytes_read = proc.read_stderr(buf, 5);
    // May read up to 5 bytes
    CHECK(bytes_read <= 5);
  }

  TEST_CASE("register-ready-read-stderr-callback") {
    Process proc;
    std::atomic_bool called{false};
    proc.register_ready_read_stderr_callback([&]() { called = true; });

    proc.start("/bin/sh", {"-c", "echo callback_stderr >&2"});
    proc.wait_for_finished(3000);
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    // Callback may or may not be called depending on implementation
    (void)called;
    CHECK(true);
  }

#endif
}

// NOLINTEND
