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
 * @file bytes.h
 * @brief Versatile byte buffer with small-buffer optimisation, ownership semantics and compression.
 *
 * @details
 * @c Bytes is the primary binary data carrier in VLink.  Every message serialised or received by
 * a publisher/subscriber is wrapped in a @c Bytes object.  The class is designed to minimise
 * heap allocations on the hot path:
 *
 * - Buffers up to @c kStackSize (96) bytes are stored entirely within the object's inline
 *   @c stack_data_ array (small-buffer optimisation / SBO).
 * - Larger buffers are allocated through @c bytes_malloc() / @c bytes_free(), which forward
 *   to @c MemoryPool::global_instance() (a tiered free-list pool, see @c memory_pool.h).
 * - The total object size is exactly 128 bytes regardless of content.
 *
 * Ownership model:
 *
 * | Factory method                  | Owns memory | On copy     | Use case                          |
 * | ------------------------------- | ----------- | ----------- | --------------------------------- |
 * | @c Bytes::create()              | Yes         | Deep copy   | Fresh allocation                  |
 * | @c Bytes::shallow_copy()        | No          | Ptr alias   | Zero-copy wrapping of extern buf  |
 * | @c Bytes::deep_copy()           | Yes         | Deep copy   | Owned copy of extern buf          |
 * | @c Bytes::loan_internal()       | No (loaned) | Ptr alias   | Iceoryx zero-copy loan            |
 * | @c Bytes::shallow_copy_ptr()    | No          | Ptr alias   | Wrap opaque pointer (size == 0)   |
 *
 * Compression support (LZAV):
 * - @c compress_data() appends a 4-byte header magic and a 4-byte footer magic around the LZAV
 *   compressed payload, enabling @c is_compress_data() to detect compressed buffers.
 * - @c uncompress_data() strips the header/footer and decompresses; optionally validates the
 *   magic bytes first.
 *
 * Utility helpers:
 * - Base-64 encode/decode (@c encode_to_base64 / @c decode_from_base64)
 * - CRC-32 checksum (@c get_crc_32)
 * - Byte-order reversal (@c reverse_order)
 * - Hex-string conversion (@c convert_to_hex_str)
 * - User-input parsing from hex/binary string literals (@c from_user_input)
 *
 * @note
 * - The @c offset_ field reserves a prefix region inside the allocated buffer.  @c data()
 *   returns @c real_data() + @c offset().  This allows transport layers to prepend headers
 *   in-place without re-allocation.
 * - @c is_loaned() is set by @c loan_internal(), which is used exclusively for iceoryx
 *   zero-copy payloads that must @b not be freed by VLink.
 * - @c is_ptr() returns @c true when size == 0 and offset == 0 and the object is not an owner,
 *   meaning the buffer holds only an opaque pointer created via @c shallow_copy_ptr().
 * - @c init_memory_pool() may be called explicitly at application
 *   startup/shutdown if the memory pool is desired.  They are no-ops when the pool is unavailable.
 *
 * @par Example
 * @code
 * // SBO path (no heap allocation):
 * auto buf = vlink::Bytes::create(64);
 * std::memcpy(buf.data(), payload, 64);
 *
 * // Zero-copy shallow wrap:
 * auto view = vlink::Bytes::shallow_copy(ext_ptr, ext_size);
 *
 * // Compression:
 * auto compressed = vlink::Bytes::compress_data(buf.data(), buf.size());
 * if (vlink::Bytes::is_compress_data(compressed.data(), compressed.size())) {
 *   auto original = vlink::Bytes::uncompress_data(compressed.data(), compressed.size());
 * }
 * @endcode
 */

#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

#include "./macros.h"

namespace vlink {

/**
 * @class Bytes
 * @brief Versatile 128-byte byte buffer with SBO, five ownership modes and compression helpers.
 *
 * @details
 * The total object size is always 128 bytes (96-byte inline stack storage + metadata).
 * Allocations larger than 96 bytes spill to the memory pool or system heap.
 */
class VLINK_EXPORT Bytes final {  // size == 128 bytes
 public:
  /**
   * @brief Initialises the global thread-safe memory pool for @c Bytes allocations.
   *
   * @details
   * Triggers construction of the static @c MemoryPool used by @c bytes_malloc.
   * The pool's tier configuration is built from @c MemoryPool::get_default_config(),
   * which honours the @c VLINK_MEMORY_LEVEL environment variable (0..9,
   * default 3; 0 = bypass mode, every allocation goes straight to
   * @c ::operator @c new / @c delete).
   * Call this once at application start before any @c Bytes objects are created.
   * Safe to call multiple times; subsequent calls are no-ops.
   */
  static void init_memory_pool() noexcept;

  /**
   * @brief Releases every fully-free upstream chunk cached by the global
   *        @c Bytes memory pool.
   *
   * @details
   * Forwards to @c MemoryPool::global_instance().clear().  Drops only those
   * cached chunks that have zero live allocations -- chunks containing any
   * @c Bytes buffer still in use stay intact, so the call is safe to invoke
   * without first reaping every @c Bytes instance.  Lifetime counters
   * (@c upstream_alloc_count / @c upstream_alloc_bytes) and the geometric
   * growth state (@c next_chunk_blocks) are preserved.
   *
   * @note
   * Under @c MemoryPool's per-tier locking, this method is safe to call
   * concurrently with other @c Bytes operations (@c create / @c reserve /
   * destruction).  The cleanup pass holds each tier's lock for
   * @c O(N_freelist * N_chunks) work, so callers running it during heavy
   * traffic will see contention on hot tiers -- treat it as a maintenance
   * call (e.g. periodic memory trim, idle-time pruning), not a hot-path
   * primitive.  Calling it never invalidates a still-live @c Bytes
   * instance.
   */
  static void release_memory_pool() noexcept;

  /**
   * @brief Allocates a raw byte buffer from the memory pool (or heap if pool is unavailable).
   *
   * @param size  Number of bytes to allocate.
   * @return Pointer to the allocated memory, or @c nullptr on failure.
   *
   * @note The returned pointer must be freed with @c bytes_free() using the same @p size.
   */
  [[nodiscard]] static uint8_t* bytes_malloc(size_t size) noexcept;

  /**
   * @brief Frees a buffer previously allocated by @c bytes_malloc().
   *
   * @param ptr   Pointer returned by @c bytes_malloc().
   * @param size  Original size passed to @c bytes_malloc().
   */
  static void bytes_free(uint8_t* ptr, size_t size) noexcept;

  /**
   * @brief Creates an owned @c Bytes buffer of the given size.
   *
   * @details
   * Memory is allocated from the pool or heap.  If @p size <= @c kStackSize (96) the inline
   * stack buffer is used instead (no heap allocation).  The contents are @b not initialised.
   *
   * @param size    Number of usable bytes (i.e. @c size() after construction).
   * @param offset  Number of bytes to reserve at the start of the backing buffer as a prefix
   *                region (e.g., for protocol headers). @c data() = @c real_data() + @p offset.
   *                Default: 0.
   * @return A new owned @c Bytes object.
   */
  [[nodiscard]] static Bytes create(size_t size, uint8_t offset = 0) noexcept;

  /**
   * @brief Creates a non-owning @c Bytes alias pointing to an external mutable buffer.
   *
   * @details
   * No memory is allocated or copied.  The caller is responsible for ensuring the external
   * buffer outlives the returned @c Bytes object.  @c is_owner() returns @c false.
   *
   * @param data  Pointer to the external buffer.
   * @param size  Length of the buffer in bytes.
   * @return A non-owning @c Bytes object wrapping @p data.
   */
  [[nodiscard]] static Bytes shallow_copy(uint8_t* data, size_t size) noexcept;

  /**
   * @brief Creates a non-owning @c Bytes alias pointing to an external read-only buffer.
   *
   * @details
   * Same as the mutable overload except the @p data pointer is @c const.
   * Calling the non-const @c data() accessor on the result returns @c nullptr.
   *
   * @param data  Pointer to the external read-only buffer.
   * @param size  Length of the buffer in bytes.
   * @return A non-owning @c Bytes object wrapping @p data.
   */
  [[nodiscard]] static Bytes shallow_copy(const uint8_t* data, size_t size) noexcept;

  /**
   * @brief Wraps an opaque pointer without associating a byte size.
   *
   * @details
   * Sets size == 0 and offset == 0 so that @c is_ptr() returns @c true.  Useful for
   * passing opaque C handles or iceoryx chunk pointers through the @c Bytes transport.
   * The caller owns the pointed-to memory; VLink will not free it.
   *
   * @param data  Opaque pointer to wrap.
   * @return A non-owning, zero-size @c Bytes object carrying the pointer.
   */
  [[nodiscard]] static Bytes shallow_copy_ptr(void* data) noexcept;

  /**
   * @brief Creates an owned deep copy of an external mutable buffer.
   *
   * @details
   * Allocates new memory, copies @p size bytes from @p data, and returns a fully
   * owned @c Bytes object.  Safe even after the original buffer is freed.
   *
   * @param data    Pointer to the source buffer.
   * @param size    Number of bytes to copy.
   * @param offset  Prefix-region size in the new buffer.  Default: 0.
   * @return A new owned @c Bytes containing a copy of the source data.
   */
  [[nodiscard]] static Bytes deep_copy(uint8_t* data, size_t size, uint8_t offset = 0) noexcept;

  /**
   * @brief Creates an owned deep copy of an external read-only buffer.
   *
   * @details
   * Same as the mutable overload but accepts a @c const source pointer.
   *
   * @param data    Pointer to the source read-only buffer.
   * @param size    Number of bytes to copy.
   * @param offset  Prefix-region size in the new buffer.  Default: 0.
   * @return A new owned @c Bytes containing a copy of the source data.
   */
  [[nodiscard]] static Bytes deep_copy(const uint8_t* data, size_t size, uint8_t offset = 0) noexcept;

  /**
   * @brief Creates a loaned (non-owning) alias for an iceoryx zero-copy payload (mutable).
   *
   * @details
   * Marks the object with @c is_loaned() == @c true.  VLink will not free the memory
   * because it is owned by the iceoryx RouDi daemon.  This factory is used internally
   * by the @c shm:// transport backend.
   *
   * @param data  Pointer to the iceoryx chunk payload.
   * @param size  Size of the payload in bytes.
   * @return A loaned @c Bytes object.
   */
  [[nodiscard]] static Bytes loan_internal(uint8_t* data, size_t size) noexcept;

  /**
   * @brief Creates a loaned (non-owning) alias for an iceoryx zero-copy payload (read-only).
   *
   * @details
   * Same as the mutable overload but for @c const payloads.
   *
   * @param data  Pointer to the read-only iceoryx chunk payload.
   * @param size  Size of the payload in bytes.
   * @return A loaned @c Bytes object.
   */
  [[nodiscard]] static Bytes loan_internal(const uint8_t* data, size_t size) noexcept;

  /**
   * @brief Constructs a @c Bytes buffer from a @c std::string by deep-copying its contents.
   *
   * @param str     Source string.
   * @param offset  Prefix-region reserved before the string data.  Default: 0.
   * @return A new owned @c Bytes containing the UTF-8 bytes of @p str.
   */
  [[nodiscard]] static Bytes from_string(const std::string& str, uint8_t offset = 0) noexcept;

  /**
   * @brief Parses a user-provided hex or binary string literal into a @c Bytes buffer.
   *
   * @details
   * Accepts formats such as @c "0x1A2B3C" (hex) or raw binary strings.
   * Sets @p ok to @c false if parsing fails.
   *
   * @param str  Input string to parse.
   * @param ok   Optional pointer set to @c true on success, @c false on failure.
   * @return The parsed @c Bytes, or an empty object on failure.
   */
  [[nodiscard]] static Bytes from_user_input(const std::string& str, bool* ok = nullptr) noexcept;

  /**
   * @brief Converts a raw byte array to an uppercase hex string with spaces.
   *
   * @details
   * Each byte is rendered as two uppercase hex digits followed by a space, e.g.,
   * @c {0x1A, 0xB2} -> @c "1A B2 ".  Useful for logging binary frames.
   *
   * @param value  Pointer to the byte array.
   * @param size   Number of bytes to convert.
   * @return Hex string representation.
   */
  [[nodiscard]] static std::string convert_to_hex_str(const uint8_t* value, size_t size) noexcept;

  /**
   * @brief Returns a new @c Bytes object with the byte order of @p target reversed.
   *
   * @param target  Source buffer to reverse.
   * @return A new owned @c Bytes with bytes in reversed order.
   */
  [[nodiscard]] static Bytes reverse_order(const Bytes& target) noexcept;

  /**
   * @brief Encodes a @c Bytes buffer as a standard Base-64 ASCII string.
   *
   * @param target  Buffer to encode.
   * @return Base-64 encoded string.
   */
  [[nodiscard]] static std::string encode_to_base64(const Bytes& target) noexcept;

  /**
   * @brief Decodes a Base-64 ASCII string into a @c Bytes buffer.
   *
   * @param target  Base-64 encoded string.
   * @return Decoded @c Bytes.  Returns an empty object on invalid input.
   */
  [[nodiscard]] static Bytes decode_from_base64(const std::string& target) noexcept;

  /**
   * @brief Computes the CRC-32 checksum of a @c Bytes buffer.
   *
   * @param target  Buffer to checksum.
   * @return 32-bit CRC value.
   */
  [[nodiscard]] static uint32_t get_crc_32(const Bytes& target) noexcept;

  /**
   * @brief Constructs an empty, unallocated @c Bytes object.
   *
   * @details
   * @c data() returns @c nullptr and @c size() returns 0.
   * @c is_owner() and @c is_loaned() are both @c false.
   */
  Bytes() noexcept;

  /**
   * @brief Copy constructor.
   *
   * @details
   * If @p target is an owner, a deep copy of its data is made.
   * If @p target is a shallow alias (non-owner, non-loaned), the alias is shared.
   * Loaned objects are shallow-copied as well (ownership is not transferred).
   *
   * @param target  Source @c Bytes to copy.
   */
  Bytes(const Bytes& target) noexcept;

  /**
   * @brief Move constructor.
   *
   * @details
   * Transfers all ownership and data from @p target.  After the move, @p target is empty.
   *
   * @param target  Source @c Bytes to move from.
   */
  Bytes(Bytes&& target) noexcept;

  /**
   * @brief Constructs an owned @c Bytes from an initialiser list of byte values.
   *
   * @details
   * Equivalent to @c create(list.size()) followed by a @c memcpy of the list elements.
   *
   * @param list  Initialiser list of @c uint8_t values.
   */
  Bytes(const std::initializer_list<uint8_t>& list) noexcept;

  /**
   * @brief Constructs an owned @c Bytes by deep-copying a @c std::vector<uint8_t>.
   *
   * @param data  Vector whose contents are copied into the new buffer.
   */
  explicit Bytes(const std::vector<uint8_t>& data) noexcept;

  /**
   * @brief Destructor.  Frees owned memory if @c is_owner() is @c true and @c is_loaned() is @c false.
   */
  ~Bytes() noexcept;

  /**
   * @brief Copy assignment operator.
   *
   * @details
   * Releases the current buffer, then copies @p target (deep if owner, shallow otherwise).
   *
   * @param target  Source @c Bytes to copy-assign from.
   * @return Reference to @c *this.
   */
  Bytes& operator=(const Bytes& target) noexcept;

  /**
   * @brief Move assignment operator.
   *
   * @details
   * Releases the current buffer, then transfers all state from @p target.
   *
   * @param target  Source @c Bytes to move from.
   * @return Reference to @c *this.
   */
  Bytes& operator=(Bytes&& target) noexcept;

  /**
   * @brief Assignment from a @c std::vector<uint8_t> (deep copy).
   *
   * @param data  Vector whose contents are deep-copied into this buffer.
   * @return Reference to @c *this.
   */
  Bytes& operator=(const std::vector<uint8_t>& data) noexcept;

  /**
   * @brief Equality comparison -- compares byte content.
   *
   * @param target  @c Bytes to compare with.
   * @return @c true if both objects have identical size and content.
   */
  [[nodiscard]] bool operator==(const Bytes& target) const noexcept;

  /**
   * @brief Inequality comparison -- compares byte content.
   *
   * @param target  @c Bytes to compare with.
   * @return @c true if the objects differ in size or content.
   */
  [[nodiscard]] bool operator!=(const Bytes& target) const noexcept;

  /**
   * @brief Equality comparison with a @c std::vector<uint8_t>.
   *
   * @param data  Vector to compare with.
   * @return @c true if sizes and contents match.
   */
  [[nodiscard]] bool operator==(const std::vector<uint8_t>& data) const noexcept;

  /**
   * @brief Inequality comparison with a @c std::vector<uint8_t>.
   *
   * @param data  Vector to compare with.
   * @return @c true if sizes or contents differ.
   */
  [[nodiscard]] bool operator!=(const std::vector<uint8_t>& data) const noexcept;

  /**
   * @brief Mutable subscript operator.
   *
   * @details
   * Accesses the byte at logical index @p index (i.e., @c real_data()[offset + index]).
   * No bounds checking is performed.
   *
   * @param index  Logical index (0-based from the start of the user data region).
   * @return Reference to the byte at @p index.
   */
  [[nodiscard]] uint8_t& operator[](size_t index) noexcept;

  /**
   * @brief Read-only subscript operator.
   *
   * @param index  Logical index (0-based from the start of the user data region).
   * @return Const reference to the byte at @p index.
   */
  [[nodiscard]] const uint8_t& operator[](size_t index) const noexcept;

  /**
   * @brief Returns a pointer to the start of the user data region (after the prefix offset).
   *
   * @details
   * Equivalent to @c real_data() + @c offset().
   * Returns @c nullptr if the buffer is empty.
   *
   * @return Mutable pointer to user data, or @c nullptr if not allocated.
   */
  [[nodiscard]] uint8_t* data() noexcept;

  /**
   * @brief Returns a const pointer to the start of the user data region.
   *
   * @return Read-only pointer to user data, or @c nullptr if not allocated.
   */
  [[nodiscard]] const uint8_t* data() const noexcept;

  /**
   * @brief Returns a pointer to the very beginning of the backing buffer (before the offset).
   *
   * @details
   * Use this to write protocol headers into the reserved prefix region.
   * @c real_data() + @c offset() == @c data().
   *
   * @return Mutable pointer to the raw buffer origin.
   */
  [[nodiscard]] uint8_t* real_data() noexcept;

  /**
   * @brief Returns a const pointer to the very beginning of the backing buffer.
   *
   * @return Read-only pointer to the raw buffer origin.
   */
  [[nodiscard]] const uint8_t* real_data() const noexcept;

  /**
   * @brief Returns the number of usable bytes (excluding the prefix offset region).
   *
   * @return Size in bytes.  Returns 0 if empty.
   */
  [[nodiscard]] size_t size() const noexcept;

  /**
   * @brief Returns the total backing-buffer size including the prefix offset region.
   *
   * @details
   * @c real_size() == @c size() + @c offset().
   *
   * @return Total size of the backing buffer in bytes.
   */
  [[nodiscard]] size_t real_size() const noexcept;

  /**
   * @brief Returns the allocated capacity of the backing buffer.
   *
   * @details
   * @c capacity() >= @c real_size() always holds.  For SBO buffers, @c capacity() == @c kStackSize.
   *
   * @return Capacity in bytes.
   */
  [[nodiscard]] size_t capacity() const noexcept;

  /**
   * @brief Returns the size of the prefix offset region in bytes.
   *
   * @details
   * @c data() == @c real_data() + @c offset().
   *
   * @return Offset in bytes.
   */
  [[nodiscard]] uint8_t offset() const noexcept;

  /**
   * @brief Returns @c true if this object owns and is responsible for freeing its buffer.
   *
   * @return @c true for objects created via @c create() or @c deep_copy().
   */
  [[nodiscard]] bool is_owner() const noexcept;

  /**
   * @brief Returns @c true if this object holds an iceoryx loaned chunk.
   *
   * @details
   * Loaned objects must not free the underlying memory; ownership belongs to RouDi.
   *
   * @return @c true for objects created via @c loan_internal().
   */
  [[nodiscard]] bool is_loaned() const noexcept;

  /**
   * @brief Returns @c true if the buffer is empty (no data pointer and size == 0).
   *
   * @return @c true if @c data_ == nullptr and @c size_ == 0.
   */
  [[nodiscard]] bool empty() const noexcept;

  /**
   * @brief Iterator begin (mutable) -- same as @c data().
   *
   * @return Pointer to the first usable byte.
   */
  [[nodiscard]] uint8_t* begin() noexcept;

  /**
   * @brief Iterator begin (const) -- same as @c data() const.
   *
   * @return Const pointer to the first usable byte.
   */
  [[nodiscard]] const uint8_t* begin() const noexcept;

  /**
   * @brief Iterator end (mutable) -- one past the last usable byte.
   *
   * @return Pointer to one past the last byte.
   */
  [[nodiscard]] uint8_t* end() noexcept;

  /**
   * @brief Iterator end (const) -- one past the last usable byte.
   *
   * @return Const pointer to one past the last byte.
   */
  [[nodiscard]] const uint8_t* end() const noexcept;

  /**
   * @brief Iterator begin for the full backing buffer (mutable) -- same as @c real_data().
   *
   * @return Pointer to the first byte of the raw buffer (before the offset).
   */
  [[nodiscard]] uint8_t* real_begin() noexcept;

  /**
   * @brief Iterator begin for the full backing buffer (const).
   *
   * @return Const pointer to the first byte of the raw buffer.
   */
  [[nodiscard]] const uint8_t* real_begin() const noexcept;

  /**
   * @brief Iterator end for the full backing buffer (mutable).
   *
   * @return Pointer to one past the last byte of the raw buffer.
   */
  [[nodiscard]] uint8_t* real_end() noexcept;

  /**
   * @brief Iterator end for the full backing buffer (const).
   *
   * @return Const pointer to one past the last byte of the raw buffer.
   */
  [[nodiscard]] const uint8_t* real_end() const noexcept;

  /**
   * @brief Returns @c true if the object holds only an opaque pointer (size == 0 and not owner).
   *
   * @details
   * An object created via @c shallow_copy_ptr() has @c size_ == 0, @c offset_ == 0 and
   * @c is_owner_ == false.  Use @c to_ptr<T>() to retrieve the wrapped pointer.
   *
   * @return @c true if this is a pointer-only wrapper.
   */
  [[nodiscard]] bool is_ptr() const noexcept;

  /**
   * @brief Copies the usable byte region into a new @c std::vector<uint8_t>.
   *
   * @return A vector containing a copy of @c data()[0..size()-1].
   */
  [[nodiscard]] std::vector<uint8_t> to_raw_data() const noexcept;

  /**
   * @brief Returns the usable byte region as a @c std::string.
   *
   * @return A string constructed from @c data() and @c size().
   */
  [[nodiscard]] std::string to_string() const noexcept;

  /**
   * @brief Returns a zero-copy @c std::string_view over the usable byte region.
   *
   * @details
   * The returned view is valid only as long as this @c Bytes object is alive and unmodified.
   *
   * @return @c string_view pointing into the buffer.
   */
  [[nodiscard]] std::string_view to_string_view() const noexcept;

  /**
   * @brief Reinterprets the backing buffer as a pointer to @c T.
   *
   * @details
   * Calls @c reinterpret_cast<T*>(real_data()).  Use with care; alignment must be
   * compatible with @c T.
   *
   * @tparam T  Target type.  Defaults to @c void.
   * @return Pointer to @c T, or @c nullptr if the buffer is empty.
   */
  template <typename T = void>
  [[nodiscard]] T* to_ptr() const noexcept;

  /**
   * @brief Returns the size of the inline stack storage in bytes.
   *
   * @details
   * Buffers of this size or smaller use the embedded @c stack_data_ array and
   * never incur a heap allocation.
   *
   * @return @c kStackSize (96).
   */
  [[nodiscard]] static constexpr uint8_t stack_size() noexcept;

  /**
   * @brief Returns @c true if the platform uses little-endian byte order.
   *
   * @details
   * Determined at compile time from preprocessor macros (@c __BYTE_ORDER__,
   * @c __LITTLE_ENDIAN__, Windows defaults).
   *
   * @return @c true on little-endian platforms (x86, arm-le, etc.).
   */
  [[nodiscard]] static constexpr bool is_little_endian() noexcept;

  /**
   * @brief Returns @c true if the platform uses big-endian byte order.
   *
   * @return @c true on big-endian platforms.  Equivalent to @c !is_little_endian().
   */
  [[nodiscard]] static constexpr bool is_big_endian() noexcept;

  /**
   * @brief Checks whether a raw byte buffer contains LZAV-compressed VLink data.
   *
   * @details
   * Inspects the first 4 bytes for the header magic @c {0x17, 0x49, 0xB2, 0x6F}
   * and the last 4 bytes for the footer magic @c {0xA7, 0x05, 0xED, 0x71}.
   * Both must match for the function to return @c true.
   *
   * @param data  Pointer to the buffer to inspect.
   * @param size  Length of the buffer.
   * @return @c true if header and footer magic are present.
   */
  [[nodiscard]] static bool is_compress_data(const uint8_t* data, size_t size) noexcept;

  /**
   * @brief Compresses a raw byte buffer using the LZAV algorithm.
   *
   * @details
   * Wraps the compressed payload with a 4-byte header magic and a 4-byte footer magic
   * so that @c is_compress_data() can recognise it.  Buffers larger than
   * @c kMaxCompressCacheSize (1 MiB) are rejected and an empty @c Bytes is returned.
   *
   * @param data       Pointer to the uncompressed data.
   * @param size       Number of bytes to compress.
   * @param high_ratio If @c true, uses LZAV high-compression mode (slower but better ratio).
   *                   Default: @c false (normal mode).
   * @return Compressed @c Bytes with header/footer magic, or empty on failure.
   */
  [[nodiscard]] static Bytes compress_data(const uint8_t* data, size_t size, bool high_ratio = false) noexcept;

  /**
   * @brief Decompresses a LZAV-compressed @c Bytes buffer.
   *
   * @details
   * Strips the 4-byte header and footer magic, then calls @c lzav_decompress().
   * If @p check_valid is @c true the magic bytes are validated first; an invalid
   * magic returns an empty @c Bytes.
   *
   * @param data         Pointer to the compressed buffer (including header/footer).
   * @param size         Total size of the compressed buffer.
   * @param check_valid  If @c true, validate the header/footer magic before decompressing.
   *                     Default: @c true.
   * @return Decompressed @c Bytes, or empty on failure.
   */
  [[nodiscard]] static Bytes uncompress_data(const uint8_t* data, size_t size, bool check_valid = true) noexcept;

  /**
   * @brief Releases the buffer and resets the object to the empty state.
   *
   * @details
   * If @c is_owner() is @c true, the backing memory is returned to the pool or heap.
   * After this call, @c empty() returns @c true.
   */
  void clear() noexcept;

  /**
   * @brief Reduces the logical size of the buffer without reallocating.
   *
   * @details
   * Sets @c size_ to @p size if @p size <= current @c size(); the backing buffer is
   * not freed or shrunk.
   *
   * @param size  New logical size in bytes.  Must be <= current @c size().
   * @return @c true on success, @c false if @p size exceeds the current size.
   */
  [[nodiscard]] bool shrink_to(size_t size) noexcept;

  /**
   * @brief Ensures the backing buffer can hold at least @p new_capacity bytes.
   *
   * @details
   * If the current capacity is already >= @p new_capacity this is a no-op.
   * Otherwise a new buffer is allocated and the existing data is copied.
   *
   * @param new_capacity  Minimum required capacity in bytes.
   * @return @c true on success, @c false if allocation failed.
   */
  bool reserve(size_t new_capacity) noexcept;

  /**
   * @brief Resizes the logical data region to @p size bytes.
   *
   * @details
   * If @p size <= current capacity, only @c size_ is updated.
   * If @p size > current capacity, @c reserve(size) is called first.
   * Newly added bytes are @b not initialised.
   *
   * @param size  New desired size in bytes.
   * @return @c true on success, @c false if reallocation failed.
   */
  [[nodiscard]] bool resize(size_t size) noexcept;

  /**
   * @brief Makes this object a non-owning alias of @p bytes (shallow copy in-place).
   *
   * @details
   * Releases the current buffer, then copies the pointer and metadata from @p bytes
   * without copying the underlying data.  After this call @c is_owner() is @c false.
   *
   * @param bytes  Source to alias.
   * @return Reference to @c *this.
   */
  Bytes& shallow_copy(const Bytes& bytes) noexcept;

  /**
   * @brief Replaces this object with a fully owned deep copy of @p bytes.
   *
   * @details
   * Releases the current buffer, allocates new memory, and copies all bytes from
   * @p bytes.  After this call @c is_owner() is @c true.
   *
   * @param bytes  Source to copy.
   * @return Reference to @c *this.
   */
  Bytes& deep_copy(const Bytes& bytes) noexcept;

  /**
   * @brief Converts this object from a non-owning alias to a fully owned deep copy of its own data.
   *
   * @details
   * If the object is already an owner, this is a no-op.
   * Otherwise, new memory is allocated, the current data is copied, and
   * @c is_owner_ is set to @c true.
   *
   * @return Reference to @c *this.
   */
  Bytes& deep_copy_self() noexcept;

  /**
   * @brief Stream insertion operator -- prints bytes as space-separated hex pairs.
   *
   * @param ostream  Output stream.
   * @param target   @c Bytes to print.
   * @return Reference to @p ostream.
   */
  VLINK_EXPORT friend std::ostream& operator<<(std::ostream& ostream, const Bytes& target) noexcept;

 private:
  enum Type : uint8_t {
    kCreate = 0,
    kShallowCopy = 1,
    kDeepCopy = 2,
    kMove = 3,
  };

  Bytes(Type type, uint8_t* data, size_t size, uint8_t offset, bool loaned) noexcept;

  void process_type(Type type, uint8_t* data, size_t size, uint8_t offset, bool loaned, Bytes* tmp = nullptr) noexcept;

  static constexpr uint8_t kStackSize{96};
  alignas(std::max_align_t) uint8_t stack_data_[kStackSize]{0};
  bool is_owner_{false};
  bool is_loaned_{false};
  uint8_t offset_{0};
  uint8_t* data_{nullptr};
  size_t size_{0};
  size_t capacity_{0};
};

////////////////////////////////////////////////////////////////
/// Details
////////////////////////////////////////////////////////////////

inline uint8_t& Bytes::operator[](size_t index) noexcept { return data_[offset_ + index]; }

inline const uint8_t& Bytes::operator[](size_t index) const noexcept { return data_[offset_ + index]; }

inline uint8_t* Bytes::data() noexcept { return data_ ? (data_ + offset_) : nullptr; }

inline const uint8_t* Bytes::data() const noexcept { return data_ ? (data_ + offset_) : nullptr; }

inline uint8_t* Bytes::real_data() noexcept { return data_; }

inline const uint8_t* Bytes::real_data() const noexcept { return data_; }

inline size_t Bytes::size() const noexcept { return size_; }

inline size_t Bytes::real_size() const noexcept { return size_ + offset_; }

inline size_t Bytes::capacity() const noexcept { return capacity_; }

inline uint8_t Bytes::offset() const noexcept { return offset_; }

inline bool Bytes::is_owner() const noexcept { return is_owner_; }

inline bool Bytes::is_loaned() const noexcept { return is_loaned_; }

inline bool Bytes::empty() const noexcept { return data_ == nullptr && size_ == 0; }

inline uint8_t* Bytes::begin() noexcept { return data_ ? (data_ + offset_) : nullptr; }

inline const uint8_t* Bytes::begin() const noexcept { return data_ ? (data_ + offset_) : nullptr; }

inline uint8_t* Bytes::end() noexcept { return data_ ? (data_ + offset_ + size_) : nullptr; }

inline const uint8_t* Bytes::end() const noexcept { return data_ ? (data_ + offset_ + size_) : nullptr; }

inline uint8_t* Bytes::real_begin() noexcept { return data_; }

inline const uint8_t* Bytes::real_begin() const noexcept { return data_; }

inline uint8_t* Bytes::real_end() noexcept { return data_ ? (data_ + offset_ + size_) : nullptr; }

inline const uint8_t* Bytes::real_end() const noexcept { return data_ ? (data_ + offset_ + size_) : nullptr; }

inline bool Bytes::is_ptr() const noexcept { return data_ != nullptr && size_ == 0 && offset_ == 0 && !is_owner_; }

template <typename T>
inline T* Bytes::to_ptr() const noexcept {
  return reinterpret_cast<T*>(data_);
}

inline constexpr uint8_t Bytes::stack_size() noexcept { return kStackSize; }

inline constexpr bool Bytes::is_little_endian() noexcept {
#if defined(_WIN32) || defined(__LITTLE_ENDIAN__) || \
    (defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__)
  return true;
#elif defined(__BIG_ENDIAN__) || (defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__)
  return false;
#else
  return true;
#endif
}

inline constexpr bool Bytes::is_big_endian() noexcept { return !is_little_endian(); }

}  // namespace vlink
