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
 * @file uuid.h
 * @brief Lightweight RFC 4122 UUID value type used across VLink.
 *
 * @details
 * Provides a value-typed @c Uuid class with the canonical RFC 4122 layout (128 bits,
 * big-endian byte order, variant + version bits) and the project-wide random-byte
 * primitive used by the proxy auth-token handshake.
 *
 * The class is default-constructible to a nil value, can be built from a
 * @c std::array or a raw 16-byte buffer, exposes byte-level access via @c bytes(),
 * canonical text conversion via @c to_string() / @c from_string(), and structural
 * validation via @c is_valid().
 *
 * Random v4 UUID generation pipeline: eight @c std::random_device samples are fed
 * into a @c std::seed_seq, then a @c std::mt19937 engine, then a
 * @c std::uniform_int_distribution<uint32_t> emits four bytes per draw.  Byte
 * extraction uses explicit shifts (no @c reinterpret_cast), so the output is
 * identical on little-endian and big-endian targets given the same seed sequence.
 * The generator is pseudo-random and is not a CSPRNG.
 *
 * @note Following the @c Bytes header layout, most non-trivial bodies live in
 *       @c uuid.cc.  @c constexpr value operations stay inline to preserve
 *       literal-type use at compile time.
 */

#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <iterator>
#include <optional>
#include <ostream>
#include <random>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

#include "./macros.h"

namespace vlink {

/**
 * @class Uuid
 * @brief Value-typed RFC 4122 UUID.
 *
 * @details
 * Stores 16 bytes in network (big-endian) order.  The class is trivially copyable and
 * comparable, and offers parse/serialise helpers as well as a v4 random-UUID generator
 * backed by a thread-local @c std::mt19937 engine.
 */
class VLINK_EXPORT Uuid final {
 public:
  /**
   * @brief Underlying byte type.
   */
  using value_type = uint8_t;

  /**
   * @brief UUID byte length (always 16 per RFC 4122).
   */
  static constexpr size_t kByteSize = 16U;

  /**
   * @brief Length of the canonical textual representation @c "xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx".
   */
  static constexpr size_t kStringSize = 36U;

  /**
   * @enum Variant
   * @brief UUID variant field (octet 8 high bits) per RFC 4122 section 4.1.1.
   */
  enum class Variant : uint8_t {
    kNcs = 0,        ///< NCS backward compatibility (bit pattern 0xxx).
    kRfc = 1,        ///< RFC 4122 / DCE 1.1 (bit pattern 10xx).
    kMicrosoft = 2,  ///< Microsoft GUID (bit pattern 110x).
    kReserved = 3,   ///< Reserved for future use (bit pattern 111x).
  };

  /**
   * @enum Version
   * @brief UUID version field (octet 6 high nibble) per RFC 4122 section 4.1.3.
   */
  enum class Version : uint8_t {
    kNone = 0,           ///< No version (nil UUID or invalid version bits).
    kTimeBased = 1,      ///< Time-based v1.
    kDceSecurity = 2,    ///< DCE Security v2.
    kNameBasedMd5 = 3,   ///< Name-based MD5 hashing v3.
    kRandomBased = 4,    ///< Random v4 (produced by @c generate_random()).
    kNameBasedSha1 = 5,  ///< Name-based SHA-1 hashing v5.
  };

  /**
   * @brief Default constructs a nil UUID (all 16 bytes zero).
   */
  constexpr Uuid() noexcept;

  /**
   * @brief Constructs from an explicit byte array.
   *
   * @param data  16-byte UUID payload in network order.
   */
  constexpr explicit Uuid(const std::array<value_type, kByteSize>& data) noexcept;

  /**
   * @brief Constructs from a raw C array reference of exactly 16 bytes.
   *
   * @param arr  Reference to a 16-byte array.
   */
  constexpr explicit Uuid(const value_type (&arr)[kByteSize]) noexcept;

  /**
   * @brief Constructs from a forward-iterator range of exactly 16 bytes.
   *
   * @details
   * If @c std::distance(first, last) is not 16 the UUID is left in the nil state.  A
   * @c static_assert enforces that @p ForwardIteratorT satisfies the forward-iterator
   * concept; passing an input-only iterator is a compile error.
   *
   * @tparam ForwardIteratorT  Forward iterator yielding @c uint8_t-convertible values.
   * @param first  Begin iterator.
   * @param last   End iterator.
   */
  template <typename ForwardIteratorT>
  Uuid(ForwardIteratorT first, ForwardIteratorT last);

  /**
   * @brief Returns the UUID variant field inferred from octet 8.
   *
   * @return UUID variant enum value.
   */
  [[nodiscard]] constexpr Variant variant() const noexcept;

  /**
   * @brief Returns the UUID version inferred from the high nibble of octet 6.
   *
   * @return Version enum value.
   */
  [[nodiscard]] constexpr Version version() const noexcept;

  /**
   * @brief Returns @c true when every byte of the UUID is zero (nil UUID).
   *
   * @return @c true when all 16 bytes are zero.
   */
  [[nodiscard]] constexpr bool is_nil() const noexcept;

  /**
   * @brief Direct read-only access to the underlying 16-byte payload.
   *
   * @return Const reference to the internal byte array.
   */
  [[nodiscard]] constexpr const std::array<value_type, kByteSize>& bytes() const noexcept;

  /**
   * @brief Formats the UUID as a lowercase 36-character canonical string with hyphens.
   *
   * @details
   * Output form: @c "xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx".
   *
   * @return 36-character lowercase string.
   */
  [[nodiscard]] std::string to_string() const noexcept;

  /**
   * @brief Returns the UUID as a 32-character lowercase hex string with no hyphens.
   *
   * @return 32-character lowercase string (compact form).
   */
  [[nodiscard]] std::string to_compact_string() const noexcept;

  /**
   * @brief Swaps contents with another @c Uuid (no-throw).
   *
   * @param other  UUID to swap with.
   */
  void swap(Uuid& other) noexcept;

  /**
   * @brief Validates whether @p str is a well-formed UUID textual representation.
   *
   * @details
   * Accepts the 36-character canonical form (@c "xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx"),
   * the 32-character compact form with no hyphens, and either form wrapped in an
   * outer brace pair (@c "{...}").  Hyphen positions are not enforced: any 32 hex
   * digits within the allowed shape pass.
   *
   * @param str  Candidate string.
   * @return @c true when @p str is a valid UUID representation.
   */
  [[nodiscard]] static bool is_valid(std::string_view str) noexcept;

  /**
   * @brief Null-safe C-string overload of @c is_valid().
   *
   * @details
   * Explicitly returns @c false when @p str is @c nullptr; non-null inputs are
   * converted via @c std::string_view(str) which stops at the first NUL byte.
   *
   * @param str  NUL-terminated candidate string, or @c nullptr.
   * @return @c true when @p str is non-null and a valid UUID representation.
   */
  [[nodiscard]] static bool is_valid(const char* str) noexcept;

  /**
   * @brief Parses a UUID string into a @c Uuid value.
   *
   * @details
   * Accepts the same shapes as @c is_valid() (36-character canonical, 32-character
   * compact, and either form wrapped in @c "{...}").  Returns @c std::nullopt for
   * any malformed input.
   *
   * @param str  Candidate string.
   * @return Parsed UUID, or @c std::nullopt on failure.
   */
  [[nodiscard]] static std::optional<Uuid> from_string(std::string_view str) noexcept;

  /**
   * @brief Null-safe C-string overload of @c from_string().
   *
   * @details
   * Returns @c std::nullopt when @p str is @c nullptr; otherwise forwards to the
   * @c std::string_view overload, which stops at the first embedded NUL byte.
   *
   * @param str  NUL-terminated candidate string, or @c nullptr.
   * @return Parsed UUID, or @c std::nullopt on malformed or null input.
   */
  [[nodiscard]] static std::optional<Uuid> from_string(const char* str) noexcept;

  /**
   * @brief Generates a random v4 UUID using a thread-local seeded engine.
   *
   * @details
   * The engine is lazily seeded on first use from eight @c std::random_device samples
   * through a @c std::seed_seq, then reused for the lifetime of the thread.  Sets the
   * RFC variant bits (10xxxxxx in octet 8) and the v4 version bits (0100xxxx in octet 6).
   *
   * @return Freshly generated v4 UUID.
   */
  [[nodiscard]] static Uuid generate_random() noexcept;

  /**
   * @brief Generates a random v4 UUID using a caller-supplied engine.
   *
   * @details
   * Useful for deterministic test fixtures.  Sets the RFC variant and v4 version bits
   * identically to the no-argument overload.
   *
   * @param engine  Caller-managed @c std::mt19937 instance.
   * @return Freshly generated v4 UUID.
   */
  [[nodiscard]] static Uuid generate_random(std::mt19937& engine) noexcept;

  /**
   * @brief Produces @p count random-device-seeded pseudo-random bytes.
   *
   * @details
   * Uses the same canonical pipeline as @c generate_random(): eight-word @c std::seed_seq
   * from @c std::random_device, @c std::mt19937 engine, @c std::uniform_int_distribution<uint32_t>
   * distribution emitting four bytes per draw.  Byte extraction is endian-deterministic.
   * Returns an empty vector when @p count is @c 0 or allocation fails.
   *
   * @warning The underlying @c std::mt19937 engine is @b not a cryptographic RNG:
   *          observing 624 consecutive 32-bit outputs from the same thread allows the
   *          attacker to reconstruct the engine state.  Do NOT use this for long-term
   *          cryptographic secrets; use a dedicated CSPRNG (e.g. OpenSSL @c RAND_bytes)
   *          for that purpose.
   *
   * @param count  Number of bytes to emit.
   * @return Vector containing exactly @p count pseudo-random bytes.
   */
  [[nodiscard]] static std::vector<value_type> random_bytes(size_t count) noexcept;

  /**
   * @brief Produces @p byte_count pseudo-random bytes encoded as a lowercase hex string.
   *
   * @details
   * The output is always @p byte_count * 2 characters long.  Defaults to 16 bytes
   * (a 128-bit token, 32 hex characters), matching the width used by the proxy
   * auth-token handshake.  Returns an empty string when @p byte_count is @c 0 or
   * allocation fails.
   *
   * @warning Same non-CSPRNG caveat as @c random_bytes() -- suitable for short-lived
   *          session identifiers, not long-term secrets.
   *
   * @param byte_count  Number of bytes worth of randomness to encode.
   * @return Lowercase hex string of length @p byte_count * 2.
   */
  [[nodiscard]] static std::string random_hex(size_t byte_count = 16U) noexcept;

  /**
   * @brief Equality comparison.
   *
   * @param lhs  Left UUID.
   * @param rhs  Right UUID.
   * @return @c true when the two payloads compare byte-equal.
   */
  friend bool operator==(const Uuid& lhs, const Uuid& rhs) noexcept;

  /**
   * @brief Inequality comparison.
   *
   * @param lhs  Left UUID.
   * @param rhs  Right UUID.
   * @return @c true when the two payloads differ in any byte.
   */
  friend bool operator!=(const Uuid& lhs, const Uuid& rhs) noexcept;

  /**
   * @brief Lexicographic ordering on the underlying bytes.
   *
   * @param lhs  Left UUID.
   * @param rhs  Right UUID.
   * @return @c true when @p lhs sorts strictly before @p rhs.
   */
  friend bool operator<(const Uuid& lhs, const Uuid& rhs) noexcept;

  /**
   * @brief Stream insertion using @c to_string().
   *
   * @param ostream  Output stream.
   * @param id       UUID to format.
   * @return Reference to @p ostream.
   */
  VLINK_EXPORT friend std::ostream& operator<<(std::ostream& ostream, const Uuid& id);

 private:
  std::array<value_type, kByteSize> data_{};
};

////////////////////////////////////////////////////////////////
/// Details
////////////////////////////////////////////////////////////////

template <typename ForwardIteratorT>
inline Uuid::Uuid(ForwardIteratorT first, ForwardIteratorT last) {
  static_assert(
      std::is_base_of_v<std::forward_iterator_tag, typename std::iterator_traits<ForwardIteratorT>::iterator_category>,
      "Uuid range constructor requires at least a forward iterator");

  if (std::distance(first, last) == static_cast<std::ptrdiff_t>(kByteSize)) {
    std::copy(first, last, data_.begin());
  }
}

inline constexpr Uuid::Uuid() noexcept = default;

inline constexpr Uuid::Uuid(const std::array<value_type, kByteSize>& data) noexcept : data_{data} {}

inline constexpr Uuid::Uuid(const value_type (&arr)[kByteSize]) noexcept {
  for (size_t i = 0U; i < kByteSize; ++i) {
    data_[i] = arr[i];
  }
}

inline constexpr Uuid::Variant Uuid::variant() const noexcept {
  const uint8_t octet = data_[8];

  if ((octet & 0x80U) == 0x00U) {
    return Variant::kNcs;
  }

  if ((octet & 0xC0U) == 0x80U) {
    return Variant::kRfc;
  }

  if ((octet & 0xE0U) == 0xC0U) {
    return Variant::kMicrosoft;
  }

  return Variant::kReserved;
}

inline constexpr Uuid::Version Uuid::version() const noexcept {
  switch (data_[6] & 0xF0U) {
    case 0x10U:
      return Version::kTimeBased;
    case 0x20U:
      return Version::kDceSecurity;
    case 0x30U:
      return Version::kNameBasedMd5;
    case 0x40U:
      return Version::kRandomBased;
    case 0x50U:
      return Version::kNameBasedSha1;
    default:
      return Version::kNone;
  }
}

inline constexpr bool Uuid::is_nil() const noexcept {
  for (uint8_t byte_value : data_) {
    if (byte_value != 0U) {
      return false;
    }
  }

  return true;
}

inline constexpr const std::array<Uuid::value_type, Uuid::kByteSize>& Uuid::bytes() const noexcept { return data_; }

inline void Uuid::swap(Uuid& other) noexcept { data_.swap(other.data_); }

inline bool operator==(const Uuid& lhs, const Uuid& rhs) noexcept { return lhs.data_ == rhs.data_; }

inline bool operator!=(const Uuid& lhs, const Uuid& rhs) noexcept { return lhs.data_ != rhs.data_; }

inline bool operator<(const Uuid& lhs, const Uuid& rhs) noexcept { return lhs.data_ < rhs.data_; }

}  // namespace vlink

namespace std {

/**
 * @brief Specialisation of @c std::hash so @c vlink::Uuid is usable in unordered containers.
 */
template <>
struct hash<vlink::Uuid> {
  size_t operator()(const vlink::Uuid& id) const noexcept {
    const auto& data = id.bytes();
    size_t result = 0U;

    for (uint8_t byte_value : data) {
      result = (result * 131U) + static_cast<size_t>(byte_value);
    }

    return result;
  }
};

}  // namespace std
