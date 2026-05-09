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
 * @file uint128.h
 * @brief Portable 128-bit unsigned integer with full arithmetic, bitwise, and comparison operators.
 *
 * @details
 * @c Uint128 represents a 128-bit unsigned value as two @c uint64_t halves (@c high_ and
 * @c low_).  On platforms that provide @c __uint128_t (GCC/Clang with 64-bit targets),
 * multiplication is delegated to the compiler's native type for maximum performance.  On
 * other platforms a portable fallback algorithm (@c mul_u128_fallback) is used.
 *
 * Supported operations:
 *
 * | Category      | Operators                                     | Notes                          |
 * | ------------- | --------------------------------------------- | ------------------------------ |
 * | Arithmetic    | +  -  *  /  %  +=  -=  *=  /=  %=             | /  and % throw on divisor zero |
 * | Bitwise       | |  &  ^  ~  <<  >>  |=  &=  ^=  <<=  >>=      | Shifts clamped to [0, 127]     |
 * | Comparison    | ==  !=  <  >  <=  >=                          | Lexicographic (high then low)  |
 * | Increment     | ++  --  (prefix and postfix)                  | 128-bit carry propagated       |
 * | Stream        | operator<<(ostream)                           | Hexadecimal output             |
 *
 * @par Implicit conversion from integral types
 * A non-explicit single-argument constructor accepts any integral type @c T:
 * - If @c T is @c __uint128_t (when available), high and low halves are extracted.
 * - Otherwise the value is zero-extended into the low 64 bits.
 *
 * @par Conversion to __uint128_t
 * On platforms that support it, an explicit @c operator __uint128_t() is provided for
 * interoperability with compiler-native 128-bit arithmetic.
 *
 * @par std::hash specialisation
 * A @c std::hash<vlink::Uint128> specialisation is defined at the bottom of this file,
 * enabling @c Uint128 to be used as a key in @c std::unordered_map / @c std::unordered_set.
 *
 * @par Type alias
 * @code
 * using uint128_t = Uint128;
 * @endcode
 *
 * @par Example
 * @code
 * vlink::Uint128 a(0, UINT64_MAX);      // 0xFFFFFFFFFFFFFFFF
 * vlink::Uint128 b(1, 0);               // 0x10000000000000000
 * vlink::Uint128 c = a + vlink::Uint128(0, 1);  // carry propagates to high word
 * assert(c == b);
 *
 * // Hash map usage:
 * std::unordered_map<vlink::uint128_t, std::string> map;
 * map[vlink::Uint128(0xDEAD, 0xBEEF)] = "key";
 * @endcode
 */

#pragma once

#include <cstdint>
#include <iostream>
#include <type_traits>
#include <utility>

#include "./macros.h"

namespace vlink {

/**
 * @class Uint128
 * @brief 128-bit unsigned integer stored as two 64-bit halves with full operator support.
 *
 * @details
 * Stores the value as @c (high_ << 64) | low_.  All arithmetic correctly handles
 * carry/borrow across the 64-bit boundary.  Division and modulo are implemented using
 * a portable binary-long-division algorithm (@c u128_divmod).
 */
class Uint128 final {
 public:
  /**
   * @brief Default-constructs a zero-valued @c Uint128.
   */
  Uint128() noexcept = default;

  /**
   * @brief Constructs a @c Uint128 from an integral-like type @p T (implicit conversion).
   *
   * @details
   * - If @c T is @c __uint128_t (available on GCC/Clang 64-bit), splits into
   *   high and low halves.
   * - Else if @c T is signed and @c uint64_t is constructible from @c T,
   *   sign-extends @p v: negative values yield @c high_ @c = @c ~uint64_t{0}
   *   and @c low_ @c = the two's-complement bit pattern of @p v, so e.g.
   *   @c Uint128(int64_t{-1}) equals @c (~uint64_t{0}, @c ~uint64_t{0})
   *   matching @c __uint128_t(int64_t{-1}).
   * - Else if @c uint64_t is constructible from @c T (unsigned case),
   *   zero-extends @p v into @c low_ and sets @c high_ to @c 0.
   * - Otherwise both halves keep their default-member-initialized value of
   *   @c 0 — there is no diagnostic for non-integral @p T because the
   *   integrality assertion is intentionally not enforced at this layer.
   *
   * @tparam T  Source type.  Expected to be integral or @c __uint128_t; other
   *            types silently produce a zero-valued @c Uint128.
   * @param v   Source value.
   *
   * @note This constructor is intentionally non-explicit to allow natural integral literals.
   */
  template <typename T>
  constexpr Uint128(T v) noexcept;  // NOLINT(google-explicit-constructor)

  /**
   * @brief Constructs a @c Uint128 from explicit high and low 64-bit halves.
   *
   * @param high  Upper 64 bits.
   * @param low   Lower 64 bits.
   */
  explicit Uint128(uint64_t high, uint64_t low) noexcept;

#if defined(__SIZEOF_INT128__)
  /**
   * @brief Converts to the compiler-native @c __uint128_t type.
   *
   * @details
   * Only available on platforms that provide @c __uint128_t (GCC/Clang, 64-bit targets).
   * Reconstructed as @c (high_ << 64) | low_.
   *
   * @return Native 128-bit value.
   */
  explicit operator __uint128_t() const noexcept;
#endif

  /**
   * @brief Returns the sum of @c *this and @p other.
   *
   * @param other  Right-hand operand.
   * @return New @c Uint128 equal to @c *this + @p other.
   */
  Uint128 operator+(const Uint128& other) const noexcept;

  /**
   * @brief Returns the difference of @c *this and @p other.
   *
   * @param other  Right-hand operand.
   * @return New @c Uint128 equal to @c *this - @p other (wraps on underflow).
   */
  Uint128 operator-(const Uint128& other) const noexcept;

  /**
   * @brief Returns the product of @c *this and @p other.
   *
   * @details
   * Uses @c __uint128_t multiplication when available; falls back to
   * @c mul_u128_fallback otherwise.
   *
   * @param other  Right-hand operand.
   * @return New @c Uint128 equal to @c *this * @p other (low 128 bits of true product).
   */
  Uint128 operator*(const Uint128& other) const noexcept;

  /**
   * @brief Returns the quotient of @c *this divided by @p other.
   *
   * @param other  Divisor.
   * @return New @c Uint128 equal to @c *this / @p other.
   *
   * @throws std::domain_error if @p other is zero.
   */
  Uint128 operator/(const Uint128& other) const;

  /**
   * @brief Returns the remainder of @c *this divided by @p other.
   *
   * @param other  Divisor.
   * @return New @c Uint128 equal to @c *this % @p other.
   *
   * @throws std::domain_error if @p other is zero.
   */
  Uint128 operator%(const Uint128& other) const;

  /**
   * @brief Adds @p other to @c *this in-place with carry propagation.
   *
   * @param other  Right-hand operand.
   * @return Reference to @c *this.
   */
  Uint128& operator+=(const Uint128& other) noexcept;

  /**
   * @brief Subtracts @p other from @c *this in-place with borrow propagation.
   *
   * @param other  Right-hand operand.
   * @return Reference to @c *this.
   */
  Uint128& operator-=(const Uint128& other) noexcept;

  /**
   * @brief Multiplies @c *this by @p other in-place.
   *
   * @param other  Right-hand operand.
   * @return Reference to @c *this.
   */
  Uint128& operator*=(const Uint128& other) noexcept;

  /**
   * @brief Divides @c *this by @p other in-place.
   *
   * @param other  Divisor.
   * @return Reference to @c *this.
   *
   * @throws std::domain_error if @p other is zero.
   */
  Uint128& operator/=(const Uint128& other);

  /**
   * @brief Computes @c *this modulo @p other in-place.
   *
   * @param other  Divisor.
   * @return Reference to @c *this.
   *
   * @throws std::domain_error if @p other is zero.
   */
  Uint128& operator%=(const Uint128& other);

  /**
   * @brief Returns the bitwise OR of @c *this and @p other.
   *
   * @param other  Right-hand operand.
   * @return New @c Uint128 with each bit set if it is set in either operand.
   */
  Uint128 operator|(const Uint128& other) const noexcept;

  /**
   * @brief Returns the bitwise AND of @c *this and @p other.
   *
   * @param other  Right-hand operand.
   * @return New @c Uint128 with each bit set only if set in both operands.
   */
  Uint128 operator&(const Uint128& other) const noexcept;

  /**
   * @brief Returns the bitwise XOR of @c *this and @p other.
   *
   * @param other  Right-hand operand.
   * @return New @c Uint128 with each bit set if it differs between operands.
   */
  Uint128 operator^(const Uint128& other) const noexcept;

  /**
   * @brief Returns the bitwise NOT (complement) of @c *this.
   *
   * @return New @c Uint128 with all 128 bits inverted.
   */
  Uint128 operator~() const noexcept;

  /**
   * @brief Returns @c *this shifted left by @p shift bits.
   *
   * @details
   * - Shift <= 0: returns @c *this unchanged.
   * - Shift >= 128: returns zero.
   * - Shift in [64, 127]: low bits are shifted into the high word.
   *
   * @param shift  Number of bit positions to shift left.
   * @return Shifted value.
   */
  Uint128 operator<<(int shift) const noexcept;

  /**
   * @brief Returns @c *this shifted right by @p shift bits (logical, zero-fill).
   *
   * @details
   * - Shift <= 0: returns @c *this unchanged.
   * - Shift >= 128: returns zero.
   * - Shift in [64, 127]: high bits are shifted into the low word.
   *
   * @param shift  Number of bit positions to shift right.
   * @return Shifted value.
   */
  Uint128 operator>>(int shift) const noexcept;

  /**
   * @brief Applies bitwise OR with @p other in-place.
   *
   * @param other  Right-hand operand.
   * @return Reference to @c *this.
   */
  Uint128& operator|=(const Uint128& other) noexcept;

  /**
   * @brief Applies bitwise AND with @p other in-place.
   *
   * @param other  Right-hand operand.
   * @return Reference to @c *this.
   */
  Uint128& operator&=(const Uint128& other) noexcept;

  /**
   * @brief Applies bitwise XOR with @p other in-place.
   *
   * @param other  Right-hand operand.
   * @return Reference to @c *this.
   */
  Uint128& operator^=(const Uint128& other) noexcept;

  /**
   * @brief Shifts @c *this left by @p shift bits in-place.
   *
   * @param shift  Number of bit positions to shift left.
   * @return Reference to @c *this.
   */
  Uint128& operator<<=(int shift) noexcept;

  /**
   * @brief Shifts @c *this right by @p shift bits in-place (logical, zero-fill).
   *
   * @param shift  Number of bit positions to shift right.
   * @return Reference to @c *this.
   */
  Uint128& operator>>=(int shift) noexcept;

  /**
   * @brief Returns @c true if @c *this equals @p other.
   *
   * @param other  Right-hand operand.
   * @return @c true if high and low words are both equal.
   */
  [[nodiscard]] bool operator==(const Uint128& other) const noexcept;

  /**
   * @brief Returns @c true if @c *this does not equal @p other.
   *
   * @param other  Right-hand operand.
   * @return @c true if any word differs.
   */
  [[nodiscard]] bool operator!=(const Uint128& other) const noexcept;

  /**
   * @brief Returns @c true if @c *this is less than @p other.
   *
   * @details
   * Compares the high word first; if equal, compares the low word.
   *
   * @param other  Right-hand operand.
   * @return @c true if @c *this < @p other.
   */
  [[nodiscard]] bool operator<(const Uint128& other) const noexcept;

  /**
   * @brief Returns @c true if @c *this is greater than @p other.
   *
   * @param other  Right-hand operand.
   * @return @c true if @c *this > @p other.
   */
  [[nodiscard]] bool operator>(const Uint128& other) const noexcept;

  /**
   * @brief Returns @c true if @c *this is less than or equal to @p other.
   *
   * @param other  Right-hand operand.
   * @return @c true if @c *this <= @p other.
   */
  [[nodiscard]] bool operator<=(const Uint128& other) const noexcept;

  /**
   * @brief Returns @c true if @c *this is greater than or equal to @p other.
   *
   * @param other  Right-hand operand.
   * @return @c true if @c *this >= @p other.
   */
  [[nodiscard]] bool operator>=(const Uint128& other) const noexcept;

  /**
   * @brief Pre-increment: increments the value by one and returns @c *this.
   *
   * @details
   * Carry from the low word is propagated to the high word.
   *
   * @return Reference to the incremented value.
   */
  Uint128& operator++() noexcept;

  /**
   * @brief Post-increment: increments the value by one and returns the previous value.
   *
   * @return Copy of the value before incrementing.
   */
  Uint128 operator++(int) noexcept;

  /**
   * @brief Pre-decrement: decrements the value by one and returns @c *this.
   *
   * @details
   * Borrow from the low word underflow is propagated to the high word.
   *
   * @return Reference to the decremented value.
   */
  Uint128& operator--() noexcept;

  /**
   * @brief Post-decrement: decrements the value by one and returns the previous value.
   *
   * @return Copy of the value before decrementing.
   */
  Uint128 operator--(int) noexcept;

  /**
   * @brief Returns the upper 64 bits of the 128-bit value.
   *
   * @return High 64-bit word.
   */
  [[nodiscard]] uint64_t get_high() const noexcept;

  /**
   * @brief Returns the lower 64 bits of the 128-bit value.
   *
   * @return Low 64-bit word.
   */
  [[nodiscard]] uint64_t get_low() const noexcept;

  /**
   * @brief Writes the hexadecimal string representation of the value to @p os.
   *
   * @param os     Output stream.
   * @param value  Value to print.
   * @return Reference to @p os.
   */
  VLINK_EXPORT friend std::ostream& operator<<(std::ostream& os, const Uint128& value) noexcept;

 private:
  VLINK_EXPORT static int clz64(uint64_t x) noexcept;

  VLINK_EXPORT static void mul_64_128(uint64_t a, uint64_t b, uint64_t& hi, uint64_t& lo) noexcept;

  VLINK_EXPORT static uint64_t add64_carry(uint64_t a, uint64_t b, uint64_t& carry_out) noexcept;

  VLINK_EXPORT static void add_128_with64(uint64_t& high, uint64_t& low, uint64_t add_low,
                                          uint64_t add_high = 0) noexcept;

  VLINK_EXPORT static Uint128 mul_u128_fallback(const Uint128& x, const Uint128& y) noexcept;

  VLINK_EXPORT static std::pair<Uint128, Uint128> u128_divmod(const Uint128& dividend, const Uint128& divisor);

  uint64_t high_{0};
  uint64_t low_{0};
};

using uint128_t = Uint128;

////////////////////////////////////////////////////////////////
/// Details
////////////////////////////////////////////////////////////////

inline Uint128::Uint128(uint64_t high, uint64_t low) noexcept : high_(high), low_(low) {}

#if defined(__SIZEOF_INT128__)
inline Uint128::operator __uint128_t() const noexcept { return (static_cast<__uint128_t>(high_) << 64) | low_; }
#endif

inline Uint128 Uint128::operator+(const Uint128& other) const noexcept {
  Uint128 r = *this;
  r += other;

  return r;
}

inline Uint128 Uint128::operator-(const Uint128& other) const noexcept {
  Uint128 r = *this;
  r -= other;

  return r;
}

inline Uint128 Uint128::operator*(const Uint128& other) const noexcept {
  Uint128 r = *this;
  r *= other;

  return r;
}

inline Uint128 Uint128::operator/(const Uint128& other) const { return u128_divmod(*this, other).first; }

inline Uint128 Uint128::operator%(const Uint128& other) const { return u128_divmod(*this, other).second; }

inline Uint128& Uint128::operator+=(const Uint128& other) noexcept {
  uint64_t old_low = low_;

  low_ += other.low_;

  uint64_t carry = (low_ < old_low) ? 1 : 0;

  high_ += other.high_ + carry;

  return *this;
}

inline Uint128& Uint128::operator-=(const Uint128& other) noexcept {
  uint64_t borrow = (low_ < other.low_) ? 1 : 0;

  low_ -= other.low_;
  high_ = high_ - other.high_ - borrow;

  return *this;
}

inline Uint128& Uint128::operator*=(const Uint128& other) noexcept {
#if defined(__SIZEOF_INT128__)
  __uint128_t lhs = (static_cast<__uint128_t>(high_) << 64) | low_;
  __uint128_t rhs = (static_cast<__uint128_t>(other.high_) << 64) | other.low_;
  __uint128_t res = lhs * rhs;

  high_ = static_cast<uint64_t>(res >> 64);
  low_ = static_cast<uint64_t>(res);
#else
  Uint128 r = mul_u128_fallback(*this, other);

  high_ = r.high_;
  low_ = r.low_;
#endif
  return *this;
}

inline Uint128& Uint128::operator/=(const Uint128& other) {
  auto qr = u128_divmod(*this, other);

  high_ = qr.first.high_;
  low_ = qr.first.low_;

  return *this;
}

inline Uint128& Uint128::operator%=(const Uint128& other) {
  auto qr = u128_divmod(*this, other);

  high_ = qr.second.high_;
  low_ = qr.second.low_;

  return *this;
}

inline Uint128 Uint128::operator|(const Uint128& other) const noexcept {
  return Uint128(high_ | other.high_, low_ | other.low_);
}

inline Uint128 Uint128::operator&(const Uint128& other) const noexcept {
  return Uint128(high_ & other.high_, low_ & other.low_);
}

inline Uint128 Uint128::operator^(const Uint128& other) const noexcept {
  return Uint128(high_ ^ other.high_, low_ ^ other.low_);
}

inline Uint128 Uint128::operator~() const noexcept { return Uint128(~high_, ~low_); }

inline Uint128 Uint128::operator<<(int shift) const noexcept {
  if (shift <= 0) {
    return *this;
  }

  if (shift >= 128) {
    return Uint128(0, 0);
  }

  if (shift >= 64) {
    return Uint128(low_ << (shift - 64), 0);
  }

  uint64_t new_high = (high_ << shift) | (low_ >> (64 - shift));
  uint64_t new_low = low_ << shift;

  return Uint128(new_high, new_low);
}

inline Uint128 Uint128::operator>>(int shift) const noexcept {
  if (shift <= 0) {
    return *this;
  }

  if (shift >= 128) {
    return Uint128(0, 0);
  }

  if (shift >= 64) {
    return Uint128(0, high_ >> (shift - 64));
  }

  uint64_t new_low = (low_ >> shift) | (high_ << (64 - shift));
  uint64_t new_high = high_ >> shift;

  return Uint128(new_high, new_low);
}

inline Uint128& Uint128::operator|=(const Uint128& other) noexcept {
  high_ |= other.high_;
  low_ |= other.low_;

  return *this;
}

inline Uint128& Uint128::operator&=(const Uint128& other) noexcept {
  high_ &= other.high_;
  low_ &= other.low_;

  return *this;
}

inline Uint128& Uint128::operator^=(const Uint128& other) noexcept {
  high_ ^= other.high_;
  low_ ^= other.low_;

  return *this;
}

inline Uint128& Uint128::operator<<=(int shift) noexcept {
  if (shift <= 0) {
    return *this;
  }

  if (shift >= 128) {
    high_ = 0;
    low_ = 0;

    return *this;
  }

  if (shift >= 64) {
    high_ = (low_ << (shift - 64));
    low_ = 0;
  } else {
    uint64_t nh = (high_ << shift) | (low_ >> (64 - shift));
    uint64_t nl = low_ << shift;

    high_ = nh;
    low_ = nl;
  }

  return *this;
}

inline Uint128& Uint128::operator>>=(int shift) noexcept {
  if (shift <= 0) {
    return *this;
  }

  if (shift >= 128) {
    high_ = 0;
    low_ = 0;
    return *this;
  }

  if (shift >= 64) {
    low_ = (high_ >> (shift - 64));
    high_ = 0;
  } else {
    uint64_t nl = (low_ >> shift) | (high_ << (64 - shift));
    uint64_t nh = high_ >> shift;

    high_ = nh;
    low_ = nl;
  }

  return *this;
}

inline bool Uint128::operator==(const Uint128& other) const noexcept {
  return high_ == other.high_ && low_ == other.low_;
}

inline bool Uint128::operator!=(const Uint128& other) const noexcept { return !(*this == other); }

inline bool Uint128::operator<(const Uint128& other) const noexcept {
  return (high_ < other.high_) || (high_ == other.high_ && low_ < other.low_);
}

inline bool Uint128::operator>(const Uint128& other) const noexcept { return other < *this; }

inline bool Uint128::operator<=(const Uint128& other) const noexcept { return !(other < *this); }

inline bool Uint128::operator>=(const Uint128& other) const noexcept { return !(*this < other); }

inline Uint128& Uint128::operator++() noexcept {
  uint64_t old_low = low_;

  low_ += 1;

  if (low_ < old_low) {
    ++high_;
  }

  return *this;
}

inline Uint128 Uint128::operator++(int) noexcept {
  Uint128 tmp = *this;

  ++(*this);

  return tmp;
}

inline Uint128& Uint128::operator--() noexcept {
  uint64_t old_low = low_;
  low_ -= 1;

  if (old_low == 0) {
    --high_;
  }

  return *this;
}

inline Uint128 Uint128::operator--(int) noexcept {
  Uint128 tmp = *this;

  --(*this);

  return tmp;
}

inline uint64_t Uint128::get_high() const noexcept { return high_; }

inline uint64_t Uint128::get_low() const noexcept { return low_; }

template <typename T>
inline constexpr Uint128::Uint128(T v) noexcept {
  // static_assert(std::is_integral_v<T>, "Uint128(T): T must be an integral type");

#if defined(__SIZEOF_INT128__)
  if constexpr (std::is_same_v<__uint128_t, T>) {
    high_ = static_cast<uint64_t>(v >> 64);
    low_ = static_cast<uint64_t>(v);

    return;
  }
#endif

  if constexpr (std::is_constructible_v<uint64_t, T>) {
    if constexpr (std::is_signed_v<T>) {
      high_ = (v < 0) ? ~uint64_t{0} : uint64_t{0};
      low_ = static_cast<uint64_t>(static_cast<int64_t>(v));
    } else {
      high_ = 0;
      low_ = v;
    }
  }
}

}  // namespace vlink

namespace std {

/**
 * @brief @c std::hash specialisation for @c vlink::Uint128.
 *
 * @details
 * Enables @c vlink::Uint128 (and the @c vlink::uint128_t alias) to be used as a key in
 * @c std::unordered_map, @c std::unordered_set, and similar hash-based containers.
 *
 * The hash function combines the high and low 64-bit words to produce a @c size_t result.
 */
template <>
struct hash<vlink::Uint128> {
  /**
   * @brief Computes the hash of @p value.
   *
   * @param value  The 128-bit value to hash.
   * @return Hash value derived from both the high and low 64-bit words.
   */
  VLINK_EXPORT size_t operator()(const vlink::Uint128& value) const noexcept;
};

}  // namespace std
