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
 * @file object_array.h
 * @brief Zero-copy 3-D object detection / tracking result array for VLink transport.
 *
 * @details
 * @c ObjectArray carries a variable-length list of fixed-size @c Object
 * records produced by 3-D perception, multi-sensor fusion, or tracking
 * pipelines.  Each @c Object captures pose, dimensions, kinematics,
 * classification, tracking identity, and covariance for one obstacle.  The
 * container struct is 112 bytes and each @c Object record is 144 bytes; both
 * are verified at compile time via @c static_assert.  @c Object is
 * intentionally @c alignas(4) (not 8): the wire payload starts at offset
 * @c 4 + @c 112 = @c 116, which is only 4-byte-aligned, so a typed
 * @c objects() pointer would risk SIGBUS on strict-alignment architectures
 * if @c Object required 8-byte alignment.  All record fields are at most
 * 4 bytes wide, so 4-byte alignment is sufficient.
 *
 * @par Binary wire format
 * @code
 * [ magic_begin (4) | ObjectArray struct (112) | Object[0..count) (count*144) | magic_end (4) ]
 * @endcode
 * The struct block is a raw snapshot of the 64-bit ABI layout used by this
 * library; receivers must parse it through @c operator<< and must not treat
 * embedded pointer/ownership fields as portable wire values.
 *
 * @par Usage
 * @code
 * vlink::zerocopy::ObjectArray arr;
 * arr.create(256);                              // pre-allocate 256 slots
 * arr.set_source_id("fusion_v2");
 *
 * vlink::zerocopy::ObjectArray::Object obj;        // public POD -- assign fields directly
 * std::strncpy(obj.label, "car", sizeof(obj.label) - 1);
 * obj.position[0] = 12.0f;
 * obj.position[1] = 0.3f;
 * obj.position[2] = 0.0f;
 * obj.size[0] = 4.5f;
 * obj.size[1] = 1.8f;
 * obj.size[2] = 1.6f;
 * obj.yaw = 0.1f;
 * obj.velocity[0] = 8.5f;
 * obj.class_id = 1;
 * obj.track_id = 42;
 * arr.push_value(obj);
 *
 * vlink::Bytes wire;
 * arr >> wire;
 *
 * vlink::zerocopy::ObjectArray arr2;
 * arr2 << wire;
 * @endcode
 *
 * @note
 * - 32-bit architectures emit a compile-time warning and are not supported.
 * - After @c operator<<, the data pointer references memory inside the
 *   source @c Bytes.  The @c Bytes must outlive the @c ObjectArray.
 */

#pragma once

#include <cstdint>
#include <string_view>

#include "../base/bytes.h"
#include "./header.h"

namespace vlink {

namespace zerocopy {

/**
 * @struct ObjectArray
 * @brief Zero-copy variable-length array of 3-D detection / tracking objects.
 *
 * @details
 * Stores a packed buffer of homogeneous @c Object records.  The container
 * struct is 112 bytes on 64-bit targets with a 15-byte reserved tail.
 */
struct VLINK_EXPORT_AND_ALIGNED(8) ObjectArray final {
  /**
   * @brief Motion / kinematic state of a tracked object.
   */
  enum MotionState : uint8_t {
    kMotionUnknown = 0,     ///< Unknown.
    kMotionStationary = 1,  ///< Stationary (parked / fixed obstacle).
    kMotionMoving = 2,      ///< Actively moving.
    kMotionStopped = 3,     ///< Temporarily stopped (e.g. at a red light).
    kMotionParked = 4,      ///< Long-term parked vehicle.
  };

  /**
   * @brief Source / origin sensor of a detection.
   */
  enum SourceType : uint8_t {
    kSourceUnknown = 0,     ///< Unknown.
    kSourceLidar = 1,       ///< LiDAR-only detection.
    kSourceCamera = 2,      ///< Camera-only detection.
    kSourceRadar = 3,       ///< Radar-only detection.
    kSourceFusion = 4,      ///< Multi-sensor fusion result.
    kSourceUltrasonic = 5,  ///< Ultrasonic sensor.
  };

  /**
   * @struct Object
   * @brief Fixed-size 3-D object record (144 bytes).
   *
   * @details
   * Public POD that captures pose, dimensions, kinematics, classification,
   * and tracking metadata for one detection / track.  All members are
   * directly accessible; populate by assigning fields and pushing with
   * @c ObjectArray::push_value.
   */
  struct VLINK_EXPORT_AND_ALIGNED(4) Object final {
    /**
     * @brief Default constructor -- zero-initialises all fields.
     *
     * @details
     * Verifies via @c static_assert that the struct is exactly 144 bytes.
     */
    Object() noexcept;

    char label[32]{0};                         ///< NUL-terminated class name (e.g. "car", "pedestrian").
    float position[3]{0};                      ///< Centre position in metres (world frame).
    float yaw{0};                              ///< Yaw angle in radians (REP-103).
    float size[3]{0};                          ///< Bounding-box dimensions: length, width, height in metres.
    float yaw_rate{0};                         ///< Yaw rate in radians per second.
    float velocity[3]{0};                      ///< Linear velocity in metres per second.
    float score{0};                            ///< Detection score in [0, 1].
    float acceleration[3]{0};                  ///< Linear acceleration in metres per second squared.
    float existence_probability{0};            ///< Existence probability in [0, 1].
    float position_covariance[6]{0};           ///< Upper-triangle of the 3x3 position covariance: xx,xy,xz,yy,yz,zz.
    uint32_t class_id{0};                      ///< Numeric class identifier.
    uint32_t track_id{0};                      ///< Tracking identifier (0 for unassociated detection).
    uint32_t age{0};                           ///< Tracking age in frames.
    uint32_t num_observations{0};              ///< Cumulative observation count.
    MotionState motion_state{kMotionUnknown};  ///< Motion / kinematic state tag.
    SourceType source_type{kSourceUnknown};    ///< Source / origin sensor tag.
    uint16_t subtype_id{0};                    ///< Fine-grained subtype identifier (e.g. sedan vs. SUV).
    uint32_t reserved32{0};                    ///< Reserved for future extension.
  };

  /**
   * @brief Default constructor.
   *
   * @details
   * Verifies via @c static_assert that the container is exactly 112 bytes
   * and that @c Object is exactly 144 bytes on 64-bit platforms.  32-bit
   * architectures emit a compile-time warning.
   */
  ObjectArray() noexcept;

  /**
   * @brief Destructor -- frees the owned record buffer if @c is_owner() is @c true.
   */
  ~ObjectArray() noexcept;

  /**
   * @brief Copy constructor -- performs a deep copy of @p target.
   */
  ObjectArray(const ObjectArray& target) noexcept;

  /**
   * @brief Move constructor -- transfers ownership from @p target.
   */
  ObjectArray(ObjectArray&& target) noexcept;

  /**
   * @brief Copy-assignment operator -- deep-copies @p target.
   */
  ObjectArray& operator=(const ObjectArray& target) noexcept;

  /**
   * @brief Move-assignment operator -- transfers ownership from @p target.
   */
  ObjectArray& operator=(ObjectArray&& target) noexcept;

  /**
   * @brief Deserialises an @c ObjectArray from a @c Bytes wire buffer.
   *
   * @details
   * Zero-copy: the record pointer references memory inside @p bytes.
   */
  bool operator<<(const Bytes& bytes) noexcept;

  /**
   * @brief Serialises this @c ObjectArray into a @c Bytes wire buffer.
   */
  bool operator>>(Bytes& bytes) const noexcept;

  /**
   * @brief Checks whether @p bytes contains a valid @c ObjectArray wire buffer.
   */
  [[nodiscard]] static bool check_valid(const Bytes& bytes) noexcept;

  /**
   * @brief Returns the total serialised byte count for this array.
   */
  [[nodiscard]] size_t get_serialized_size() const noexcept;

  /**
   * @brief Returns @c true when the record buffer is present and non-empty.
   */
  [[nodiscard]] bool is_valid() const noexcept;

  /**
   * @brief Borrows the record buffer from @p target without copying.
   */
  bool shallow_copy(const ObjectArray& target) noexcept;

  /**
   * @brief Deep-copies the record buffer from @p target.
   *
   * @details
   * If @c *this already owns a buffer whose capacity matches @c target's
   * @c count * @c pack_size, the data is copied in place; otherwise a new
   * buffer is allocated sized to the target's current logical content.
   */
  bool deep_copy(const ObjectArray& target) noexcept;

  /**
   * @brief Transfers ownership from @p target.
   */
  bool move_copy(ObjectArray& target) noexcept;

  /**
   * @brief Allocates an owned record buffer sized to @p count Objects.
   *
   * @details
   * Frees any existing owned buffer.  Sets @c pack_size to @c sizeof(Object)
   * and capacity to @c count * @c sizeof(Object).  Sets @c count to 0.
   *
   * @param count Maximum number of Objects to pre-allocate.  Must be non-zero.
   * @return       @c false if @p count is zero, otherwise @c true.
   */
  bool create(size_t count) noexcept;

  /**
   * @brief Releases owned resources and resets metadata and @c header.
   */
  void clear() noexcept;

  /**
   * @brief Appends one @c Object record at the end of the buffer.
   *
   * @param object Source record.
   * @return        @c false on capacity overflow / invalid state.
   */
  bool push_value(const Object& object) noexcept;

  /**
   * @brief Overwrites the record at @p index with @p object.
   *
   * @param index Zero-based slot index.  Must be < @c count().
   * @param object New record contents.
   * @return        @c false on out-of-range / invalid state.
   */
  bool set_value(uint32_t index, const Object& object) noexcept;

  /**
   * @brief Reads the record at @p index into @p object.
   *
   * @return @c false on out-of-range; @p object is zeroed.
   */
  bool get_value(uint32_t index, Object& object) const noexcept;

  /**
   * @brief Returns the record at @p index by value (zero-initialised on out-of-range).
   */
  [[nodiscard]] Object get_value(uint32_t index) const noexcept;

  /**
   * @brief Resets the logical record count without reallocating.
   *
   * @param count New logical count.  Must not exceed allocated capacity.
   * @return       @c false on capacity overflow.
   */
  bool resize(uint32_t count) noexcept;

  /**
   * @brief Returns the map state timestamp in nanoseconds since epoch.
   */
  [[nodiscard]] uint64_t update_time_ns() const noexcept;

  /**
   * @brief Returns the source / producer module identifier.
   */
  [[nodiscard]] std::string_view source_id() const noexcept;

  /**
   * @brief Returns the sensor / producer channel identifier.
   */
  [[nodiscard]] uint32_t channel() const noexcept;

  /**
   * @brief Returns the nominal publish frequency in Hz.
   */
  [[nodiscard]] uint32_t freq() const noexcept;

  /**
   * @brief Returns the current Object count.
   */
  [[nodiscard]] uint32_t count() const noexcept;

  /**
   * @brief Returns the byte size of one Object record.
   */
  [[nodiscard]] uint32_t pack_size() const noexcept;

  /**
   * @brief Returns a read-only pointer to the Object at @p index.
   *
   * @return @c nullptr on out-of-range / invalid state.
   */
  [[nodiscard]] const Object* objects(uint32_t index = 0) const noexcept;

  /**
   * @brief Returns a read-only pointer to the contiguous record buffer.
   */
  [[nodiscard]] const uint8_t* data() const noexcept;

  /**
   * @brief Returns the buffer byte capacity.
   */
  [[nodiscard]] size_t capacity() const noexcept;

  /**
   * @brief Returns @c true if this object owns its record buffer.
   */
  [[nodiscard]] bool is_owner() const noexcept;

  /**
   * @brief Sets the map state timestamp in nanoseconds since epoch.
   */
  void set_update_time_ns(uint64_t update_time_ns) noexcept;

  /**
   * @brief Sets the source / producer module identifier.
   */
  void set_source_id(std::string_view source_id) noexcept;

  /**
   * @brief Sets the sensor / producer channel identifier.
   */
  void set_channel(uint32_t channel) noexcept;

  /**
   * @brief Sets the nominal publish frequency in Hz.
   */
  void set_freq(uint32_t freq) noexcept;

  /**
   * @brief Gets the reserved field.
   */
  uint32_t& get_reserved() noexcept { return reserved32_; }

  Header header;  ///< Sequencing and timestamp metadata for this array.

  static constexpr bool kZerocopyTypes{true};  ///< Internal marker for VLink zero-copy type traits.

 private:
  uint8_t* data_{nullptr};
  size_t capacity_{0};
  uint64_t update_time_ns_{0};
  char source_id_[16]{0};
  uint32_t channel_{0};
  uint32_t freq_{0};
  uint32_t count_{0};
  uint32_t pack_size_{0};
  bool is_owner_{false};
  uint8_t reserved8_{0};
  uint16_t reserved16_{0};
  uint32_t reserved32_{0};
  uint32_t reserved2_{0};
  uint32_t reserved3_{0};

  static constexpr uint32_t kMagicNumberBegin{0x98B7F18A};
  static constexpr uint32_t kMagicNumberEnd{0x98B7F18F};
};

}  // namespace zerocopy

}  // namespace vlink
