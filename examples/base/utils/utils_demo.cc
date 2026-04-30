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

// Example: Utils - comprehensive demonstration of platform-agnostic system utilities

#include <vlink/base/logger.h>
#include <vlink/base/utils.h>

#include <chrono>
#include <string>
#include <thread>
#include <vector>

// Demonstrate process and path information queries.
void demo_process_info() {
  VLOG_I("=== Process & Path Information ===");

  int32_t pid = vlink::Utils::get_pid();
  std::string pid_str = vlink::Utils::get_pid_str();
  VLOG_I("  PID (int32): ", pid, "  PID (string): ", pid_str);

  std::string app_path = vlink::Utils::get_app_path();
  std::string app_dir = vlink::Utils::get_app_dir();
  std::string app_name = vlink::Utils::get_app_name();
  VLOG_I("  App path: ", app_path);
  VLOG_I("  App dir:  ", app_dir);
  VLOG_I("  App name: ", app_name);

  std::string host_name = vlink::Utils::get_host_name();
  std::string tmp_dir = vlink::Utils::get_tmp_dir();
  std::string machine_id = vlink::Utils::get_machine_id();
  VLOG_I("  Hostname:   ", host_name);
  VLOG_I("  Tmp dir:    ", tmp_dir);
  VLOG_I("  Machine ID: ", machine_id);
}

// Demonstrate environment variable operations.
void demo_environment() {
  VLOG_I("=== Environment Variables ===");

  // Read an existing variable with a default fallback.
  std::string home = vlink::Utils::get_env("HOME", "/unknown");
  VLOG_I("  HOME = ", home);

  // Set a custom variable, read it back, then unset it.
  vlink::Utils::set_env("VLINK_DEMO_VAR", "hello_vlink", true);
  std::string val = vlink::Utils::get_env("VLINK_DEMO_VAR", "not_found");
  VLOG_I("  VLINK_DEMO_VAR (after set) = ", val);

  vlink::Utils::unset_env("VLINK_DEMO_VAR");
  val = vlink::Utils::get_env("VLINK_DEMO_VAR", "not_found");
  VLOG_I("  VLINK_DEMO_VAR (after unset) = ", val);

  // Read a non-existent variable with default.
  std::string missing = vlink::Utils::get_env("NO_SUCH_VAR_12345", "default_value");
  VLOG_I("  NO_SUCH_VAR_12345 = ", missing);
}

// Demonstrate network address queries.
void demo_network() {
  VLOG_I("=== Network Information ===");

  std::vector<std::string> ipv4_all = vlink::Utils::get_all_ipv4_address(false);
  VLOG_I("  All IPv4 addresses (", ipv4_all.size(), " found):");
  for (const auto& addr : ipv4_all) {
    VLOG_I("    ", addr);
  }

  std::vector<std::string> ipv4_up = vlink::Utils::get_all_ipv4_address(true);
  VLOG_I("  Available IPv4 addresses (", ipv4_up.size(), " found):");
  for (const auto& addr : ipv4_up) {
    std::string iface = vlink::Utils::get_interface_name_by_ipv4(addr);
    VLOG_I("    ", addr, " -> interface: ", iface);
  }

  std::vector<std::string> dds_addrs = vlink::Utils::get_dds_default_address(true, 3);
  VLOG_I("  DDS default addresses (", dds_addrs.size(), " found):");
  for (const auto& addr : dds_addrs) {
    VLOG_I("    ", addr);
  }
}

// Demonstrate singleton check.
void demo_singleton() {
  VLOG_I("=== Singleton Check ===");

  bool is_single = vlink::Utils::check_singleton("vlink_utils_demo");
  VLOG_I("  check_singleton(\"vlink_utils_demo\"): ", is_single ? "true" : "false");

  // A second call with the same name should still return true (same process).
  bool is_single2 = vlink::Utils::check_singleton("vlink_utils_demo");
  VLOG_I("  check_singleton again: ", is_single2 ? "true" : "false");
}

// Demonstrate thread management utilities.
void demo_thread() {
  VLOG_I("=== Thread Management ===");

  // Set the current thread name.
  bool ok = vlink::Utils::set_thread_name("demo_main");
  VLOG_I("  set_thread_name(\"demo_main\"): ", ok ? "success" : "failed");

  // Get native thread ID.
  uint64_t tid = vlink::Utils::get_native_thread_id();
  VLOG_I("  Native thread ID: ", tid);

  // Set thread priority (SCHED_OTHER, priority 0 is typical for non-RT).
  bool prio_ok = vlink::Utils::set_thread_priority(0, -1, nullptr);
  VLOG_I("  set_thread_priority(0): ", prio_ok ? "success" : "failed");

  // Demonstrate yield_cpu in a tight spin loop.
  VLOG_I("  Executing 1000 yield_cpu() calls for busy-wait optimization...");
  for (int i = 0; i < 1000; ++i) {
    vlink::Utils::yield_cpu();
  }
  VLOG_I("  yield_cpu loop completed");
}

// Demonstrate system resource monitoring.
void demo_resource_monitoring() {
  VLOG_I("=== Resource Monitoring ===");

  double cpu = vlink::Utils::get_cpu_usage();
  double mem = vlink::Utils::get_memory_usage();
  VLOG_I("  CPU usage: ", cpu, "%");
  VLOG_I("  Memory usage (RSS): ", mem, "%");

  // Check if our own process is running.
  std::string self_name = vlink::Utils::get_app_name();
  bool running = vlink::Utils::is_process_running(self_name);
  VLOG_I("  is_process_running(\"", self_name, "\"): ", running ? "true" : "false");
}

// Demonstrate timestamp queries using ElapsedTimer static methods.
void demo_timestamps() {
  VLOG_I("=== Timestamps ===");

  // Timezone offset.
  int32_t tz_diff = vlink::Utils::get_timezone_diff();
  int tz_hours = tz_diff / 3600;
  VLOG_I("  Timezone offset: ", tz_diff, "s (UTC", (tz_hours >= 0 ? "+" : ""), tz_hours, ")");

  // Terminal size query.
  auto [cols, rows] = vlink::Utils::get_terminal_size();
  VLOG_I("  Terminal size: ", cols, " cols x ", rows, " rows");
}

// Demonstrate signal registration (non-blocking).
void demo_signal_registration() {
  VLOG_I("=== Signal Registration ===");

  // Register a terminate signal handler (SIGTERM, SIGINT).
  vlink::Utils::register_terminate_signal([](int sig) { VLOG_W("Received termination signal: ", sig); }, false, false);
  VLOG_I("  Terminate signal handler registered (SIGTERM/SIGINT)");

  // Register a crash signal handler (SIGSEGV, SIGABRT, etc.).
  vlink::Utils::register_crash_signal([](int sig) { VLOG_E("Crash signal received: ", sig); });
  VLOG_I("  Crash signal handler registered (SIGSEGV/SIGABRT/SIGFPE/SIGBUS)");
}

int main() {
  VLOG_I("===================================================");
  VLOG_I("  VLink Utils Comprehensive Demo");
  VLOG_I("===================================================");

  demo_process_info();
  demo_environment();
  demo_network();
  demo_singleton();
  demo_thread();
  demo_resource_monitoring();
  demo_timestamps();
  demo_signal_registration();

  // Console UTF-8 setup (no-op on Linux, useful on Windows).
  vlink::Utils::set_console_utf8_output();

  // Hint the OS to release any unused heap pages.
  vlink::Utils::try_release_sys_memory();
  VLOG_I("  try_release_sys_memory() called");

  VLOG_I("===================================================");
  VLOG_I("  Utils demo completed successfully");
  VLOG_I("===================================================");

  vlink::Logger::flush();
  return 0;
}
