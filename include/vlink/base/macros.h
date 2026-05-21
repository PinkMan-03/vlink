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
 * @file macros.h
 * @brief Platform-independent macro definitions for the VLink library.
 *
 * @details
 * This header provides a unified set of macros used throughout VLink to handle
 * platform differences, visibility control, branch prediction hints, and common
 * class-level utilities such as singleton enforcement and copy-prevention.
 *
 * The macros are grouped as follows:
 *
 * | Group              | Macros                                         | Purpose                            |
 * | ------------------ | ---------------------------------------------- | ---------------------------------- |
 * | Symbol visibility  | VLINK_EXPORT, VLINK_EXPORT_AND_ALIGNED         | DLL export / visibility control    |
 * | Branch hints       | VLINK_LIKELY, VLINK_UNLIKELY, VLIKELY, ...     | Branch prediction optimization     |
 * | Copy prevention    | VLINK_DISALLOW_COPY_AND_ASSIGN                 | Disable copy constructor/operator  |
 * | Singleton guard    | VLINK_SINGLETON_CHECK, VLINK_SINGLETON_DECLARE | Enforce single-instance classes    |
 * | String utilities   | VLINK_MACRO_STRING_IFY, VLINK_MACRO_STRING_GET | Stringify macro tokens             |
 * | Constant check     | VLINK_ASSERT_CONSTANT                          | Static-assert constant string args |
 *
 * @note
 * - All macros are designed to be no-ops when the relevant feature is unavailable
 *   (e.g., VLINK_LIKELY falls back to a plain cast on platforms without __builtin_expect).
 * - The VLINK_EXPORT macro must be included before any class or function declaration
 *   that needs to be part of the public ABI.
 * - VLINK_SINGLETON_CHECK enforces that a class can only ever be instantiated once
 *   per process via a static atomic flag; any second instantiation will throw
 *   std::runtime_error.
 *
 * @par Example
 * @code
 * // Marking a class as exported:
 * class VLINK_EXPORT MyClass final { ... };
 *
 * // Preventing copy and assignment:
 * class MyClass {
 *   VLINK_DISALLOW_COPY_AND_ASSIGN(MyClass)
 * };
 *
 * // Branch prediction hint:
 * if VLIKELY(ptr != nullptr) {
 *   ptr->do_something();
 * }
 * @endcode
 */

#pragma once

// export
#undef VLINK_EXPORT
#ifdef VLINK_LIBRARY_STATIC
#define VLINK_EXPORT
#elif defined(_WIN32) || defined(__CYGWIN__)
#ifdef VLINK_LIBRARY
/// @cond INTERNAL
#define VLINK_EXPORT __declspec(dllexport)
#else
#define VLINK_EXPORT __declspec(dllimport)
/// @endcond
#endif
#else
#define VLINK_EXPORT __attribute__((visibility("default")))
#endif

// align
#undef VLINK_EXPORT_AND_ALIGNED
#ifdef _MSC_VER
#define VLINK_EXPORT_AND_ALIGNED(align_num) VLINK_EXPORT __declspec(align(align_num))
#else
/**
 * @def VLINK_EXPORT_AND_ALIGNED(align_num)
 * @brief Marks a class or variable as both exported and aligned to the given byte boundary.
 *
 * @details
 * On MSVC this expands to @c __declspec(align(align_num));
 * on GCC/Clang it expands to @c __attribute__((aligned(align_num))).
 *
 * @param align_num  The required alignment in bytes (must be a power of two).
 */
#define VLINK_EXPORT_AND_ALIGNED(align_num) VLINK_EXPORT __attribute__((aligned(align_num)))
#endif

// win32
#if defined(_WIN32) && !defined(WIN32_LEAN_AND_MEAN)
#define WIN32_LEAN_AND_MEAN
#endif

// msvc
#ifdef _MSC_VER
#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS  // NOLINT(bugprone-reserved-identifier, readability-identifier-naming)
#endif
#pragma warning(disable : 4065)
#pragma warning(disable : 4251)
#pragma warning(disable : 4244)
#pragma warning(disable : 4267)
#pragma warning(disable : 4324)
#pragma warning(disable : 4702)
#pragma warning(disable : 4819)
#pragma warning(disable : 4840)
#pragma warning(disable : 4996)
#endif

// __has_builtin
#ifndef __has_builtin
#define __has_builtin(x) 0
#endif

#ifdef __cplusplus
#if __cplusplus >= 202002L
/**
 * @def VLINK_LIKELY(x)
 * @brief Hints to the compiler that expression @p x is very likely to be true.
 *
 * @details
 * Under C++20 this expands to the @c [[likely]] attribute; under earlier standards
 * on GCC/Clang it uses @c __builtin_expect; on MSVC it is a no-op.
 *
 * @param x  A boolean expression.
 */
#define VLINK_LIKELY(x) (x) [[likely]]
/**
 * @def VLINK_UNLIKELY(x)
 * @brief Hints to the compiler that expression @p x is very unlikely to be true.
 *
 * @details
 * Under C++20 this expands to the @c [[unlikely]] attribute; under earlier standards
 * on GCC/Clang it uses @c __builtin_expect; on MSVC it is a no-op.
 *
 * @param x  A boolean expression.
 */
#define VLINK_UNLIKELY(x) (x) [[unlikely]]
#else
#ifdef _WIN32
#define VLINK_LIKELY(x) (x)
#define VLINK_UNLIKELY(x) (x)
#else
#define VLINK_LIKELY(x) (__builtin_expect(!!(x), 1))
#define VLINK_UNLIKELY(x) (__builtin_expect(!!(x), 0))
#endif
#endif
#else
#define VLINK_LIKELY(x) (x)
#define VLINK_UNLIKELY(x) (x)
#endif

/**
 * @def VLINK_DISALLOW_COPY_AND_ASSIGN(classname)
 * @brief Deletes the copy constructor and copy-assignment operator of @p classname.
 *
 * @details
 * Place this macro in the private section of a class to prevent accidental copying.
 * It expands to:
 * @code
 * classname(const classname&) = delete;
 * classname& operator=(const classname&) = delete;
 * @endcode
 *
 * @param classname  The unqualified name of the enclosing class.
 */
#define VLINK_DISALLOW_COPY_AND_ASSIGN(classname) \
  classname(const classname&) = delete;           \
  classname& operator=(const classname&) = delete;

/**
 * @def VLINK_SINGLETON_CHECK(classname)
 * @brief Injects a compile-time and run-time singleton enforcement guard.
 *
 * @details
 * This macro:
 * - Uses @c static_assert to verify that @p classname is @c final and has a
 *   private (non-default-constructible-from-outside) constructor.
 * - Uses a @c std::atomic_flag to guarantee that the constructor is only invoked
 *   once per process.  A second instantiation throws @c std::runtime_error.
 *
 * @note Must be placed in the @c private section of the class body.
 *
 * @param classname  The unqualified name of the class being protected.
 */
#define VLINK_SINGLETON_CHECK(classname)                                                          \
 private:                                                                                         \
  struct ConstructorChecker {                                                                     \
    inline ConstructorChecker() {                                                                 \
      static_assert(std::is_final_v<classname>, "Constructor must be final.");                    \
      static_assert(!std::is_default_constructible_v<classname>, "Constructor must be private."); \
                                                                                                  \
      static std::atomic_flag flag;                                                               \
                                                                                                  \
      if VLINK_UNLIKELY (flag.test_and_set()) {                                                   \
        throw std::runtime_error("Constructor has been instantiated.");                           \
      }                                                                                           \
    }                                                                                             \
  };                                                                                              \
                                                                                                  \
  ConstructorChecker constructor_checker_;

/**
 * @def VLINK_SINGLETON_DECLARE(classname)
 * @brief Declares a complete Meyer's-singleton @c get() method with safety guards.
 *
 * @details
 * Combines VLINK_SINGLETON_CHECK with a public static @c get() accessor that
 * returns a reference to the single static instance.  The constructor must be
 * @c private and @c final, otherwise the embedded @c static_assert will fail.
 *
 * @param classname  The unqualified name of the singleton class.
 *
 * @par Example
 * @code
 * class MyService final {
 *   VLINK_SINGLETON_DECLARE(MyService)
 * public:
 *   void do_work();
 * private:
 *   MyService() = default;
 * };
 *
 * MyService::get().do_work();
 * @endcode
 */
#define VLINK_SINGLETON_DECLARE(classname)    \
 public:                                      \
  inline static classname& get() {            \
    static classname singleton = classname(); \
    return singleton;                         \
  }                                           \
                                              \
 private:                                     \
  VLINK_SINGLETON_CHECK(classname)            \
  VLINK_DISALLOW_COPY_AND_ASSIGN(classname)

/**
 * @def VLINK_MACRO_STRING_IFY(name)
 * @brief Converts @p name to a C string literal using the preprocessor stringification operator.
 *
 * @param name  A macro token (not a string literal) to stringify.
 */
#define VLINK_MACRO_STRING_IFY(name) #name

/**
 * @def VLINK_MACRO_STRING_GET(name)
 * @brief Stringifies the expanded value of macro @p name (two-level expansion).
 *
 * @details
 * This two-step macro ensures that @p name is fully expanded before
 * stringification.  Use this instead of VLINK_MACRO_STRING_IFY when @p name
 * is itself a macro that should be expanded first.
 *
 * @param name  A macro whose expansion will be converted to a string literal.
 */
#define VLINK_MACRO_STRING_GET(name) VLINK_MACRO_STRING_IFY(name)

#if __has_builtin(__builtin_constant_p)
/**
 * @def VLINK_ASSERT_CONSTANT(msg)
 * @brief Asserts (at compile time) that @p msg is a constant string literal.
 *
 * @details
 * On compilers that support @c __builtin_constant_p this expands to a
 * @c static_assert; on others it is a no-op.
 *
 * @param msg  The expression to check for compile-time constancy.
 */
#define VLINK_ASSERT_CONSTANT(msg) static_assert(__builtin_constant_p(msg), "Must be a constant string literal.");
#else
#define VLINK_ASSERT_CONSTANT(msg)
#endif

#if !defined(VLIKELY) && !defined(VUNLIKELY)
/**
 * @def VLIKELY(...)
 * @brief Shorthand alias for VLINK_LIKELY.  Hints that the expression is likely true.
 */
#define VLIKELY(...) VLINK_LIKELY(__VA_ARGS__)
/**
 * @def VUNLIKELY(...)
 * @brief Shorthand alias for VLINK_UNLIKELY.  Hints that the expression is unlikely true.
 */
#define VUNLIKELY(...) VLINK_UNLIKELY(__VA_ARGS__)
#endif
