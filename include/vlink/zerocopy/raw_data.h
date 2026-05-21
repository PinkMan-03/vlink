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
 * @file raw_data.h
 * @brief Generic zero-copy raw-byte data container for VLink transport.
 *
 * @details
 * @c RawData wraps an untyped byte buffer together with a @c Header for
 * sequencing and timestamping.  It is the simplest zero-copy container in
 * VLink and serves as a building block when the payload format is opaque or
 * application-defined.
 *
 * The struct is exactly 64 bytes on 64-bit platforms (verified via
 * @c static_assert).  Three ownership modes allow callers to manage memory
 * without extra copies wherever possible:
 *
 * | Mode               | Created by                     | Owns memory |
 * | ------------------ | ------------------------------ | ----------- |
 * | Owned              | @c create(size)                | Yes         |
 * | Shallow (borrow)   | @c shallow_copy(ptr, size)     | No          |
 * | Deserialised       | @c operator<<(bytes)           | No          |
 *
 * @par Binary wire format
 * @code
 * [ magic_begin (4) | RawData struct (64) | payload bytes (N) | magic_end (4) ]
 * @endcode
 * Magic numbers allow the receiver to detect data corruption before
 * @c memcpy-ing the struct header.  The struct block is a raw snapshot of the
 * 64-bit ABI layout used by this library; receivers must parse it through
 * @c operator<< and must not treat embedded pointer/ownership fields as
 * portable wire values.
 *
 * @par Usage
 * @code
 * vlink::zerocopy::RawData rd;
 * rd.header.seq = 1;
 * rd.header.time_pub = get_time_ns();
 * rd.create(1024);
 * std::memcpy(const_cast<uint8_t*>(rd.data()), src_buf, 1024);
 *
 * vlink::Bytes wire;
 * rd >> wire;                  // serialise to Bytes
 *
 * vlink::zerocopy::RawData rd2;
 * rd2 << wire;                 // deserialise from Bytes (zero-copy, borrows wire)
 * @endcode
 *
 * @note
 * - 32-bit architectures emit a compile-time warning and are not supported.
 * - After @c operator<<, the internal data pointer references memory inside
 *   the source @c Bytes object.  The @c Bytes must outlive the @c RawData.
 * - @c fill_data is an alias for @c deep_copy(uint8_t*, size_t).
 */

#pragma once

#include <cstdint>

#include "../base/bytes.h"
#include "./header.h"

namespace vlink {

namespace zerocopy {

/**
 * @struct RawData
 * @brief Generic zero-copy raw-byte data container with Header metadata.
 *
 * @details
 * Manages an untyped byte payload together with a @c Header for sequencing
 * and timestamping.  The struct size is fixed at 64 bytes on 64-bit targets.
 * Copies of the struct are either shallow (borrow the data pointer) or deep
 * (allocate and copy).  The move constructor and move-assignment transfer
 * ownership from the source without allocation.
 */
struct VLINK_EXPORT_AND_ALIGNED(8) RawData final {
  /**
   * @brief Default constructor.
   *
   * @details
   * Verifies via @c static_assert that the struct is exactly 64 bytes on
   * 64-bit platforms.  32-bit architectures emit a compile-time warning.
   */
  RawData() noexcept;

  /**
   * @brief Destructor.
   *
   * @details
   * Frees the owned data buffer if @c is_owner() is @c true.
   */
  ~RawData() noexcept;

  /**
   * @brief Copy constructor.
   *
   * @details
   * Performs a deep copy of @p target, allocating a new buffer and copying
   * the payload.
   *
   * @param target Source to copy from.
   */
  RawData(const RawData& target) noexcept;

  /**
   * @brief Move constructor.
   *
   * @details
   * Transfers ownership from @p target.  After the call @p target is empty
   * and no longer owns any buffer.
   *
   * @param target Source to move from.
   */
  RawData(RawData&& target) noexcept;

  /**
   * @brief Copy-assignment operator.
   *
   * @details
   * Deep-copies @p target into @c *this.  Self-assignment is a no-op.
   *
   * @param target Source to copy from.
   * @return        Reference to @c *this.
   */
  RawData& operator=(const RawData& target) noexcept;

  /**
   * @brief Move-assignment operator.
   *
   * @details
   * Transfers ownership from @p target into @c *this.  Self-assignment is a
   * no-op.  After the call @p target is empty.
   *
   * @param target Source to move from.
   * @return        Reference to @c *this.
   */
  RawData& operator=(RawData&& target) noexcept;

  /**
   * @brief Deserialises a @c RawData from a @c Bytes wire buffer.
   *
   * @details
   * Validates the magic-number envelope, then @c memcpy's the raw struct
   * snapshot from the buffer.  The internal data pointer is set to point
   * directly into @p bytes (zero-copy, @c is_owner() == false).  The caller
   * must ensure @p bytes outlives this @c RawData.
   *
   * @param bytes Serialised buffer produced by @c operator>>.
   * @return       @c true on success, @c false if the buffer is invalid or
   *               the total size does not match @c get_serialized_size().
   */
  bool operator<<(const Bytes& bytes) noexcept;

  /**
   * @brief Serialises this @c RawData into a @c Bytes wire buffer.
   *
   * @details
   * Writes the magic-number envelope, this object's raw struct snapshot, and
   * the payload into @p bytes.  If @p bytes is the wrong size it is reallocated
   * automatically.
   *
   * @param bytes Output buffer (resized if necessary).
   * @return       Always @c true.
   */
  bool operator>>(Bytes& bytes) const noexcept;

  /**
   * @brief Checks whether @p bytes contains a valid @c RawData wire format.
   *
   * @details
   * Verifies that the buffer is large enough and that both the begin and end
   * magic numbers match the expected constants.
   *
   * @param bytes Buffer to check.
   * @return       @c true if the magic numbers are present and the minimum
   *               size constraint is satisfied.
   */
  [[nodiscard]] static bool check_valid(const Bytes& bytes) noexcept;

  /**
   * @brief Returns the total serialised byte count for this @c RawData.
   *
   * @details
   * Computed as: @code sizeof(magic_begin) + sizeof(RawData) + size() + sizeof(magic_end) @endcode
   *
   * @return Total number of bytes that @c operator>> will write.
   */
  [[nodiscard]] size_t get_serialized_size() const noexcept;

  /**
   * @brief Returns @c true if the data pointer is non-null and the size is non-zero.
   *
   * @return @c true when there is a valid payload available.
   */
  [[nodiscard]] bool is_valid() const noexcept;

  /**
   * @brief Borrows (shallow-copies) the payload from another @c RawData.
   *
   * @details
   * Sets the internal pointer to the same buffer as @p target without
   * copying data.  @c is_owner() becomes @c false.  The source must outlive
   * this object.  Any previously owned buffer is freed first.
   *
   * @param target Source to borrow from.
   * @return        @c false if @p target == @c *this, otherwise @c true.
   */
  bool shallow_copy(const RawData& target) noexcept;

  /**
   * @brief Deep-copies the payload from another @c RawData.
   *
   * @details
   * Allocates a new buffer and copies the payload from @p target.  If @c *this
   * already owns a buffer of the same size, the data is copied in-place without
   * reallocation.
   *
   * @param target Source to copy from.
   * @return        @c false if @p target == @c *this, otherwise @c true.
   */
  bool deep_copy(const RawData& target) noexcept;

  /**
   * @brief Transfers ownership from another @c RawData.
   *
   * @details
   * Equivalent to a move operation implemented as a member function.  After
   * the call @p target is empty and @c is_owner() on @p target is @c false.
   *
   * @param target Source to move from.
   * @return        @c false if @p target == @c *this, otherwise @c true.
   */
  bool move_copy(RawData& target) noexcept;

  /**
   * @brief Allocates an owned buffer of @p size bytes.
   *
   * @details
   * Any previously owned buffer is freed before the new allocation.  The
   * new buffer content is uninitialised.
   *
   * @param size Number of bytes to allocate.  Must be non-zero.
   * @return      @c false if @p size is zero, otherwise @c true.
   */
  bool create(size_t size) noexcept;

  /**
   * @brief Releases all resources and resets all fields to zero.
   *
   * @details
   * Frees the owned buffer if @c is_owner() is @c true.  The @c Header is
   * also zeroed.
   */
  void clear() noexcept;

  /**
   * @brief Borrows a raw pointer without copying.
   *
   * @details
   * Sets the internal pointer to @p data without allocating.  Any previously
   * owned buffer is freed.  The caller is responsible for the lifetime of
   * @p data.
   *
   * @param data Pointer to the data to borrow.  Must be non-null.
   * @param size Size of the data in bytes.  Must be non-zero.
   * @return      @c false if @p data is null, @p size is zero, or @p data
   *              already equals the current internal pointer.
   */
  bool shallow_copy(uint8_t* data, size_t size) noexcept;

  /**
   * @brief Copies data from a raw pointer into an owned buffer.
   *
   * @details
   * If @c *this already owns a buffer of the same @p size the data is copied
   * in-place; otherwise a new buffer is allocated first via @c create().
   *
   * @param data Source data pointer.  Must be non-null.
   * @param size Number of bytes to copy.  Must be non-zero.
   * @return      @c false if @p data is null, @p size is zero, this object
   *              claims ownership but has no buffer, or @p data already equals
   *              the current internal pointer; otherwise @c true.
   */
  bool deep_copy(uint8_t* data, size_t size) noexcept;

  /**
   * @brief Alias for @c deep_copy(uint8_t*, size_t).
   *
   * @param data Source data pointer.
   * @param size Number of bytes.
   * @return      Result of the underlying @c deep_copy call.
   */
  bool fill_data(uint8_t* data, size_t size) noexcept;

  /**
   * @brief Returns a mutable reference to the 16-bit user-reserved field.
   *
   * @details
   * This field travels through serialisation and can be used by the application
   * for flags or minor sub-type identification without extending the struct.
   *
   * @return Mutable reference to @c reserved_buf_.
   */
  [[nodiscard]] uint16_t& reserved_buf() noexcept;

  /**
   * @brief Returns a read-only pointer to the payload bytes.
   *
   * @return Pointer to the payload.  Empty deserialised payloads may still
   *         hold a non-null borrowed pointer; use @c size() / @c is_valid() to
   *         test for usable payload bytes.
   */
  [[nodiscard]] const uint8_t* data() const noexcept;

  /**
   * @brief Returns the payload size in bytes.
   *
   * @return Number of payload bytes, or 0 if the object is empty.
   */
  [[nodiscard]] size_t size() const noexcept;

  /**
   * @brief Returns @c true if this object owns its data buffer.
   *
   * @details
   * An owned buffer is freed in the destructor.  Non-owned buffers (created by
   * @c shallow_copy or @c operator<<) are never freed.
   *
   * @return @c true when memory ownership is held.
   */
  [[nodiscard]] bool is_owner() const noexcept;

  Header header;  ///< Sequencing and timestamp metadata for this data packet.

  static constexpr bool kZerocopyTypes{true};  ///< Internal marker for VLink zero-copy type traits.

 private:
  uint8_t* data_{nullptr};
  size_t size_{0};
  bool is_owner_{false};
  uint16_t reserved_buf_{0};

  static constexpr uint32_t kMagicNumberBegin{0x98B7F11A};
  static constexpr uint32_t kMagicNumberEnd{0x98B7F11F};
};

}  // namespace zerocopy

}  // namespace vlink
