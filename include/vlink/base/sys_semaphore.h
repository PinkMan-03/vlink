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
 * @file sys_semaphore.h
 * @brief Named, cross-process counting semaphore backed by the OS IPC layer.
 *
 * @details
 * @c SysSemaphore wraps a named POSIX semaphore on Linux-like POSIX targets,
 * a named Win32 semaphore on Windows, or an in-process dispatch semaphore on
 * macOS.  Cross-process synchronisation via a shared OS name is available on
 * the named backends, not on the macOS dispatch backend.
 *
 * Lifecycle:
 * -# Construct the @c SysSemaphore object.
 * -# Call @c attach(name) to create or open a named semaphore.
 * -# Use @c acquire() / @c release() for P/V operations.
 * -# Call @c detach(force) to close the handle.  If @p force is @c true the
 *    semaphore is also removed from the OS namespace.
 * -# The destructor calls @c detach(false) automatically.
 *
 * @note
 * - The initial count passed to the constructor is only used when the
 *   semaphore is @b created by @c attach().  If the semaphore already exists
 *   in the OS namespace the constructor count is ignored and the existing
 *   value is used.  On macOS, @c attach() creates an unnamed in-process
 *   dispatch semaphore and ignores the name.
 * - @c attach() reports creation/open failures by returning @c false.
 * - Semaphore names on Linux-like POSIX backends must start with '/'
 *   (e.g., @c "/vlink_ready").  On Windows any non-empty string is accepted.
 *
 * @par Example
 * @code
 * // Process A (server) creates the semaphore:
 * vlink::SysSemaphore sem(0);
 * sem.attach("/vlink_ready");
 * do_init();
 * sem.release();  // signal Process B
 *
 * // Process B (client) opens the same semaphore:
 * vlink::SysSemaphore sem;
 * sem.attach("/vlink_ready");
 * sem.acquire();  // blocks until Process A releases
 * @endcode
 */

#pragma once

#include <memory>
#include <string>

#include "./macros.h"

namespace vlink {

/**
 * @class SysSemaphore
 * @brief Named cross-process counting semaphore.
 *
 * @details
 * Backed by the OS named-semaphore API (POSIX @c sem_open or Win32
 * @c CreateSemaphore) where available.  The macOS backend uses an in-process
 * dispatch semaphore.
 */
class VLINK_EXPORT SysSemaphore final {
 public:
  /**
   * @brief Sentinel value for @c acquire() meaning "wait indefinitely".
   */
  static constexpr int kInfinite{-1};

  /**
   * @brief Constructs a @c SysSemaphore with the given initial count.
   *
   * @details
   * The semaphore is not yet attached to any OS name.  Call @c attach()
   * before using @c acquire() or @c release().
   *
   * @param count  Initial count used when a new named semaphore is created
   *               by @c attach() (default: 0).
   */
  explicit SysSemaphore(size_t count = 0);

  /**
   * @brief Destructor.  Calls @c detach(false) if the semaphore is still attached.
   */
  ~SysSemaphore();

  /**
   * @brief Creates or opens a named semaphore with the given name.
   *
   * @details
   * If a semaphore with @p name already exists in the OS namespace, it is
   * opened and the constructor-provided initial count is ignored.  If it does
   * not exist it is created with that count.  On macOS the name is ignored and
   * a new in-process dispatch semaphore is created.
   *
   * @param name  OS semaphore name (Linux-like POSIX: must start with '/';
   *              ignored on macOS).
   * @return @c true on success, @c false if the semaphore could not be created
   *         or opened.
   */
  bool attach(const std::string& name);

  /**
   * @brief Closes the semaphore handle and optionally removes it from the
   *        OS namespace.
   *
   * @details
   * After @c detach(), @c is_attached() returns @c false.
   *
   * @param force  If @c true, the semaphore is unlinked from the OS namespace
   *               on named POSIX backends.  Ignored on Windows/macOS.
   *               Default: @c true.
   * @return @c true on success, @c false if not attached or unlink failed.
   */
  bool detach(bool force = true);

  /**
   * @brief Decrements the semaphore counter by @p n, blocking if necessary.
   *
   * @details
   * Blocks the caller until @p n permits are available or @p timeout_ms
   * milliseconds elapse.
   *
   * @param n           Number of permits to acquire (default: 1).
   * @param timeout_ms  Maximum time to wait in milliseconds.
   *                    Use @c kInfinite (-1) to wait indefinitely (default).
   * @return @c true if permits were acquired, @c false on timeout or error.
   *
   * @pre @c is_attached() must be @c true.
   */
  bool acquire(size_t n = 1, int timeout_ms = kInfinite);

  /**
   * @brief Increments the semaphore counter by @p n.
   *
   * @details
   * Wakes at most @p n threads (from any process) blocked in @c acquire().
   *
   * @param n  Number of permits to release (default: 1).
   *
   * @pre @c is_attached() must be @c true.
   */
  void release(size_t n = 1);

  /**
   * @brief Returns @c true if the semaphore is currently attached to an OS name.
   *
   * @return @c true if attached, @c false otherwise.
   */
  [[nodiscard]] bool is_attached() const;

  /**
   * @brief Returns the current count of the semaphore.
   *
   * @details
   * The value is a snapshot and may change immediately after the call.
   * Intended for diagnostics only.
   *
   * @return Current semaphore count, or 0 if not attached or unsupported on
   *         the current backend.
   */
  [[nodiscard]] size_t get_count() const;

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;

  VLINK_DISALLOW_COPY_AND_ASSIGN(SysSemaphore)
};

}  // namespace vlink
