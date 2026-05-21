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
 * @file sys_sharemem.h
 * @brief Named cross-process shared memory region.
 *
 * @details
 * @c SysSharemem wraps the POSIX shared-memory API (@c shm_open + @c mmap) on
 * Linux/macOS and the Win32 @c CreateFileMapping / @c MapViewOfFile API on
 * Windows to provide a named shared memory region that multiple processes can
 * map into their address space simultaneously.
 *
 * Lifecycle:
 * -# Construct a @c SysSharemem object.
 * -# Creator process: call @c create(name, size) to allocate and map the region.
 * -# Peer processes: call @c attach(name) to map the existing region.
 * -# Access the raw memory via @c data().
 * -# Call @c detach(force) to unmap.  If @c force is @c true, POSIX backends
 *    also unlink the backing object from the OS namespace.
 * -# The destructor calls @c detach() automatically with @c force == @c false.
 *
 * Access modes:
 *
 * | Mode        | Description                                       |
 * | ----------- | ------------------------------------------------- |
 * | kReadOnly   | Maps with PROT_READ (no write access)             |
 * | kReadWrite  | Maps with PROT_READ | PROT_WRITE (default)        |
 *
 * @note
 * - POSIX shared-memory names must start with '/' (e.g., @c "/vlink_cam0").
 *   On Windows any non-empty name is valid.
 * - A newly created region is provided by the OS mapping backend and is zero-filled
 *   on the supported POSIX/Windows implementations.
 * - Concurrent access from multiple processes requires external synchronisation
 *   (e.g., @c SysSemaphore or a mutex in the shared region itself).
 * - @c size() returns 0 when not attached.
 *
 * @par Example
 * @code
 * // Process A (creator):
 * vlink::SysSharemem shm;
 * shm.create("/vlink_frame", 1024 * 1024);  // 1 MB
 * auto* frame = static_cast<FrameHeader*>(shm.data());
 * frame->seq = 0;
 * sem.release();  // signal Process B
 *
 * // Process B (consumer):
 * vlink::SysSharemem shm;
 * shm.attach("/vlink_frame");
 * sem.acquire();
 * const auto* frame = static_cast<const FrameHeader*>(shm.data());
 * process_frame(*frame);
 * @endcode
 */

#pragma once

#include <cstdint>
#include <memory>
#include <string>

#include "./macros.h"

namespace vlink {

/**
 * @class SysSharemem
 * @brief Named cross-process shared memory backed by the OS IPC layer.
 *
 * @details
 * Provides a typed @c data() pointer into a shared memory region that is
 * accessible by any process that attaches the same name.
 */
class VLINK_EXPORT SysSharemem final {
 public:
  /**
   * @brief Access mode for the shared memory mapping.
   *
   * @note The enum names mirror POSIX @c O_RDONLY / @c O_RDWR semantics.
   *       On Linux and macOS the implementation uses @c shm_open with the
   *       appropriate flags; on Windows it maps to @c CreateFileMapping /
   *       @c MapViewOfFile page-protection constants.  The observable
   *       behaviour (read-only vs. read-write access) is identical across
   *       all supported platforms.
   */
  enum Mode : uint8_t {
    kReadOnly = 0,  ///< Read-only access (no write permitted).
    kReadWrite = 1  ///< Read-write access.
  };

  /**
   * @brief Default constructor.  The object is not attached until @c create()
   *        or @c attach() is called.
   */
  SysSharemem();

  /**
   * @brief Destructor.  Calls @c detach(false) if still attached.
   */
  ~SysSharemem();

  /**
   * @brief Creates a new named shared memory region of the given size and maps it.
   *
   * @details
   * Fails if a region with the same name already exists.  The supported
   * POSIX/Windows backends provide a zero-filled new region.
   *
   * @param name  OS name for the shared memory object (POSIX: must start with '/').
   * @param size  Size of the region in bytes.
   * @param mode  Access mode (default: @c kReadWrite).
   * @return @c true on success, @c false on failure.
   */
  bool create(const std::string& name, size_t size, Mode mode = kReadWrite);

  /**
   * @brief Attaches to an existing named shared memory region.
   *
   * @details
   * The region must have been created by another process (or an earlier call
   * to @c create()) before @c attach() can succeed.
   *
   * @param name  OS name of the shared memory object.
   * @param mode  Access mode (default: @c kReadWrite).
   * @return @c true on success, @c false if the region does not exist or
   *         access is denied.
   */
  bool attach(const std::string& name, Mode mode = kReadWrite);

  /**
   * @brief Unmaps the shared memory region and optionally unlinks the OS object.
   *
   * @details
   * After @c detach(), @c is_attached() returns @c false and @c data() returns
   * @c nullptr.
   *
   * @param force  If @c true, POSIX backends unlink the backing OS object.
   *               Set @c false to merely unmap without destroying the object
   *               (other processes keep access). On Windows this only closes
   *               the local mapping handle. Default: @c true.
   * @return @c true on success, @c false if not attached or unmap failed.
   */
  bool detach(bool force = true);

  /**
   * @brief Returns @c true if the region is currently mapped.
   *
   * @return @c true if attached (mapped), @c false otherwise.
   */
  [[nodiscard]] bool is_attached() const;

  /**
   * @brief Returns a writable pointer to the beginning of the shared memory region.
   *
   * @return Pointer to the mapped region, or @c nullptr if not attached or in
   *         read-only mode (use the @c const overload for read-only access).
   */
  [[nodiscard]] void* data();

  /**
   * @brief Returns a read-only pointer to the beginning of the shared memory region.
   *
   * @return Const pointer to the mapped region, or @c nullptr if not attached.
   */
  [[nodiscard]] const void* data() const;

  /**
   * @brief Returns the size of the mapped region in bytes.
   *
   * @return Region size in bytes, or 0 if not attached.
   */
  [[nodiscard]] size_t size() const;

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;

  VLINK_DISALLOW_COPY_AND_ASSIGN(SysSharemem)
};

}  // namespace vlink
