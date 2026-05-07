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
 * @file functional.h
 * @brief Type-erased callable wrappers: @c vlink::Function tracks the public
 *        surface of @c std::function (C++11) and @c vlink::MoveFunction tracks
 *        @c std::move_only_function (C++23). Storage uses a 64-byte SBO and a
 *        @c vlink::MemoryPool-backed heap fallback.
 *
 * @details
 * @par Storage predicate (kIsInline)
 * A target @c FunctorT is held inline iff @b all three conditions hold:
 * @c sizeof(FunctorT) @c <= @c 64 (one cache line), @c alignof(FunctorT) @c <=
 * @c alignof(std::max_align_t), and @c FunctorT is nothrow move-constructible.
 * Otherwise @c FunctorT is allocated through @c vlink::MemoryPool::global_instance();
 * the original @c sizeof(FunctorT) and @c alignof(FunctorT) are passed back to
 * @c deallocate so the block routes to its source tier. This predicate
 * matches the @c __stored_locally rule used by libstdc++
 * @c std::move_only_function. The 64-byte budget intentionally exceeds the
 * typical @c std::function SBO (~16-24 B) so common VLink capture sets
 * (a handful of @c shared_ptr / pointers / small POD) stay inline.
 *
 * @par Empty-state propagation
 * Constructing or assigning from any of the following yields an empty
 * wrapper -- no stored target, no allocation, no installed vtable:
 *  - an empty function-wrapper source: @c std::function /
 *    @c vlink::Function (plus @c std::move_only_function and
 *    @c vlink::MoveFunction when targeting @c MoveFunction);
 *  - a null function pointer;
 *  - a null pointer-to-member.
 * Invoking an empty wrapper throws @c std::bad_function_call. This matches
 * @c std::function; for @c MoveFunction it is a deliberate divergence from
 * @c std::move_only_function (which leaves the empty call as UB) so misuse
 * is diagnosable.
 *
 * @par Exception safety
 * Copy construction and copy assignment provide the strong guarantee via
 * copy-and-swap. @c swap, the move constructor, and move assignment are
 * @c noexcept -- the @c kIsInline predicate guarantees the underlying
 * FunctorT-move never throws on the inline path, and the heap path moves only a
 * pointer. Inline construction of @c FunctorT uses placement-new directly into the
 * SBO; heap construction uses a @c try/@c catch so the pool block is
 * returned on a constructor exception.
 *
 * @par Object-lifetime model
 * Inline targets are placed into the SBO via @c ::new(&storage_) @c FunctorT(...).
 * Heap-mode storage holds an @c FunctorT*; its lifetime is started explicitly
 * via @c ::new(dst) @c FunctorT*(p) at every site that introduces a new slot
 * (@c construct_from, @c HeapVTable::copy_construct, @c
 * HeapVTable::move_construct (dst)) -- this avoids relying on C++20
 * @c [intro.object]/10 implicit object creation, so the same source is
 * well-defined under both C++17 and C++20. All subsequent accesses to
 * the placement-new'd target -- whether the inline @c FunctorT or the heap @c FunctorT*
 * slot -- go through @c std::launder to obtain a pointer with provenance
 * matching the live object, satisfying @c [basic.life]/8 strictly.
 * After @c destroy / move-out the heap slot is set to @c nullptr but its
 * @c FunctorT* object lifetime continues until the wrapper itself is destroyed.
 *
 * @par RTTI surface
 * When @c __cpp_rtti is defined, both wrappers expose @c target_type() and
 * @c target<FunctorT>() with @c std::function semantics. For @c MoveFunction this
 * is an extension -- @c std::move_only_function omits target inspection.
 *
 * @par Limitations
 * Only the unqualified @c ReturnT(ArgsT...) signature is specialized; the
 * @c std::move_only_function cv / ref / @c noexcept qualified forms are
 * not supported and select the undefined primary template (hard error).
 *
 * @note
 * - @c Function requires copy-constructible targets and is itself copyable.
 * - @c MoveFunction accepts move-only targets and is itself move-only.
 * - Class template declarations precede the @c Details section;
 *   out-of-class member definitions follow it.
 */

#pragma once

#include <functional>

#if !defined(VLINK_ENABLE_BASE_FUNCTIONAL)
#define VLINK_ENABLE_BASE_FUNCTIONAL
#endif

#ifdef VLINK_ENABLE_BASE_FUNCTIONAL
#include <cstddef>
#include <new>
#include <type_traits>
#include <typeinfo>
#include <utility>

#include "./macros.h"
#include "./memory_pool.h"

namespace vlink {

template <typename SignatureT>
class Function;

template <typename SignatureT>
class MoveFunction;

namespace detail {

template <typename TypeT>
struct IsStdFunction : std::false_type {};

template <typename SignatureT>
struct IsStdFunction<std::function<SignatureT>> : std::true_type {};

template <typename TypeT>
struct IsVlinkFunction : std::false_type {};

template <typename SignatureT>
struct IsVlinkFunction<Function<SignatureT>> : std::true_type {};

template <typename TypeT>
struct IsVlinkMoveFunction : std::false_type {};

template <typename SignatureT>
struct IsVlinkMoveFunction<MoveFunction<SignatureT>> : std::true_type {};

#if defined(__cpp_lib_move_only_function) && __cpp_lib_move_only_function >= 202110L
template <typename TypeT>
struct IsStdMoveOnlyFunction : std::false_type {};

template <typename SignatureT>
struct IsStdMoveOnlyFunction<std::move_only_function<SignatureT>> : std::true_type {};
#endif

}  // namespace detail

/**
 * @class Function
 * @brief Copyable type-erased callable wrapper -- @c std::function analogue
 *        with a 64-byte SBO and @c vlink::MemoryPool-backed heap fallback.
 *
 * @details
 * @c Function<ReturnT(ArgsT...)> stores any copy-constructible callable that is
 * invocable as @c ReturnT(ArgsT...). The observable contract mirrors
 * @c std::function:
 *  - @c operator() is @c const (the @c std::function "logical const"
 *    convention -- the placement-newed target is non-const, so the standard
 *    @c const_cast pattern in the invoker is well-defined);
 *  - empty construction is supported via the default constructor and from
 *    @c nullptr;
 *  - invoking an empty wrapper throws @c std::bad_function_call;
 *  - @c target_type() and @c target<FunctorT>() expose the stored target when
 *    @c __cpp_rtti is defined.
 *
 * Copy construction and copy assignment provide the strong exception
 * guarantee through copy-and-swap. The move constructor, move assignment,
 * and @c swap are @c noexcept.
 */
template <typename ReturnT, typename... ArgsT>
class Function<ReturnT(ArgsT...)> {
 public:
  /**
   * @brief Inline storage budget before falling back to @c MemoryPool.
   */
  static constexpr std::size_t kSboSize = 64U;

  /**
   * @brief Result type alias, matching @c std::function.
   */
  using result_type = ReturnT;

  /**
   * @brief Constructs an empty function wrapper.
   */
  Function() noexcept = default;

  /**
   * @brief Constructs an empty function wrapper from @c nullptr.
   */
  Function(std::nullptr_t) noexcept;  // NOLINT(google-explicit-constructor)

  /**
   * @brief Copy-constructs the stored callable, or remains empty.
   */
  Function(const Function& other);

  /**
   * @brief Move-constructs the stored callable and leaves @p other empty.
   */
  Function(Function&& other) noexcept;

  /**
   * @brief Constructs from a compatible copy-constructible callable.
   *
   * @details
   * Empty function-wrapper sources and null callable pointers produce an empty
   * wrapper instead of storing a null target.
   *
   * @tparam FunctorT  Callable type accepted by @c std::invoke.
   */
  template <
      typename FunctorT, typename DecayFunctorT = std::decay_t<FunctorT>,
      typename = std::enable_if_t<
          !std::is_same_v<DecayFunctorT, Function> && !std::is_same_v<DecayFunctorT, std::nullptr_t> &&
          std::is_copy_constructible_v<DecayFunctorT> && std::is_invocable_r_v<ReturnT, DecayFunctorT&, ArgsT...>>>
  Function(FunctorT&& f);  // NOLINT(google-explicit-constructor)

  /**
   * @brief Replaces the target with a copy of @p other.
   */
  Function& operator=(const Function& other);

  /**
   * @brief Replaces the target by moving from @p other.
   */
  Function& operator=(Function&& other) noexcept;

  /**
   * @brief Clears the stored callable.
   */
  Function& operator=(std::nullptr_t) noexcept;

  /**
   * @brief Replaces the target with a compatible copy-constructible callable.
   */
  template <
      typename FunctorT, typename DecayFunctorT = std::decay_t<FunctorT>,
      typename = std::enable_if_t<
          !std::is_same_v<DecayFunctorT, Function> && !std::is_same_v<DecayFunctorT, std::nullptr_t> &&
          std::is_copy_constructible_v<DecayFunctorT> && std::is_invocable_r_v<ReturnT, DecayFunctorT&, ArgsT...>>>
  Function& operator=(FunctorT&& f);

  /**
   * @brief Destroys the stored callable.
   */
  ~Function();

  /**
   * @brief Invokes the stored callable.
   *
   * @throws std::bad_function_call if the wrapper is empty.
   */
  ReturnT operator()(ArgsT... args) const;

  /**
   * @brief Returns whether a callable target is stored.
   */
  explicit operator bool() const noexcept;

#if defined(__cpp_rtti)
  /**
   * @brief Returns the stored callable type, or @c typeid(void) when empty.
   */
  const std::type_info& target_type() const noexcept;

  /**
   * @brief Returns the stored target when its type is exactly @c FunctorT.
   */
  template <typename FunctorT>
  FunctorT* target() noexcept;

  /**
   * @brief Returns the stored target when its type is exactly @c FunctorT.
   */
  template <typename FunctorT>
  const FunctorT* target() const noexcept;
#endif

  /**
   * @brief Swaps this wrapper with @p other.
   */
  void swap(Function& other) noexcept;

 private:
  template <typename FunctorT>
  static constexpr bool kIsPointerLike = std::is_pointer_v<FunctorT> || std::is_member_pointer_v<FunctorT> ||
                                         std::is_function_v<std::remove_pointer_t<FunctorT>>;

  template <typename FunctorT>
  static constexpr bool kIsFunctionWrapper =
      detail::IsStdFunction<FunctorT>::value || detail::IsVlinkFunction<FunctorT>::value;

  template <typename FunctorT>
  static constexpr bool kIsInline = sizeof(FunctorT) <= kSboSize && alignof(FunctorT) <= alignof(std::max_align_t) &&
                                    std::is_nothrow_move_constructible_v<FunctorT>;

  struct VTable final {
    ReturnT (*invoke)(const void* storage, ArgsT... args);
    void (*copy_construct)(void* dst, const void* src);
    void (*move_construct)(void* dst, void* src) noexcept;
    void (*destroy)(void* storage) noexcept;
#if defined(__cpp_rtti)
    const std::type_info& (*target_type)() noexcept;
    void* (*target)(void* storage) noexcept;
    const void* (*target_const)(const void* storage) noexcept;
#endif
  };

  template <typename FunctorT>
  struct InlineVTable {
    static ReturnT invoke(const void* storage, ArgsT... args);
    static void copy_construct(void* dst, const void* src);
    static void move_construct(void* dst, void* src) noexcept;
    static void destroy(void* storage) noexcept;
#if defined(__cpp_rtti)
    static const std::type_info& target_type() noexcept;
    static void* target(void* storage) noexcept;
    static const void* target_const(const void* storage) noexcept;
#endif
  };

  template <typename FunctorT>
  struct HeapVTable {
    static ReturnT invoke(const void* storage, ArgsT... args);
    static void copy_construct(void* dst, const void* src);
    static void move_construct(void* dst, void* src) noexcept;
    static void destroy(void* storage) noexcept;
#if defined(__cpp_rtti)
    static const std::type_info& target_type() noexcept;
    static void* target(void* storage) noexcept;
    static const void* target_const(const void* storage) noexcept;
#endif
  };

  template <typename FunctorT>
  static const VTable* get_vtable() noexcept;

  template <typename FunctorT, typename SourceT>
  void construct_from(SourceT&& src);

  void copy_from(const Function& other);
  void move_from(Function&& other) noexcept;
  void reset() noexcept;

  alignas(std::max_align_t) std::byte storage_[kSboSize]{};
  const VTable* vtable_{nullptr};
};

/**
 * @brief Swaps two function wrappers.
 */
template <typename ReturnT, typename... ArgsT>
void swap(Function<ReturnT(ArgsT...)>& lhs, Function<ReturnT(ArgsT...)>& rhs) noexcept;

/**
 * @brief Returns whether @p cb is empty.
 */
template <typename ReturnT, typename... ArgsT>
bool operator==(const Function<ReturnT(ArgsT...)>& cb, std::nullptr_t) noexcept;

/**
 * @brief Returns whether @p cb is empty.
 */
template <typename ReturnT, typename... ArgsT>
bool operator==(std::nullptr_t, const Function<ReturnT(ArgsT...)>& cb) noexcept;

/**
 * @brief Returns whether @p cb stores a callable target.
 */
template <typename ReturnT, typename... ArgsT>
bool operator!=(const Function<ReturnT(ArgsT...)>& cb, std::nullptr_t) noexcept;

/**
 * @brief Returns whether @p cb stores a callable target.
 */
template <typename ReturnT, typename... ArgsT>
bool operator!=(std::nullptr_t, const Function<ReturnT(ArgsT...)>& cb) noexcept;

/**
 * @class MoveFunction
 * @brief Move-only type-erased callable wrapper -- @c std::move_only_function
 *        analogue sharing the 64-byte SBO and @c MemoryPool fallback used by
 *        @c Function.
 *
 * @details
 * @c MoveFunction<ReturnT(ArgsT...)> stores any move-constructible callable that
 * is invocable as @c ReturnT(ArgsT...). It mirrors @c std::move_only_function with
 * two deliberate divergences and one extension:
 *  - @b Divergence: invoking an empty wrapper throws
 *    @c std::bad_function_call (matching @c Function and @c std::function);
 *    @c std::move_only_function leaves it as UB.
 *  - @b Divergence: only the unqualified @c ReturnT(ArgsT...) signature is
 *    specialized; cv / ref / @c noexcept qualified forms are unsupported.
 *  - @b Extension: @c target_type() / @c target<FunctorT>() are provided when
 *    @c __cpp_rtti is defined.
 *
 * @c operator() is non-@c const, so @c MoveFunction can carry mutating /
 * @c std::packaged_task -style targets without the @c std::function
 * "logical const" workaround. Move construction, move assignment, and
 * @c swap are @c noexcept; copy operations are @c =delete.
 */
template <typename ReturnT, typename... ArgsT>
class MoveFunction<ReturnT(ArgsT...)> {
 public:
  /**
   * @brief Inline storage budget before falling back to @c MemoryPool.
   */
  static constexpr std::size_t kSboSize = 64U;

  /**
   * @brief Result type alias.
   */
  using result_type = ReturnT;

  /**
   * @brief Constructs an empty move-only function wrapper.
   */
  MoveFunction() noexcept = default;

  /**
   * @brief Constructs an empty move-only function wrapper from @c nullptr.
   */
  MoveFunction(std::nullptr_t) noexcept;  // NOLINT(google-explicit-constructor)

  MoveFunction(const MoveFunction&) = delete;
  MoveFunction& operator=(const MoveFunction&) = delete;

  /**
   * @brief Move-constructs the stored callable and leaves @p other empty.
   */
  MoveFunction(MoveFunction&& other) noexcept;

  /**
   * @brief Constructs from a compatible move-constructible callable.
   *
   * @details
   * Empty function-wrapper sources and null callable pointers produce an empty
   * wrapper instead of storing a null target.
   *
   * @tparam FunctorT  Callable type accepted by @c std::invoke.
   */
  template <typename FunctorT, typename DecayFunctorT = std::decay_t<FunctorT>,
            typename = std::enable_if_t<
                !std::is_same_v<DecayFunctorT, MoveFunction> && !std::is_same_v<DecayFunctorT, std::nullptr_t> &&
                std::is_constructible_v<DecayFunctorT, FunctorT> && std::is_move_constructible_v<DecayFunctorT> &&
                std::is_invocable_r_v<ReturnT, DecayFunctorT&, ArgsT...>>>
  MoveFunction(FunctorT&& f);  // NOLINT(google-explicit-constructor)

  /**
   * @brief Replaces the target by moving from @p other.
   */
  MoveFunction& operator=(MoveFunction&& other) noexcept;

  /**
   * @brief Clears the stored callable.
   */
  MoveFunction& operator=(std::nullptr_t) noexcept;

  /**
   * @brief Replaces the target with a compatible move-constructible callable.
   */
  template <typename FunctorT, typename DecayFunctorT = std::decay_t<FunctorT>,
            typename = std::enable_if_t<
                !std::is_same_v<DecayFunctorT, MoveFunction> && !std::is_same_v<DecayFunctorT, std::nullptr_t> &&
                std::is_constructible_v<DecayFunctorT, FunctorT> && std::is_move_constructible_v<DecayFunctorT> &&
                std::is_invocable_r_v<ReturnT, DecayFunctorT&, ArgsT...>>>
  MoveFunction& operator=(FunctorT&& f);

  /**
   * @brief Destroys the stored callable.
   */
  ~MoveFunction();

  /**
   * @brief Invokes the stored callable.
   *
   * @throws std::bad_function_call if the wrapper is empty.
   */
  ReturnT operator()(ArgsT... args);

  /**
   * @brief Returns whether a callable target is stored.
   */
  explicit operator bool() const noexcept;

#if defined(__cpp_rtti)
  /**
   * @brief Returns the stored callable type, or @c typeid(void) when empty.
   */
  const std::type_info& target_type() const noexcept;

  /**
   * @brief Returns the stored target when its type is exactly @c FunctorT.
   */
  template <typename FunctorT>
  FunctorT* target() noexcept;

  /**
   * @brief Returns the stored target when its type is exactly @c FunctorT.
   */
  template <typename FunctorT>
  const FunctorT* target() const noexcept;
#endif

  /**
   * @brief Swaps this wrapper with @p other.
   */
  void swap(MoveFunction& other) noexcept;

 private:
  template <typename FunctorT>
  static constexpr bool kIsPointerLike = std::is_pointer_v<FunctorT> || std::is_member_pointer_v<FunctorT> ||
                                         std::is_function_v<std::remove_pointer_t<FunctorT>>;

  template <typename FunctorT>
  static constexpr bool kIsFunctionWrapper =
      detail::IsStdFunction<FunctorT>::value || detail::IsVlinkFunction<FunctorT>::value ||
#if defined(__cpp_lib_move_only_function) && __cpp_lib_move_only_function >= 202110L
      detail::IsVlinkMoveFunction<FunctorT>::value || detail::IsStdMoveOnlyFunction<FunctorT>::value;
#else
      detail::IsVlinkMoveFunction<FunctorT>::value;
#endif

  template <typename FunctorT>
  static constexpr bool kIsInline = sizeof(FunctorT) <= kSboSize && alignof(FunctorT) <= alignof(std::max_align_t) &&
                                    std::is_nothrow_move_constructible_v<FunctorT>;

  struct VTable final {
    ReturnT (*invoke)(void* storage, ArgsT... args);
    void (*move_construct)(void* dst, void* src) noexcept;
    void (*destroy)(void* storage) noexcept;
#if defined(__cpp_rtti)
    const std::type_info& (*target_type)() noexcept;
    void* (*target)(void* storage) noexcept;
    const void* (*target_const)(const void* storage) noexcept;
#endif
  };

  template <typename FunctorT>
  struct InlineVTable {
    static ReturnT invoke(void* storage, ArgsT... args);
    static void move_construct(void* dst, void* src) noexcept;
    static void destroy(void* storage) noexcept;
#if defined(__cpp_rtti)
    static const std::type_info& target_type() noexcept;
    static void* target(void* storage) noexcept;
    static const void* target_const(const void* storage) noexcept;
#endif
  };

  template <typename FunctorT>
  struct HeapVTable {
    static ReturnT invoke(void* storage, ArgsT... args);
    static void move_construct(void* dst, void* src) noexcept;
    static void destroy(void* storage) noexcept;
#if defined(__cpp_rtti)
    static const std::type_info& target_type() noexcept;
    static void* target(void* storage) noexcept;
    static const void* target_const(const void* storage) noexcept;
#endif
  };

  template <typename FunctorT>
  static const VTable* get_vtable() noexcept;

  template <typename FunctorT, typename SourceT>
  void construct_from(SourceT&& src);

  void move_from(MoveFunction&& other) noexcept;
  void reset() noexcept;

  alignas(std::max_align_t) std::byte storage_[kSboSize]{};
  const VTable* vtable_{nullptr};
};

/**
 * @brief Swaps two move-only function wrappers.
 */
template <typename ReturnT, typename... ArgsT>
void swap(MoveFunction<ReturnT(ArgsT...)>& lhs, MoveFunction<ReturnT(ArgsT...)>& rhs) noexcept;

/**
 * @brief Returns whether @p cb is empty.
 */
template <typename ReturnT, typename... ArgsT>
bool operator==(const MoveFunction<ReturnT(ArgsT...)>& cb, std::nullptr_t) noexcept;

/**
 * @brief Returns whether @p cb is empty.
 */
template <typename ReturnT, typename... ArgsT>
bool operator==(std::nullptr_t, const MoveFunction<ReturnT(ArgsT...)>& cb) noexcept;

/**
 * @brief Returns whether @p cb stores a callable target.
 */
template <typename ReturnT, typename... ArgsT>
bool operator!=(const MoveFunction<ReturnT(ArgsT...)>& cb, std::nullptr_t) noexcept;

/**
 * @brief Returns whether @p cb stores a callable target.
 */
template <typename ReturnT, typename... ArgsT>
bool operator!=(std::nullptr_t, const MoveFunction<ReturnT(ArgsT...)>& cb) noexcept;

////////////////////////////////////////////////////////////////
/// Details
////////////////////////////////////////////////////////////////

template <typename ReturnT, typename... ArgsT>
inline Function<ReturnT(ArgsT...)>::Function(std::nullptr_t) noexcept {}

template <typename ReturnT, typename... ArgsT>
inline Function<ReturnT(ArgsT...)>::Function(const Function& other) {
  copy_from(other);
}

template <typename ReturnT, typename... ArgsT>
inline Function<ReturnT(ArgsT...)>::Function(Function&& other) noexcept {
  move_from(std::move(other));
}

template <typename ReturnT, typename... ArgsT>
template <typename FunctorT, typename DecayFunctorT, typename>
inline Function<ReturnT(ArgsT...)>::Function(FunctorT&& f) {
  if constexpr (kIsFunctionWrapper<DecayFunctorT>) {
    if VUNLIKELY (!f) {
      return;
    }
  } else if constexpr (kIsPointerLike<DecayFunctorT>) {
    if VUNLIKELY (f == nullptr) {
      return;
    }
  }

  construct_from<DecayFunctorT>(std::forward<FunctorT>(f));
}

template <typename ReturnT, typename... ArgsT>
inline Function<ReturnT(ArgsT...)>& Function<ReturnT(ArgsT...)>::operator=(const Function& other) {
  if VLIKELY (this != &other) {
    Function tmp(other);
    swap(tmp);
  }

  return *this;
}

template <typename ReturnT, typename... ArgsT>
inline Function<ReturnT(ArgsT...)>& Function<ReturnT(ArgsT...)>::operator=(Function&& other) noexcept {
  if VLIKELY (this != &other) {
    reset();
    move_from(std::move(other));
  }

  return *this;
}

template <typename ReturnT, typename... ArgsT>
inline Function<ReturnT(ArgsT...)>& Function<ReturnT(ArgsT...)>::operator=(std::nullptr_t) noexcept {
  reset();
  return *this;
}

template <typename ReturnT, typename... ArgsT>
template <typename FunctorT, typename DecayFunctorT, typename>
inline Function<ReturnT(ArgsT...)>& Function<ReturnT(ArgsT...)>::operator=(FunctorT&& f) {
  Function tmp(std::forward<FunctorT>(f));
  swap(tmp);
  return *this;
}

template <typename ReturnT, typename... ArgsT>
inline Function<ReturnT(ArgsT...)>::~Function() {
  reset();
}

template <typename ReturnT, typename... ArgsT>
inline ReturnT Function<ReturnT(ArgsT...)>::operator()(ArgsT... args) const {
  if VUNLIKELY (vtable_ == nullptr) {
    throw std::bad_function_call();
  }

  return vtable_->invoke(&storage_, std::forward<ArgsT>(args)...);
}

template <typename ReturnT, typename... ArgsT>
inline Function<ReturnT(ArgsT...)>::operator bool() const noexcept {
  return vtable_ != nullptr;
}

#if defined(__cpp_rtti)
template <typename ReturnT, typename... ArgsT>
inline const std::type_info& Function<ReturnT(ArgsT...)>::target_type() const noexcept {
  if VLIKELY (vtable_ != nullptr) {
    return vtable_->target_type();
  }

  return typeid(void);
}

template <typename ReturnT, typename... ArgsT>
template <typename FunctorT>
inline FunctorT* Function<ReturnT(ArgsT...)>::target() noexcept {
  if constexpr (std::is_object_v<FunctorT>) {
    if VLIKELY (vtable_ != nullptr && typeid(FunctorT) == target_type()) {
      return static_cast<FunctorT*>(vtable_->target(&storage_));
    }
  }

  return nullptr;
}

template <typename ReturnT, typename... ArgsT>
template <typename FunctorT>
inline const FunctorT* Function<ReturnT(ArgsT...)>::target() const noexcept {
  if constexpr (std::is_object_v<FunctorT>) {
    if VLIKELY (vtable_ != nullptr && typeid(FunctorT) == target_type()) {
      return static_cast<const FunctorT*>(vtable_->target_const(&storage_));
    }
  }

  return nullptr;
}
#endif

template <typename ReturnT, typename... ArgsT>
inline void Function<ReturnT(ArgsT...)>::swap(Function& other) noexcept {
  if VUNLIKELY (this == &other) {
    return;
  }

  Function tmp(std::move(*this));
  *this = std::move(other);
  other = std::move(tmp);
}

template <typename ReturnT, typename... ArgsT>
template <typename FunctorT>
inline ReturnT Function<ReturnT(ArgsT...)>::InlineVTable<FunctorT>::invoke(const void* storage, ArgsT... args) {
  auto* f = std::launder(reinterpret_cast<FunctorT*>(const_cast<void*>(storage)));

  if constexpr (std::is_void_v<ReturnT>) {
    std::invoke(*f, std::forward<ArgsT>(args)...);
  } else {
    return std::invoke(*f, std::forward<ArgsT>(args)...);
  }
}

template <typename ReturnT, typename... ArgsT>
template <typename FunctorT>
inline void Function<ReturnT(ArgsT...)>::InlineVTable<FunctorT>::copy_construct(void* dst, const void* src) {
  const auto* src_f = std::launder(reinterpret_cast<const FunctorT*>(src));
  ::new (dst) FunctorT(*src_f);
}

template <typename ReturnT, typename... ArgsT>
template <typename FunctorT>
inline void Function<ReturnT(ArgsT...)>::InlineVTable<FunctorT>::move_construct(void* dst, void* src) noexcept {
  auto* src_f = std::launder(reinterpret_cast<FunctorT*>(src));
  ::new (dst) FunctorT(std::move(*src_f));
  src_f->~FunctorT();
}

template <typename ReturnT, typename... ArgsT>
template <typename FunctorT>
inline void Function<ReturnT(ArgsT...)>::InlineVTable<FunctorT>::destroy(void* storage) noexcept {
  auto* f = std::launder(reinterpret_cast<FunctorT*>(storage));
  f->~FunctorT();
}

#if defined(__cpp_rtti)
template <typename ReturnT, typename... ArgsT>
template <typename FunctorT>
inline const std::type_info& Function<ReturnT(ArgsT...)>::InlineVTable<FunctorT>::target_type() noexcept {
  return typeid(FunctorT);
}

template <typename ReturnT, typename... ArgsT>
template <typename FunctorT>
inline void* Function<ReturnT(ArgsT...)>::InlineVTable<FunctorT>::target(void* storage) noexcept {
  return storage;
}

template <typename ReturnT, typename... ArgsT>
template <typename FunctorT>
inline const void* Function<ReturnT(ArgsT...)>::InlineVTable<FunctorT>::target_const(const void* storage) noexcept {
  return storage;
}
#endif

template <typename ReturnT, typename... ArgsT>
template <typename FunctorT>
inline ReturnT Function<ReturnT(ArgsT...)>::HeapVTable<FunctorT>::invoke(const void* storage, ArgsT... args) {
  FunctorT* f = *std::launder(static_cast<FunctorT* const*>(storage));

  if constexpr (std::is_void_v<ReturnT>) {
    std::invoke(*f, std::forward<ArgsT>(args)...);
  } else {
    return std::invoke(*f, std::forward<ArgsT>(args)...);
  }
}

template <typename ReturnT, typename... ArgsT>
template <typename FunctorT>
inline void Function<ReturnT(ArgsT...)>::HeapVTable<FunctorT>::copy_construct(void* dst, const void* src) {
  FunctorT* src_f = *std::launder(static_cast<FunctorT* const*>(src));

  auto& pool = MemoryPool::global_instance();
  void* mem = pool.allocate(sizeof(FunctorT), alignof(FunctorT));

  if VUNLIKELY (mem == nullptr) {
    throw std::bad_alloc();
  }

  try {
    auto* new_f = ::new (mem) FunctorT(*src_f);
    ::new (dst) FunctorT*(new_f);
  } catch (...) {
    pool.deallocate(mem, sizeof(FunctorT), alignof(FunctorT));
    throw;
  }
}

template <typename ReturnT, typename... ArgsT>
template <typename FunctorT>
inline void Function<ReturnT(ArgsT...)>::HeapVTable<FunctorT>::move_construct(void* dst, void* src) noexcept {
  FunctorT** src_slot = std::launder(static_cast<FunctorT**>(src));
  FunctorT* src_f = *src_slot;
  ::new (dst) FunctorT*(src_f);
  *src_slot = nullptr;
}

template <typename ReturnT, typename... ArgsT>
template <typename FunctorT>
inline void Function<ReturnT(ArgsT...)>::HeapVTable<FunctorT>::destroy(void* storage) noexcept {
  FunctorT** slot = std::launder(static_cast<FunctorT**>(storage));
  FunctorT* f = *slot;

  if VLIKELY (f != nullptr) {
    f->~FunctorT();

    auto& pool = MemoryPool::global_instance();
    pool.deallocate(f, sizeof(FunctorT), alignof(FunctorT));

    *slot = nullptr;
  }
}

#if defined(__cpp_rtti)
template <typename ReturnT, typename... ArgsT>
template <typename FunctorT>
inline const std::type_info& Function<ReturnT(ArgsT...)>::HeapVTable<FunctorT>::target_type() noexcept {
  return typeid(FunctorT);
}

template <typename ReturnT, typename... ArgsT>
template <typename FunctorT>
inline void* Function<ReturnT(ArgsT...)>::HeapVTable<FunctorT>::target(void* storage) noexcept {
  return *std::launder(static_cast<FunctorT**>(storage));
}

template <typename ReturnT, typename... ArgsT>
template <typename FunctorT>
inline const void* Function<ReturnT(ArgsT...)>::HeapVTable<FunctorT>::target_const(const void* storage) noexcept {
  return *std::launder(static_cast<FunctorT* const*>(storage));
}
#endif

template <typename ReturnT, typename... ArgsT>
template <typename FunctorT>
inline const typename Function<ReturnT(ArgsT...)>::VTable* Function<ReturnT(ArgsT...)>::get_vtable() noexcept {
  if constexpr (kIsInline<FunctorT>) {
    static constexpr VTable kVTable = {
        &InlineVTable<FunctorT>::invoke,         &InlineVTable<FunctorT>::copy_construct,
        &InlineVTable<FunctorT>::move_construct, &InlineVTable<FunctorT>::destroy,
#if defined(__cpp_rtti)
        &InlineVTable<FunctorT>::target_type,    &InlineVTable<FunctorT>::target,
        &InlineVTable<FunctorT>::target_const,
#endif
    };
    return &kVTable;
  } else {
    static constexpr VTable kVTable = {
        &HeapVTable<FunctorT>::invoke,         &HeapVTable<FunctorT>::copy_construct,
        &HeapVTable<FunctorT>::move_construct, &HeapVTable<FunctorT>::destroy,
#if defined(__cpp_rtti)
        &HeapVTable<FunctorT>::target_type,    &HeapVTable<FunctorT>::target,
        &HeapVTable<FunctorT>::target_const,
#endif
    };
    return &kVTable;
  }
}

template <typename ReturnT, typename... ArgsT>
template <typename FunctorT, typename SourceT>
inline void Function<ReturnT(ArgsT...)>::construct_from(SourceT&& src) {
  if constexpr (kIsInline<FunctorT>) {
    ::new (&storage_) FunctorT(std::forward<SourceT>(src));
  } else {
    auto& pool = MemoryPool::global_instance();
    auto* mem = pool.allocate(sizeof(FunctorT), alignof(FunctorT));

    if VUNLIKELY (mem == nullptr) {
      throw std::bad_alloc();
    }

    try {
      auto* new_f = ::new (mem) FunctorT(std::forward<SourceT>(src));
      ::new (static_cast<void*>(&storage_)) FunctorT*(new_f);
    } catch (...) {
      pool.deallocate(mem, sizeof(FunctorT), alignof(FunctorT));
      throw;
    }
  }

  vtable_ = get_vtable<FunctorT>();
}

template <typename ReturnT, typename... ArgsT>
inline void Function<ReturnT(ArgsT...)>::copy_from(const Function& other) {
  if VLIKELY (other.vtable_ != nullptr) {
    other.vtable_->copy_construct(&storage_, &other.storage_);
    vtable_ = other.vtable_;
  }
}

template <typename ReturnT, typename... ArgsT>
inline void Function<ReturnT(ArgsT...)>::move_from(Function&& other) noexcept {
  if VLIKELY (other.vtable_ != nullptr) {
    other.vtable_->move_construct(&storage_, &other.storage_);
    vtable_ = other.vtable_;
    other.vtable_ = nullptr;
  }
}

template <typename ReturnT, typename... ArgsT>
inline void Function<ReturnT(ArgsT...)>::reset() noexcept {
  if VLIKELY (vtable_ != nullptr) {
    vtable_->destroy(&storage_);
    vtable_ = nullptr;
  }
}

template <typename ReturnT, typename... ArgsT>
inline void swap(Function<ReturnT(ArgsT...)>& lhs, Function<ReturnT(ArgsT...)>& rhs) noexcept {
  lhs.swap(rhs);
}

template <typename ReturnT, typename... ArgsT>
inline bool operator==(const Function<ReturnT(ArgsT...)>& cb, std::nullptr_t) noexcept {
  return !cb;
}

template <typename ReturnT, typename... ArgsT>
inline bool operator==(std::nullptr_t, const Function<ReturnT(ArgsT...)>& cb) noexcept {
  return !cb;
}

template <typename ReturnT, typename... ArgsT>
inline bool operator!=(const Function<ReturnT(ArgsT...)>& cb, std::nullptr_t) noexcept {
  return static_cast<bool>(cb);
}

template <typename ReturnT, typename... ArgsT>
inline bool operator!=(std::nullptr_t, const Function<ReturnT(ArgsT...)>& cb) noexcept {
  return static_cast<bool>(cb);
}

template <typename ReturnT, typename... ArgsT>
inline MoveFunction<ReturnT(ArgsT...)>::MoveFunction(std::nullptr_t) noexcept {}

template <typename ReturnT, typename... ArgsT>
inline MoveFunction<ReturnT(ArgsT...)>::MoveFunction(MoveFunction&& other) noexcept {
  move_from(std::move(other));
}

template <typename ReturnT, typename... ArgsT>
template <typename FunctorT, typename DecayFunctorT, typename>
inline MoveFunction<ReturnT(ArgsT...)>::MoveFunction(FunctorT&& f) {
  if constexpr (kIsFunctionWrapper<DecayFunctorT>) {
    if VUNLIKELY (!f) {
      return;
    }
  } else if constexpr (kIsPointerLike<DecayFunctorT>) {
    if VUNLIKELY (f == nullptr) {
      return;
    }
  }

  construct_from<DecayFunctorT>(std::forward<FunctorT>(f));
}

template <typename ReturnT, typename... ArgsT>
inline MoveFunction<ReturnT(ArgsT...)>& MoveFunction<ReturnT(ArgsT...)>::operator=(MoveFunction&& other) noexcept {
  if VLIKELY (this != &other) {
    reset();
    move_from(std::move(other));
  }

  return *this;
}

template <typename ReturnT, typename... ArgsT>
inline MoveFunction<ReturnT(ArgsT...)>& MoveFunction<ReturnT(ArgsT...)>::operator=(std::nullptr_t) noexcept {
  reset();
  return *this;
}

template <typename ReturnT, typename... ArgsT>
template <typename FunctorT, typename DecayFunctorT, typename>
inline MoveFunction<ReturnT(ArgsT...)>& MoveFunction<ReturnT(ArgsT...)>::operator=(FunctorT&& f) {
  MoveFunction tmp(std::forward<FunctorT>(f));
  swap(tmp);
  return *this;
}

template <typename ReturnT, typename... ArgsT>
inline MoveFunction<ReturnT(ArgsT...)>::~MoveFunction() {
  reset();
}

template <typename ReturnT, typename... ArgsT>
inline ReturnT MoveFunction<ReturnT(ArgsT...)>::operator()(ArgsT... args) {
  if VUNLIKELY (vtable_ == nullptr) {
    throw std::bad_function_call();
  }

  return vtable_->invoke(&storage_, std::forward<ArgsT>(args)...);
}

template <typename ReturnT, typename... ArgsT>
inline MoveFunction<ReturnT(ArgsT...)>::operator bool() const noexcept {
  return vtable_ != nullptr;
}

#if defined(__cpp_rtti)
template <typename ReturnT, typename... ArgsT>
inline const std::type_info& MoveFunction<ReturnT(ArgsT...)>::target_type() const noexcept {
  if VLIKELY (vtable_ != nullptr) {
    return vtable_->target_type();
  }

  return typeid(void);
}

template <typename ReturnT, typename... ArgsT>
template <typename FunctorT>
inline FunctorT* MoveFunction<ReturnT(ArgsT...)>::target() noexcept {
  if constexpr (std::is_object_v<FunctorT>) {
    if VLIKELY (vtable_ != nullptr && typeid(FunctorT) == target_type()) {
      return static_cast<FunctorT*>(vtable_->target(&storage_));
    }
  }

  return nullptr;
}

template <typename ReturnT, typename... ArgsT>
template <typename FunctorT>
inline const FunctorT* MoveFunction<ReturnT(ArgsT...)>::target() const noexcept {
  if constexpr (std::is_object_v<FunctorT>) {
    if VLIKELY (vtable_ != nullptr && typeid(FunctorT) == target_type()) {
      return static_cast<const FunctorT*>(vtable_->target_const(&storage_));
    }
  }

  return nullptr;
}
#endif

template <typename ReturnT, typename... ArgsT>
inline void MoveFunction<ReturnT(ArgsT...)>::swap(MoveFunction& other) noexcept {
  if VUNLIKELY (this == &other) {
    return;
  }

  MoveFunction tmp(std::move(*this));
  *this = std::move(other);
  other = std::move(tmp);
}

template <typename ReturnT, typename... ArgsT>
template <typename FunctorT>
inline ReturnT MoveFunction<ReturnT(ArgsT...)>::InlineVTable<FunctorT>::invoke(void* storage, ArgsT... args) {
  auto* f = std::launder(reinterpret_cast<FunctorT*>(storage));

  if constexpr (std::is_void_v<ReturnT>) {
    std::invoke(*f, std::forward<ArgsT>(args)...);
  } else {
    return std::invoke(*f, std::forward<ArgsT>(args)...);
  }
}

template <typename ReturnT, typename... ArgsT>
template <typename FunctorT>
inline void MoveFunction<ReturnT(ArgsT...)>::InlineVTable<FunctorT>::move_construct(void* dst, void* src) noexcept {
  auto* src_f = std::launder(reinterpret_cast<FunctorT*>(src));
  ::new (dst) FunctorT(std::move(*src_f));
  src_f->~FunctorT();
}

template <typename ReturnT, typename... ArgsT>
template <typename FunctorT>
inline void MoveFunction<ReturnT(ArgsT...)>::InlineVTable<FunctorT>::destroy(void* storage) noexcept {
  auto* f = std::launder(reinterpret_cast<FunctorT*>(storage));
  f->~FunctorT();
}

#if defined(__cpp_rtti)
template <typename ReturnT, typename... ArgsT>
template <typename FunctorT>
inline const std::type_info& MoveFunction<ReturnT(ArgsT...)>::InlineVTable<FunctorT>::target_type() noexcept {
  return typeid(FunctorT);
}

template <typename ReturnT, typename... ArgsT>
template <typename FunctorT>
inline void* MoveFunction<ReturnT(ArgsT...)>::InlineVTable<FunctorT>::target(void* storage) noexcept {
  return storage;
}

template <typename ReturnT, typename... ArgsT>
template <typename FunctorT>
inline const void* MoveFunction<ReturnT(ArgsT...)>::InlineVTable<FunctorT>::target_const(const void* storage) noexcept {
  return storage;
}
#endif

template <typename ReturnT, typename... ArgsT>
template <typename FunctorT>
inline ReturnT MoveFunction<ReturnT(ArgsT...)>::HeapVTable<FunctorT>::invoke(void* storage, ArgsT... args) {
  FunctorT* f = *std::launder(static_cast<FunctorT**>(storage));

  if constexpr (std::is_void_v<ReturnT>) {
    std::invoke(*f, std::forward<ArgsT>(args)...);
  } else {
    return std::invoke(*f, std::forward<ArgsT>(args)...);
  }
}

template <typename ReturnT, typename... ArgsT>
template <typename FunctorT>
inline void MoveFunction<ReturnT(ArgsT...)>::HeapVTable<FunctorT>::move_construct(void* dst, void* src) noexcept {
  FunctorT** src_slot = std::launder(static_cast<FunctorT**>(src));
  FunctorT* src_f = *src_slot;
  ::new (dst) FunctorT*(src_f);
  *src_slot = nullptr;
}

template <typename ReturnT, typename... ArgsT>
template <typename FunctorT>
inline void MoveFunction<ReturnT(ArgsT...)>::HeapVTable<FunctorT>::destroy(void* storage) noexcept {
  FunctorT** slot = std::launder(static_cast<FunctorT**>(storage));
  FunctorT* f = *slot;

  if VLIKELY (f != nullptr) {
    f->~FunctorT();

    auto& pool = MemoryPool::global_instance();
    pool.deallocate(f, sizeof(FunctorT), alignof(FunctorT));

    *slot = nullptr;
  }
}

#if defined(__cpp_rtti)
template <typename ReturnT, typename... ArgsT>
template <typename FunctorT>
inline const std::type_info& MoveFunction<ReturnT(ArgsT...)>::HeapVTable<FunctorT>::target_type() noexcept {
  return typeid(FunctorT);
}

template <typename ReturnT, typename... ArgsT>
template <typename FunctorT>
inline void* MoveFunction<ReturnT(ArgsT...)>::HeapVTable<FunctorT>::target(void* storage) noexcept {
  return *std::launder(static_cast<FunctorT**>(storage));
}

template <typename ReturnT, typename... ArgsT>
template <typename FunctorT>
inline const void* MoveFunction<ReturnT(ArgsT...)>::HeapVTable<FunctorT>::target_const(const void* storage) noexcept {
  return *std::launder(static_cast<FunctorT* const*>(storage));
}
#endif

template <typename ReturnT, typename... ArgsT>
template <typename FunctorT>
inline const typename MoveFunction<ReturnT(ArgsT...)>::VTable* MoveFunction<ReturnT(ArgsT...)>::get_vtable() noexcept {
  if constexpr (kIsInline<FunctorT>) {
    static constexpr VTable kVTable = {
        &InlineVTable<FunctorT>::invoke,       &InlineVTable<FunctorT>::move_construct,
        &InlineVTable<FunctorT>::destroy,
#if defined(__cpp_rtti)
        &InlineVTable<FunctorT>::target_type,  &InlineVTable<FunctorT>::target,
        &InlineVTable<FunctorT>::target_const,
#endif
    };
    return &kVTable;
  } else {
    static constexpr VTable kVTable = {
        &HeapVTable<FunctorT>::invoke,      &HeapVTable<FunctorT>::move_construct, &HeapVTable<FunctorT>::destroy,
#if defined(__cpp_rtti)
        &HeapVTable<FunctorT>::target_type, &HeapVTable<FunctorT>::target,         &HeapVTable<FunctorT>::target_const,
#endif
    };
    return &kVTable;
  }
}

template <typename ReturnT, typename... ArgsT>
template <typename FunctorT, typename SourceT>
inline void MoveFunction<ReturnT(ArgsT...)>::construct_from(SourceT&& src) {
  if constexpr (kIsInline<FunctorT>) {
    ::new (&storage_) FunctorT(std::forward<SourceT>(src));
  } else {
    auto& pool = MemoryPool::global_instance();
    auto* mem = pool.allocate(sizeof(FunctorT), alignof(FunctorT));

    if VUNLIKELY (mem == nullptr) {
      throw std::bad_alloc();
    }

    try {
      auto* new_f = ::new (mem) FunctorT(std::forward<SourceT>(src));
      ::new (static_cast<void*>(&storage_)) FunctorT*(new_f);
    } catch (...) {
      pool.deallocate(mem, sizeof(FunctorT), alignof(FunctorT));
      throw;
    }
  }

  vtable_ = get_vtable<FunctorT>();
}

template <typename ReturnT, typename... ArgsT>
inline void MoveFunction<ReturnT(ArgsT...)>::move_from(MoveFunction&& other) noexcept {
  if VLIKELY (other.vtable_ != nullptr) {
    other.vtable_->move_construct(&storage_, &other.storage_);
    vtable_ = other.vtable_;
    other.vtable_ = nullptr;
  }
}

template <typename ReturnT, typename... ArgsT>
inline void MoveFunction<ReturnT(ArgsT...)>::reset() noexcept {
  if VLIKELY (vtable_ != nullptr) {
    vtable_->destroy(&storage_);
    vtable_ = nullptr;
  }
}

template <typename ReturnT, typename... ArgsT>
inline void swap(MoveFunction<ReturnT(ArgsT...)>& lhs, MoveFunction<ReturnT(ArgsT...)>& rhs) noexcept {
  lhs.swap(rhs);
}

template <typename ReturnT, typename... ArgsT>
inline bool operator==(const MoveFunction<ReturnT(ArgsT...)>& cb, std::nullptr_t) noexcept {
  return !cb;
}

template <typename ReturnT, typename... ArgsT>
inline bool operator==(std::nullptr_t, const MoveFunction<ReturnT(ArgsT...)>& cb) noexcept {
  return !cb;
}

template <typename ReturnT, typename... ArgsT>
inline bool operator!=(const MoveFunction<ReturnT(ArgsT...)>& cb, std::nullptr_t) noexcept {
  return static_cast<bool>(cb);
}

template <typename ReturnT, typename... ArgsT>
inline bool operator!=(std::nullptr_t, const MoveFunction<ReturnT(ArgsT...)>& cb) noexcept {
  return static_cast<bool>(cb);
}

}  // namespace vlink

#else

namespace vlink {

template <typename SignatureT>
using Function = std::function<SignatureT>;

#if defined(__cpp_lib_move_only_function) && __cpp_lib_move_only_function >= 202110L
template <typename SignatureT>
using MoveFunction = std::move_only_function<SignatureT>;
#endif

}  // namespace vlink

#endif
