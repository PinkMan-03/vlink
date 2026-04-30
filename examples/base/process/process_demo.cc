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

// Example: Process - cross-platform child process management with I/O piping

#include <vlink/base/logger.h>
#include <vlink/base/process.h>

#include <string>
#include <vector>

// Demonstrate basic process creation and synchronous execution.
void demo_synchronous_execute() {
  VLOG_I("=== Synchronous Execute ===");

  // Execute a simple command and get exit code.
  int exit_code = vlink::Process::execute("/bin/echo", {"hello", "from", "vlink"}, 5000);
  VLOG_I("  echo exit code: ", exit_code);

  // Execute ls to list temp directory.
  int ls_code = vlink::Process::execute("/bin/ls", {"/tmp"}, 5000);
  VLOG_I("  ls /tmp exit code: ", ls_code);

  // Execute a non-existent command to demonstrate error handling.
  int bad_code = vlink::Process::execute("/bin/no_such_command_xyz", {}, 3000);
  VLOG_I("  non-existent command exit code: ", bad_code);
}

// Demonstrate async process start with stdout capture.
void demo_async_stdout_capture() {
  VLOG_I("=== Async Process with stdout Capture ===");

  vlink::Process proc;
  proc.set_process_mode(vlink::Process::kSeparateMode);

  // Collect all stdout output after the process finishes.
  proc.start("/bin/echo", {"VLink Process API works!"});
  bool started = proc.wait_for_started(3000);
  VLOG_I("  Process started: ", started ? "yes" : "no");

  if (started) {
    VLOG_I("  Process PID: ", proc.get_process_id());
    bool finished = proc.wait_for_finished(5000);
    VLOG_I("  Process finished: ", finished ? "yes" : "no");

    // Read all stdout output.
    std::string output;
    proc.read_all_output(output);
    VLOG_I("  stdout: ", output);
    VLOG_I("  Exit code: ", proc.get_exit_code());

    auto status = proc.get_exit_status();
    VLOG_I("  Exit status: ", status == vlink::Process::kNormalExitStatus ? "normal" : "crash");
  }
}

// Demonstrate process with arguments and separate stderr capture.
void demo_separate_stderr() {
  VLOG_I("=== Separate stderr Capture ===");

  vlink::Process proc;
  proc.set_process_mode(vlink::Process::kSeparateMode);

  // Use sh -c to produce both stdout and stderr output.
  proc.start("/bin/sh", {"-c", "echo stdout_msg && echo stderr_msg >&2"});
  proc.wait_for_started(3000);
  proc.wait_for_finished(5000);

  std::string out_str;
  std::string err_str;
  proc.read_all_output(out_str);
  proc.read_all_error(err_str);

  VLOG_I("  stdout: ", out_str);
  VLOG_I("  stderr: ", err_str);
  VLOG_I("  Exit code: ", proc.get_exit_code());
}

// Demonstrate writing to the child process stdin.
void demo_stdin_write() {
  VLOG_I("=== Stdin Write (pipe) ===");

  vlink::Process proc;
  proc.set_process_mode(vlink::Process::kSeparateMode);

  // Use cat which echoes stdin to stdout.
  proc.start("/bin/cat", {});
  proc.wait_for_started(3000);

  // Write data to the child's stdin.
  std::string input = "Hello from parent via pipe!\n";
  size_t written = proc.write(input);
  VLOG_I("  Bytes written to stdin: ", written);

  // Close the write channel to signal EOF to cat.
  proc.close_write_channel();

  proc.wait_for_finished(5000);

  std::string output;
  proc.read_all_output(output);
  VLOG_I("  cat echoed back: ", output);
}

// Demonstrate line-by-line reading with callback.
void demo_line_reading() {
  VLOG_I("=== Line-by-Line Reading ===");

  vlink::Process proc;
  proc.set_process_mode(vlink::Process::kSeparateMode);

  // Generate multiple lines of output.
  proc.start("/bin/sh", {"-c", "echo line1 && echo line2 && echo line3"});
  proc.wait_for_started(3000);
  proc.wait_for_finished(5000);

  // Read lines one at a time.
  int line_count = 0;
  std::string line;
  while (proc.can_read_line_stdout()) {
    proc.read_line_stdout(line);
    line_count++;
    VLOG_I("  Line ", line_count, ": ", line);
  }
  VLOG_I("  Total lines read: ", line_count);
}

// Demonstrate process termination.
void demo_terminate() {
  VLOG_I("=== Process Terminate & Kill ===");

  vlink::Process proc;
  proc.set_process_mode(vlink::Process::kForwardedMode);

  // Start a long-running sleep process.
  proc.start("/bin/sleep", {"30"});
  proc.wait_for_started(3000);

  VLOG_I("  Sleep process started, PID=", proc.get_process_id());
  VLOG_I("  is_running: ", proc.is_running() ? "true" : "false");

  // Terminate gracefully.
  proc.terminate();
  bool finished = proc.wait_for_finished(3000);
  VLOG_I("  After terminate, finished=", finished ? "true" : "false");

  if (!finished) {
    // Force kill if terminate did not work.
    proc.kill();
    finished = proc.wait_for_finished(3000);
    VLOG_I("  After kill, finished=", finished ? "true" : "false");
  }

  VLOG_I("  Final state: ", proc.is_running() ? "still running" : "stopped");
  VLOG_I("  Exit code: ", proc.get_exit_code());
}

// Demonstrate state/finish callbacks and detached process.
void demo_callbacks_and_detached() {
  VLOG_I("=== Callbacks & Detached Process ===");

  vlink::Process proc;
  proc.set_process_mode(vlink::Process::kSeparateMode);

  proc.register_finished_callback([](int code, vlink::Process::ExitStatus status) {
    VLOG_I("  [callback] Finished code=", code,
           " status=", status == vlink::Process::kNormalExitStatus ? "normal" : "crash");
  });

  proc.start("/bin/echo", {"callback test"});
  proc.wait_for_finished(5000);

  // Detached process runs independently of the parent.
  bool detached = vlink::Process::start_detached("/bin/true", {});
  VLOG_I("  start_detached: ", detached ? "success" : "failed");
}

int main() {
  VLOG_I("=== VLink Process Management Demo ===");

  demo_synchronous_execute();
  demo_async_stdout_capture();
  demo_separate_stderr();
  demo_stdin_write();
  demo_line_reading();
  demo_terminate();
  demo_callbacks_and_detached();

  VLOG_I("===================================================");
  VLOG_I("  Process demo completed successfully");
  VLOG_I("===================================================");

  vlink::Logger::flush();
  return 0;
}
