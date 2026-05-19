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
 * @file camera_frame.h
 * @brief Zero-copy camera image frame container for VLink transport.
 *
 * @details
 * @c CameraFrame carries a single image frame -- either uncompressed (YUV, RGB)
 * or compressed (JPEG, H.264, H.265) -- together with a @c Header for sequencing
 * and timestamping.  The struct is exactly 80 bytes on 64-bit platforms.
 *
 * @par Pixel formats
 * | Value               | Description                                |
 * | ------------------- | ------------------------------------------ |
 * | kFormatYuv420       | Planar 4:2:0 (I420)                        |
 * | kFormatYuv422       | Planar 4:2:2                               |
 * | kFormatYuv444       | Planar 4:4:4                               |
 * | kFormatNv12         | Semi-planar YUV 4:2:0 (Y + interleaved UV) |
 * | kFormatNv21         | Semi-planar YUV 4:2:0 (Y + interleaved VU) |
 * | kFormatYuyv         | Packed YUYV 4:2:2                          |
 * | kFormatYvyu         | Packed YVYU 4:2:2                          |
 * | kFormatUyvy         | Packed UYVY 4:2:2                          |
 * | kFormatVyuy         | Packed VYUY 4:2:2                          |
 * | kFormatBgr888Packed | Packed 24-bit BGR                          |
 * | kFormatRgb888Packed | Packed 24-bit RGB                          |
 * | kFormatRgb888Planar | Planar 24-bit RGB                          |
 * | kFormatJpeg         | JPEG compressed                            |
 * | kFormatH264         | H.264 / AVC compressed                     |
 * | kFormatH265         | H.265 / HEVC compressed                    |
 *
 * @par Video stream types
 * | Value        | Description            |
 * | ------------ | ---------------------- |
 * | kStreamI     | I-frame (key frame)    |
 * | kStreamP     | P-frame (predicted)    |
 * | kStreamB     | B-frame (bi-predicted) |
 *
 * @par Binary wire format
 * @code
 * [ magic_begin (4) | CameraFrame struct (80) | pixel data (N) | magic_end (4) ]
 * @endcode
 *
 * @par Usage
 * @code
 * vlink::zerocopy::CameraFrame frame;
 * frame.set_width(1920);
 * frame.set_height(1080);
 * frame.set_format(vlink::zerocopy::CameraFrame::kFormatNv12);
 * frame.create(1920 * 1080 * 3 / 2);
 * // fill frame.data() ...
 *
 * vlink::Bytes wire;
 * frame >> wire;                       // serialise
 *
 * vlink::zerocopy::CameraFrame frame2;
 * frame2 << wire;                      // deserialise (zero-copy, borrows wire)
 * @endcode
 *
 * @note
 * - 32-bit architectures emit a compile-time warning and are not supported.
 * - After @c operator<<, the data pointer references memory inside the source
 *   @c Bytes object.  The @c Bytes must outlive the @c CameraFrame.
 * - @c fill_data is an alias for @c deep_copy(uint8_t*, size_t).
 */

#pragma once

#include <cstdint>

#include "../base/bytes.h"
#include "./header.h"

namespace vlink {

namespace zerocopy {

/**
 * @struct CameraFrame
 * @brief Zero-copy camera image frame with format metadata and Header.
 *
 * @details
 * Carries one complete image frame (any @c Format) along with resolution,
 * channel count, capture frequency, and video stream-type metadata.
 * The struct size is fixed at 80 bytes on 64-bit targets.
 */
struct VLINK_EXPORT_AND_ALIGNED(8) CameraFrame final {
  /**
   * @brief Pixel/codec encoding format of the image payload.
   *
   * @details
   * Values 1-12 describe uncompressed planar/packed formats.
   * Values 101-103 describe compressed codec formats.
   * Pass to @c set_format() and read via @c format().
   */
  enum Format : uint8_t {
    kFormatUnknown = 0,  ///< Unknown or uninitialised format.

    kFormatYuv420 = 1,         ///< Planar YUV 4:2:0 (I420).
    kFormatYuv422 = 2,         ///< Planar YUV 4:2:2.
    kFormatYuv444 = 3,         ///< Planar YUV 4:4:4.
    kFormatNv12 = 4,           ///< Semi-planar YUV 4:2:0 (Y + interleaved UV).
    kFormatNv21 = 5,           ///< Semi-planar YUV 4:2:0 (Y + interleaved VU).
    kFormatYuyv = 6,           ///< Packed YUYV 4:2:2.
    kFormatYvyu = 7,           ///< Packed YVYU 4:2:2.
    kFormatUyvy = 8,           ///< Packed UYVY 4:2:2.
    kFormatVyuy = 9,           ///< Packed VYUY 4:2:2.
    kFormatBgr888Packed = 10,  ///< Packed 24-bit BGR (3 bytes/pixel).
    kFormatRgb888Packed = 11,  ///< Packed 24-bit RGB (3 bytes/pixel).
    kFormatRgb888Planar = 12,  ///< Planar 24-bit RGB (separate R, G, B planes).

    kFormatJpeg = 101,  ///< JPEG compressed image.
    kFormatH264 = 102,  ///< H.264 / AVC compressed video frame.
    kFormatH265 = 103,  ///< H.265 / HEVC compressed video frame.
  };

  /**
   * @brief Video stream frame type for compressed formats.
   *
   * @details
   * Relevant when @c format() is @c kFormatH264 or @c kFormatH265.
   * Set via @c set_stream(); read via @c stream().
   */
  enum Stream : uint8_t {
    kStreamUnknown = 0,  ///< Unknown stream type.
    kStreamI,            ///< I-frame (intra-coded, key frame).
    kStreamP,            ///< P-frame (predicted from previous frame).
    kStreamB,            ///< B-frame (bi-directionally predicted).
  };

  /**
   * @brief Default constructor.
   *
   * @details
   * Verifies via @c static_assert that the struct is exactly 80 bytes on
   * 64-bit platforms.  32-bit architectures emit a compile-time warning.
   */
  CameraFrame() noexcept;

  /**
   * @brief Destructor -- frees the owned pixel buffer if @c is_owner() is @c true.
   */
  ~CameraFrame() noexcept;

  /**
   * @brief Copy constructor -- performs a deep copy of @p target.
   *
   * @param target  Source frame to copy.
   */
  CameraFrame(const CameraFrame& target) noexcept;

  /**
   * @brief Move constructor -- transfers ownership from @p target.
   *
   * @details
   * After the call @p target is empty and does not own any buffer.
   *
   * @param target  Source frame to move from.
   */
  CameraFrame(CameraFrame&& target) noexcept;

  /**
   * @brief Copy-assignment operator -- deep-copies @p target.
   *
   * @param target  Source frame to copy.  Self-assignment is a no-op.
   * @return        Reference to @c *this.
   */
  CameraFrame& operator=(const CameraFrame& target) noexcept;

  /**
   * @brief Move-assignment operator -- transfers ownership from @p target.
   *
   * @param target  Source frame to move.  Self-assignment is a no-op.
   * @return        Reference to @c *this.
   */
  CameraFrame& operator=(CameraFrame&& target) noexcept;

  /**
   * @brief Deserialises a @c CameraFrame from a @c Bytes wire buffer.
   *
   * @details
   * Validates the magic-number envelope, copies the struct header, and sets
   * the pixel data pointer to reference memory inside @p bytes (zero-copy).
   * @c is_owner() will be @c false after a successful call; @p bytes must
   * outlive this @c CameraFrame.
   *
   * @param bytes  Buffer produced by @c operator>>.
   * @return       @c true on success, @c false if the buffer fails validation
   *               or the total size is inconsistent.
   */
  bool operator<<(const Bytes& bytes) noexcept;

  /**
   * @brief Serialises this @c CameraFrame into a @c Bytes wire buffer.
   *
   * @details
   * Writes the magic-number envelope, struct fields, and pixel payload into
   * @p bytes, resizing it if necessary.
   *
   * @param bytes  Output buffer (reallocated automatically if needed).
   * @return       Always @c true.
   */
  bool operator>>(Bytes& bytes) const noexcept;

  /**
   * @brief Checks whether @p bytes contains a valid @c CameraFrame wire buffer.
   *
   * @details
   * Verifies minimum size and both magic-number sentinels.
   *
   * @param bytes  Buffer to validate.
   * @return       @c true if the sentinels match and the size is sufficient.
   */
  [[nodiscard]] static bool check_valid(const Bytes& bytes) noexcept;

  /**
   * @brief Returns the total serialised byte count for this frame.
   *
   * @details
   * Equals: @c sizeof(magic_begin) + @c sizeof(CameraFrame) + @c size() + @c sizeof(magic_end)
   *
   * @return Total bytes written by @c operator>>.
   */
  [[nodiscard]] size_t get_serialized_size() const noexcept;

  /**
   * @brief Returns @c true when the pixel buffer is present and non-empty.
   *
   * @return @c true if @c data() is non-null and @c size() > 0.
   */
  [[nodiscard]] bool is_valid() const noexcept;

  /**
   * @brief Borrows the pixel buffer from @p target without copying.
   *
   * @details
   * Sets header, camera metadata, and data pointer to match @p target;
   * @c is_owner() becomes @c false.  Any previously owned buffer is freed
   * first.  The reserved field is not copied.
   *
   * @param target  Source frame to borrow from.
   * @return        @c false if @p target == @c *this, otherwise @c true.
   */
  bool shallow_copy(const CameraFrame& target) noexcept;

  /**
   * @brief Deep-copies the pixel buffer from @p target.
   *
   * @details
   * If @c *this already owns a same-size buffer the data is copied in-place;
   * otherwise a new buffer is allocated.  The reserved field is not copied.
   *
   * @param target  Source frame to copy.
   * @return        @c false if @p target == @c *this, otherwise @c true.
   */
  bool deep_copy(const CameraFrame& target) noexcept;

  /**
   * @brief Transfers ownership from @p target to @c *this.
   *
   * @details
   * After the call @p target is empty.  Self-move is a no-op.
   *
   * @param target  Source frame to move from.
   * @return        @c false if @p target == @c *this, otherwise @c true.
   */
  bool move_copy(CameraFrame& target) noexcept;

  /**
   * @brief Allocates an owned pixel buffer of @p size bytes.
   *
   * @details
   * Frees any existing owned buffer before allocating the new one.
   * Buffer content is uninitialised after the call.
   *
   * @param size  Number of bytes to allocate.  Must be non-zero.
   * @return      @c false if @p size is zero, otherwise @c true.
   */
  bool create(size_t size) noexcept;

  /**
   * @brief Releases owned resources and resets frame metadata and @c header.
   *
   * @details
   * The reserved field returned by @c get_reserved() is left unchanged.
   */
  void clear() noexcept;

  /**
   * @brief Borrows an external raw pixel pointer without copying.
   *
   * @details
   * Sets the internal data pointer to @p data with @c is_owner() == false.
   * The caller is responsible for the buffer lifetime.
   *
   * @param data  Non-null pointer to pixel data.
   * @param size  Size of the buffer in bytes.
   * @return      @c false on invalid arguments or if the pointer is unchanged.
   */
  bool shallow_copy(uint8_t* data, size_t size) noexcept;

  /**
   * @brief Deep-copies pixel data from a raw pointer.
   *
   * @details
   * Allocates or reuses an owned buffer and copies @p size bytes from @p data.
   *
   * @param data  Source pixel data pointer.  Must be non-null.
   * @param size  Number of bytes to copy.  Must be non-zero.
   * @return      @c false if @p data is null, @p size is zero, this object
   *              claims ownership but has no buffer, or @p data already equals
   *              the current internal pointer; otherwise @c true.
   */
  bool deep_copy(uint8_t* data, size_t size) noexcept;

  /**
   * @brief Alias for @c deep_copy(uint8_t*, size_t).
   *
   * @param data  Source pointer.
   * @param size  Number of bytes.
   * @return      Result of the underlying @c deep_copy call.
   */
  bool fill_data(uint8_t* data, size_t size) noexcept;

  /**
   * @brief Returns the camera channel (sensor index).
   *
   * @return Channel number set by @c set_channel().
   */
  [[nodiscard]] uint32_t channel() const noexcept;

  /**
   * @brief Returns the image width in pixels.
   *
   * @return Width set by @c set_width().
   */
  [[nodiscard]] uint32_t width() const noexcept;

  /**
   * @brief Returns the image height in pixels.
   *
   * @return Height set by @c set_height().
   */
  [[nodiscard]] uint32_t height() const noexcept;

  /**
   * @brief Returns the capture frequency in Hz.
   *
   * @return Frequency set by @c set_freq().
   */
  [[nodiscard]] uint32_t freq() const noexcept;

  /**
   * @brief Returns the pixel/codec encoding format.
   *
   * @return @c Format value set by @c set_format().
   */
  [[nodiscard]] Format format() const noexcept;

  /**
   * @brief Returns the video stream frame type.
   *
   * @return @c Stream value set by @c set_stream().
   */
  [[nodiscard]] Stream stream() const noexcept;

  /**
   * @brief Returns a read-only pointer to the pixel data buffer.
   *
   * @return Pointer to pixel bytes, or @c nullptr if the frame is empty.
   */
  [[nodiscard]] const uint8_t* data() const noexcept;

  /**
   * @brief Returns the pixel buffer size in bytes.
   *
   * @return Number of payload bytes, or 0 if empty.
   */
  [[nodiscard]] size_t size() const noexcept;

  /**
   * @brief Returns @c true if this object owns its pixel buffer.
   *
   * @details
   * An owned buffer is freed in the destructor.  Borrowed buffers (created
   * by @c shallow_copy or @c operator<<) are not freed.
   *
   * @return @c true when memory ownership is held.
   */
  [[nodiscard]] bool is_owner() const noexcept;

  /**
   * @brief Sets the camera channel (sensor index).
   *
   * @param channel  Channel identifier.
   */
  void set_channel(uint32_t channel) noexcept;

  /**
   * @brief Sets the image width in pixels.
   *
   * @param width  Pixel width of the image.
   */
  void set_width(uint32_t width) noexcept;

  /**
   * @brief Sets the image height in pixels.
   *
   * @param height  Pixel height of the image.
   */
  void set_height(uint32_t height) noexcept;

  /**
   * @brief Sets the capture frequency in Hz.
   *
   * @param freq  Capture rate in frames per second.
   */
  void set_freq(uint32_t freq) noexcept;

  /**
   * @brief Sets the pixel/codec encoding format.
   *
   * @param format  One of the @c Format enum values.
   */
  void set_format(Format format) noexcept;

  /**
   * @brief Sets the video stream frame type.
   *
   * @param stream  One of the @c Stream enum values.
   */
  void set_stream(Stream stream) noexcept;

  /**
   * @brief Gets the reserved field.
   *
   * @details
   * This field is not reset by @c clear() and is not copied by the current
   * copy/move helpers.
   *
   * @return Reference to the reserved field.
   */
  uint32_t& get_reserved() noexcept { return reserved_; }

  Header header;  ///< Sequencing and timestamp metadata for this frame.

  static constexpr bool kZerocopyTypes{true};  /// Internal

 private:
  uint8_t* data_{nullptr};
  size_t size_{0};
  uint32_t channel_{0};
  uint32_t width_{0};
  uint32_t height_{0};
  uint32_t freq_{0};
  Format format_{kFormatUnknown};
  Stream stream_{kStreamUnknown};
  bool is_owner_{false};
  uint32_t reserved_{0};

  static constexpr uint32_t kMagicNumberBegin{0x98B7F15A};
  static constexpr uint32_t kMagicNumberEnd{0x98B7F15F};
};

}  // namespace zerocopy

}  // namespace vlink
