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
 * @file proxy_data.h
 * @brief Zero-copy proxy message envelope carrying raw payload plus routing metadata.
 *
 * @details
 * @c ProxyData is used by the VLink proxy layer to bundle a serialised message
 * with its routing context (URL, serialisation type, source hostname) and
 * control fields (control ID, mode, timestamp, sequence number) into a single
 * flat allocation.
 *
 * The struct is exactly 80 bytes on 64-bit platforms.  Internally a single
 * owned buffer is laid out as:
 *
 * @code
 * [ raw data | url string | ser type string | hostname string ]
 * @endcode
 *
 * Position and length of each region are stored as @c uint32_t fields in the
 * struct itself so that after binary round-trip the sub-buffers can be accessed
 * as @c std::string_view without additional allocation.
 *
 * @par Binary wire format
 * @code
 * [ magic_begin (4) | ProxyData struct (80) | variable payload | magic_end (4) ]
 * @endcode
 *
 * @par Usage
 * @code
 * vlink::zerocopy::ProxyData pd;
 * pd.set_control_id(42);
 * pd.set_timestamp(get_time_us());
 * pd.create(raw_bytes, "dds://my/topic", "demo.proto.PointCloud",
 *           static_cast<uint32_t>(SchemaType::kProtobuf), "host01");
 *
 * vlink::Bytes wire;
 * pd >> wire;                         // serialise
 *
 * vlink::zerocopy::ProxyData pd2;
 * pd2 << wire;                        // deserialise (zero-copy, borrows wire)
 * auto url = pd2.url();               // std::string_view into wire
 * @endcode
 *
 * @note
 * - 32-bit architectures emit a compile-time warning and are not supported.
 * - After @c operator<<, all sub-buffer string_views reference memory inside
 *   the source @c Bytes.  The @c Bytes must outlive the @c ProxyData.
 * - @c is_valid() performs an additional internal consistency check that all
 *   position + size values are contiguous and sum correctly.
 */

#pragma once

#include <cstdint>
#include <string_view>

#include "../base/bytes.h"

namespace vlink {

namespace zerocopy {

/**
 * @struct ProxyData
 * @brief Proxy routing envelope: raw payload bundled with URL, serialisation type, and hostname.
 *
 * @details
 * Used internally by the VLink proxy subsystem.  The struct is 80 bytes on
 * 64-bit targets.  The variable-length tail buffer is allocated once by
 * @c create() and is contiguous:
 * @c [raw | url | ser | hostname].
 */
struct VLINK_EXPORT_AND_ALIGNED(8) ProxyData final {
  /**
   * @brief Default constructor.
   *
   * @details
   * Verifies via @c static_assert that the struct is exactly 80 bytes on
   * 64-bit platforms.
   */
  ProxyData() noexcept;

  /**
   * @brief Destructor -- frees the owned tail buffer if @c is_owner() is @c true.
   */
  ~ProxyData() noexcept;

  /**
   * @brief Copy constructor -- deep-copies @p target.
   *
   * @param target  Source to copy.
   */
  ProxyData(const ProxyData& target) noexcept;

  /**
   * @brief Move constructor -- transfers ownership from @p target.
   *
   * @details
   * After the call @p target is empty and does not own any buffer.
   *
   * @param target  Source to move from.
   */
  ProxyData(ProxyData&& target) noexcept;

  /**
   * @brief Copy-assignment operator.
   *
   * @param target  Source to copy.  Self-assignment is a no-op.
   * @return        Reference to @c *this.
   */
  ProxyData& operator=(const ProxyData& target) noexcept;

  /**
   * @brief Move-assignment operator.
   *
   * @param target  Source to move.  Self-assignment is a no-op.
   * @return        Reference to @c *this.
   */
  ProxyData& operator=(ProxyData&& target) noexcept;

  /**
   * @brief Deserialises a @c ProxyData from a @c Bytes wire buffer.
   *
   * @details
   * Validates magic-number sentinels and the internal region layout
   * (positions and sizes must be contiguous and sum to @c size_).
   * The tail buffer pointer references memory inside @p bytes (zero-copy);
   * @p bytes must outlive this @c ProxyData.
   *
   * @param bytes  Buffer produced by @c operator>>.
   * @return       @c true on success; @c false on magic-number mismatch,
   *               size inconsistency, or invalid region layout.
   */
  bool operator<<(const Bytes& bytes) noexcept;

  /**
   * @brief Serialises this @c ProxyData into a @c Bytes wire buffer.
   *
   * @details
   * Writes the magic-number envelope, the struct fields, and the tail
   * payload into @p bytes, resizing it as needed.
   *
   * @param bytes  Output buffer (reallocated automatically if necessary).
   * @return       Always @c true.
   */
  bool operator>>(Bytes& bytes) const noexcept;

  /**
   * @brief Returns the proxy control identifier.
   *
   * @return Value set by @c set_control_id().
   */
  [[nodiscard]] uint32_t control_id() const noexcept;

  /**
   * @brief Returns the proxy operation mode.
   *
   * @return Value set by @c set_mode().
   */
  [[nodiscard]] uint32_t mode() const noexcept;

  /**
   * @brief Returns the message timestamp in microseconds.
   *
   * @return Value set by @c set_timestamp().
   */
  [[nodiscard]] int64_t timestamp() const noexcept;

  /**
   * @brief Returns the message sequence number.
   *
   * @return Value set by @c set_seq().
   */
  [[nodiscard]] int64_t seq() const noexcept;

  /**
   * @brief Returns the coarse schema family carried with this payload.
   *
   * @return Numeric @c SchemaType value stored by @c set_schema().
   */
  [[nodiscard]] uint32_t schema() const noexcept;

  /**
   * @brief Returns a shallow @c Bytes view of the raw message payload.
   *
   * @details
   * The returned @c Bytes points into the internal tail buffer without
   * copying.  It must not outlive this @c ProxyData.
   *
   * @return Shallow @c Bytes of the raw payload, or empty if not set.
   */
  [[nodiscard]] Bytes raw() const noexcept;

  /**
   * @brief Returns the topic URL as a @c string_view.
   *
   * @details
   * Points into the internal tail buffer.  Lifetime is tied to this object.
   *
   * @return View of the URL string, or empty if not set.
   */
  [[nodiscard]] std::string_view url() const noexcept;

  /**
   * @brief Returns the serialisation type string as a @c string_view.
   *
   * @details
   * For example @c "demo.proto.PointCloud" or @c "demo.fbs.CameraFrame".
   * Points into the internal tail buffer.
   *
   * @return View of the serialisation type string, or empty if not set.
   */
  [[nodiscard]] std::string_view ser() const noexcept;

  /**
   * @brief Returns the source hostname as a @c string_view.
   *
   * @details
   * Optional field; empty if not provided to @c create().
   * Points into the internal tail buffer.
   *
   * @return View of the hostname string, or empty if not set.
   */
  [[nodiscard]] std::string_view hostname() const noexcept;

  /**
   * @brief Sets the proxy control identifier.
   *
   * @param control_id  Control identifier value.
   */
  void set_control_id(uint32_t control_id) noexcept;

  /**
   * @brief Sets the proxy operation mode.
   *
   * @param mode  Mode value.
   */
  void set_mode(uint32_t mode) noexcept;

  /**
   * @brief Sets the message timestamp in microseconds.
   *
   * @param timestamp  Timestamp value (microseconds since epoch).
   */
  void set_timestamp(int64_t timestamp) noexcept;

  /**
   * @brief Sets the message sequence number.
   *
   * @param seq  Sequence number.
   */
  void set_seq(int64_t seq) noexcept;

  /**
   * @brief Sets the coarse schema family associated with the payload.
   *
   * @param schema  Numeric @c SchemaType value.
   */
  void set_schema(uint32_t schema) noexcept;

  /**
   * @brief Checks whether @p bytes contains a valid @c ProxyData wire buffer.
   *
   * @details
   * Verifies minimum buffer size and both magic-number sentinels.
   *
   * @param bytes  Buffer to validate.
   * @return       @c true if magic numbers match and size is sufficient.
   */
  [[nodiscard]] static bool check_valid(const Bytes& bytes) noexcept;

  /**
   * @brief Returns the total serialised byte count for this @c ProxyData.
   *
   * @details
   * Equals: @c sizeof(magic_begin) + @c sizeof(ProxyData) + @c size() + @c sizeof(magic_end)
   *
   * @return Total bytes written by @c operator>>.
   */
  [[nodiscard]] size_t get_serialized_size() const noexcept;

  /**
   * @brief Returns @c true when all internal region positions and sizes are consistent.
   *
   * @details
   * Checks that raw, url, ser, and hostname regions are contiguous and that
   * their combined size equals the tail buffer size.  Also requires a non-null
   * data pointer and non-zero total size.
   *
   * @return @c true when the object holds valid, usable data.
   */
  [[nodiscard]] bool is_valid() const noexcept;

  /**
   * @brief Borrows the tail buffer from @p target without copying.
   *
   * @details
   * Sets metadata and pointer to match @p target; @c is_owner() becomes
   * @c false.  Any previously owned buffer is freed.
   *
   * @param target  Source to borrow from.
   * @return        @c false if @p target == @c *this, otherwise @c true.
   */
  bool shallow_copy(const ProxyData& target) noexcept;

  /**
   * @brief Deep-copies the tail buffer from @p target.
   *
   * @details
   * Allocates a new buffer of the same size and copies the payload.  If
   * @c *this already owns a same-size buffer the data is copied in-place.
   *
   * @param target  Source to copy.
   * @return        @c false if @p target == @c *this, otherwise @c true.
   */
  bool deep_copy(const ProxyData& target) noexcept;

  /**
   * @brief Transfers ownership from @p target to @c *this.
   *
   * @details
   * After the call @p target is empty.
   *
   * @param target  Source to move from.
   * @return        @c false if @p target == @c *this, otherwise @c true.
   */
  bool move_copy(ProxyData& target) noexcept;

  /**
   * @brief Constructs the envelope by packing all fields into a single allocation.
   *
   * @details
   * Allocates a buffer of size @c raw.size() + url.size() + ser.size() + hostname.size()
   * and copies each region in order.  Any previously owned buffer is freed first.
   * If any region length or the total length exceeds @c UINT32_MAX, the object
   * is cleared and no buffer is retained.
   *
   * @param raw       Raw serialised message payload.
   * @param url       Topic URL string (e.g., @c "dds://my/topic").
   * @param ser       Serialisation type (e.g., @c "demo.proto.PointCloud").
   * @param schema    Coarse schema family, typically a @c SchemaType value.
   * @param hostname  Optional source hostname; empty if not provided.
   */
  void create(const Bytes& raw, std::string_view url, std::string_view ser, uint32_t schema = 0,
              std::string_view hostname = {}) noexcept;

  /**
   * @brief Releases all resources and resets all fields to zero.
   */
  void clear() noexcept;

  /**
   * @brief Returns the total tail buffer size in bytes.
   *
   * @details
   * Equals the sum of the raw payload, URL, serialisation type, and hostname
   * sizes.
   *
   * @return Total variable-length buffer size, or 0 if empty.
   */
  [[nodiscard]] size_t size() const noexcept;

  /**
   * @brief Returns @c true if this object owns its tail buffer.
   *
   * @details
   * Owned buffers are freed in the destructor.  Borrowed buffers (from
   * @c shallow_copy or @c operator<<) are not freed.
   *
   * @return @c true when memory ownership is held.
   */
  [[nodiscard]] bool is_owner() const noexcept;

  static constexpr bool kZerocopyTypes{true};  /// Internal

 private:
  uint8_t* data_{nullptr};
  size_t size_{0};
  uint32_t control_id_{0};
  uint32_t mode_{0};
  int64_t timestamp_{0};
  int64_t seq_{0};
  uint32_t data_pos_{0};
  uint32_t data_size_{0};
  uint32_t url_pos_{0};
  uint32_t url_size_{0};
  uint32_t ser_pos_{0};
  uint32_t ser_size_{0};
  uint32_t hostname_pos_{0};
  uint32_t hostname_size_{0};
  uint32_t schema_{0};
  bool is_owner_{false};

  static constexpr uint32_t kMagicNumberBegin{0x98B7F12A};
  static constexpr uint32_t kMagicNumberEnd{0x98B7F12F};
};

}  // namespace zerocopy

}  // namespace vlink
