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
 * @file tensor.h
 * @brief Zero-copy N-dimensional tensor container for VLink transport.
 *
 * @details
 * @c Tensor carries a contiguous (or strided-view) N-D array of homogeneous
 * scalars together with a @c Header for sequencing and timestamping.  It is
 * the canonical conduit for neural-network inputs / outputs in autonomous
 * driving and embodied-intelligence stacks: camera feature maps, point-cloud
 * features, language-model hidden states, action heads, diffusion latents,
 * etc.
 *
 * The struct is exactly 248 bytes on 64-bit platforms.  Up to 8 dimensions
 * are supported; both shape and element strides are embedded so that
 * non-contiguous views (e.g. NCHW slice into a larger tensor) round-trip
 * without additional metadata.
 *
 * @par Element types
 * | Value          | C++ type | Size |
 * | -------------- | -------- | ---- |
 * | kBool          | bool     | 1    |
 * | kInt8          | int8_t   | 1    |
 * | kUint8         | uint8_t  | 1    |
 * | kInt16         | int16_t  | 2    |
 * | kUint16        | uint16_t | 2    |
 * | kInt32         | int32_t  | 4    |
 * | kUint32        | uint32_t | 4    |
 * | kInt64         | int64_t  | 8    |
 * | kUint64        | uint64_t | 8    |
 * | kFloat16       | (custom) | 2    |
 * | kBfloat16      | (custom) | 2    |
 * | kFloat32       | float    | 4    |
 * | kFloat64       | double   | 8    |
 *
 * @par Binary wire format
 * @code
 * [ magic_begin (4) | Tensor struct (248) | element bytes (num_elements*element_size) | magic_end (4) ]
 * @endcode
 * The struct block is a raw snapshot of the 64-bit ABI layout used by this
 * library; receivers must parse it through @c operator<< and must not treat
 * embedded pointer/ownership fields as portable wire values.
 *
 * @par Usage
 * @code
 * vlink::zerocopy::Tensor t;
 * t.set_name("image");
 * t.set_layout("NCHW");
 * t.set_dtype(vlink::zerocopy::Tensor::kFloat32);
 * uint32_t shape[] = {1, 3, 224, 224};
 * t.set_shape(shape, 4);                       // also fills strides + num_elements
 * t.create(t.num_elements() * sizeof(float));
 *
 * vlink::Bytes wire;
 * t >> wire;
 *
 * vlink::zerocopy::Tensor t2;
 * t2 << wire;
 * @endcode
 *
 * @note
 * - 32-bit architectures emit a compile-time warning and are not supported.
 * - After @c operator<<, the data pointer references memory inside the
 *   source @c Bytes.  The @c Bytes must outlive the @c Tensor.
 * - @c fill_data is an alias for @c deep_copy(uint8_t*, size_t).
 */

#pragma once

#include <cstdint>
#include <string_view>

#include "../base/bytes.h"
#include "./header.h"

namespace vlink {

namespace zerocopy {

/**
 * @struct Tensor
 * @brief Zero-copy N-D tensor with shape / strides / dtype metadata and Header.
 *
 * @details
 * Stores a contiguous or strided N-D array of homogeneous scalars together
 * with shape, strides, element type, layout label, quantisation parameters,
 * model identifier, and device hint.  The struct size is fixed at 248 bytes
 * on 64-bit targets; a 7-byte reserved tail accommodates future extensions
 * without breaking the wire format.
 */
struct VLINK_EXPORT_AND_ALIGNED(8) Tensor final {
  /**
   * @brief Maximum number of dimensions encoded in @c shape() / @c strides().
   */
  static constexpr uint8_t kMaxRank{8};

  /**
   * @brief Scalar element type tag.
   *
   * @details
   * Pass to @c set_dtype() and read via @c dtype().  The byte size of one
   * element is derived via @c element_size_of() and cached in @c element_size().
   */
  enum DataType : uint8_t {
    kDataUnknown = 0,  ///< Unknown or uninitialised element type.
    kBool = 1,         ///< @c bool (1 byte).
    kInt8 = 2,         ///< @c int8_t (1 byte).
    kUint8 = 3,        ///< @c uint8_t (1 byte).
    kInt16 = 4,        ///< @c int16_t (2 bytes).
    kUint16 = 5,       ///< @c uint16_t (2 bytes).
    kInt32 = 6,        ///< @c int32_t (4 bytes).
    kUint32 = 7,       ///< @c uint32_t (4 bytes).
    kInt64 = 8,        ///< @c int64_t (8 bytes).
    kUint64 = 9,       ///< @c uint64_t (8 bytes).
    kFloat16 = 10,     ///< IEEE-754 half-precision (2 bytes).
    kBfloat16 = 11,    ///< Brain float (2 bytes).
    kFloat32 = 12,     ///< IEEE-754 single-precision (4 bytes).
    kFloat64 = 13,     ///< IEEE-754 double-precision (8 bytes).
  };

  /**
   * @brief Device hint indicating where the tensor data lives.
   */
  enum Device : uint8_t {
    kDeviceCpu = 0,  ///< Host / CPU memory.
    kDeviceGpu = 1,  ///< Discrete or integrated GPU memory.
    kDeviceNpu = 2,  ///< Neural processing unit (e.g. automotive NPU).
    kDeviceDsp = 3,  ///< Digital signal processor.
  };

  /**
   * @brief Default constructor.
   *
   * @details
   * Verifies via @c static_assert that the struct is exactly 248 bytes on
   * 64-bit platforms.  32-bit architectures emit a compile-time warning.
   */
  Tensor() noexcept;

  /**
   * @brief Destructor -- frees the owned data buffer if @c is_owner() is @c true.
   */
  ~Tensor() noexcept;

  /**
   * @brief Copy constructor -- performs a deep copy of @p target.
   */
  Tensor(const Tensor& target) noexcept;

  /**
   * @brief Move constructor -- transfers ownership from @p target.
   */
  Tensor(Tensor&& target) noexcept;

  /**
   * @brief Copy-assignment operator -- deep-copies @p target.
   */
  Tensor& operator=(const Tensor& target) noexcept;

  /**
   * @brief Move-assignment operator -- transfers ownership from @p target.
   */
  Tensor& operator=(Tensor&& target) noexcept;

  /**
   * @brief Deserialises a @c Tensor from a @c Bytes wire buffer.
   *
   * @details
   * Zero-copy: the data pointer references memory inside @p bytes.  Two
   * fields are sanity-corrected after the raw struct memcpy:
   * - @c rank_ is clamped to @c kMaxRank to prevent out-of-bounds shape access.
   * - @c element_size_ is re-derived from @c dtype_ to keep the two consistent
   *   regardless of producer-side bugs.
   *
   * @param bytes Buffer produced by @c operator>>.
   * @return       @c true on success.
   */
  bool operator<<(const Bytes& bytes) noexcept;

  /**
   * @brief Serialises this @c Tensor into a @c Bytes wire buffer.
   *
   * @param bytes Output buffer (reallocated automatically if needed).
   * @return       Always @c true.
   */
  bool operator>>(Bytes& bytes) const noexcept;

  /**
   * @brief Checks whether @p bytes contains a valid @c Tensor wire buffer.
   */
  [[nodiscard]] static bool check_valid(const Bytes& bytes) noexcept;

  /**
   * @brief Returns the total serialised byte count for this tensor.
   */
  [[nodiscard]] size_t get_serialized_size() const noexcept;

  /**
   * @brief Returns @c true when the data buffer is present and non-empty.
   */
  [[nodiscard]] bool is_valid() const noexcept;

  /**
   * @brief Borrows the data buffer from @p target without copying.
   */
  bool shallow_copy(const Tensor& target) noexcept;

  /**
   * @brief Deep-copies the data buffer from @p target.
   */
  bool deep_copy(const Tensor& target) noexcept;

  /**
   * @brief Transfers ownership from @p target.
   */
  bool move_copy(Tensor& target) noexcept;

  /**
   * @brief Allocates an owned data buffer of @p size bytes.
   *
   * @details
   * The caller is responsible for sizing @p size consistently with
   * @c num_elements() * @c element_size().
   */
  bool create(size_t size) noexcept;

  /**
   * @brief Releases owned resources and resets metadata and @c header.
   */
  void clear() noexcept;

  /**
   * @brief Borrows an external raw data pointer without copying.
   */
  bool shallow_copy(uint8_t* data, size_t size) noexcept;

  /**
   * @brief Deep-copies data from a raw pointer.
   */
  bool deep_copy(uint8_t* data, size_t size) noexcept;

  /**
   * @brief Alias for @c deep_copy(uint8_t*, size_t).
   */
  bool fill_data(uint8_t* data, size_t size) noexcept;

  /**
   * @brief Returns the tensor state timestamp in nanoseconds since epoch.
   */
  [[nodiscard]] uint64_t update_time_ns() const noexcept;

  /**
   * @brief Returns the cached total element count (product of shape dims).
   */
  [[nodiscard]] uint64_t num_elements() const noexcept;

  /**
   * @brief Returns the tensor name (e.g. @c "image", @c "hidden_state").
   */
  [[nodiscard]] std::string_view name() const noexcept;

  /**
   * @brief Returns the source model identifier.
   */
  [[nodiscard]] std::string_view model_id() const noexcept;

  /**
   * @brief Returns the layout label (e.g. @c "NCHW", @c "NHWC", @c "BLC").
   */
  [[nodiscard]] std::string_view layout() const noexcept;

  /**
   * @brief Returns a read-only pointer to the @c kMaxRank-sized shape array.
   */
  [[nodiscard]] const uint32_t* shape() const noexcept;

  /**
   * @brief Returns the shape entry at dimension @p dim, or 0 if out of range.
   */
  [[nodiscard]] uint32_t shape_at(uint8_t dim) const noexcept;

  /**
   * @brief Returns a read-only pointer to the @c kMaxRank-sized stride array.
   */
  [[nodiscard]] const uint32_t* strides() const noexcept;

  /**
   * @brief Returns the stride entry at dimension @p dim, or 0 if out of range.
   */
  [[nodiscard]] uint32_t stride_at(uint8_t dim) const noexcept;

  /**
   * @brief Returns the sensor / producer / output-port channel identifier.
   */
  [[nodiscard]] uint32_t channel() const noexcept;

  /**
   * @brief Returns the nominal publish frequency in Hz.
   */
  [[nodiscard]] uint32_t freq() const noexcept;

  /**
   * @brief Returns the cached batch size (typically @c shape_at(0)).
   */
  [[nodiscard]] uint32_t batch_size() const noexcept;

  /**
   * @brief Returns the INT8 quantisation scale (or 0 if unused).
   */
  [[nodiscard]] float quant_scale() const noexcept;

  /**
   * @brief Returns the INT8 quantisation zero point (or 0 if unused).
   */
  [[nodiscard]] int32_t quant_zero_point() const noexcept;

  /**
   * @brief Returns the scalar element type tag.
   */
  [[nodiscard]] DataType dtype() const noexcept;

  /**
   * @brief Returns the actual rank (1..@c kMaxRank).
   */
  [[nodiscard]] uint8_t rank() const noexcept;

  /**
   * @brief Returns the device hint indicating where the data lives.
   */
  [[nodiscard]] Device device() const noexcept;

  /**
   * @brief Returns the byte size of one element (cached from @c dtype()).
   */
  [[nodiscard]] uint8_t element_size() const noexcept;

  /**
   * @brief Returns a read-only pointer to the tensor data buffer.
   */
  [[nodiscard]] const uint8_t* data() const noexcept;

  /**
   * @brief Returns the data buffer size in bytes.
   */
  [[nodiscard]] size_t size() const noexcept;

  /**
   * @brief Returns @c true if this object owns its data buffer.
   */
  [[nodiscard]] bool is_owner() const noexcept;

  /**
   * @brief Sets the tensor state timestamp in nanoseconds since epoch.
   */
  void set_update_time_ns(uint64_t update_time_ns) noexcept;

  /**
   * @brief Sets the tensor name (truncated to fit @c sizeof(name) - 1).
   */
  void set_name(std::string_view name) noexcept;

  /**
   * @brief Sets the source model identifier.
   */
  void set_model_id(std::string_view model_id) noexcept;

  /**
   * @brief Sets the layout label.
   */
  void set_layout(std::string_view layout) noexcept;

  /**
   * @brief Sets the full shape array and recomputes row-major strides and @c num_elements.
   *
   * @details
   * @p rank is clamped to @c kMaxRank.  Strides are derived assuming
   * contiguous row-major layout (last dimension changes fastest).  Also
   * caches @c batch_size from @p shape[0].
   *
   * @param shape Pointer to a shape array of length @p rank.  Must be non-null when @p rank > 0.
   * @param rank Number of valid dimensions (1..@c kMaxRank).
   */
  void set_shape(const uint32_t* shape, uint8_t rank) noexcept;

  /**
   * @brief Sets the shape entry at dimension @p dim.
   *
   * @details
   * Does NOT recompute strides or @c num_elements; call @c set_shape() for
   * a coherent bulk update.
   */
  void set_shape_at(uint8_t dim, uint32_t value) noexcept;

  /**
   * @brief Sets the stride entry at dimension @p dim.
   */
  void set_stride_at(uint8_t dim, uint32_t value) noexcept;

  /**
   * @brief Sets the sensor / producer / output-port channel identifier.
   */
  void set_channel(uint32_t channel) noexcept;

  /**
   * @brief Sets the nominal publish frequency in Hz.
   */
  void set_freq(uint32_t freq) noexcept;

  /**
   * @brief Sets the cached batch size.
   */
  void set_batch_size(uint32_t batch_size) noexcept;

  /**
   * @brief Sets the INT8 quantisation scale factor.
   */
  void set_quant_scale(float quant_scale) noexcept;

  /**
   * @brief Sets the INT8 quantisation zero point.
   */
  void set_quant_zero_point(int32_t quant_zero_point) noexcept;

  /**
   * @brief Sets the scalar element type tag (also caches @c element_size).
   */
  void set_dtype(DataType dtype) noexcept;

  /**
   * @brief Sets the device hint.
   */
  void set_device(Device device) noexcept;

  /**
   * @brief Returns the byte size of one element of @p dtype.
   *
   * @param dtype Element type tag.
   * @return       Element size in bytes (0 for @c kDataUnknown).
   */
  [[nodiscard]] static uint8_t element_size_of(DataType dtype) noexcept;

  /**
   * @brief Gets the reserved field.
   */
  uint32_t& get_reserved() noexcept { return reserved32_; }

  Header header;  ///< Sequencing and timestamp metadata for this tensor.

  static constexpr bool kZerocopyTypes{true};  ///< Internal marker for VLink zero-copy type traits.

 private:
  uint8_t* data_{nullptr};
  size_t size_{0};
  uint64_t update_time_ns_{0};
  uint64_t num_elements_{0};
  char name_[32]{0};
  char model_id_[32]{0};
  char layout_[16]{0};
  uint32_t shape_[kMaxRank]{0};
  uint32_t strides_[kMaxRank]{0};
  uint32_t channel_{0};
  uint32_t freq_{0};
  uint32_t batch_size_{0};
  float quant_scale_{0};
  int32_t quant_zero_point_{0};
  DataType dtype_{kDataUnknown};
  uint8_t rank_{0};
  Device device_{kDeviceCpu};
  bool is_owner_{false};
  uint8_t element_size_{0};
  uint8_t reserved8_{0};
  uint16_t reserved16_{0};
  uint32_t reserved32_{0};

  static constexpr uint32_t kMagicNumberBegin{0x98B7F19A};
  static constexpr uint32_t kMagicNumberEnd{0x98B7F19F};
};

}  // namespace zerocopy

}  // namespace vlink
