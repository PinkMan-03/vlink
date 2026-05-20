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
 * @file point_cloud.h
 * @brief Zero-copy, schema-aware 3-D point cloud container for VLink transport.
 *
 * @details
 * @c PointCloud stores an array of fixed-size point records together with a
 * compact compile-time schema that describes the name, type, and byte size of
 * every field per point.  The schema is encoded into two @c uint64_t values
 * (@c size_num and @c type_num) where each nibble (4 bits) encodes one field,
 * and a comma-separated name string (stored in 160 bytes, 3-16 fields).
 *
 * @par Protocol encoding
 * @code
 * size_num: 0x0408...  (highest effective nibble = first field byte-size)
 * type_num: 0x0A0B...  (highest effective nibble = first field Type enum value)
 * names:    "x,y,z,intensity"
 * @endcode
 *
 * @par Supported field types
 * | Enum          | C++ type  | Size |
 * | ------------- | --------- | ---- |
 * | kBoolType     | bool      | 1    |
 * | kInt8Type     | int8_t    | 1    |
 * | kUint8Type    | uint8_t   | 1    |
 * | kInt16Type    | int16_t   | 2    |
 * | kUint16Type   | uint16_t  | 2    |
 * | kInt32Type    | int32_t   | 4    |
 * | kUint32Type   | uint32_t  | 4    |
 * | kInt64Type    | int64_t   | 8    |
 * | kUint64Type   | uint64_t  | 8    |
 * | kFloatType    | float     | 4    |
 * | kDoubleType   | double    | 8    |
 *
 * @par Binary wire format
 * @code
 * [ magic_begin (4) | PointCloud struct (256) | point data (size * pack_size) | magic_end (4) ]
 * @endcode
 * The struct block is a raw snapshot of the 64-bit ABI layout used by this
 * library; receivers must parse it through @c operator<< and must not treat
 * embedded pointer/ownership/capacity fields as portable wire values.
 *
 * @par Usage -- float XYZ + intensity
 * @code
 * vlink::zerocopy::PointCloud pc;
 * pc.create_v3f<float>(1000, {"intensity"});
 *
 * // Append points
 * pc.push_value_v3f(1.0f, 2.0f, 3.0f, 0.8f);
 * pc.push_value_v3f(4.0f, 5.0f, 6.0f, 0.5f);
 *
 * // Read back
 * auto key_map = pc.get_key_map();
 * float x = pc.get_value<float>(0, key_map, "x");
 *
 * // Serialise
 * vlink::Bytes wire;
 * pc >> wire;
 *
 * // Deserialise (zero-copy, borrows wire)
 * vlink::zerocopy::PointCloud pc2;
 * pc2 << wire;
 * @endcode
 *
 * @note
 * - 32-bit architectures emit a compile-time warning and are not supported.
 * - After @c operator<<, the data pointer references memory inside the source
 *   @c Bytes.  The @c Bytes must outlive the @c PointCloud.
 * - @c create<T...>() requires 3-16 type parameters, all of which must be
 *   fundamental types.
 * - @c set_value() requires the logical size and internal write cursor to be
 *   consistent.  @c resize(), @c fill_packed_data(), or completed
 *   @c push_value() calls can establish that state.
 * - The struct is exactly 256 bytes on 64-bit platforms (verified by
 *   @c static_assert).
 */

#pragma once

#include <cstdint>
#include <iostream>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "../base/bytes.h"
#include "./header.h"

namespace vlink {

namespace zerocopy {

/**
 * @struct PointCloud
 * @brief Schema-aware zero-copy 3-D point cloud with typed per-point fields.
 *
 * @details
 * Stores an array of homogeneous point records whose layout is described by
 * a compact binary protocol embedded in the struct.  The struct size is fixed
 * at 256 bytes on 64-bit targets.
 */
struct VLINK_EXPORT_AND_ALIGNED(8) PointCloud final {
  /**
   * @brief Fundamental field type tags used in the binary protocol.
   *
   * @details
   * Each nibble in @c Protocol::type_num encodes one of these values for
   * the corresponding field.
   */
  enum Type : uint8_t {
    kUnknownType = 0,  ///< Unknown or unsupported type.
    kBoolType = 1,     ///< bool (1 byte).
    kInt8Type = 2,     ///< int8_t (1 byte).
    kUint8Type = 3,    ///< uint8_t (1 byte).
    kInt16Type = 4,    ///< int16_t (2 bytes).
    kUint16Type = 5,   ///< uint16_t (2 bytes).
    kInt32Type = 6,    ///< int32_t (4 bytes).
    kUint32Type = 7,   ///< uint32_t (4 bytes).
    kInt64Type = 8,    ///< int64_t (8 bytes).
    kUint64Type = 9,   ///< uint64_t (8 bytes).
    kFloatType = 10,   ///< float (4 bytes).
    kDoubleType = 11,  ///< double (8 bytes).
  };

  /**
   * @struct Key
   * @brief Describes one named field in a point record.
   */
  struct Key final {
    std::string name;            ///< Field name (e.g., "x", "intensity").
    uint8_t type{kUnknownType};  ///< @c Type enum value for this field.
    uint8_t size{0};             ///< Field byte size derived from the @c Type.
  };

  /**
   * @brief Maps a field name to its byte offset within one packed point record.
   *
   * @details
   * Obtain via @c get_key_map() and pass to the @c get_value() key-map
   * overloads for efficient repeated access.
   */
  using KeyMap = std::unordered_map<std::string, uint16_t>;

  /**
   * @brief Ordered list of field descriptors for one point record.
   */
  using KeyList = std::vector<Key>;

  /**
   * @struct Vector3f
   * @brief Compact 3-component single-precision vector (12 bytes, 4-byte aligned).
   *
   * @details
   * Convenience type for XYZ coordinates when using the @c float-based
   * @c create_v3f, @c push_value_v3f, and @c get_value_v3f helpers.
   */
  struct VLINK_EXPORT_AND_ALIGNED(4) Vector3f final {
    float x{0};  ///< X component.
    float y{0};  ///< Y component.
    float z{0};  ///< Z component.

    /**
     * @brief Default constructor.
     *
     * @details
     * Verifies via @c static_assert that @c sizeof(Vector3f) == 12 on
     * 64-bit platforms.
     */
    Vector3f() noexcept;

    /**
     * @brief Value constructor.
     *
     * Parameter @c _x: X component.
     * Parameter @c _y: Y component.
     * Parameter @c _z: Z component.
     */
    explicit Vector3f(float _x, float _y, float _z) noexcept;

    /**
     * @brief Formats the vector as @c (x, y, z) on an output stream.
     *
     * Parameter @c ostream: Output stream.
     * Parameter @c v3f: Vector to format.
     * @return         Reference to @p ostream.
     */
    friend std::ostream& operator<<(std::ostream& ostream, const Vector3f& v3f) noexcept;
  };

  /**
   * @struct Vector3d
   * @brief Compact 3-component double-precision vector (24 bytes, 8-byte aligned).
   *
   * @details
   * Convenience type for XYZ coordinates when using the @c double-based
   * @c create_v3d, @c push_value_v3d, and @c get_value_v3d helpers.
   */
  struct VLINK_EXPORT_AND_ALIGNED(8) Vector3d final {
    double x{0};  ///< X component.
    double y{0};  ///< Y component.
    double z{0};  ///< Z component.

    /**
     * @brief Default constructor.
     *
     * @details
     * Verifies via @c static_assert that @c sizeof(Vector3d) == 24 on
     * 64-bit platforms.
     */
    Vector3d() noexcept;

    /**
     * @brief Value constructor.
     *
     * Parameter @c _x: X component.
     * Parameter @c _y: Y component.
     * Parameter @c _z: Z component.
     */
    explicit Vector3d(double _x, double _y, double _z) noexcept;

    /**
     * @brief Formats the vector as @c (x, y, z) on an output stream.
     *
     * Parameter @c ostream: Output stream.
     * Parameter @c v3d: Vector to format.
     * @return         Reference to @p ostream.
     */
    friend std::ostream& operator<<(std::ostream& ostream, const Vector3d& v3d) noexcept;
  };

  /**
   * @brief Default constructor.
   *
   * @details
   * Verifies via @c static_assert that the struct is exactly 256 bytes on
   * 64-bit platforms.
   */
  PointCloud() noexcept;

  /**
   * @brief Destructor -- frees the owned point data buffer if @c is_owner() is @c true.
   */
  ~PointCloud() noexcept;

  /**
   * @brief Copy constructor -- performs a deep copy of @p target.
   *
   * Parameter @c target: Source to copy.
   */
  PointCloud(const PointCloud& target) noexcept;

  /**
   * @brief Move constructor -- transfers ownership from @p target.
   *
   * Parameter @c target: Source to move from.  Left empty after the call.
   */
  PointCloud(PointCloud&& target) noexcept;

  /**
   * @brief Copy-assignment operator -- deep-copies @p target.
   *
   * Parameter @c target: Source to copy.  Self-assignment is a no-op.
   * @return        Reference to @c *this.
   */
  PointCloud& operator=(const PointCloud& target) noexcept;

  /**
   * @brief Move-assignment operator -- transfers ownership from @p target.
   *
   * Parameter @c target: Source to move.  Self-assignment is a no-op.
   * @return        Reference to @c *this.
   */
  PointCloud& operator=(PointCloud&& target) noexcept;

  /**
   * @brief Deserialises a @c PointCloud from a @c Bytes wire buffer.
   *
   * @details
   * Validates magic-number sentinels, copies the struct header (including
   * the embedded @c Protocol), and sets the point data pointer to reference
   * memory inside @p bytes (zero-copy; @c is_owner() == false).
   * @p bytes must outlive this @c PointCloud.
   *
   * Parameter @c bytes: Buffer produced by @c operator>>.
   * @return       @c true on success; @c false on invalid magic numbers or
   *               size mismatch.
   */
  bool operator<<(const Bytes& bytes) noexcept;

  /**
   * @brief Serialises this @c PointCloud into a @c Bytes wire buffer.
   *
   * @details
   * Writes the magic-number envelope, all struct fields (including the
   * protocol descriptor), and @c size() * @c pack_size() bytes of point data.
   *
   * Parameter @c bytes: Output buffer (reallocated if the size does not match).
   * @return       Always @c true.
   */
  bool operator>>(Bytes& bytes) const noexcept;

  /**
   * @brief Checks whether @p bytes contains a valid @c PointCloud wire buffer.
   *
   * Parameter @c bytes: Buffer to validate.
   * @return       @c true if magic sentinels are present and minimum size is met.
   */
  [[nodiscard]] static bool check_valid(const Bytes& bytes) noexcept;

  /**
   * @brief Returns @c true when the object has a valid point buffer.
   *
   * @return @c true if data pointer is non-null, @c size() > 0, and
   *         @c pack_size() > 0.
   */
  [[nodiscard]] bool is_valid() const noexcept;

  /**
   * @brief Borrows protocol and data pointer from @p target without copying.
   *
   * @details
   * @c is_owner() becomes @c false.  Any previously owned buffer is freed.
   * The caller must ensure @p target outlives this object.
   *
   * Parameter @c target: Source to borrow from.
   * @return        @c false if @p target == @c *this, otherwise @c true.
   */
  bool shallow_copy(const PointCloud& target) noexcept;

  /**
   * @brief Deep-copies protocol and point data from @p target.
   *
   * @details
   * If @c *this already owns a buffer whose capacity exactly matches
   * @c target.size() * @c target.pack_size(), the data is copied in-place;
   * otherwise a new buffer sized to the target's current logical point data is
   * allocated.  Extra reserved capacity from @p target is not preserved.
   *
   * Parameter @c target: Source to copy.
   * @return        @c false if @p target == @c *this, otherwise @c true.
   */
  bool deep_copy(const PointCloud& target) noexcept;

  /**
   * @brief Transfers ownership of the point buffer from @p target to @c *this.
   *
   * @details
   * After the call @p target is empty.
   *
   * Parameter @c target: Source to move from.
   * @return        @c false if @p target == @c *this, otherwise @c true.
   */
  bool move_copy(PointCloud& target) noexcept;

  /**
   * @brief Returns the total serialised byte count for this point cloud.
   *
   * @details
   * Equals: @c sizeof(magic_begin) + @c sizeof(PointCloud) + @c size() * @c pack_size() + @c sizeof(magic_end)
   *
   * @return Total bytes written by @c operator>>.
   */
  [[nodiscard]] size_t get_serialized_size() const noexcept;

  /**
   * @brief Builds a @c KeyMap from the embedded protocol descriptor.
   *
   * @details
   * The returned map maps each field name to its byte offset within one
   * packed point record.  Optionally fills @p key_list with ordered @c Key
   * entries that also carry type information.
   *
   * Parameter @c key_list: Optional output; filled with ordered field descriptors.
   * @return          Unordered map from field name to byte offset.
   */
  [[nodiscard]] KeyMap get_key_map(KeyList* key_list = nullptr) const noexcept;

  /**
   * @brief Returns the number of points currently stored.
   *
   * @return Point count (@c 0 if empty).
   */
  [[nodiscard]] size_t size() const noexcept;

  /**
   * @brief Returns the byte size of one packed point record.
   *
   * @details
   * Equals the sum of the byte sizes of all fields as encoded in the
   * @c Protocol.  Computed once during @c create() and cached.
   *
   * @return Bytes per point, or 0 if no schema has been set.
   */
  [[nodiscard]] size_t pack_size() const noexcept;

  /**
   * @brief Returns @c true if this object owns its point data buffer.
   *
   * @details
   * An owned buffer is freed in the destructor.  Borrowed buffers (from
   * @c shallow_copy or @c operator<<) are not freed.
   *
   * @return @c true when memory ownership is held.
   */
  [[nodiscard]] bool is_owner() const noexcept;

  /**
   * @brief Returns the raw @c size_num protocol field.
   *
   * @details
   * Each nibble encodes the byte size of one field (high nibble = first field).
   *
   * @return Protocol size-encoding integer.
   */
  [[nodiscard]] uint64_t get_protocol_size_num() const noexcept;

  /**
   * @brief Returns the raw @c type_num protocol field.
   *
   * @details
   * Each nibble encodes the @c Type enum value of one field.
   *
   * @return Protocol type-encoding integer.
   */
  [[nodiscard]] uint64_t get_protocol_type_num() const noexcept;

  /**
   * @brief Returns a human-readable comma-separated field size string.
   *
   * @details
   * Example output for a float XYZ + uint8 intensity schema: @c "4,4,4,1"
   *
   * @return Printable size string.
   */
  [[nodiscard]] std::string get_protocol_size_str() const noexcept;

  /**
   * @brief Returns the raw comma-separated field name string.
   *
   * @details
   * Example: @c "x,y,z,intensity"
   *
   * @return Field names as stored in the protocol.  String helpers treat the
   *         fixed @c names buffer as NUL-terminated, so schemas should keep the
   *         encoded name string below 160 bytes to leave room for the terminator.
   */
  [[nodiscard]] std::string get_protocol_name_str() const noexcept;

  /**
   * @brief Returns a human-readable comma-separated field type string.
   *
   * @details
   * Example: @c "float,float,float,uint8"
   *
   * @return Printable type string.
   */
  [[nodiscard]] std::string get_protocol_type_str() const noexcept;

  /**
   * @brief Returns a read-only pointer to the raw packed point buffer.
   *
   * @details
   * The buffer contains @c size() * @c pack_size() bytes in tightly-packed
   * row order (one row = one point).
   *
   * @return Pointer to the point data.  Empty deserialised payloads may still
   *         hold a non-null borrowed pointer; use @c size() / @c is_valid() to
   *         test for usable point data.
   */
  [[nodiscard]] const uint8_t* get_internal_data() const noexcept;

  /**
   * @brief Returns the maximum number of points the current allocation can hold.
   *
   * @details
   * Computed as @c capacity_ / @c pack_size_.  For borrowed buffers obtained
   * from @c operator<< or @c shallow_copy(), this returns 0.
   *
   * @return Reserved (pre-allocated) point capacity.
   */
  [[nodiscard]] size_t get_reserved_size() const noexcept;

  /**
   * @brief Reads the first three floats of point @p loop_index as XYZ.
   *
   * @details
   * Requires that the schema was created with @c create_v3f or that the first
   * three fields are @c float.  Reads directly using @c memcpy.
   *
   * Parameter @c x: Output X value.
   * Parameter @c y: Output Y value.
   * Parameter @c z: Output Z value.
   * Parameter @c loop_index: Zero-based point index.
   * @return            @c false if @p loop_index is out of range.
   */
  bool get_value_v3f(float& x, float& y, float& z, size_t loop_index) const noexcept;

  /**
   * @brief Reads the first three floats of point @p loop_index into a @c Vector3f.
   *
   * Parameter @c v3f: Output vector.
   * Parameter @c loop_index: Zero-based point index.
   * @return            @c false if @p loop_index is out of range.
   */
  bool get_value_v3f(Vector3f& v3f, size_t loop_index) const noexcept;

  /**
   * @brief Returns the first three floats of point @p loop_index as a @c Vector3f.
   *
   * Parameter @c loop_index: Zero-based point index.
   * @return            @c Vector3f with X, Y, Z; zero-initialised on out-of-range.
   */
  [[nodiscard]] Vector3f get_value_v3f(size_t loop_index) const noexcept;

  /**
   * @brief Reads the first three doubles of point @p loop_index as XYZ.
   *
   * Parameter @c x: Output X value.
   * Parameter @c y: Output Y value.
   * Parameter @c z: Output Z value.
   * Parameter @c loop_index: Zero-based point index.
   * @return            @c false if @p loop_index is out of range.
   */
  bool get_value_v3d(double& x, double& y, double& z, size_t loop_index) const noexcept;

  /**
   * @brief Reads the first three doubles of point @p loop_index into a @c Vector3d.
   *
   * Parameter @c v3d: Output vector.
   * Parameter @c loop_index: Zero-based point index.
   * @return            @c false if @p loop_index is out of range.
   */
  bool get_value_v3d(Vector3d& v3d, size_t loop_index) const noexcept;

  /**
   * @brief Returns the first three doubles of point @p loop_index as a @c Vector3d.
   *
   * Parameter @c loop_index: Zero-based point index.
   * @return            @c Vector3d with X, Y, Z; zero-initialised on out-of-range.
   */
  [[nodiscard]] Vector3d get_value_v3d(size_t loop_index) const noexcept;

  /**
   * @brief Reads a fundamental-type field from point @p loop_index at byte @p offset.
   *
   * @details
   * @p T must be a fundamental type (enforced by @c static_assert).  Use
   * @c get_key_map() to resolve field names to byte offsets.
   *
   * Template parameter @c T: Field type; must be fundamental.
   * Parameter @c t: Output value.
   * Parameter @c loop_index: Zero-based point index.
   * Parameter @c offset: Byte offset within one packed record.
   * @return            @c false and zeroes @p t if the access would be out of bounds.
   */
  template <typename T>
  bool get_value(T& t, size_t loop_index, uint16_t offset) const noexcept;

  /**
   * @brief Returns a fundamental-type field from point @p loop_index at byte @p offset.
   *
   * Template parameter @c T: Field type; must be fundamental.
   * Parameter @c loop_index: Zero-based point index.
   * Parameter @c offset: Byte offset within one packed record.
   * @return            Field value; zero-initialised if out of bounds.
   */
  template <typename T>
  [[nodiscard]] T get_value(size_t loop_index, uint16_t offset) const noexcept;

  /**
   * @brief Reads a named field from point @p loop_index using a cached @c KeyMap.
   *
   * @details
   * Looks up @p key in @p key_map to obtain the byte offset, then calls the
   * offset-based overload.  Returns @c false and zeroes @p t if the key is not
   * found.  The current lookup uses @c key.data(), so pass a NUL-terminated
   * full field name rather than an arbitrary non-terminated @c string_view
   * slice.
   *
   * Template parameter @c T: Field type; must be fundamental.
   * Parameter @c t: Output value.
   * Parameter @c loop_index: Zero-based point index.
   * Parameter @c key_map: Map obtained from @c get_key_map().
   * Parameter @c key: NUL-terminated full field name to look up.
   * @return            @c false if the key is not found or the access is out of bounds.
   */
  template <typename T>
  bool get_value(T& t, size_t loop_index, KeyMap& key_map, std::string_view key) const noexcept;

  /**
   * @brief Returns a named field from point @p loop_index using a cached @c KeyMap.
   *
   * Template parameter @c T: Field type; must be fundamental.
   * Parameter @c loop_index: Zero-based point index.
   * Parameter @c key_map: Map obtained from @c get_key_map().
   * Parameter @c key: NUL-terminated full field name.
   * @return            Field value; zero-initialised if not found or out of bounds.
   */
  template <typename T>
  [[nodiscard]] T get_value(size_t loop_index, KeyMap& key_map, std::string_view key) const noexcept;

  /**
   * @brief Returns a field value as @c double for display or numeric analysis.
   *
   * @details
   * Dispatches to the appropriate @c get_value<T> specialisation based on
   * @p type and casts the result to @c double.  Useful when the caller knows
   * the field type at runtime (e.g. from @c Key::type).
   *
   * Parameter @c loop_index: Zero-based point index.
   * Parameter @c offset: Byte offset within one record.
   * Parameter @c type: @c Type enum value for this field.
   * @return            Field value as @c double, or 0 on unknown type.
   */
  [[nodiscard]] double get_value_for_double_float(size_t loop_index, uint16_t offset, uint8_t type) const noexcept;

  /**
   * @brief Returns a named field value as @c double using a @c KeyMap.
   *
   * Parameter @c loop_index: Zero-based point index.
   * Parameter @c key_map: Map obtained from @c get_key_map().
   * Parameter @c key: NUL-terminated full field name.
   * Parameter @c type: @c Type enum value for this field.
   * @return            Field value as @c double, or 0 on unknown type or key not found.
   */
  [[nodiscard]] double get_value_for_double_float(size_t loop_index, KeyMap& key_map, std::string_view key,
                                                  uint8_t type) const noexcept;

  /**
   * @brief Returns a field value formatted as a printable string.
   *
   * @details
   * Calls @c std::to_string (or "true"/"false" for bool) based on @p type.
   *
   * Parameter @c loop_index: Zero-based point index.
   * Parameter @c offset: Byte offset within one record.
   * Parameter @c type: @c Type enum value.
   * @return            String representation, or empty on unknown type.
   */
  [[nodiscard]] std::string get_value_for_print(size_t loop_index, uint16_t offset, uint8_t type) const noexcept;

  /**
   * @brief Returns a named field value formatted as a printable string.
   *
   * Parameter @c loop_index: Zero-based point index.
   * Parameter @c key_map: Map obtained from @c get_key_map().
   * Parameter @c key: NUL-terminated full field name.
   * Parameter @c type: @c Type enum value.
   * @return            String representation; missing keys or out-of-bounds
   *                    reads format the zero value for known types, while
   *                    unknown types return an empty string.
   */
  [[nodiscard]] std::string get_value_for_print(size_t loop_index, KeyMap& key_map, std::string_view key,
                                                uint8_t type) const noexcept;

  /**
   * @brief Creates a point cloud from pre-built protocol integers and a name string.
   *
   * @details
   * Low-level creation path used when the caller has already computed
   * @p size_num and @p type_num (e.g. from a received wire buffer).
   * Use the template @c create<T...>() for type-safe construction.
   *
   * Parameter @c size: Maximum number of points to pre-allocate.
   * Parameter @c size_num: Protocol size encoding (nibbles = byte sizes per field).
   * Parameter @c type_num: Protocol type encoding (nibbles = @c Type per field), stored as provided.
   * Parameter @c key_str: Comma-separated field names (accepted up to 160 bytes,
   *                 3-16 fields).  Keep the encoded string below 160 bytes if
   *                 you rely on string-returning helpers.
   * @return         @c false if @p size_num or @p key_str fail protocol validation.
   */
  bool create(size_t size, uint64_t size_num, uint64_t type_num, std::string_view key_str) noexcept;

  /**
   * @brief Creates a point cloud with a type-safe variadic field schema.
   *
   * @details
   * Derives @c size_num and @c type_num at compile time from the template
   * parameters.  @p keys must contain exactly @c sizeof...(T) names.
   *
   * Template parameter @c T: Field types; must all be fundamental.  3-16 types required.
   * Parameter @c _size: Maximum number of points.
   * Parameter @c keys: Field names in the same order as @c T... .
   * @return         @c false if the number of keys does not match, the field
   *                 count is out of range, or key/type packing fails.  The
   *                 caller should use the supported exact C++ types listed
   *                 above; other fundamental types may encode as unknown.
   */
  template <typename... T>
  bool create(size_t _size, const std::vector<std::string>& keys = {}) noexcept;

  /**
   * @brief Creates a point cloud with @c float XYZ as the first three fields,
   *        plus additional variadic fields.
   *
   * @details
   * Prepends @c "x","y","z" to @p keys and @c float,float,float to the type list,
   * then calls @c create<float,float,float,T...>().
   *
   * Template parameter @c T: Types for additional fields beyond XYZ.
   * Parameter @c _size: Maximum number of points.
   * Parameter @c keys: Names for the additional fields (not including x, y, z).
   * @return         @c false if schema creation fails.
   */
  template <typename... T>
  bool create_v3f(size_t _size, const std::vector<std::string>& keys = {}) noexcept;

  /**
   * @brief Creates a point cloud with @c double XYZ as the first three fields,
   *        plus additional variadic fields.
   *
   * @details
   * Prepends @c "x","y","z" and @c double,double,double, then calls
   * @c create<double,double,double,T...>().
   *
   * Template parameter @c T: Types for additional fields beyond XYZ.
   * Parameter @c _size: Maximum number of points.
   * Parameter @c keys: Names for the additional fields (not including x, y, z).
   * @return         @c false if schema creation fails.
   */
  template <typename... T>
  bool create_v3d(size_t _size, const std::vector<std::string>& keys = {}) noexcept;

  /**
   * @brief Bulk-fills the point buffer from pre-packed external memory.
   *
   * @details
   * Copies @p _size * @c pack_size() bytes from @p src_data directly into the
   * owned buffer without interpreting field types.  The schema (protocol) must
   * have been set up by a prior @c create() call.  The buffer must be large
   * enough to hold @p _size points.
   *
   * Parameter @c src_data: Source buffer in packed row order.  Must be non-null.
   * Parameter @c _size: Number of points to copy.  Must be non-zero.
   * @return          @c false on invalid arguments or insufficient capacity.
   */
  bool fill_packed_data(const uint8_t* src_data, size_t _size) noexcept;

  /**
   * @brief Appends one point record at the end of the buffer.
   *
   * @details
   * Each argument maps to one field in schema order.  The total byte size of
   * @c T... must exactly equal @c pack_size().  Returns @c false if the buffer
   * is full or an owned buffer has not been set up.  Runtime validation checks
   * the total packed byte size, not semantic equality with @c Protocol::type_num.
   *
   * Template parameter @c T: Field value types; must all be fundamental.  3-16 required.
   * Parameter @c args: Field values in schema order.
   * @return       @c false if there is no owned buffer, the buffer is full, or
   *               the pack size does not match.
   */
  template <typename... T>
  bool push_value(T... args) noexcept;

  /**
   * @brief Appends one point with float XYZ coordinates plus additional fields.
   *
   * Template parameter @c T: Types for additional fields beyond XYZ.
   * Parameter @c x: X coordinate.
   * Parameter @c y: Y coordinate.
   * Parameter @c z: Z coordinate.
   * Parameter @c args: Additional field values.
   * @return       @c false on overflow or packed-size mismatch.
   */
  template <typename... T>
  bool push_value_v3f(float x, float y, float z, T... args) noexcept;

  /**
   * @brief Appends one point from a @c Vector3f plus additional fields.
   *
   * Template parameter @c T: Types for additional fields beyond XYZ.
   * Parameter @c v3f: XYZ coordinates.
   * Parameter @c args: Additional field values.
   * @return       @c false on overflow or packed-size mismatch.
   */
  template <typename... T>
  bool push_value_v3f(Vector3f v3f, T... args) noexcept;

  /**
   * @brief Appends one point with double XYZ coordinates plus additional fields.
   *
   * Template parameter @c T: Types for additional fields beyond XYZ.
   * Parameter @c x: X coordinate.
   * Parameter @c y: Y coordinate.
   * Parameter @c z: Z coordinate.
   * Parameter @c args: Additional field values.
   * @return       @c false on overflow or packed-size mismatch.
   */
  template <typename... T>
  bool push_value_v3d(double x, double y, double z, T... args) noexcept;

  /**
   * @brief Appends one point from a @c Vector3d plus additional fields.
   *
   * Template parameter @c T: Types for additional fields beyond XYZ.
   * Parameter @c v3d: XYZ coordinates.
   * Parameter @c args: Additional field values.
   * @return       @c false on overflow or packed-size mismatch.
   */
  template <typename... T>
  bool push_value_v3d(Vector3d v3d, T... args) noexcept;

  /**
   * @brief Sets the logical point count without reallocating.
   *
   * @details
   * Updates the internal write cursor to @c size * @c pack_size_.  Useful
   * before calling @c set_value() to overwrite existing records.  Does not
   * zero-initialise newly exposed records.
   *
   * Parameter @c size: New logical point count.  Must not exceed allocated capacity.
   * @return      @c false if no owned buffer exists, @c pack_size_ is zero, or
   *              @p size exceeds allocated capacity.
   */
  bool resize(size_t size) noexcept;

  /**
   * @brief Overwrites the point record at @p loop_index with new field values.
   *
   * @details
   * Requires the logical size and internal write cursor to be consistent
   * (@c index_ == @c size_ * @c pack_size_).  Calling @c resize() is the usual
   * way to establish that state before random overwrites, while
   * @c fill_packed_data() or a completely filled append sequence can also make
   * the cursor consistent.  Returns @c false and logs a diagnostic if the
   * cursor state is invalid.
   *
   * Template parameter @c T: Field value types; must all be fundamental.  3-16 required.
   * Parameter @c loop_index: Zero-based point index to overwrite.
   * Parameter @c args: Field values in schema order.
   * @return            @c false if there is no owned buffer, @p loop_index is
   *                    out of range, current size exceeds capacity, the write
   *                    cursor is inconsistent, or the pack size does not match.
   */
  template <typename... T>
  bool set_value(size_t loop_index, T... args) noexcept;

  /**
   * @brief Overwrites point @p loop_index with float XYZ plus additional fields.
   *
   * Template parameter @c T: Types for additional fields.
   * Parameter @c loop_index: Zero-based point index.
   * Parameter @c x, @c y, @c z: XYZ float coordinates.
   * Parameter @c args: Additional field values.
   * @return            @c false on failure.
   */
  template <typename... T>
  bool set_value_v3f(size_t loop_index, float x, float y, float z, T... args) noexcept;

  /**
   * @brief Overwrites point @p loop_index from a @c Vector3f plus additional fields.
   *
   * Template parameter @c T: Types for additional fields.
   * Parameter @c loop_index: Zero-based point index.
   * Parameter @c v3f: XYZ float coordinates.
   * Parameter @c args: Additional field values.
   * @return            @c false on failure.
   */
  template <typename... T>
  bool set_value_v3f(size_t loop_index, Vector3f v3f, T... args) noexcept;

  /**
   * @brief Overwrites point @p loop_index with double XYZ plus additional fields.
   *
   * Template parameter @c T: Types for additional fields.
   * Parameter @c loop_index: Zero-based point index.
   * Parameter @c x, @c y, @c z: XYZ double coordinates.
   * Parameter @c args: Additional field values.
   * @return            @c false on failure.
   */
  template <typename... T>
  bool set_value_v3d(size_t loop_index, double x, double y, double z, T... args) noexcept;

  /**
   * @brief Overwrites point @p loop_index from a @c Vector3d plus additional fields.
   *
   * Template parameter @c T: Types for additional fields.
   * Parameter @c loop_index: Zero-based point index.
   * Parameter @c v3d: XYZ double coordinates.
   * Parameter @c args: Additional field values.
   * @return            @c false on failure.
   */
  template <typename... T>
  bool set_value_v3d(size_t loop_index, Vector3d v3d, T... args) noexcept;

  /**
   * @brief Resets the logical point count (and optionally releases all resources).
   *
   * @details
   * When @p force is @c false (default), only @c size_ and the write cursor
   * are zeroed; the buffer and protocol remain intact, allowing the buffer to
   * be refilled without reallocation.
   *
   * When @p force is @c true, the owned buffer is freed and all protocol and
   * header fields are zeroed -- equivalent to returning the object to its
   * default-constructed state.
   *
   * Parameter @c force: If @c true, free the buffer and reset all state.
   */
  void clear(bool force = false) noexcept;

  Header header;  ///< Sequencing and timestamp metadata for this point cloud.

  static constexpr bool kZerocopyTypes{true};  ///< Internal marker for VLink zero-copy type traits.

 private:
  struct VLINK_EXPORT_AND_ALIGNED(8) Protocol final {
    uint64_t size_num{0};
    char names[160]{0};
    uint64_t type_num{0};

    Protocol() noexcept = default;

    template <typename... T>
    static uint64_t get_size_num() noexcept;

    template <typename... T>
    static uint64_t get_type_num() noexcept;

    template <typename T>
    static constexpr uint8_t get_type() noexcept;

    constexpr uint64_t get_pack_size() const noexcept;

    static bool check_valid(uint64_t _size_num, std::string_view _names) noexcept;

    static std::string get_names(const std::vector<std::string>& keys) noexcept;

    KeyList get_key_list() const noexcept;

    std::string get_size_for_print() const noexcept;

    std::string get_type_for_print() const noexcept;
  };

  Protocol protocol_;
  uint8_t* data_{nullptr};
  size_t capacity_{0};
  size_t size_{0};
  uint32_t reserved_buf_{0};
  uint16_t pack_size_{0};
  bool is_owner_{false};
  uint64_t index_{0};

  static constexpr uint32_t kMagicNumberBegin{0x98B7F16A};
  static constexpr uint32_t kMagicNumberEnd{0x98B7F16F};
};

////////////////////////////////////////////////////////////////
/// Details
////////////////////////////////////////////////////////////////

/// @cond INTERNAL

template <typename T>
inline bool PointCloud::get_value(T& t, size_t loop_index, uint16_t offset) const noexcept {
  static_assert(std::is_fundamental_v<T>, "T must be fundamental.");

  size_t p = (loop_index * pack_size_) + offset;

  if VUNLIKELY (p + sizeof(T) > size_ * pack_size_) {
    std::memset(&t, 0, sizeof(T));
    return false;
  }

  std::memcpy(&t, data_ + p, sizeof(T));

  return true;
}

template <typename T>
inline T PointCloud::get_value(size_t loop_index, uint16_t offset) const noexcept {
  static_assert(std::is_fundamental_v<T>, "T must be fundamental.");

  T t;

  get_value(t, loop_index, offset);

  return t;
}

template <typename T>
inline bool PointCloud::get_value(T& t, size_t loop_index, KeyMap& key_map, std::string_view key) const noexcept {
  static_assert(std::is_fundamental_v<T>, "T must be fundamental.");

  auto iter = key_map.find(key.data());

  if VUNLIKELY (iter == key_map.end()) {
    std::memset(&t, 0, sizeof(T));
    return false;
  }

  return get_value<T>(t, loop_index, iter->second);
}

template <typename T>
inline T PointCloud::get_value(size_t loop_index, KeyMap& key_map, std::string_view key) const noexcept {
  static_assert(std::is_fundamental_v<T>, "T must be fundamental.");

  T t;

  get_value(t, loop_index, key_map, key);

  return t;
}

template <typename... T>
inline bool PointCloud::create(size_t size, const std::vector<std::string>& keys) noexcept {
  static_assert((std::is_fundamental_v<T> && ...), "All types must be fundamental.");

  static_assert(sizeof...(T) >= 3 && sizeof...(T) <= 16, "The number of keys ranges is [3 ~ 16].");

  if VUNLIKELY (sizeof...(T) != keys.size()) {
    return false;
  }

  uint64_t size_num = Protocol::get_size_num<T...>();

  if VUNLIKELY (size_num == 0) {
    return false;
  }

  std::string key_str = Protocol::get_names(keys);

  if VUNLIKELY (key_str.empty()) {
    return false;
  }

  uint64_t type_num = Protocol::get_type_num<T...>();

  if VUNLIKELY (type_num == 0) {
    return false;
  }

  if (is_owner_ && data_ && capacity_ != 0) {
    Bytes::bytes_free(data_, capacity_);
  }

  protocol_.size_num = size_num;
  std::memset(protocol_.names, 0, sizeof(protocol_.names));
  std::memcpy(protocol_.names, key_str.c_str(), key_str.size());
  protocol_.type_num = type_num;

  size_ = 0;
  pack_size_ = protocol_.get_pack_size();
  capacity_ = size * pack_size_;

  if VLIKELY (capacity_ != 0) {
    data_ = Bytes::bytes_malloc(capacity_);
    is_owner_ = true;
  }

  index_ = 0;

  return true;
}

template <typename... T>
inline bool PointCloud::create_v3f(size_t _size, const std::vector<std::string>& keys) noexcept {
  std::vector<std::string> target_keys{"x", "y", "z"};

  target_keys.insert(target_keys.end(), keys.begin(), keys.end());

  return create<float, float, float, T...>(_size, target_keys);
}

template <typename... T>
inline bool PointCloud::create_v3d(size_t _size, const std::vector<std::string>& keys) noexcept {
  std::vector<std::string> target_keys{"x", "y", "z"};

  target_keys.insert(target_keys.end(), keys.begin(), keys.end());

  return create<double, double, double, T...>(_size, target_keys);
}

inline bool PointCloud::fill_packed_data(const uint8_t* src_data, size_t _size) noexcept {
  bool is_fill_success = false;

  if VUNLIKELY (_size == 0 || !src_data) {
    is_fill_success = false;
  } else if VUNLIKELY (!is_owner_ || !data_ || pack_size_ == 0 || capacity_ == 0) {
    is_fill_success = false;
  } else if VUNLIKELY (_size * pack_size_ > capacity_) {
    is_fill_success = false;
  } else {
    std::memcpy(data_, src_data, _size * pack_size_);
    size_ = _size;
    index_ = _size * pack_size_;
    is_fill_success = true;
  }

  return is_fill_success;
}

template <typename... T>
inline bool PointCloud::push_value(T... args) noexcept {
  static_assert((std::is_fundamental_v<T> && ...), "All types must be fundamental.");

  static_assert(sizeof...(T) >= 3 && sizeof...(T) <= 16, "The number of keys ranges is [3 ~ 16].");

  if VUNLIKELY (!is_owner_ || !data_ || pack_size_ == 0 || capacity_ == 0) {
    return false;
  }

  if VUNLIKELY (size_ * pack_size_ >= capacity_) {
    return false;
  }

  constexpr size_t kTargetPackSize = (sizeof(T) + ...);

  if VUNLIKELY (kTargetPackSize != pack_size_) {
    return false;
  }

  auto* target_ptr = data_ + index_;

  (
      [&]() {
        std::memcpy(target_ptr, &args, sizeof(args));
        target_ptr += sizeof(args);
      }(),
      ...);

  index_ += pack_size_;
  ++size_;

  return true;
}

template <typename... T>
inline bool PointCloud::push_value_v3f(float x, float y, float z, T... args) noexcept {
  return push_value(x, y, z, args...);
}

template <typename... T>
inline bool PointCloud::push_value_v3f(Vector3f v3f, T... args) noexcept {
  return push_value(v3f.x, v3f.y, v3f.z, args...);
}

template <typename... T>
inline bool PointCloud::push_value_v3d(double x, double y, double z, T... args) noexcept {
  return push_value(x, y, z, args...);
}

template <typename... T>
inline bool PointCloud::push_value_v3d(Vector3d v3d, T... args) noexcept {
  return push_value(v3d.x, v3d.y, v3d.z, args...);
}

inline bool PointCloud::resize(size_t size) noexcept {
  if VUNLIKELY (!is_owner_ || !data_ || pack_size_ == 0 || capacity_ == 0) {
    return false;
  }

  if VUNLIKELY (size * pack_size_ > capacity_) {
    return false;
  }

  size_ = size;
  index_ = size_ * pack_size_;

  return true;
}

template <typename... T>
bool PointCloud::set_value(size_t loop_index, T... args) noexcept {
  static_assert((std::is_fundamental_v<T> && ...), "All types must be fundamental.");

  static_assert(sizeof...(T) >= 3 && sizeof...(T) <= 16, "The number of keys ranges is [3 ~ 16].");

  if VUNLIKELY (!is_owner_ || !data_ || pack_size_ == 0 || capacity_ == 0) {
    return false;
  }

  if VUNLIKELY (loop_index >= size_ || size_ * pack_size_ > capacity_) {
    return false;
  }

  if VUNLIKELY (index_ != size_ * pack_size_) {
    std::cerr << "[PointCloud::set_value] Invalid buffer state: "
              << "The current buffer size does not match the capacity. "
              << "Please call resize(size) before using set_value()." << std::endl;
    return false;
  }

  constexpr size_t kTargetPackSize = (sizeof(T) + ...);

  if VUNLIKELY (kTargetPackSize != pack_size_) {
    return false;
  }

  auto* target_ptr = data_ + (loop_index * pack_size_);

  (
      [&]() {
        std::memcpy(target_ptr, &args, sizeof(args));
        target_ptr += sizeof(args);
      }(),
      ...);

  return true;
}

template <typename... T>
inline bool PointCloud::set_value_v3f(size_t loop_index, float x, float y, float z, T... args) noexcept {
  return set_value(loop_index, x, y, z, args...);
}

template <typename... T>
inline bool PointCloud::set_value_v3f(size_t loop_index, Vector3f v3f, T... args) noexcept {
  return set_value(loop_index, v3f.x, v3f.y, v3f.z, args...);
}

template <typename... T>
inline bool PointCloud::set_value_v3d(size_t loop_index, double x, double y, double z, T... args) noexcept {
  return set_value(loop_index, x, y, z, args...);
}

template <typename... T>
inline bool PointCloud::set_value_v3d(size_t loop_index, Vector3d v3d, T... args) noexcept {
  return set_value(loop_index, v3d.x, v3d.y, v3d.z, args...);
}

template <typename... T>
inline uint64_t PointCloud::Protocol::get_size_num() noexcept {
  uint64_t target_num = 0;

  uint64_t key_shift = sizeof...(T) * 4;

  (
      [&](auto type) {
        key_shift -= 4;
        target_num |= (static_cast<uint64_t>(sizeof(type)) & 0xF) << key_shift;
      }(T{}),
      ...);

  return target_num;
}

template <typename... T>
inline uint64_t PointCloud::Protocol::get_type_num() noexcept {
  uint64_t target_num = 0;

  uint64_t key_shift = sizeof...(T) * 4;

  (
      [&](auto type) {
        key_shift -= 4;
        target_num |= (static_cast<uint64_t>(get_type<decltype(type)>()) & 0xF) << key_shift;
      }(T{}),
      ...);

  return target_num;
}

template <typename T>
inline constexpr uint8_t PointCloud::Protocol::get_type() noexcept {
  if constexpr (std::is_same_v<T, bool>) {
    return kBoolType;
  } else if constexpr (std::is_same_v<T, int8_t>) {
    return kInt8Type;
  } else if constexpr (std::is_same_v<T, uint8_t>) {
    return kUint8Type;
  } else if constexpr (std::is_same_v<T, int16_t>) {
    return kInt16Type;
  } else if constexpr (std::is_same_v<T, uint16_t>) {
    return kUint16Type;
  } else if constexpr (std::is_same_v<T, int32_t>) {
    return kInt32Type;
  } else if constexpr (std::is_same_v<T, uint32_t>) {
    return kUint32Type;
  } else if constexpr (std::is_same_v<T, int64_t>) {
    return kInt64Type;
  } else if constexpr (std::is_same_v<T, uint64_t>) {
    return kUint64Type;
  } else if constexpr (std::is_same_v<T, float>) {
    return kFloatType;
  } else if constexpr (std::is_same_v<T, double>) {
    return kDoubleType;
  } else {
    return kUnknownType;
  }
}

inline constexpr uint64_t PointCloud::Protocol::get_pack_size() const noexcept {
  int sum = 0;

  auto target_num = size_num;

  while (target_num > 0) {
    sum += target_num & 0xF;
    target_num >>= 4;
  }

  return sum;
}

/// @endcond

}  // namespace zerocopy

}  // namespace vlink
