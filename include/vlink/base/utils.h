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
 * @file utils.h
 * @brief Platform-agnostic system utilities for process, thread, network and signal management.
 *
 * @details
 * The @c Utils namespace provides a set of free functions that wrap platform-specific
 * system calls into a portable API.  Supported targets include Linux, macOS, Windows,
 * QNX, and Android.
 *
 * @note
 * - All functions are @c noexcept -- errors are indicated by empty strings, @c false
 *   return values or sentinel values (e.g., @c pid == -1).
 * - @c yield_cpu() is inlined and emits the most efficient CPU-pause instruction for
 *   the target architecture (PAUSE on x86, YIELD on ARM, .word 0x0100000f on RISC-V).
 *
 * @par Example
 * @code
 * // Set thread name and pin to cores 0 and 1:
 * vlink::Utils::set_thread_name("worker");
 * vlink::Utils::set_thread_stick(0b11);
 *
 * // Register SIGTERM / SIGINT handler:
 * vlink::Utils::register_terminate_signal([](int sig) {
 *   // shutdown logic
 * });
 * @endcode
 */

#pragma once

#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "./functional.h"
#include "./macros.h"

#ifdef _MSC_VER
extern "C" void _mm_pause(void);
#endif

namespace vlink {

/**
 * @namespace vlink::Utils
 * @brief Platform-agnostic system utility functions.
 */
namespace Utils {  // NOLINT(readability-identifier-naming)

/**
 * @brief Returns the absolute path of the running executable.
 *
 * @return Full file-system path, or empty string on failure.
 */
[[nodiscard]] VLINK_EXPORT std::string get_app_path() noexcept;

/**
 * @brief Returns the directory containing the running executable.
 *
 * @return Directory path (without trailing slash), or empty string on failure.
 */
[[nodiscard]] VLINK_EXPORT std::string get_app_dir() noexcept;

/**
 * @brief Returns the file name of the running executable (without directory prefix).
 *
 * @return Executable name, or empty string on failure.
 */
[[nodiscard]] VLINK_EXPORT std::string get_app_name() noexcept;

/**
 * @brief Returns the host name of the current machine.
 *
 * @return Host name as reported by @c gethostname(), or empty string on failure.
 */
[[nodiscard]] VLINK_EXPORT std::string get_host_name() noexcept;

/**
 * @brief Returns the process ID of the current process.
 *
 * @return Process ID (PID), or -1 on failure.
 */
[[nodiscard]] VLINK_EXPORT int32_t get_pid() noexcept;

/**
 * @brief Returns the process ID of the current process as a decimal string.
 *
 * @return PID string, or empty string on failure.
 */
[[nodiscard]] VLINK_EXPORT std::string get_pid_str() noexcept;

/**
 * @brief Returns the platform-specific temporary directory path.
 *
 * @details
 * Returns @c /tmp on Linux/macOS, @c %TEMP% on Windows, or the equivalent on other platforms.
 *
 * @return Temporary directory path, or empty string on failure.
 */
[[nodiscard]] VLINK_EXPORT std::string get_tmp_dir() noexcept;

/**
 * @brief Reads the value of an environment variable.
 *
 * @param key            Name of the environment variable.
 * @param default_value  Value returned if the variable is not set.  Default: empty string.
 * @return Environment variable value, or @p default_value if not found.
 */
[[nodiscard]] VLINK_EXPORT std::string get_env(const std::string& key, const std::string& default_value = "") noexcept;

/**
 * @brief Sets or updates an environment variable.
 *
 * @param key    Name of the environment variable.
 * @param value  New value.
 * @param force  If @c true, overwrite an existing variable.  Default: @c true.
 * @return @c true on success.
 */
VLINK_EXPORT bool set_env(const std::string& key, const std::string& value, bool force = true) noexcept;

/**
 * @brief Removes an environment variable.
 *
 * @param key  Name of the variable to unset.
 * @return @c true on success.
 */
VLINK_EXPORT bool unset_env(const std::string& key) noexcept;

/**
 * @brief Returns all IPv4 addresses assigned to local network interfaces.
 *
 * @param filter_available  If @c true, only return addresses on interfaces that are UP.
 *                          Default: @c false.
 * @return Vector of dotted-decimal IPv4 address strings.
 */
[[nodiscard]] VLINK_EXPORT std::vector<std::string> get_all_ipv4_address(bool filter_available = false) noexcept;

/**
 * @brief Returns all IPv6 addresses assigned to local network interfaces.
 *
 * @param filter_available  If @c true, only return addresses on interfaces that are UP.
 *                          Default: @c false.
 * @return Vector of IPv6 address strings.
 */
[[nodiscard]] VLINK_EXPORT std::vector<std::string> get_all_ipv6_address(bool filter_available = false) noexcept;

/**
 * @brief Returns the network interface name that owns a given IPv4 address.
 *
 * @param ipv4  Dotted-decimal IPv4 address to look up.
 * @return Interface name (e.g., @c "eth0"), or empty string if not found.
 */
[[nodiscard]] VLINK_EXPORT std::string get_interface_name_by_ipv4(const std::string& ipv4) noexcept;

/**
 * @brief Returns the network interface name that owns a given IPv6 address.
 *
 * @param ipv6  IPv6 address string to look up.
 * @return Interface name, or empty string if not found.
 */
[[nodiscard]] VLINK_EXPORT std::string get_interface_name_by_ipv6(const std::string& ipv6) noexcept;

/**
 * @brief Returns suitable IPv4 addresses for use as DDS participant unicast locators.
 *
 * @details
 * Filters out loopback and link-local addresses, preferring routable unicast addresses.
 *
 * @param filter_available  If @c true, only return addresses on UP interfaces.  Default: @c false.
 * @param max_count         Maximum number of addresses to return.  Default: 5.
 * @return Vector of selected IPv4 address strings.
 */
[[nodiscard]] VLINK_EXPORT std::vector<std::string> get_dds_default_address(bool filter_available = false,
                                                                            int max_count = 5) noexcept;

/**
 * @brief Checks that only one instance of the process is running (singleton guard).
 *
 * @details
 * Uses a lock file or named semaphore to ensure mutual exclusion.
 * Returns @c false if another instance is already running.
 *
 * @param program_name  Program name used for the lock.  Defaults to the executable name.
 * @return @c true if this is the only running instance.
 */
[[nodiscard]] VLINK_EXPORT bool check_singleton(const std::string& program_name = "") noexcept;

/**
 * @brief Blocks until a file-system path appears or the timeout elapses.
 *
 * @details
 * Polls the path every @p poll_ms milliseconds.  Useful for waiting for a device
 * node (e.g., @c /dev/video0) to become available at startup.
 *
 * @param path        File-system path to poll.
 * @param timeout_ms  Maximum wait time in milliseconds.
 * @param poll_ms     Polling interval in milliseconds.  Default: 50.
 * @return @c true if the path appeared within the timeout; @c false on timeout.
 */
VLINK_EXPORT bool wait_for_device(const std::string& path, int timeout_ms, int poll_ms = 50) noexcept;

/**
 * @brief Emits a CPU pause/yield hint to reduce bus contention in busy-wait loops.
 *
 * @details
 * Issues the most efficient idle instruction for the target:
 * - x86/x86-64: @c PAUSE
 * - ARMv7/AArch64: @c YIELD
 * - RISC-V: fence hint (@c .word 0x0100000f)
 * - Fallback: @c std::this_thread::yield()
 *
 * This function is always inlined and has zero call overhead.
 */
VLINK_EXPORT void yield_cpu() noexcept;

/**
 * @brief Configures the Windows console for UTF-8 output.
 *
 * @details
 * Calls @c SetConsoleOutputCP(CP_UTF8) on Windows.  No-op on other platforms.
 */
VLINK_EXPORT void set_console_utf8_output() noexcept;

/**
 * @brief Sets the OS-level name of a thread for debugging tools (e.g., gdb, perf).
 *
 * @param name    Thread name string (max 15 characters on Linux due to @c pthread_setname_np).
 * @param thread  Thread to rename, or @c nullptr for the calling thread.  Default: @c nullptr.
 * @return @c true on success.
 */
VLINK_EXPORT bool set_thread_name(const std::string& name, std::thread* thread = nullptr) noexcept;

/**
 * @brief Sets the scheduling policy and priority of a thread.
 *
 * @details
 * On Linux, wraps @c pthread_setschedparam.  Requires appropriate @c CAP_SYS_NICE or
 * @c RLIMIT_RTPRIO permissions for real-time policies.
 *
 * @param priority_level  Scheduling priority (policy-dependent range).
 * @param policy          Scheduling policy (e.g., @c SCHED_FIFO, @c SCHED_RR, @c SCHED_OTHER).
 *                        Pass -1 to keep the current policy.  Default: -1.
 * @param thread          Thread to configure, or @c nullptr for the calling thread.  Default: @c nullptr.
 * @return @c true on success.
 */
VLINK_EXPORT bool set_thread_priority(int priority_level, int policy = -1, std::thread* thread = nullptr) noexcept;

/**
 * @brief Pins a thread to a set of CPU cores specified by a bitmask.
 *
 * @details
 * Wraps @c pthread_setaffinity_np on Linux.  Bit @c i of @p core_mask corresponds to core @c i.
 * For example, @c core_mask == 0b0101 pins to cores 0 and 2.
 *
 * @param core_mask  Bitmask of CPU cores (bit 0 = core 0).
 * @param thread     Thread to pin, or @c nullptr for the calling thread.  Default: @c nullptr.
 * @return @c true on success.
 */
VLINK_EXPORT bool set_thread_stick(uint32_t core_mask, std::thread* thread = nullptr) noexcept;

/**
 * @brief Returns the native OS thread identifier of the calling thread.
 *
 * @details
 * Returns @c pthread_self() on POSIX or @c GetCurrentThreadId() on Windows.
 * The value can be used with profiling tools or for thread identification in logs.
 *
 * @return Native thread ID.
 */
[[nodiscard]] VLINK_EXPORT uint64_t get_native_thread_id() noexcept;

/**
 * @brief Registers a callback for graceful termination signals (SIGTERM, SIGINT, etc.).
 *
 * @details
 * On POSIX, installs a @c sigaction handler for @c SIGINT, @c SIGTERM and @c SIGHUP.
 * On Windows, hooks @c SIGINT and @c SIGTERM via @c ::signal.
 * The callback receives the signal number as its argument.
 *
 * @param callback      Callback invoked when a termination signal arrives.
 * @param is_async      If @c true, the callback runs asynchronously in a dedicated thread.
 *                      Default: @c false (synchronous in the signal context).
 * @param pass_through  If @c true, re-raise the signal after the callback returns so that
 *                      the default OS behaviour (core dump, etc.) also occurs.  Default: @c false.
 */
VLINK_EXPORT void register_terminate_signal(MoveFunction<void(int)>&& callback, bool is_async = false,
                                            bool pass_through = false) noexcept;

/**
 * @brief Registers a callback for crash signals (SIGSEGV, SIGABRT, SIGFPE, SIGBUS, etc.).
 *
 * @details
 * Useful for dumping logs or state before an unrecoverable crash.
 * The callback should be async-signal-safe or very short.
 *
 * @param callback  Callback invoked with the signal number when a crash signal fires.
 */
VLINK_EXPORT void register_crash_signal(MoveFunction<void(int)>&& callback) noexcept;

/**
 * @brief Starts a background thread that detects keyboard input.
 *
 * @details
 * Polls stdin every @p poll_ms milliseconds for a key press.  When a key is detected,
 * @p callback is invoked with the key name as a string (e.g., @c "enter", @c "q").
 * Stop the detector with @c stop_detect_keyboard().
 *
 * @param callback  Callback invoked with the key name, or @c nullptr to ignore.  Default: @c nullptr.
 * @param poll_ms   Polling interval in milliseconds.  Default: 20.
 */
VLINK_EXPORT void start_detect_keyboard(MoveFunction<void(const std::string& key)>&& callback = nullptr,
                                        int poll_ms = 20) noexcept;

/**
 * @brief Stops the keyboard detection thread started by @c start_detect_keyboard().
 */
VLINK_EXPORT void stop_detect_keyboard() noexcept;

/**
 * @brief Returns the current terminal window dimensions.
 *
 * @return A pair @c {columns, rows}.  Returns @c {0, 0} on failure or if not a tty.
 */
[[nodiscard]] VLINK_EXPORT std::pair<int, int> get_terminal_size() noexcept;

/**
 * @brief Returns the current CPU usage of the process as a percentage.
 *
 * @details
 * Computes @c (user_time + kernel_time) / elapsed_time * 100.  The value is a snapshot
 * since the last call and may return 0 on the first invocation.
 *
 * @return CPU usage in the range [0.0, 100.0 * num_cpus].
 */
[[nodiscard]] VLINK_EXPORT double get_cpu_usage() noexcept;

/**
 * @brief Returns the resident set size (RSS) of the process as a percentage of total RAM.
 *
 * @return Memory usage percentage in the range [0.0, 100.0].
 */
[[nodiscard]] VLINK_EXPORT double get_memory_usage() noexcept;

/**
 * @brief Checks whether a process with the given name is currently running.
 *
 * @param process_name  Executable name to search for (without path).
 * @return @c true if at least one process with that name is running.
 */
[[nodiscard]] VLINK_EXPORT bool is_process_running(const std::string& process_name) noexcept;

/**
 * @brief Returns the local timezone offset from UTC in seconds.
 *
 * @details
 * For example, UTC+8 returns @c 28800, UTC-5 returns @c -18000.
 *
 * @return Timezone offset in seconds.
 */
[[nodiscard]] VLINK_EXPORT int32_t get_timezone_diff() noexcept;

/**
 * @brief Returns a unique identifier for the current machine.
 *
 * @details
 * On Linux, reads @c /etc/machine-id.  On other platforms a platform-specific
 * identifier is used.
 *
 * @return Machine ID string, or empty string on failure.
 */
[[nodiscard]] VLINK_EXPORT std::string get_machine_id() noexcept;

/**
 * @brief Hints to the OS that any unreferenced cached memory pages can be released.
 *
 * @details
 * On Linux calls @c malloc_trim(0) to return freed memory from the glibc heap to the OS.
 * On other platforms this is a no-op.
 */
VLINK_EXPORT void try_release_sys_memory() noexcept;

////////////////////////////////////////////////////////////////
/// Details
////////////////////////////////////////////////////////////////

#if defined(__GNUC__) && !defined(_WIN32) && !defined(__CYGWIN__)
inline __attribute__((artificial)) void yield_cpu() noexcept {
#else
inline void yield_cpu() noexcept {
#endif
#if __has_builtin(__yield)
  __yield();
#elif defined(_MSC_VER) && defined(_YIELD_PROCESSOR)
  _YIELD_PROCESSOR();
#elif __has_builtin(__builtin_ia32_pause)
__builtin_ia32_pause();
#elif defined(__x86_64__) || defined(_M_X64) || defined(__i386) || defined(_M_IX86)
#if defined(_MSC_VER)
_mm_pause();
#elif defined(__GNUC__)
__builtin_ia32_pause();
#else
__asm__("pause");
#endif
#elif __has_builtin(__builtin_arm_yield)
__builtin_arm_yield();
#elif defined(__arm__) || defined(__aarch64__)
#if defined(__GNUC__)
__asm__("yield");
#else
std::this_thread::yield();
#endif
#elif defined(__riscv)
__asm__(".word 0x0100000f");
#elif defined(_MSC_VER) && defined(_YIELD_PROCESSOR)
_YIELD_PROCESSOR();
#else
std::this_thread::yield();
#endif
}

}  // namespace Utils

}  // namespace vlink
