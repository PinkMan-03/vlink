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
 * @file dynamic_data.h
 * @brief Type-erased data container for runtime serialisation and deserialisation.
 *
 * @details
 * @c DynamicData stores a serialized payload (@c Bytes) together with a type-name tag
 * in a reserved @c kOffset = 20 byte prefix of the buffer.  The type-name string
 * literal, including its trailing NUL, must be shorter than this prefix.  This allows
 * the payload to be transported through channels that do not know the compile-time message
 * type, and later deserialized back to the concrete type.
 *
 * @par Internal layout
 * @code
 * [ type name (20 bytes max) | serialized payload ]
 * ^--- type_                      ^--- data_.data()
 * @endcode
 *
 * The type name is stored in-place in the first @c kOffset bytes of the @c Bytes buffer
 * and exposed as a @c std::string_view pointing into that buffer.
 *
 * @par Restrictions
 * - Cannot serialize or deserialize another @c DynamicData object (static_assert).
 * - Cannot serialize CDR types (static_assert).
 * - The type name string literal (including NUL) must be shorter than @c kOffset (20) bytes (static_assert).
 *
 * @par Serialisation
 * @code
 * vlink::DynamicData dd;
 * MyProtoMsg msg;
 * msg.set_value(42);
 * dd.load("MyProtoMsg", msg);
 *
 * // Store/transport dd ...
 *
 * MyProtoMsg recovered = dd.as<MyProtoMsg>();
 * @endcode
 *
 * @par Binary transport (operator<< / operator>>)
 * @code
 * vlink::Bytes wire_bytes;
 * dd >> wire_bytes;   // serialize DynamicData to Bytes
 *
 * vlink::DynamicData dd2;
 * dd2 << wire_bytes;  // deserialize Bytes back to DynamicData
 * @endcode
 */

#pragma once

#include <memory>
#include <string_view>
#include <utility>

#include "../serializer.h"

namespace vlink {

/**
 * @class DynamicData
 * @brief Runtime-typed container that serializes any VLink-compatible message type.
 *
 * @details
 * Uses @c Serializer to pack the message into internal @c Bytes storage.
 * The type tag is embedded in the first @c kOffset bytes of the buffer.
 */
class VLINK_EXPORT DynamicData final {
 public:
  /**
   * @brief Constructs an empty @c DynamicData with no payload.
   */
  DynamicData();

  /**
   * @brief Copy-constructs a @c DynamicData and rebinds the type view.
   *
   * The copied @c type_ view points into this object's copied buffer, not the
   * source object's buffer.
   */
  DynamicData(const DynamicData& target);

  /**
   * @brief Move-constructs a @c DynamicData and rebinds the type view.
   *
   * The moved-to @c type_ view points into this object's buffer.  The moved-from
   * object is left with an empty type view.
   */
  DynamicData(DynamicData&& target) noexcept;

  /**
   * @brief Copy-assigns a @c DynamicData and rebinds the type view.
   *
   * The assigned @c type_ view points into this object's copied buffer, not the
   * source object's buffer.
   */
  DynamicData& operator=(const DynamicData& target);

  /**
   * @brief Move-assigns a @c DynamicData and rebinds the type view.
   *
   * The moved-to @c type_ view points into this object's buffer.  The moved-from
   * object is left with an empty type view.
   */
  DynamicData& operator=(DynamicData&& target) noexcept;

  /**
   * @brief Serializes @p t with a type-name tag into the internal buffer.
   *
   * @details
   * The type name is stored in the first @c kOffset bytes of the underlying @c Bytes buffer.
   * A @c static_assert enforces that @p SizeT < @c kOffset (20).
   *
   * @tparam SizeT  Length of the type-name string literal including NUL.
   * @tparam T      Message type to serialize (must be supported by @c Serializer).
   * @param type    Type name string literal, e.g., @c "MyProtoMsg".
   * @param t       Message instance to serialize.
   * @return Reference to @c *this for chaining.
   *
   * @note Logs a failure if serialisation returns @c false.
   */
  template <uint8_t SizeT, typename T>
  DynamicData& load(const char (&type)[SizeT], const T& t);

  /**
   * @brief Deserializes the internal buffer into @p t.
   *
   * @details
   * Uses @c Serializer to populate @p t from the payload bytes.
   * Returns @c false if the buffer is empty or deserialisation fails.
   *
   * @tparam T  Message type to deserialize into.
   * @param t   Output parameter populated on success.
   * @return @c true if deserialization succeeded; @c false otherwise.
   */
  template <typename T>
  bool convert(T& t) const;

  /**
   * @brief Deserializes the internal buffer and returns the result by value.
   *
   * @details
   * Constructs a default @p T (or @c shared_ptr<T> for pointer types) and
   * calls @c convert().  Returns a default-constructed @p T on failure.
   *
   * @tparam T  Message type to deserialize.
   * @return Deserialized instance, or default-constructed value on failure.
   */
  template <typename T>
  [[nodiscard]] T as() const;

  /**
   * @brief Returns a const reference to the raw serialized bytes (including type tag).
   *
   * @return Internal @c Bytes buffer.
   */
  [[nodiscard]] const Bytes& get_data() const;

  /**
   * @brief Returns the type name string embedded in the buffer.
   *
   * @details
   * Points into the first @c kOffset bytes of the internal @c Bytes buffer.
   * The view is only valid for the lifetime of this @c DynamicData instance.
   *
   * @return @c string_view of the type name.
   */
  [[nodiscard]] const std::string_view& get_type() const;

  /**
   * @brief Returns @c true if no payload has been loaded.
   *
   * @return @c true if the internal @c Bytes buffer is empty.
   */
  [[nodiscard]] bool is_empty() const;

  /**
   * @brief Returns @c true if both @c DynamicData objects have identical buffers.
   *
   * @param target  Right-hand side.
   * @return @c true if the raw bytes are equal.
   */
  [[nodiscard]] bool operator==(const DynamicData& target) const;

  /**
   * @brief Returns @c true if the objects differ.
   *
   * @param target  Right-hand side.
   * @return @c true if the raw bytes differ.
   */
  [[nodiscard]] bool operator!=(const DynamicData& target) const;

  /**
   * @brief Compile-time trait marker -- returns @c true to identify this as a @c DynamicData type.
   *
   * @details
   * Used internally by @c Serializer via template specialisation detection.
   *
   * @return Always @c true.
   */
  [[nodiscard]] static constexpr bool is_vlink_dynamic_data();

  /**
   * @brief Returns the byte offset where the serialized payload begins.
   *
   * @details
   * Equal to @c kOffset (20).  The first @c kOffset bytes are reserved for the type name.
   *
   * @return Offset value (20).
   */
  [[nodiscard]] static constexpr uint8_t get_offset();

  /**
   * @brief Deserializes a wire-format @c Bytes blob into this @c DynamicData.
   *
   * @param bytes  Wire-format bytes produced by @c operator>>.
   * @return @c true on success; @c false if parsing failed.
   */
  bool operator<<(const Bytes& bytes) noexcept;

  /**
   * @brief Serializes this @c DynamicData to a wire-format @c Bytes blob.
   *
   * @param bytes  Output buffer.
   * @return @c true on success; @c false if the internal buffer is empty, non-owning,
   *         or does not include the reserved prefix.
   */
  bool operator>>(Bytes& bytes) const noexcept;

 private:
  void refresh_type_view() noexcept;

  void refresh_type_view(size_t type_size) noexcept;

  static constexpr uint8_t kOffset{20};
  Bytes data_;
  std::string_view type_;
};

////////////////////////////////////////////////////////////////
/// Details
////////////////////////////////////////////////////////////////

template <uint8_t SizeT, typename T>
inline DynamicData& DynamicData::load(const char (&type)[SizeT], const T& t) {
  static_assert(SizeT < get_offset(), "DynamicData name out of size.");
  static_assert(!std::is_same_v<DynamicData, T>, "DynamicData can not serialize self.");
  static_assert(!Serializer::is_cdr_type<T>(), "DynamicData can not serialize cdr data.");

  Bytes next_data;
  bool ret = Serializer::serialize<Serializer::get_type_of<T>()>(t, next_data, TransportType::kUnknown, get_offset());

  if VUNLIKELY (!ret || next_data.real_data() == nullptr) {
    data_ = Bytes();
    type_ = std::string_view();
    VLOG_F("DynamicData serialize failed.");
    return *this;
  }

  data_ = std::move(next_data);

  if constexpr (SizeT > 0) {
    std::memcpy(data_.real_data(), type, SizeT);

    if constexpr (SizeT < kOffset) {
      std::memset(data_.real_data() + SizeT, 0, kOffset - SizeT);
    }

    size_t name_len = SizeT;

    if (name_len > 0 && type[SizeT - 1] == '\0') {
      name_len -= 1;
    }

    type_ = std::string_view(reinterpret_cast<const char*>(data_.real_data()), name_len);
  } else {
    std::memset(data_.real_data(), 0, kOffset);
    type_ = std::string_view();
  }

  return *this;
}

template <typename T>
inline bool DynamicData::convert(T& t) const {
  static_assert(!std::is_same_v<DynamicData, T>, "DynamicData can not deserialize self.");
  static_assert(!Serializer::is_cdr_type<T>(), "DynamicData can not deserialize cdr data.");

  if VUNLIKELY (data_.empty()) {
    return false;
  }

  return Serializer::deserialize<Serializer::get_type_of<T>()>(data_, t, TransportType::kUnknown);
}

template <typename T>
inline T DynamicData::as() const {
  T t;

  if constexpr (Traits::IsSharedPtr<T>()) {
    t = std::make_shared<typename T::element_type>();
  }

  if VUNLIKELY (!convert(t)) {
    VLOG_F("DynamicData deserialize failed.");
    return T{};
  }

  return t;
}

inline constexpr bool DynamicData::is_vlink_dynamic_data() { return true; }

inline constexpr uint8_t DynamicData::get_offset() { return kOffset; }

}  // namespace vlink
