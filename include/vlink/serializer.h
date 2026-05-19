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
 * @file serializer.h
 * @brief Compile-time type-detection and serialisation utilities for VLink messages.
 *
 * @details
 * The @c Serializer namespace provides a unified serialisation and
 * deserialisation interface that automatically selects the correct codec
 * based on the C++ type of the message via @c constexpr if chains.
 *
 * @par Supported Serializer Types
 * | Type constant       | C++ Type Criteria                                      | Notes                             |
 * | ------------------- | ------------------------------------------------------ | --------------------------------- |
 * | @c kBytesType       | @c T == Bytes                                          | Pass-through, no-copy.            |
 * | @c kDynamicType     | Has @c is_vlink_dynamic_data() member                  | Dynamic type via @c operator>>/<< |
 * | @c kCdrType         | Has @c serialize(Cdr&) and @c deserialize(Cdr&)        | FastDDS CDR codec.                |
 * | @c kProtoType       | Has @c SerializeToArray() and @c ParseFromArray()      | Protobuf by value.                |
 * | @c kProtoPtrType    | Pointer with @c SerializeToArray / @c ParseFromArray   | Arena-managed proto pointer.      |
 * | @c kFlatTableType   | Inherits @c flatbuffers::NativeTable                   | FlatBuffers native table.         |
 * | @c kFlatPtrType     | Pointer to @c flatbuffers::Table subclass              | Zero-copy FlatBuffers read.       |
 * | @c kFlatBuilderType | Has @c fbb_ member and @c Finish()                     | FlatBuffers builder.              |
 * | @c kCustomType      | Has @c operator>>(Bytes&) and @c operator<<(const Bytes&) | User-defined codec.            |
 * | @c kStringType      | @c T == std::string                                    | UTF-8 string.                     |
 * | @c kCharsType       | Constructible as @c std::string but not @c std::string | C string literal / @c char*.      |
 * | @c kStreamType      | Has @c operator<< and @c operator>> with stringstream  | Stream-based text encoding.       |
 * | @c kStandardType    | Trivial standard-layout value (POD)                    | Byte-copied during serialization. |
 * | @c kStandardPtrType | Pointer to trivial standard-layout type                | Zero-copy POD pointer.            |
 *
 * Most value-like detectors unwrap @c std::shared_ptr<T> before matching
 * (for example protobuf values, CDR values, FlatBuffers native tables, custom
 * codecs, strings, stream types, and standard-layout values).
 *
 * @par Type Detection
 * @code
 * // At compile time, get the Type enumerator for any message type:
 * constexpr auto t = Serializer::get_type_of<MyMsg>();  // e.g. kProtoType
 * static_assert(Serializer::is_supported(t), "");
 * @endcode
 *
 * @par Serialize and Deserialize
 * @code
 * MyProto msg;
 * Bytes bytes;
 * Serializer::serialize(msg, bytes);       // MyProto -> Bytes
 *
 * MyProto out;
 * Serializer::deserialize(bytes, out);     // Bytes -> MyProto
 * @endcode
 *
 * @par Custom Type
 * Implement @c operator>> and @c operator<< on your type:
 * @code
 * struct MyCustomMsg {
 *     int x;
 *     void operator>>(vlink::Bytes& out) const { ... }   // serialize
 *     void operator<<(const vlink::Bytes& in)  { ... }   // deserialize
 * };
 * // Now Serializer::get_type_of<MyCustomMsg>() == kCustomType
 * @endcode
 *
 * @par Transport-aware Serialization
 * Some transports (e.g. @c dds://) use a special fast-path for CDR types
 * (pointer passing instead of byte-copy).  Pass the @c TransportType to the
 * explicit overloads to activate the transport-specific path:
 * @code
 * Serializer::serialize<kCdrType>(msg, bytes, TransportType::kDds);
 * @endcode
 *
 * @note All functions in this namespace are @c static and @c inline.
 *       The namespace name starts with a capital letter by convention (matches
 *       VLink style).  Direct use by application code is rarely needed; the
 *       framework calls these functions automatically inside @c publish(),
 *       @c listen(), @c invoke(), etc.
 */

#pragma once

#include <string>

#include "./base/bytes.h"
#include "./impl/types.h"

namespace vlink {

/**
 * @namespace Serializer
 * @brief Compile-time type detection and codec dispatch for VLink messages.
 *
 * @details
 * All functions are @c static @c inline template functions.  Application code
 * rarely calls these directly; the framework invokes them inside
 * @c publish(), @c listen(), @c invoke(), @c set(), and @c get().
 */
namespace Serializer {  // NOLINT(readability-identifier-naming)

/**
 * @enum Type
 * @brief Identifies the serialisation codec to use for a given C++ type.
 *
 * @details
 * Resolved at compile time by @c get_type_of<T>().  The value is stored
 * in the @c Publisher / @c Subscriber etc. as a @c constexpr member so that
 * all codec dispatch is zero-cost at runtime.
 */
enum Type : uint8_t {
  kUnknownType = 0,       ///< Unsupported type -- @c is_supported() returns @c false.
  kBytesType = 1,         ///< @c Bytes -- raw byte pass-through.
  kDynamicType = 2,       ///< Dynamic typed data via @c is_vlink_dynamic_data().
  kCustomType = 3,        ///< User-defined via @c operator>>(Bytes&) / @c operator<<(const Bytes&).
  kCdrType = 4,           ///< FastDDS CDR via @c serialize(Cdr&) / @c deserialize(Cdr&).
  kProtoType = 5,         ///< Protobuf value (@c MessageLite derived).
  kProtoPtrType = 6,      ///< Protobuf raw pointer (Arena-managed).
  kFlatTableType = 7,     ///< FlatBuffers NativeTable (object API).
  kFlatPtrType = 8,       ///< Pointer to @c flatbuffers::Table (zero-copy read).
  kFlatBuilderType = 9,   ///< FlatBuffers builder (@c fbb_ + @c Finish()).
  kStringType = 10,       ///< @c std::string -- UTF-8 text.
  kCharsType = 11,        ///< @c char* / string literal.
  kStreamType = 12,       ///< Stream-serialisable via @c operator<<(std::stringstream).
  kStandardType = 13,     ///< Trivial standard-layout struct (POD value).
  kStandardPtrType = 14,  ///< Pointer to trivial standard-layout struct (POD pointer).
};

/**
 * @brief Returns @c true when @p type identifies a supported serializer.
 *
 * @details
 * @c kUnknownType is the only unsupported value.  This function is called
 * by the @c static_assert in every node constructor to catch unsupported
 * message types at compile time.
 *
 * @param type  Serializer type enumerator.
 * @return      @c false only for @c kUnknownType.
 */
[[maybe_unused]] [[nodiscard]] static constexpr bool is_supported(Type type) noexcept;

/**
 * @brief Deduces the @c Type enumerator for @c T at compile time.
 *
 * @details
 * Evaluates a compile-time @c if constexpr chain that tests each codec's
 * trait (e.g. @c is_proto_type<T>()) and returns the first matching @c Type.
 * Returns @c kUnknownType if no codec matches.
 *
 * The detection order is:
 * Bytes, Dynamic, CDR, Proto, ProtoPtr, FlatTable, FlatPtr, FlatBuilder,
 * Custom, String, Chars, Standard, StandardPtr, Stream.
 *
 * @tparam T  The C++ message type to classify.
 * @return    @c Type enumerator identifying the codec for @c T.
 */
template <typename T>
[[nodiscard]] static constexpr Type get_type_of() noexcept;

/**
 * @brief Returns the coarse schema family for @p T with an explicit codec tag.
 *
 * @tparam TypeT  Explicit VLink codec kind.
 * @tparam T      C++ message type to classify.
 * @return @c SchemaType::kProtobuf, @c SchemaType::kFlatbuffers,
 *         @c SchemaType::kZeroCopy, or @c SchemaType::kRaw for
 *         schema-less payload families.
 */
template <Type TypeT, typename T>
[[nodiscard]] static constexpr SchemaType get_schema_type() noexcept;

/**
 * @brief Returns the coarse schema family inferred from @p T.
 *
 * @tparam T  C++ message type to classify.
 * @return @c SchemaType::kProtobuf, @c SchemaType::kFlatbuffers,
 *         @c SchemaType::kZeroCopy, or @c SchemaType::kRaw when the
 *         payload is schema-less but still has a stable family.
 */
template <typename T>
[[nodiscard]] static constexpr SchemaType get_schema_type() noexcept;

/**
 * @brief Returns the serialisation type name string for @c T with explicit @c TypeT.
 *
 * @details
 * Returns a human-readable type identifier used by the framework for
 * type-matching between publisher and subscriber (e.g. DDS topic type name,
 * Protobuf fully-qualified name, FlatBuffers table name).  Returns an empty
 * string for types that have no meaningful type name (e.g. @c kBytesType).
 *
 * @tparam TypeT  Explicit serializer type.
 * @tparam T      C++ message type.
 * @return        Type name string; empty if not applicable.
 */
template <Type TypeT, typename T>
[[nodiscard]] static std::string get_serialized_type() noexcept;

/**
 * @brief Returns the serialisation type name string for @c T (type auto-detected).
 *
 * @tparam T  C++ message type.
 * @return    Type name string; empty if not applicable.
 */
template <typename T>
[[nodiscard]] static std::string get_serialized_type() noexcept;

/**
 * @brief Returns the exact serialised byte size for a given @p src value.
 *
 * @details
 * Used to pre-allocate loaned buffers before serializing.  Returns @c 0 for
 * types whose serialized size is not known ahead of time or not reported by
 * the implementation (for example @c kBytesType, @c kStringType,
 * @c kFlatTableType, and @c kStandardType).
 *
 * @tparam TypeT  Serializer type.
 * @tparam T      C++ message type.
 * @param src     Source value to measure.
 * @return        Byte count needed to serialise @p src; @c 0 if unknown.
 */
template <Type TypeT, typename T>
[[nodiscard]] static size_t get_serialized_size(const T& src) noexcept;

/**
 * @brief Returns the serialized byte size (type auto-detected).
 *
 * @tparam T   C++ message type.
 * @param src  Source value to measure.
 * @return     Byte count; @c 0 if unknown.
 */
template <typename T>
[[nodiscard]] static size_t get_serialized_size(const T& src) noexcept;

/**
 * @brief Serializes @p src into @p des with explicit type and transport hints.
 *
 * @details
 * The @p transport parameter activates transport-specific fast paths (e.g. CDR
 * pointer passing for @c kDds).  Pass @c TransportType::kUnknown for the default
 * copy-based path.  The @p offset parameter prepends @p offset zero bytes
 * before the payload (used internally for some transports).
 *
 * @tparam TypeT   Serializer type.
 * @tparam T       C++ message type.
 * @param src      Source value to serialise.
 * @param des      Destination @c Bytes buffer (may be pre-allocated or loaned).
 * @param transport   Active transport backend for fast-path selection.
 * @param offset   Number of header bytes to prepend (default @c 0).
 * @return         @c true on success; @c false on serialisation failure.
 */
template <Type TypeT, typename T>
static bool serialize(const T& src, Bytes& des, TransportType transport = TransportType::kUnknown, uint8_t offset = 0);

/**
 * @brief Serializes @p src into @p des (type and transport auto-detected).
 *
 * @tparam T   C++ message type.
 * @param src  Source value.
 * @param des  Destination @c Bytes.
 * @return     @c true on success.
 */
template <typename T>
static bool serialize(const T& src, Bytes& des);

/**
 * @brief Deserializes @p src bytes into @p des with explicit type and transport hints.
 *
 * @details
 * The @p transport activates transport-specific fast paths (e.g. @c kDds
 * dereferences a CDR pointer stored in @p src instead of copying bytes).
 *
 * @tparam TypeT   Serializer type.
 * @tparam T       C++ message type.
 * @param src      Source @c Bytes buffer.
 * @param des      Destination value to fill.
 * @param transport   Active transport backend.
 * @return         @c true on success; @c false on parse failure.
 */
template <Type TypeT, typename T>
static bool deserialize(const Bytes& src, T& des, TransportType transport = TransportType::kUnknown);

/**
 * @brief Deserializes @p src bytes into @p des (type and transport auto-detected).
 *
 * @tparam T   C++ message type.
 * @param src  Source @c Bytes buffer.
 * @param des  Destination value.
 * @return     @c true on success.
 */
template <typename T>
static bool deserialize(const Bytes& src, T& des);

/**
 * @brief Converts between two types where at least one is @c Bytes.
 *
 * @details
 * A compile-time @c static_assert enforces that @c SrcT or @c DesT (or both)
 * is @c Bytes.  The three cases are:
 * - Both @c Bytes: shallow-copies @p src to @p des.
 * - @c DesT == @c Bytes: calls @c serialize(src, des).
 * - @c SrcT == @c Bytes: calls @c deserialize(src, des).
 *
 * @tparam SrcT  Source type.
 * @tparam DesT  Destination type.
 * @param src    Source value.
 * @param des    Destination value.
 * @return       @c true on success.
 */
template <typename SrcT, typename DesT>
static bool convert(const SrcT& src, DesT& des);

/**
 * @brief Dereferences a value, unwrapping @c shared_ptr if necessary.
 *
 * @details
 * If @c T is a @c shared_ptr<U>, returns @c *t; otherwise returns @c t.
 * Used internally so serialisation code handles both value and shared-ptr
 * message types uniformly.
 *
 * @tparam T  The input type (value or @c shared_ptr).
 * @param t   Input value.
 * @return    Reference to the underlying value.
 */
template <typename T>
[[nodiscard]] static constexpr auto& deref(const T& t) noexcept;

/**
 * @brief Returns @c true if @c T is exactly @c Bytes.
 *
 * @tparam T  Type to test.
 * @return    @c true for @c Bytes.
 */
template <typename T>
[[nodiscard]] static constexpr bool is_bytes_type() noexcept;

/**
 * @brief Returns @c true if @c T is a VLink dynamic data type.
 *
 * @details
 * Dynamic types have an @c is_vlink_dynamic_data() member.
 *
 * @tparam T  Type to test.
 * @return    @c true for dynamic data types.
 */
template <typename T>
[[nodiscard]] static constexpr bool is_dynamic_type() noexcept;

/**
 * @brief Returns @c true if @c T is a FastDDS CDR-serialisable type.
 *
 * @details
 * Requires @c fastcdr to be present and the type to have both
 * @c serialize(Cdr&) and @c deserialize(Cdr&) methods, or its name to
 * contain the @c VLINK_FASTDDS_IDL_PREFIX prefix.
 *
 * @tparam T  Type to test.
 * @return    @c true for CDR types when @c fastcdr is available.
 */
template <typename T>
[[nodiscard]] static constexpr bool is_cdr_type() noexcept;

/**
 * @brief Returns @c true if @c T is a Protobuf message value type.
 *
 * @details
 * Requires @c protobuf to be present and the type to have both
 * @c SerializeToArray() and @c ParseFromArray() methods.
 *
 * @tparam T  Type to test.
 * @return    @c true for Protobuf value types.
 */
template <typename T>
[[nodiscard]] static constexpr bool is_proto_type() noexcept;

/**
 * @brief Returns @c true if @c T is a raw pointer to a Protobuf message.
 *
 * @tparam T  Type to test (expected to be @c MyProto*).
 * @return    @c true for Protobuf pointer types.
 */
template <typename T>
[[nodiscard]] static constexpr bool is_proto_ptr_type() noexcept;

/**
 * @brief Returns @c true if @c T is a FlatBuffers NativeTable type.
 *
 * @details
 * Requires @c flatbuffers and the type (or its @c shared_ptr element)
 * to be derived from @c flatbuffers::NativeTable.
 *
 * @tparam T  Type to test.
 * @return    @c true for FlatBuffers NativeTable types.
 */
template <typename T>
[[nodiscard]] static constexpr bool is_flat_table_type() noexcept;

/**
 * @brief Returns @c true if @c T is a FlatBuffers builder type.
 *
 * @details
 * Requires @c flatbuffers and the type to have both a @c fbb_ member and
 * a @c Finish() method.
 *
 * @tparam T  Type to test.
 * @return    @c true for FlatBuffers builder types.
 */
template <typename T>
[[nodiscard]] static constexpr bool is_flat_builder_type() noexcept;

/**
 * @brief Returns @c true if @c T is a raw pointer to a @c flatbuffers::Table.
 *
 * @tparam T  Type to test (expected to be @c MyTable*).
 * @return    @c true for FlatBuffers Table pointer types.
 */
template <typename T>
[[nodiscard]] static constexpr bool is_flat_ptr_type() noexcept;

/**
 * @brief Returns @c true if @c T provides custom @c operator>> / @c operator<<.
 *
 * @details
 * Checks via @c Traits::Operatorable whether @c T supports @c operator>>(Bytes&)
 * and @c operator<<(const Bytes&).
 *
 * @tparam T  Type to test.
 * @return    @c true for custom-codec types.
 */
template <typename T>
[[nodiscard]] static constexpr bool is_custom_type() noexcept;

/**
 * @brief Returns @c true if @c T is @c std::string after unwrapping @c shared_ptr.
 *
 * @tparam T  Type to test.
 * @return    @c true for @c std::string and @c std::shared_ptr<std::string>.
 */
template <typename T>
[[nodiscard]] static constexpr bool is_string_type() noexcept;

/**
 * @brief Returns @c true if @c std::string is constructible from @c T.
 *
 * @details
 * Matches @c char*, @c const char*, and string literal types.
 *
 * @tparam T  Type to test.
 * @return    @c true for C-string types.
 */
template <typename T>
[[nodiscard]] static constexpr bool is_chars_type() noexcept;

/**
 * @brief Returns @c true if @c T supports both @c operator<< and @c operator>> with @c std::stringstream.
 *
 * @details
 * Detected via @c Traits::Operatorable<std::stringstream, T>(), which requires
 * both @c ss << t and @c ss >> t to be well-formed.  Non-pointer types that
 * support bidirectional streaming via @c std::stringstream (e.g. arithmetic
 * types or types with custom stream operator overloads) match.  Note that
 * higher-priority codecs (e.g. Proto, CDR) are checked first in
 * @c get_type_of(), so this function is only reached for types that do not
 * match any earlier codec.
 *
 * @tparam T  Type to test.
 * @return    @c true for stream-serialisable types.
 */
template <typename T>
[[nodiscard]] static constexpr bool is_stream_type() noexcept;

/**
 * @brief Returns @c true if @c T is a trivial standard-layout value type (POD).
 *
 * @details
 * Matches types where @c std::is_trivial_v and @c std::is_standard_layout_v
 * are both @c true and the type is not a pointer.  Such types are byte-copied
 * directly into/from a @c Bytes buffer of exactly @c sizeof(T) bytes.
 *
 * @tparam T  Type to test.
 * @return    @c true for POD value types.
 */
template <typename T>
[[nodiscard]] static constexpr bool is_standard_type() noexcept;

/**
 * @brief Returns @c true if @c T is a pointer to a trivial standard-layout type.
 *
 * @details
 * Matches @c T* where @c std::is_trivial_v<T> && @c std::is_standard_layout_v<T>.
 * The pointer is reinterpreted (not dereferenced and copied) for zero-copy usage.
 *
 * @tparam T  Pointer type to test.
 * @return    @c true for POD pointer types.
 */
template <typename T>
[[nodiscard]] static constexpr bool is_standard_ptr_type() noexcept;

}  // namespace Serializer

}  // namespace vlink

#include "./internal/serializer-inl.h"
