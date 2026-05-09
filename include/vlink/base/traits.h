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
 * @file traits.h
 * @brief Compile-time type-trait utilities used internally by VLink.
 *
 * @details
 * The @c vlink::Traits namespace collects a set of small, self-contained
 * template meta-programming helpers that are used throughout the VLink codebase
 * to detect type properties at compile time.  They follow the same conventions
 * as the C++ standard library @c <type_traits> header.
 *
 * All helpers are either:
 * - Struct templates inheriting from @c std::true_type / @c std::false_type, or
 * - @c constexpr functions returning @c bool.
 *
 * | Helper              | Description                                                           |
 * | ------------------- | --------------------------------------------------------------------- |
 * | EmptyType           | An empty tag type used as a placeholder                               |
 * | ExpectFalse         | Always evaluates to std::false_type (useful in static_assert)         |
 * | Callable            | Detects whether T is callable with no arguments                       |
 * | Assignable          | Detects whether OT is assignable from T                               |
 * | EqualityComparable  | Detects whether OT supports == with T                                 |
 * | GreaterComparable   | Detects whether OT supports < and > with T                            |
 * | Operatorable        | Detects whether OT supports << and >> stream operators with T         |
 * | IsAtomic            | Detects whether T is a std::atomic specialization                     |
 * | IsSharedPtr         | Detects whether T is a std::shared_ptr specialization                 |
 * | RemoveSharedPtr     | Strips the std::shared_ptr wrapper to obtain the element type         |
 * | has_member          | Runtime-style SFINAE check for a named member                         |
 * | is_non_char_ptr     | True when T decays to a non-char pointer                              |
 * | is_integer          | True for integer types excluding bool, char, and signed/unsigned char |
 * | is_floating         | True for floating-point types                                         |
 *
 * @note
 * These utilities live in the @c vlink::Traits namespace (note the capital T).
 * @c VLINK_HAS_MEMBER is the corresponding macro wrapper for has_member.
 *
 * @par Example: checking for a member at compile time
 * @code
 * struct Foo { void bar() {} };
 * struct Baz {};
 *
 * // Using the macro (preferred for member name checks):
 * static_assert(VLINK_HAS_MEMBER(Foo, bar));
 * static_assert(!VLINK_HAS_MEMBER(Baz, bar));
 * @endcode
 */

#pragma once

#include <atomic>
#include <memory>
#include <type_traits>

namespace vlink {

/**
 * @namespace vlink::Traits
 * @brief Collection of compile-time type-trait helpers for VLink.
 */
namespace Traits {  // NOLINT(readability-identifier-naming)

/**
 * @struct EmptyType
 * @brief An empty tag type used as a neutral placeholder in template meta-programming.
 *
 * @details
 * Used in places where a type parameter must be provided but carries no data,
 * for example as the default detail type in the Logger.
 */
struct EmptyType {};

/**
 * @struct ExpectFalse
 * @brief A type trait that always evaluates to @c std::false_type.
 *
 * @details
 * Useful inside @c static_assert to produce a dependent false, which
 * prevents the compiler from instantiating the primary template when
 * a specialization was expected.
 *
 * @tparam T  Any type (the value is always false regardless of T).
 */
template <typename T>
struct ExpectFalse : std::false_type {};

/**
 * @struct Callable
 * @brief Detects whether type @p T is callable with no arguments.
 *
 * @details
 * Inherits from @c std::true_type when @c T can be called as @c t() (i.e.,
 * a function pointer, lambda, functor, or @c std::function).
 *
 * @tparam T  The type to test.
 */
template <typename T, typename = void>
struct Callable : std::false_type {};

template <typename T>
struct Callable<T, std::void_t<decltype(std::declval<T>()())>> : std::true_type {};

/**
 * @struct Assignable
 * @brief Detects whether an object of type @p OT can be assigned a value of type @p T.
 *
 * @details
 * Inherits from @c std::true_type when the expression @c ot = t is well-formed.
 *
 * @tparam OT  The target (left-hand side) type.
 * @tparam T   The source (right-hand side) type.
 */
template <typename OT, typename T, typename = void>
struct Assignable : std::false_type {};

template <typename OT, typename T>
struct Assignable<OT, T, std::void_t<decltype(std::declval<OT&>() = std::declval<T>())>> : std::true_type {};

/**
 * @struct EqualityComparable
 * @brief Detects whether @p OT supports the @c == operator with @p T.
 *
 * @details
 * Inherits from @c std::true_type when @c ot == t is a well-formed expression.
 *
 * @tparam OT  The left-hand side type.
 * @tparam T   The right-hand side type.
 */
template <typename OT, typename T, typename = void>
struct EqualityComparable : std::false_type {};

template <typename OT, typename T>
struct EqualityComparable<OT, T, std::void_t<decltype(std::declval<OT>() == std::declval<T>())>> : std::true_type {};

/**
 * @struct GreaterComparable
 * @brief Detects whether @p OT supports both @c < and @c > operators with @p T.
 *
 * @details
 * Inherits from @c std::true_type when both @c ot < t and @c ot > t are
 * well-formed expressions.
 *
 * @tparam OT  The left-hand side type.
 * @tparam T   The right-hand side type.
 */
template <typename OT, typename T, typename = void>
struct GreaterComparable : ExpectFalse<OT> {};

template <typename OT, typename T>
struct GreaterComparable<
    OT, T,
    std::void_t<decltype(std::declval<OT&>() < std::declval<T>()), decltype(std::declval<OT&>() > std::declval<T&>())>>
    : std::true_type {};

/**
 * @struct Operatorable
 * @brief Detects whether @p OT supports @c << and @c >> stream operators with @p T.
 *
 * @details
 * Inherits from @c std::true_type when both @c ot << t and @c ot >> t are
 * well-formed expressions.  Primarily used to detect stream-compatible types.
 *
 * @tparam OT  The stream type.
 * @tparam T   The value type.
 */
template <typename OT, typename T, typename = void>
struct Operatorable : ExpectFalse<OT> {};

template <typename OT, typename T>
struct Operatorable<OT, T,
                    std::void_t<decltype(std::declval<OT&>() << std::declval<T>()),
                                decltype(std::declval<OT&>() >> std::declval<T&>())>> : std::true_type {};

/**
 * @struct IsAtomic
 * @brief Detects whether type @p T is a @c std::atomic specialization.
 *
 * @details
 * Inherits from @c std::true_type only for @c std::atomic<U> for any @c U.
 *
 * @tparam T  The type to test.
 */
template <typename T>
struct IsAtomic : std::false_type {};

template <typename T>
struct IsAtomic<std::atomic<T>> : std::true_type {};

/**
 * @struct IsSharedPtr
 * @brief Detects whether type @p T is a @c std::shared_ptr specialization.
 *
 * @details
 * Inherits from @c std::true_type when @p T is (or derives from)
 * @c std::shared_ptr<element_type>.
 *
 * @tparam T  The type to test.
 */
template <typename T, typename = void>
struct IsSharedPtr : std::false_type {};

template <typename T>
struct IsSharedPtr<T, std::void_t<typename T::element_type>>
    : std::is_base_of<std::shared_ptr<typename T::element_type>, T> {};

/**
 * @struct RemoveSharedPtr
 * @brief Strips @c std::shared_ptr to obtain the underlying element type.
 *
 * @details
 * When @p T is a @c std::shared_ptr, @c RemoveSharedPtr<T>::Type is the
 * contained element type.  Otherwise @c Type is @p T itself.
 *
 * @tparam T  The type from which to remove the shared_ptr wrapper.
 */
template <typename T, bool = IsSharedPtr<T>::value>
struct RemoveSharedPtr {
  using Type = T; /**< The type without std::shared_ptr. */
};

template <typename T>
struct RemoveSharedPtr<T, true> {
  using Type = typename T::element_type; /**< The type without std::shared_ptr. */
};

/**
 * @brief Checks whether type @p T has a member expression accessible via @p f.
 *
 * @details
 * This function is used as a building block for @c VLINK_HAS_MEMBER.  It returns
 * @c true when invoking the lambda @p f with a default-constructed @c T succeeds
 * (i.e., the member is accessible at compile time).
 *
 * @tparam T  The type to probe.
 * @tparam F  A callable that takes a @c T and accesses the member of interest.
 * @param  f  A lambda of the form @c [](auto&& obj) -> decltype(obj.member) { return 0; }
 * @return @c true if the member is accessible, @c false otherwise.
 *
 * @note Prefer the @c VLINK_HAS_MEMBER macro for member-name checks.
 */
template <typename T, typename F>
[[nodiscard]] static constexpr auto has_member(F&& f) noexcept -> decltype(f(std::declval<T>()), true) {
  return true;
}

template <typename>
// NOLINTNEXTLINE(modernize-avoid-variadic-functions)
[[nodiscard]] static constexpr auto has_member(...) noexcept {
  return false;
}

/**
 * @brief Returns @c true when @p T decays to a pointer type other than @c char*.
 *
 * @details
 * Used inside VLink serialisation to distinguish raw data pointers from C-strings.
 *
 * @tparam T  The type to test.
 * @return @c true for non-char pointer types.
 */
template <typename T>
[[nodiscard]] static constexpr auto is_non_char_ptr() noexcept {
  return std::is_pointer_v<std::decay_t<T>> &&
         !std::is_same_v<std::remove_cv_t<std::remove_pointer_t<std::decay_t<T>>>, char>;
}

/**
 * @brief Returns @c true when @p T is an integer type excluding @c bool and
 *        all @c char variants.
 *
 * @details
 * Matches @c short, @c int, @c long, @c long long, and their unsigned
 * counterparts.  Explicitly excludes @c bool, @c char, @c signed char,
 * and @c unsigned char.
 *
 * @tparam T  The type to test.
 * @return @c true for plain integer types.
 */
template <typename T>
[[nodiscard]] static constexpr bool is_integer() {
  using U = std::remove_cv_t<T>;
  return std::is_integral_v<U> && !std::is_same_v<U, bool> && !std::is_same_v<U, char> &&
         !std::is_same_v<U, signed char> && !std::is_same_v<U, unsigned char>;
}

/**
 * @brief Returns @c true when @p T is a floating-point type.
 *
 * @details
 * Matches @c float, @c double, and @c long double (with CV qualifiers stripped).
 *
 * @tparam T  The type to test.
 * @return @c true for floating-point types.
 */
template <typename T>
[[nodiscard]] static constexpr bool is_floating() {
  return std::is_floating_point_v<std::remove_cv_t<T>>;
}

}  // namespace Traits

}  // namespace vlink

////////////////////////////////////////////////////////////////
/// Macro Definitions
////////////////////////////////////////////////////////////////

/**
 * @def VLINK_HAS_MEMBER(T, member)
 * @brief Checks at compile time whether type @p T has an accessible member named @p member.
 *
 * @details
 * Expands to a @c constexpr boolean expression (true/false) evaluated at compile time.
 * Internally delegates to @c vlink::Traits::has_member.
 *
 * @param T       The type to inspect.
 * @param member  The unquoted member name to look for.
 *
 * @par Example
 * @code
 * struct Foo { int bar; };
 * static_assert(VLINK_HAS_MEMBER(Foo, bar));
 * static_assert(!VLINK_HAS_MEMBER(Foo, baz));
 * @endcode
 */
#define VLINK_HAS_MEMBER(T, member) \
  vlink::Traits::has_member<T>([](auto&& obj) -> decltype((void)(obj.member), 0) { return 0; })
