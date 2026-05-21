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
 * @file occupancy_grid.h
 * @brief Zero-copy 2-D occupancy grid map container for VLink transport.
 *
 * @details
 * @c OccupancyGrid carries one 2-D occupancy / cost map together with a
 * @c Header for sequencing and timestamping.  The map is rectangular with
 * @c width() columns and @c height() rows; cells are stored in row-major
 * order and each cell consumes 1, 2, or 4 bytes depending on @c cell_type().
 * The struct is exactly 152 bytes on 64-bit platforms.  Common per-map
 * metadata (channel, publish frequency, map identifier, state timestamp,
 * value range, default value, occupied / free thresholds, plane height) is
 * baked in to preserve binary wire compatibility across revisions.
 *
 * @par Cell types
 * | Value          | C++ type | Size | Typical use                       |
 * | -------------- | -------- | ---- | --------------------------------- |
 * | kCellInt8      | int8_t   | 1    | ROS-style -1/0..100 occupancy     |
 * | kCellUint8     | uint8_t  | 1    | 0..255 costmap / grayscale        |
 * | kCellUint16    | uint16_t | 2    | High-resolution costmap           |
 * | kCellFloat32   | float    | 4    | Log-odds / probability / SDF      |
 *
 * @par Coordinate convention
 * The map origin @c (origin_x, origin_y, origin_z) lies at the bottom-left
 * corner of the cell at row 0 / column 0.  The map is rotated by
 * @c origin_yaw radians around that origin (REP-103 convention).
 *
 * @par Binary wire format
 * @code
 * [ magic_begin (4) | OccupancyGrid struct (152) | cell bytes (W*H*cell_size) | magic_end (4) ]
 * @endcode
 * The struct block is a raw snapshot of the 64-bit ABI layout used by this
 * library; receivers must parse it through @c operator<< and must not treat
 * embedded pointer/ownership fields as portable wire values.
 *
 * @par Usage
 * @code
 * vlink::zerocopy::OccupancyGrid og;
 * og.set_width(400);
 * og.set_height(400);
 * og.set_resolution(0.05f);
 * og.set_origin_x(-10.0f);
 * og.set_origin_y(-10.0f);
 * og.set_origin_yaw(0.0f);
 * og.set_cell_type(vlink::zerocopy::OccupancyGrid::kCellInt8);
 * og.set_default_value(-1);
 * og.set_occupied_threshold(0.65f);
 * og.set_free_threshold(0.20f);
 * og.create(400 * 400);
 *
 * vlink::Bytes wire;
 * og >> wire;                            // serialise
 *
 * vlink::zerocopy::OccupancyGrid og2;
 * og2 << wire;                           // deserialise (zero-copy, borrows wire)
 * @endcode
 *
 * @note
 * - 32-bit architectures emit a compile-time warning and are not supported.
 * - After @c operator<<, the data pointer references memory inside the source
 *   @c Bytes.  The @c Bytes must outlive the @c OccupancyGrid.
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
 * @struct OccupancyGrid
 * @brief Zero-copy 2-D occupancy grid map with typed cell storage and Header.
 *
 * @details
 * Stores a rectangular grid of homogeneous cells in row-major order together
 * with the world-to-grid transform, value range, default cell, thresholds,
 * and a map identifier.  The struct size is fixed at 152 bytes on 64-bit
 * targets, with a small reserved tail (10 bytes) for future extensions.
 * Copies of the struct are either shallow (borrow the data pointer) or deep
 * (allocate and copy).  The move constructor and move-assignment transfer
 * ownership from the source without allocation.
 */
struct VLINK_EXPORT_AND_ALIGNED(8) OccupancyGrid final {
  /**
   * @brief Per-cell storage type tag.
   *
   * @details
   * Pass to @c set_cell_type() and read via @c cell_type().  The byte size of
   * one cell is derived via @c cell_size_of().
   */
  enum CellType : uint8_t {
    kCellUnknown = 0,  ///< Unknown or uninitialised cell type.
    kCellInt8 = 1,     ///< Signed 8-bit per cell (ROS @c nav_msgs/OccupancyGrid).
    kCellUint8 = 2,    ///< Unsigned 8-bit per cell (generic 0..255 costmap).
    kCellUint16 = 3,   ///< Unsigned 16-bit per cell (high-resolution costmap).
    kCellFloat32 = 4,  ///< IEEE-754 single-precision per cell (log-odds, probability, SDF).
  };

  /**
   * @brief Default constructor.
   *
   * @details
   * Verifies via @c static_assert that the struct is exactly 152 bytes on
   * 64-bit platforms.  32-bit architectures emit a compile-time warning.
   */
  OccupancyGrid() noexcept;

  /**
   * @brief Destructor -- frees the owned cell buffer if @c is_owner() is @c true.
   */
  ~OccupancyGrid() noexcept;

  /**
   * @brief Copy constructor -- performs a deep copy of @p target.
   *
   * @param target Source grid to copy.
   */
  OccupancyGrid(const OccupancyGrid& target) noexcept;

  /**
   * @brief Move constructor -- transfers ownership from @p target.
   *
   * @details
   * After the call @p target is empty and does not own any buffer.
   *
   * @param target Source grid to move from.
   */
  OccupancyGrid(OccupancyGrid&& target) noexcept;

  /**
   * @brief Copy-assignment operator -- deep-copies @p target.
   *
   * @param target Source grid to copy.  Self-assignment is a no-op.
   * @return        Reference to @c *this.
   */
  OccupancyGrid& operator=(const OccupancyGrid& target) noexcept;

  /**
   * @brief Move-assignment operator -- transfers ownership from @p target.
   *
   * @param target Source grid to move.  Self-assignment is a no-op.
   * @return        Reference to @c *this.
   */
  OccupancyGrid& operator=(OccupancyGrid&& target) noexcept;

  /**
   * @brief Deserialises an @c OccupancyGrid from a @c Bytes wire buffer.
   *
   * @details
   * Validates the magic-number envelope, copies the raw struct snapshot, and
   * sets the cell data pointer to reference memory inside @p bytes
   * (zero-copy).  @c is_owner() will be @c false after a successful call;
   * @p bytes must outlive this @c OccupancyGrid.
   *
   * @param bytes Buffer produced by @c operator>>.
   * @return       @c true on success, @c false if the buffer fails validation
   *               or the total size is inconsistent.
   */
  bool operator<<(const Bytes& bytes) noexcept;

  /**
   * @brief Serialises this @c OccupancyGrid into a @c Bytes wire buffer.
   *
   * @details
   * Writes the magic-number envelope, this object's raw struct snapshot, and
   * cell payload into @p bytes, resizing it if necessary.
   *
   * @param bytes Output buffer (reallocated automatically if needed).
   * @return       Always @c true.
   */
  bool operator>>(Bytes& bytes) const noexcept;

  /**
   * @brief Checks whether @p bytes contains a valid @c OccupancyGrid wire buffer.
   *
   * @param bytes Buffer to validate.
   * @return       @c true if the sentinels match and the size is sufficient.
   */
  [[nodiscard]] static bool check_valid(const Bytes& bytes) noexcept;

  /**
   * @brief Returns the total serialised byte count for this grid.
   *
   * @details
   * Equals: @c sizeof(magic_begin) + @c sizeof(OccupancyGrid) + @c size() + @c sizeof(magic_end)
   *
   * @return Total bytes written by @c operator>>.
   */
  [[nodiscard]] size_t get_serialized_size() const noexcept;

  /**
   * @brief Returns @c true when the cell buffer is present and non-empty.
   *
   * @return @c true if @c data() is non-null and @c size() > 0.
   */
  [[nodiscard]] bool is_valid() const noexcept;

  /**
   * @brief Borrows the cell buffer from @p target without copying.
   *
   * @details
   * Sets header, grid metadata, and data pointer to match @p target;
   * @c is_owner() becomes @c false.  Any previously owned buffer is freed
   * first.  The reserved field is not copied.  The source backing buffer must
   * outlive this borrowed grid.
   *
   * @param target Source grid to borrow from.
   * @return        @c false if @p target == @c *this, otherwise @c true.
   */
  bool shallow_copy(const OccupancyGrid& target) noexcept;

  /**
   * @brief Deep-copies the cell buffer from @p target.
   *
   * @details
   * If @c *this already owns a same-size buffer the data is copied in-place;
   * otherwise a new buffer is allocated.  The reserved field is not copied.
   *
   * @param target Source grid to copy.
   * @return        @c false if @p target == @c *this, otherwise @c true.
   */
  bool deep_copy(const OccupancyGrid& target) noexcept;

  /**
   * @brief Transfers ownership from @p target to @c *this.
   *
   * @details
   * After the call @p target is empty.  Self-move is a no-op.
   *
   * @param target Source grid to move from.
   * @return        @c false if @p target == @c *this, otherwise @c true.
   */
  bool move_copy(OccupancyGrid& target) noexcept;

  /**
   * @brief Allocates an owned cell buffer of @p size bytes.
   *
   * @details
   * Frees any existing owned buffer before allocating the new one.  Buffer
   * content is uninitialised after the call.  The caller is responsible for
   * sizing @p size consistently with @c width() * @c height() * @c cell_size().
   *
   * @param size Number of bytes to allocate.  Must be non-zero.
   * @return      @c false if @p size is zero, otherwise @c true.
   */
  bool create(size_t size) noexcept;

  /**
   * @brief Releases owned resources and resets grid metadata and @c header.
   */
  void clear() noexcept;

  /**
   * @brief Borrows an external raw cell pointer without copying.
   *
   * @details
   * Sets the internal data pointer to @p data with @c is_owner() == false.
   * The caller is responsible for the buffer lifetime.
   *
   * @param data Non-null pointer to cell data.
   * @param size Size of the buffer in bytes.
   * @return      @c false on invalid arguments or if the pointer is unchanged.
   */
  bool shallow_copy(uint8_t* data, size_t size) noexcept;

  /**
   * @brief Deep-copies cell data from a raw pointer.
   *
   * @details
   * Allocates or reuses an owned buffer and copies @p size bytes from @p data.
   *
   * @param data Source cell data pointer.  Must be non-null.
   * @param size Number of bytes to copy.  Must be non-zero.
   * @return      @c false if @p data is null, @p size is zero, this object
   *              claims ownership but has no buffer, or @p data already equals
   *              the current internal pointer; otherwise @c true.
   */
  bool deep_copy(uint8_t* data, size_t size) noexcept;

  /**
   * @brief Alias for @c deep_copy(uint8_t*, size_t).
   *
   * @param data Source pointer.
   * @param size Number of bytes.
   * @return      Result of the underlying @c deep_copy call.
   */
  bool fill_data(uint8_t* data, size_t size) noexcept;

  /**
   * @brief Returns the map state timestamp in nanoseconds since epoch.
   *
   * @details
   * Independent of @c header.time_pub -- this is the time at which the map
   * content was last updated, which may lag behind the publish timestamp.
   *
   * @return Map state timestamp.
   */
  [[nodiscard]] uint64_t update_time_ns() const noexcept;

  /**
   * @brief Returns the unique map identifier as a NUL-terminated string view.
   *
   * @details
   * Useful when multiple maps (e.g. local / global / lane-level) are
   * published on the same topic and consumers need to disambiguate.
   *
   * @return Borrowed view into the embedded @c map_id buffer.
   */
  [[nodiscard]] std::string_view map_id() const noexcept;

  /**
   * @brief Returns the sensor / producer channel identifier.
   *
   * @return Channel set by @c set_channel().
   */
  [[nodiscard]] uint32_t channel() const noexcept;

  /**
   * @brief Returns the nominal publish frequency in Hz.
   *
   * @return Frequency set by @c set_freq().
   */
  [[nodiscard]] uint32_t freq() const noexcept;

  /**
   * @brief Returns the grid width in cells (columns).
   *
   * @return Width set by @c set_width().
   */
  [[nodiscard]] uint32_t width() const noexcept;

  /**
   * @brief Returns the grid height in cells (rows).
   *
   * @return Height set by @c set_height().
   */
  [[nodiscard]] uint32_t height() const noexcept;

  /**
   * @brief Returns the count of cells whose value is not the default.
   *
   * @details
   * Producers may set this as a sparsity / quality hint; consumers should
   * treat zero as "unknown / not provided" rather than "fully empty map".
   *
   * @return Valid-cell count set by @c set_valid_cell_count().
   */
  [[nodiscard]] uint32_t valid_cell_count() const noexcept;

  /**
   * @brief Returns the cell resolution in metres per cell.
   *
   * @return Resolution set by @c set_resolution().
   */
  [[nodiscard]] float resolution() const noexcept;

  /**
   * @brief Returns the world-frame X coordinate of the grid origin.
   *
   * @return Origin X set by @c set_origin_x().
   */
  [[nodiscard]] float origin_x() const noexcept;

  /**
   * @brief Returns the world-frame Y coordinate of the grid origin.
   *
   * @return Origin Y set by @c set_origin_y().
   */
  [[nodiscard]] float origin_y() const noexcept;

  /**
   * @brief Returns the world-frame Z coordinate of the 2-D plane.
   *
   * @details
   * Allows the 2-D grid to be placed at an arbitrary height in 3-D space
   * (e.g. multi-floor occupancy stacks).
   *
   * @return Origin Z set by @c set_origin_z().
   */
  [[nodiscard]] float origin_z() const noexcept;

  /**
   * @brief Returns the rotation of the grid origin in radians.
   *
   * @return Origin yaw set by @c set_origin_yaw().
   */
  [[nodiscard]] float origin_yaw() const noexcept;

  /**
   * @brief Returns the lower bound of cell values (for normalisation / visualisation).
   *
   * @return Value lower bound set by @c set_value_min().
   */
  [[nodiscard]] float value_min() const noexcept;

  /**
   * @brief Returns the upper bound of cell values.
   *
   * @return Value upper bound set by @c set_value_max().
   */
  [[nodiscard]] float value_max() const noexcept;

  /**
   * @brief Returns the value used for unknown / uninitialised cells.
   *
   * @details
   * Typical convention: @c -1 for ROS-style int8 occupancy, @c 255 for
   * @c kCellUint8 costmaps, or a sentinel float NaN for float maps.
   *
   * @return Default cell value set by @c set_default_value().
   */
  [[nodiscard]] int32_t default_value() const noexcept;

  /**
   * @brief Returns the threshold above which a cell is considered occupied.
   *
   * @details
   * Common ROS default is @c 0.65 (65% probability).  Producers may leave
   * this at zero if no fixed threshold applies.
   *
   * @return Occupied threshold set by @c set_occupied_threshold().
   */
  [[nodiscard]] float occupied_threshold() const noexcept;

  /**
   * @brief Returns the threshold below which a cell is considered free.
   *
   * @details
   * Common ROS default is @c 0.20 (20% probability).
   *
   * @return Free threshold set by @c set_free_threshold().
   */
  [[nodiscard]] float free_threshold() const noexcept;

  /**
   * @brief Returns the per-cell storage type tag.
   *
   * @return @c CellType set by @c set_cell_type().
   */
  [[nodiscard]] CellType cell_type() const noexcept;

  /**
   * @brief Returns the byte size of one cell derived from @c cell_type().
   *
   * @return Cell size in bytes (0 for @c kCellUnknown).
   */
  [[nodiscard]] uint8_t cell_size() const noexcept;

  /**
   * @brief Returns a read-only pointer to the cell data buffer.
   *
   * @return Pointer to cell bytes.  Empty deserialised grids may still hold a
   *         non-null borrowed pointer; use @c size() / @c is_valid() to test
   *         for usable cell data.
   */
  [[nodiscard]] const uint8_t* data() const noexcept;

  /**
   * @brief Returns the cell buffer size in bytes.
   *
   * @return Number of payload bytes, or 0 if empty.
   */
  [[nodiscard]] size_t size() const noexcept;

  /**
   * @brief Returns @c true if this object owns its cell buffer.
   *
   * @return @c true when memory ownership is held.
   */
  [[nodiscard]] bool is_owner() const noexcept;

  /**
   * @brief Sets the map state timestamp in nanoseconds since epoch.
   *
   * @param update_time_ns Last-update timestamp.
   */
  void set_update_time_ns(uint64_t update_time_ns) noexcept;

  /**
   * @brief Sets the unique map identifier.
   *
   * @details
   * Copies up to @c sizeof(map_id) - 1 bytes from @p map_id and writes a NUL
   * terminator.  Longer inputs are truncated.
   *
   * @param map_id Identifier to embed.
   */
  void set_map_id(std::string_view map_id) noexcept;

  /**
   * @brief Sets the sensor / producer channel identifier.
   *
   * @param channel Channel identifier.
   */
  void set_channel(uint32_t channel) noexcept;

  /**
   * @brief Sets the nominal publish frequency in Hz.
   *
   * @param freq Publish rate in Hz.
   */
  void set_freq(uint32_t freq) noexcept;

  /**
   * @brief Sets the grid width in cells (columns).
   *
   * @param width Number of columns.
   */
  void set_width(uint32_t width) noexcept;

  /**
   * @brief Sets the grid height in cells (rows).
   *
   * @param height Number of rows.
   */
  void set_height(uint32_t height) noexcept;

  /**
   * @brief Sets the count of valid (non-default) cells.
   *
   * @param valid_cell_count Number of cells whose value differs from @c default_value().
   */
  void set_valid_cell_count(uint32_t valid_cell_count) noexcept;

  /**
   * @brief Sets the cell resolution in metres per cell.
   *
   * @param resolution Metres per cell.
   */
  void set_resolution(float resolution) noexcept;

  /**
   * @brief Sets the world-frame X coordinate of the grid origin.
   *
   * @param origin_x World X coordinate of the bottom-left corner.
   */
  void set_origin_x(float origin_x) noexcept;

  /**
   * @brief Sets the world-frame Y coordinate of the grid origin.
   *
   * @param origin_y World Y coordinate of the bottom-left corner.
   */
  void set_origin_y(float origin_y) noexcept;

  /**
   * @brief Sets the world-frame Z coordinate of the 2-D plane.
   *
   * @param origin_z World Z coordinate of the plane.
   */
  void set_origin_z(float origin_z) noexcept;

  /**
   * @brief Sets the grid yaw rotation in radians.
   *
   * @param origin_yaw Rotation of the grid origin (REP-103 convention).
   */
  void set_origin_yaw(float origin_yaw) noexcept;

  /**
   * @brief Sets the lower bound of cell values.
   *
   * @param value_min Value lower bound.
   */
  void set_value_min(float value_min) noexcept;

  /**
   * @brief Sets the upper bound of cell values.
   *
   * @param value_max Value upper bound.
   */
  void set_value_max(float value_max) noexcept;

  /**
   * @brief Sets the value used for unknown / uninitialised cells.
   *
   * @param default_value Default cell value.
   */
  void set_default_value(int32_t default_value) noexcept;

  /**
   * @brief Sets the threshold above which a cell is considered occupied.
   *
   * @param occupied_threshold Occupied threshold.
   */
  void set_occupied_threshold(float occupied_threshold) noexcept;

  /**
   * @brief Sets the threshold below which a cell is considered free.
   *
   * @param free_threshold Free threshold.
   */
  void set_free_threshold(float free_threshold) noexcept;

  /**
   * @brief Sets the per-cell storage type tag.
   *
   * @param cell_type One of the @c CellType enum values.
   */
  void set_cell_type(CellType cell_type) noexcept;

  /**
   * @brief Returns the byte size of one cell of @p type.
   *
   * @param type Cell type tag.
   * @return      Cell size in bytes (0 for @c kCellUnknown).
   */
  [[nodiscard]] static uint8_t cell_size_of(CellType type) noexcept;

  /**
   * @brief Gets the reserved field.
   *
   * @details
   * This field is not reset by @c clear() and is not copied by the current
   * copy/move helpers.
   *
   * @return Reference to the reserved field.
   */
  uint32_t& get_reserved() noexcept { return reserved32_; }

  Header header;  ///< Sequencing and timestamp metadata for this grid.

  static constexpr bool kZerocopyTypes{true};  ///< Internal marker for VLink zero-copy type traits.

 private:
  uint8_t* data_{nullptr};
  size_t size_{0};
  uint64_t update_time_ns_{0};
  char map_id_[16]{0};
  uint32_t channel_{0};
  uint32_t freq_{0};
  uint32_t width_{0};
  uint32_t height_{0};
  uint32_t valid_cell_count_{0};
  float resolution_{0};
  float origin_x_{0};
  float origin_y_{0};
  float origin_z_{0};
  float origin_yaw_{0};
  float value_min_{0};
  float value_max_{0};
  int32_t default_value_{0};
  float occupied_threshold_{0};
  float free_threshold_{0};
  CellType cell_type_{kCellUnknown};
  bool is_owner_{false};
  uint16_t reserved16_{0};
  uint32_t reserved32_{0};
  uint32_t reserved2_{0};

  static constexpr uint32_t kMagicNumberBegin{0x98B7F17A};
  static constexpr uint32_t kMagicNumberEnd{0x98B7F17F};
};

}  // namespace zerocopy

}  // namespace vlink
