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
 *        @c std::move_only_function (C++23). Storage uses a configurable SBO
 *        (default 64 bytes) and a @c vlink::MemoryPool-backed heap fallback.
 *
 * @details
 * @par Storage predicate (kIsInline)
 * A target @c FunctorT is held inline iff @b all three conditions hold:
 * @c sizeof(FunctorT) is no greater than @c SboSizeT, @c alignof(FunctorT)
 * is no greater than @c alignof(std::max_align_t), and @c FunctorT is nothrow move-constructible.
 * Otherwise @c FunctorT is allocated through @c vlink::MemoryPool::global_instance();
 * the original @c sizeof(FunctorT) and @c alignof(FunctorT) are passed back to
 * @c deallocate so the block routes to its source tier. This predicate
 * matches the @c __stored_locally rule used by libstdc++
 * @c std::move_only_function.
 *
 * @par SBO sizing
 * Both wrappers expose a second non-type template parameter @c SboSizeT that
 * selects the inline storage budget at compile time:
 * @code
 * vlink::Function<void()>                // default 64-byte SBO
 * vlink::Function<void(), 128>           // 128-byte SBO
 * vlink::MoveFunction<void(), 256>       // 256-byte SBO -- explicit form
 * vlink::LargeFunction<void()>           // alias for Function<void(), 256>
 * vlink::LargeMoveFunction<void()>       // alias for MoveFunction<void(), 256>
 * @endcode
 * The 64-byte default intentionally exceeds the typical @c std::function SBO
 * (~16-24 B) so common VLink capture sets (a handful of @c shared_ptr /
 * pointers / small POD) stay inline.  Enlarge the budget when capturing
 * heavyweight closures (e.g. @c std::array<shared_ptr<T>, N>, multiple
 * @c std::string captures by value) to avoid the @c MemoryPool roundtrip.
 * @c SboSizeT must be at least @c sizeof(void*) so the heap-fallback pointer
 * fits inside the storage; this is enforced by a @c static_assert.
 *
 * @par Type identity per SboSizeT
 * @c Function<Sig, X> and @c Function<Sig, Y> are distinct types, but
 * copyable @c Function wrappers remain constructible and assignable across
 * SBO sizes through the generic functor path: a @c Function<Sig, 64> can be
 * used as the source for @c Function<Sig, 256>, which wraps it as a regular
 * callable.  @c MoveFunction follows the same pattern for movable sources.
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
 * - @c LargeFunction / @c LargeMoveFunction are convenience aliases for
 *   @c Function<Sig, 256> / @c MoveFunction<Sig, 256>.
 * - This header self-defines @c VLINK_ENABLE_BASE_FUNCTIONAL when the macro is
 *   absent, so normal builds always use the VLink SBO + @c MemoryPool
 *   implementation.  The standard-library alias block below is not a public
 *   compile-command opt-out and is not selected by passing
 *   @c -UVLINK_ENABLE_BASE_FUNCTIONAL.
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

[[maybe_unused]] static constexpr bool kIsSupportMoveFunction = true;

template <typename SignatureT, size_t SboSizeT = 64U>
class Function;

template <typename SignatureT, size_t SboSizeT = 64U>
class MoveFunction;

template <typename SignatureT>
using LargeFunction = Function<SignatureT, 256U>;

template <typename SignatureT>
using LargeMoveFunction = MoveFunction<SignatureT, 256U>;

template <typename SignatureT>
using function = Function<SignatureT>;

template <typename SignatureT>
using move_only_function = MoveFunction<SignatureT>;

namespace detail {

template <typename TypeT>
struct IsStdFunction : std::false_type {};

template <typename SignatureT>
struct IsStdFunction<std::function<SignatureT>> : std::true_type {};

template <typename TypeT>
struct IsVlinkFunction : std::false_type {};

template <typename SignatureT, size_t SboSizeT>
struct IsVlinkFunction<Function<SignatureT, SboSizeT>> : std::true_type {};

template <typename TypeT>
struct IsVlinkMoveFunction : std::false_type {};

template <typename SignatureT, size_t SboSizeT>
struct IsVlinkMoveFunction<MoveFunction<SignatureT, SboSizeT>> : std::true_type {};

#if defined(__cpp_lib_move_only_function) && __cpp_lib_move_only_function >= 202110L
template <typename TypeT>
struct IsStdMoveOnlyFunction : std::false_type {};

template <typename SignatureT>
struct IsStdMoveOnlyFunction<std::move_only_function<SignatureT>> : std::true_type {};
#endif

[[noreturn]] VLINK_EXPORT void throw_bad_function_call();

}  // namespace detail

/**
 * @class Function
 * @brief Copyable type-erased callable wrapper -- @c std::function analogue
 *        with a configurable SBO (default 64 bytes) and a
 *        @c vlink::MemoryPool-backed heap fallback.
 *
 * @details
 * @c Function<ReturnT(ArgsT...), SboSizeT> stores any copy-constructible
 * callable that is invocable as @c ReturnT(ArgsT...).  The @c SboSizeT
 * non-type template argument selects the inline storage budget; default
 * @c Function<Sig> uses 64 bytes, @c Function<Sig, 256> (or the
 * @c LargeFunction alias) keeps any 256-byte-or-smaller functor inline.
 * The observable contract mirrors
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
template <typename ReturnT, typename... ArgsT, size_t SboSizeT>
class Function<ReturnT(ArgsT...), SboSizeT> {
  static_assert(SboSizeT >= sizeof(void*),
                "Function: SboSizeT must be at least sizeof(void*) so the heap-fallback "
                "pointer can fit in the inline storage.");

 public:
  /**
   * @brief Inline storage budget before falling back to @c MemoryPool.
   *
   * @details
   * Equal to the @c SboSizeT template argument.  Default-instantiated
   * @c Function<Sig> uses 64 bytes; override via @c Function<Sig, N> to enlarge
   * (e.g. @c Function<void(), 256> keeps 256-byte functors inline).
   */
  static constexpr size_t kSboSize = SboSizeT;

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
  template <typename FunctorT, typename DecayFunctorT = std::decay_t<FunctorT>,
            // NOLINTNEXTLINE(modernize-use-constraints)
            typename = std::enable_if_t<std::conjunction_v<std::negation<std::is_same<DecayFunctorT, Function>>,
                                                           std::negation<std::is_same<DecayFunctorT, std::nullptr_t>>,
                                                           std::is_invocable_r<ReturnT, DecayFunctorT&, ArgsT...>,
                                                           std::is_copy_constructible<DecayFunctorT>>>>
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
  template <typename FunctorT, typename DecayFunctorT = std::decay_t<FunctorT>,
            // NOLINTNEXTLINE(modernize-use-constraints)
            typename = std::enable_if_t<std::conjunction_v<std::negation<std::is_same<DecayFunctorT, Function>>,
                                                           std::negation<std::is_same<DecayFunctorT, std::nullptr_t>>,
                                                           std::is_invocable_r<ReturnT, DecayFunctorT&, ArgsT...>,
                                                           std::is_copy_constructible<DecayFunctorT>>>>
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

  alignas(std::max_align_t) std::byte storage_[SboSizeT]{};

  const VTable* vtable_{nullptr};
};

/**
 * @brief Swaps two function wrappers.
 */
template <typename ReturnT, typename... ArgsT, size_t SboSizeT>
void swap(Function<ReturnT(ArgsT...), SboSizeT>& lhs, Function<ReturnT(ArgsT...), SboSizeT>& rhs) noexcept;

/**
 * @brief Returns whether @p cb is empty.
 */
template <typename ReturnT, typename... ArgsT, size_t SboSizeT>
bool operator==(const Function<ReturnT(ArgsT...), SboSizeT>& cb, std::nullptr_t) noexcept;

/**
 * @brief Returns whether @p cb is empty.
 */
template <typename ReturnT, typename... ArgsT, size_t SboSizeT>
bool operator==(std::nullptr_t, const Function<ReturnT(ArgsT...), SboSizeT>& cb) noexcept;

/**
 * @brief Returns whether @p cb stores a callable target.
 */
template <typename ReturnT, typename... ArgsT, size_t SboSizeT>
bool operator!=(const Function<ReturnT(ArgsT...), SboSizeT>& cb, std::nullptr_t) noexcept;

/**
 * @brief Returns whether @p cb stores a callable target.
 */
template <typename ReturnT, typename... ArgsT, size_t SboSizeT>
bool operator!=(std::nullptr_t, const Function<ReturnT(ArgsT...), SboSizeT>& cb) noexcept;

/**
 * @class MoveFunction
 * @brief Move-only type-erased callable wrapper -- @c std::move_only_function
 *        analogue sharing the configurable SBO and @c MemoryPool fallback used
 *        by @c Function (default 64 bytes; pick a wider budget via the
 *        @c SboSizeT template argument or the @c LargeMoveFunction alias).
 *
 * @details
 * @c MoveFunction<ReturnT(ArgsT...), SboSizeT> stores any move-constructible
 * callable that is invocable as @c ReturnT(ArgsT...).  As with @c Function,
 * the @c SboSizeT non-type template parameter selects the inline storage
 * budget; @c MoveFunction<Sig, 256> (or @c LargeMoveFunction<Sig>) keeps any
 * 256-byte-or-smaller functor inline.  It mirrors @c std::move_only_function
 * with two deliberate divergences and one extension:
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
template <typename ReturnT, typename... ArgsT, size_t SboSizeT>
class MoveFunction<ReturnT(ArgsT...), SboSizeT> {
  static_assert(SboSizeT >= sizeof(void*),
                "MoveFunction: SboSizeT must be at least sizeof(void*) so the "
                "heap-fallback pointer can fit in the inline storage.");

 public:
  /**
   * @brief Inline storage budget before falling back to @c MemoryPool.
   *
   * @details
   * Equal to the @c SboSizeT template argument.  Default-instantiated
   * @c MoveFunction<Sig> uses 64 bytes; override via @c MoveFunction<Sig, N>
   * to enlarge (e.g. @c MoveFunction<void(), 256> keeps 256-byte functors
   * inline).
   */
  static constexpr size_t kSboSize = SboSizeT;

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
            // NOLINTNEXTLINE(modernize-use-constraints)
            typename = std::enable_if_t<std::conjunction_v<std::negation<std::is_same<DecayFunctorT, MoveFunction>>,
                                                           std::negation<std::is_same<DecayFunctorT, std::nullptr_t>>,
                                                           std::is_invocable_r<ReturnT, DecayFunctorT&, ArgsT...>,
                                                           std::is_constructible<DecayFunctorT, FunctorT>,
                                                           std::is_move_constructible<DecayFunctorT>>>>
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
            // NOLINTNEXTLINE(modernize-use-constraints)
            typename = std::enable_if_t<std::conjunction_v<std::negation<std::is_same<DecayFunctorT, MoveFunction>>,
                                                           std::negation<std::is_same<DecayFunctorT, std::nullptr_t>>,
                                                           std::is_invocable_r<ReturnT, DecayFunctorT&, ArgsT...>,
                                                           std::is_constructible<DecayFunctorT, FunctorT>,
                                                           std::is_move_constructible<DecayFunctorT>>>>
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

  alignas(std::max_align_t) std::byte storage_[SboSizeT]{};

  const VTable* vtable_{nullptr};
};

/**
 * @brief Swaps two move-only function wrappers.
 */
template <typename ReturnT, typename... ArgsT, size_t SboSizeT>
void swap(MoveFunction<ReturnT(ArgsT...), SboSizeT>& lhs, MoveFunction<ReturnT(ArgsT...), SboSizeT>& rhs) noexcept;

/**
 * @brief Returns whether @p cb is empty.
 */
template <typename ReturnT, typename... ArgsT, size_t SboSizeT>
bool operator==(const MoveFunction<ReturnT(ArgsT...), SboSizeT>& cb, std::nullptr_t) noexcept;

/**
 * @brief Returns whether @p cb is empty.
 */
template <typename ReturnT, typename... ArgsT, size_t SboSizeT>
bool operator==(std::nullptr_t, const MoveFunction<ReturnT(ArgsT...), SboSizeT>& cb) noexcept;

/**
 * @brief Returns whether @p cb stores a callable target.
 */
template <typename ReturnT, typename... ArgsT, size_t SboSizeT>
bool operator!=(const MoveFunction<ReturnT(ArgsT...), SboSizeT>& cb, std::nullptr_t) noexcept;

/**
 * @brief Returns whether @p cb stores a callable target.
 */
template <typename ReturnT, typename... ArgsT, size_t SboSizeT>
bool operator!=(std::nullptr_t, const MoveFunction<ReturnT(ArgsT...), SboSizeT>& cb) noexcept;

////////////////////////////////////////////////////////////////
/// Details
////////////////////////////////////////////////////////////////

template <typename ReturnT, typename... ArgsT, size_t SboSizeT>
inline Function<ReturnT(ArgsT...), SboSizeT>::Function(std::nullptr_t) noexcept {}

template <typename ReturnT, typename... ArgsT, size_t SboSizeT>
inline Function<ReturnT(ArgsT...), SboSizeT>::Function(const Function& other) {
  copy_from(other);
}

template <typename ReturnT, typename... ArgsT, size_t SboSizeT>
inline Function<ReturnT(ArgsT...), SboSizeT>::Function(Function&& other) noexcept {
  move_from(std::move(other));
}

template <typename ReturnT, typename... ArgsT, size_t SboSizeT>
template <typename FunctorT, typename DecayFunctorT, typename>
inline Function<ReturnT(ArgsT...), SboSizeT>::Function(FunctorT&& f) {
  if constexpr (kIsFunctionWrapper<DecayFunctorT>) {
    if VUNLIKELY (!f) {
      return;
    }
  } else if constexpr (kIsPointerLike<DecayFunctorT>) {
    if constexpr (std::is_pointer_v<std::remove_reference_t<FunctorT>> ||
                  std::is_member_pointer_v<std::remove_reference_t<FunctorT>>) {
      if VUNLIKELY (f == nullptr) {
        return;
      }
    }
  }

  construct_from<DecayFunctorT>(std::forward<FunctorT>(f));
}

template <typename ReturnT, typename... ArgsT, size_t SboSizeT>
// NOLINTNEXTLINE(bugprone-unhandled-self-assignment)
inline Function<ReturnT(ArgsT...), SboSizeT>& Function<ReturnT(ArgsT...), SboSizeT>::operator=(const Function& other) {
  if VLIKELY (this != &other) {
    Function tmp(other);
    swap(tmp);
  }

  return *this;
}

template <typename ReturnT, typename... ArgsT, size_t SboSizeT>
inline Function<ReturnT(ArgsT...), SboSizeT>& Function<ReturnT(ArgsT...), SboSizeT>::operator=(
    Function&& other) noexcept {
  if VLIKELY (this != &other) {
    reset();
    move_from(std::move(other));
  }

  return *this;
}

template <typename ReturnT, typename... ArgsT, size_t SboSizeT>
inline Function<ReturnT(ArgsT...), SboSizeT>& Function<ReturnT(ArgsT...), SboSizeT>::operator=(
    std::nullptr_t) noexcept {
  reset();
  return *this;
}

template <typename ReturnT, typename... ArgsT, size_t SboSizeT>
template <typename FunctorT, typename DecayFunctorT, typename>
inline Function<ReturnT(ArgsT...), SboSizeT>& Function<ReturnT(ArgsT...), SboSizeT>::operator=(FunctorT&& f) {
  Function tmp(std::forward<FunctorT>(f));
  swap(tmp);
  return *this;
}

template <typename ReturnT, typename... ArgsT, size_t SboSizeT>
inline Function<ReturnT(ArgsT...), SboSizeT>::~Function() {
  reset();
}

template <typename ReturnT, typename... ArgsT, size_t SboSizeT>
inline ReturnT Function<ReturnT(ArgsT...), SboSizeT>::operator()(ArgsT... args) const {
  if VUNLIKELY (vtable_ == nullptr) {
    detail::throw_bad_function_call();
  }

  return vtable_->invoke(&storage_, std::forward<ArgsT>(args)...);
}

template <typename ReturnT, typename... ArgsT, size_t SboSizeT>
inline Function<ReturnT(ArgsT...), SboSizeT>::operator bool() const noexcept {
  return vtable_ != nullptr;
}

#if defined(__cpp_rtti)
template <typename ReturnT, typename... ArgsT, size_t SboSizeT>
inline const std::type_info& Function<ReturnT(ArgsT...), SboSizeT>::target_type() const noexcept {
  if VLIKELY (vtable_ != nullptr) {
    return vtable_->target_type();
  }

  return typeid(void);
}

template <typename ReturnT, typename... ArgsT, size_t SboSizeT>
template <typename FunctorT>
inline FunctorT* Function<ReturnT(ArgsT...), SboSizeT>::target() noexcept {
  if constexpr (std::is_object_v<FunctorT>) {
    if VLIKELY (vtable_ != nullptr && typeid(FunctorT) == target_type()) {
      return static_cast<FunctorT*>(vtable_->target(&storage_));
    }
  }

  return nullptr;
}

template <typename ReturnT, typename... ArgsT, size_t SboSizeT>
template <typename FunctorT>
inline const FunctorT* Function<ReturnT(ArgsT...), SboSizeT>::target() const noexcept {
  if constexpr (std::is_object_v<FunctorT>) {
    if VLIKELY (vtable_ != nullptr && typeid(FunctorT) == target_type()) {
      return static_cast<const FunctorT*>(vtable_->target_const(&storage_));
    }
  }

  return nullptr;
}
#endif

template <typename ReturnT, typename... ArgsT, size_t SboSizeT>
inline void Function<ReturnT(ArgsT...), SboSizeT>::swap(Function& other) noexcept {
  if VUNLIKELY (this == &other) {
    return;
  }

  Function tmp(std::move(*this));
  *this = std::move(other);
  other = std::move(tmp);
}

template <typename ReturnT, typename... ArgsT, size_t SboSizeT>
template <typename FunctorT>
inline ReturnT Function<ReturnT(ArgsT...), SboSizeT>::InlineVTable<FunctorT>::invoke(const void* storage,
                                                                                     ArgsT... args) {
  auto* f = std::launder(reinterpret_cast<FunctorT*>(const_cast<void*>(storage)));

  if constexpr (std::is_void_v<ReturnT>) {
    std::invoke(*f, std::forward<ArgsT>(args)...);
  } else {
    return std::invoke(*f, std::forward<ArgsT>(args)...);
  }
}

template <typename ReturnT, typename... ArgsT, size_t SboSizeT>
template <typename FunctorT>
inline void Function<ReturnT(ArgsT...), SboSizeT>::InlineVTable<FunctorT>::copy_construct(void* dst, const void* src) {
  const auto* src_f = std::launder(reinterpret_cast<const FunctorT*>(src));
  ::new (dst) FunctorT(*src_f);
}

template <typename ReturnT, typename... ArgsT, size_t SboSizeT>
template <typename FunctorT>
inline void Function<ReturnT(ArgsT...), SboSizeT>::InlineVTable<FunctorT>::move_construct(void* dst,
                                                                                          void* src) noexcept {
  auto* src_f = std::launder(reinterpret_cast<FunctorT*>(src));
  ::new (dst) FunctorT(std::move(*src_f));
  src_f->~FunctorT();
}

template <typename ReturnT, typename... ArgsT, size_t SboSizeT>
template <typename FunctorT>
inline void Function<ReturnT(ArgsT...), SboSizeT>::InlineVTable<FunctorT>::destroy(void* storage) noexcept {
  auto* f = std::launder(reinterpret_cast<FunctorT*>(storage));
  f->~FunctorT();
}

#if defined(__cpp_rtti)
template <typename ReturnT, typename... ArgsT, size_t SboSizeT>
template <typename FunctorT>
inline const std::type_info& Function<ReturnT(ArgsT...), SboSizeT>::InlineVTable<FunctorT>::target_type() noexcept {
  return typeid(FunctorT);
}

template <typename ReturnT, typename... ArgsT, size_t SboSizeT>
template <typename FunctorT>
inline void* Function<ReturnT(ArgsT...), SboSizeT>::InlineVTable<FunctorT>::target(void* storage) noexcept {
  return storage;
}

template <typename ReturnT, typename... ArgsT, size_t SboSizeT>
template <typename FunctorT>
inline const void* Function<ReturnT(ArgsT...), SboSizeT>::InlineVTable<FunctorT>::target_const(
    const void* storage) noexcept {
  return storage;
}
#endif

template <typename ReturnT, typename... ArgsT, size_t SboSizeT>
template <typename FunctorT>
inline ReturnT Function<ReturnT(ArgsT...), SboSizeT>::HeapVTable<FunctorT>::invoke(const void* storage, ArgsT... args) {
  FunctorT* f = *std::launder(static_cast<FunctorT* const*>(storage));

  if constexpr (std::is_void_v<ReturnT>) {
    std::invoke(*f, std::forward<ArgsT>(args)...);
  } else {
    return std::invoke(*f, std::forward<ArgsT>(args)...);
  }
}

template <typename ReturnT, typename... ArgsT, size_t SboSizeT>
template <typename FunctorT>
inline void Function<ReturnT(ArgsT...), SboSizeT>::HeapVTable<FunctorT>::copy_construct(void* dst, const void* src) {
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

template <typename ReturnT, typename... ArgsT, size_t SboSizeT>
template <typename FunctorT>
inline void Function<ReturnT(ArgsT...), SboSizeT>::HeapVTable<FunctorT>::move_construct(void* dst, void* src) noexcept {
  FunctorT** src_slot = std::launder(static_cast<FunctorT**>(src));
  FunctorT* src_f = *src_slot;
  ::new (dst) FunctorT*(src_f);
  *src_slot = nullptr;
}

template <typename ReturnT, typename... ArgsT, size_t SboSizeT>
template <typename FunctorT>
inline void Function<ReturnT(ArgsT...), SboSizeT>::HeapVTable<FunctorT>::destroy(void* storage) noexcept {
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
template <typename ReturnT, typename... ArgsT, size_t SboSizeT>
template <typename FunctorT>
inline const std::type_info& Function<ReturnT(ArgsT...), SboSizeT>::HeapVTable<FunctorT>::target_type() noexcept {
  return typeid(FunctorT);
}

template <typename ReturnT, typename... ArgsT, size_t SboSizeT>
template <typename FunctorT>
inline void* Function<ReturnT(ArgsT...), SboSizeT>::HeapVTable<FunctorT>::target(void* storage) noexcept {
  return *std::launder(static_cast<FunctorT**>(storage));
}

template <typename ReturnT, typename... ArgsT, size_t SboSizeT>
template <typename FunctorT>
inline const void* Function<ReturnT(ArgsT...), SboSizeT>::HeapVTable<FunctorT>::target_const(
    const void* storage) noexcept {
  return *std::launder(static_cast<FunctorT* const*>(storage));
}
#endif

template <typename ReturnT, typename... ArgsT, size_t SboSizeT>
template <typename FunctorT>
inline const typename Function<ReturnT(ArgsT...), SboSizeT>::VTable*
Function<ReturnT(ArgsT...), SboSizeT>::get_vtable() noexcept {
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

template <typename ReturnT, typename... ArgsT, size_t SboSizeT>
template <typename FunctorT, typename SourceT>
inline void Function<ReturnT(ArgsT...), SboSizeT>::construct_from(SourceT&& src) {
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

template <typename ReturnT, typename... ArgsT, size_t SboSizeT>
inline void Function<ReturnT(ArgsT...), SboSizeT>::copy_from(const Function& other) {
  if VLIKELY (other.vtable_ != nullptr) {
    other.vtable_->copy_construct(&storage_, &other.storage_);
    vtable_ = other.vtable_;
  }
}

template <typename ReturnT, typename... ArgsT, size_t SboSizeT>
inline void Function<ReturnT(ArgsT...), SboSizeT>::move_from(Function&& other) noexcept {
  if VLIKELY (other.vtable_ != nullptr) {
    other.vtable_->move_construct(&storage_, &other.storage_);
    vtable_ = other.vtable_;
    other.vtable_ = nullptr;
  }
}

template <typename ReturnT, typename... ArgsT, size_t SboSizeT>
inline void Function<ReturnT(ArgsT...), SboSizeT>::reset() noexcept {
  if VLIKELY (vtable_ != nullptr) {
    vtable_->destroy(&storage_);
    vtable_ = nullptr;
  }
}

template <typename ReturnT, typename... ArgsT, size_t SboSizeT>
inline void swap(Function<ReturnT(ArgsT...), SboSizeT>& lhs, Function<ReturnT(ArgsT...), SboSizeT>& rhs) noexcept {
  lhs.swap(rhs);
}

template <typename ReturnT, typename... ArgsT, size_t SboSizeT>
inline bool operator==(const Function<ReturnT(ArgsT...), SboSizeT>& cb, std::nullptr_t) noexcept {
  return !cb;
}

template <typename ReturnT, typename... ArgsT, size_t SboSizeT>
inline bool operator==(std::nullptr_t, const Function<ReturnT(ArgsT...), SboSizeT>& cb) noexcept {
  return !cb;
}

template <typename ReturnT, typename... ArgsT, size_t SboSizeT>
inline bool operator!=(const Function<ReturnT(ArgsT...), SboSizeT>& cb, std::nullptr_t) noexcept {
  return static_cast<bool>(cb);
}

template <typename ReturnT, typename... ArgsT, size_t SboSizeT>
inline bool operator!=(std::nullptr_t, const Function<ReturnT(ArgsT...), SboSizeT>& cb) noexcept {
  return static_cast<bool>(cb);
}

template <typename ReturnT, typename... ArgsT, size_t SboSizeT>
inline MoveFunction<ReturnT(ArgsT...), SboSizeT>::MoveFunction(std::nullptr_t) noexcept {}

template <typename ReturnT, typename... ArgsT, size_t SboSizeT>
inline MoveFunction<ReturnT(ArgsT...), SboSizeT>::MoveFunction(MoveFunction&& other) noexcept {
  move_from(std::move(other));
}

template <typename ReturnT, typename... ArgsT, size_t SboSizeT>
template <typename FunctorT, typename DecayFunctorT, typename>
inline MoveFunction<ReturnT(ArgsT...), SboSizeT>::MoveFunction(FunctorT&& f) {
  if constexpr (kIsFunctionWrapper<DecayFunctorT>) {
    if VUNLIKELY (!f) {
      return;
    }
  } else if constexpr (kIsPointerLike<DecayFunctorT>) {
    if constexpr (std::is_pointer_v<std::remove_reference_t<FunctorT>> ||
                  std::is_member_pointer_v<std::remove_reference_t<FunctorT>>) {
      if VUNLIKELY (f == nullptr) {
        return;
      }
    }
  }

  construct_from<DecayFunctorT>(std::forward<FunctorT>(f));
}

template <typename ReturnT, typename... ArgsT, size_t SboSizeT>
inline MoveFunction<ReturnT(ArgsT...), SboSizeT>& MoveFunction<ReturnT(ArgsT...), SboSizeT>::operator=(
    MoveFunction&& other) noexcept {
  if VLIKELY (this != &other) {
    reset();
    move_from(std::move(other));
  }

  return *this;
}

template <typename ReturnT, typename... ArgsT, size_t SboSizeT>
inline MoveFunction<ReturnT(ArgsT...), SboSizeT>& MoveFunction<ReturnT(ArgsT...), SboSizeT>::operator=(
    std::nullptr_t) noexcept {
  reset();
  return *this;
}

template <typename ReturnT, typename... ArgsT, size_t SboSizeT>
template <typename FunctorT, typename DecayFunctorT, typename>
inline MoveFunction<ReturnT(ArgsT...), SboSizeT>& MoveFunction<ReturnT(ArgsT...), SboSizeT>::operator=(FunctorT&& f) {
  MoveFunction tmp(std::forward<FunctorT>(f));
  swap(tmp);
  return *this;
}

template <typename ReturnT, typename... ArgsT, size_t SboSizeT>
inline MoveFunction<ReturnT(ArgsT...), SboSizeT>::~MoveFunction() {
  reset();
}

template <typename ReturnT, typename... ArgsT, size_t SboSizeT>
inline ReturnT MoveFunction<ReturnT(ArgsT...), SboSizeT>::operator()(ArgsT... args) {
  if VUNLIKELY (vtable_ == nullptr) {
    detail::throw_bad_function_call();
  }

  return vtable_->invoke(&storage_, std::forward<ArgsT>(args)...);
}

template <typename ReturnT, typename... ArgsT, size_t SboSizeT>
inline MoveFunction<ReturnT(ArgsT...), SboSizeT>::operator bool() const noexcept {
  return vtable_ != nullptr;
}

#if defined(__cpp_rtti)
template <typename ReturnT, typename... ArgsT, size_t SboSizeT>
inline const std::type_info& MoveFunction<ReturnT(ArgsT...), SboSizeT>::target_type() const noexcept {
  if VLIKELY (vtable_ != nullptr) {
    return vtable_->target_type();
  }

  return typeid(void);
}

template <typename ReturnT, typename... ArgsT, size_t SboSizeT>
template <typename FunctorT>
inline FunctorT* MoveFunction<ReturnT(ArgsT...), SboSizeT>::target() noexcept {
  if constexpr (std::is_object_v<FunctorT>) {
    if VLIKELY (vtable_ != nullptr && typeid(FunctorT) == target_type()) {
      return static_cast<FunctorT*>(vtable_->target(&storage_));
    }
  }

  return nullptr;
}

template <typename ReturnT, typename... ArgsT, size_t SboSizeT>
template <typename FunctorT>
inline const FunctorT* MoveFunction<ReturnT(ArgsT...), SboSizeT>::target() const noexcept {
  if constexpr (std::is_object_v<FunctorT>) {
    if VLIKELY (vtable_ != nullptr && typeid(FunctorT) == target_type()) {
      return static_cast<const FunctorT*>(vtable_->target_const(&storage_));
    }
  }

  return nullptr;
}
#endif

template <typename ReturnT, typename... ArgsT, size_t SboSizeT>
inline void MoveFunction<ReturnT(ArgsT...), SboSizeT>::swap(MoveFunction& other) noexcept {
  if VUNLIKELY (this == &other) {
    return;
  }

  MoveFunction tmp(std::move(*this));
  *this = std::move(other);
  other = std::move(tmp);
}

template <typename ReturnT, typename... ArgsT, size_t SboSizeT>
template <typename FunctorT>
inline ReturnT MoveFunction<ReturnT(ArgsT...), SboSizeT>::InlineVTable<FunctorT>::invoke(void* storage, ArgsT... args) {
  auto* f = std::launder(reinterpret_cast<FunctorT*>(storage));

  if constexpr (std::is_void_v<ReturnT>) {
    std::invoke(*f, std::forward<ArgsT>(args)...);
  } else {
    return std::invoke(*f, std::forward<ArgsT>(args)...);
  }
}

template <typename ReturnT, typename... ArgsT, size_t SboSizeT>
template <typename FunctorT>
inline void MoveFunction<ReturnT(ArgsT...), SboSizeT>::InlineVTable<FunctorT>::move_construct(void* dst,
                                                                                              void* src) noexcept {
  auto* src_f = std::launder(reinterpret_cast<FunctorT*>(src));
  ::new (dst) FunctorT(std::move(*src_f));
  src_f->~FunctorT();
}

template <typename ReturnT, typename... ArgsT, size_t SboSizeT>
template <typename FunctorT>
inline void MoveFunction<ReturnT(ArgsT...), SboSizeT>::InlineVTable<FunctorT>::destroy(void* storage) noexcept {
  auto* f = std::launder(reinterpret_cast<FunctorT*>(storage));
  f->~FunctorT();
}

#if defined(__cpp_rtti)
template <typename ReturnT, typename... ArgsT, size_t SboSizeT>
template <typename FunctorT>
inline const std::type_info& MoveFunction<ReturnT(ArgsT...), SboSizeT>::InlineVTable<FunctorT>::target_type() noexcept {
  return typeid(FunctorT);
}

template <typename ReturnT, typename... ArgsT, size_t SboSizeT>
template <typename FunctorT>
inline void* MoveFunction<ReturnT(ArgsT...), SboSizeT>::InlineVTable<FunctorT>::target(void* storage) noexcept {
  return storage;
}

template <typename ReturnT, typename... ArgsT, size_t SboSizeT>
template <typename FunctorT>
inline const void* MoveFunction<ReturnT(ArgsT...), SboSizeT>::InlineVTable<FunctorT>::target_const(
    const void* storage) noexcept {
  return storage;
}
#endif

template <typename ReturnT, typename... ArgsT, size_t SboSizeT>
template <typename FunctorT>
inline ReturnT MoveFunction<ReturnT(ArgsT...), SboSizeT>::HeapVTable<FunctorT>::invoke(void* storage, ArgsT... args) {
  FunctorT* f = *std::launder(static_cast<FunctorT**>(storage));

  if constexpr (std::is_void_v<ReturnT>) {
    std::invoke(*f, std::forward<ArgsT>(args)...);
  } else {
    return std::invoke(*f, std::forward<ArgsT>(args)...);
  }
}

template <typename ReturnT, typename... ArgsT, size_t SboSizeT>
template <typename FunctorT>
inline void MoveFunction<ReturnT(ArgsT...), SboSizeT>::HeapVTable<FunctorT>::move_construct(void* dst,
                                                                                            void* src) noexcept {
  FunctorT** src_slot = std::launder(static_cast<FunctorT**>(src));
  FunctorT* src_f = *src_slot;
  ::new (dst) FunctorT*(src_f);
  *src_slot = nullptr;
}

template <typename ReturnT, typename... ArgsT, size_t SboSizeT>
template <typename FunctorT>
inline void MoveFunction<ReturnT(ArgsT...), SboSizeT>::HeapVTable<FunctorT>::destroy(void* storage) noexcept {
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
template <typename ReturnT, typename... ArgsT, size_t SboSizeT>
template <typename FunctorT>
inline const std::type_info& MoveFunction<ReturnT(ArgsT...), SboSizeT>::HeapVTable<FunctorT>::target_type() noexcept {
  return typeid(FunctorT);
}

template <typename ReturnT, typename... ArgsT, size_t SboSizeT>
template <typename FunctorT>
inline void* MoveFunction<ReturnT(ArgsT...), SboSizeT>::HeapVTable<FunctorT>::target(void* storage) noexcept {
  return *std::launder(static_cast<FunctorT**>(storage));
}

template <typename ReturnT, typename... ArgsT, size_t SboSizeT>
template <typename FunctorT>
inline const void* MoveFunction<ReturnT(ArgsT...), SboSizeT>::HeapVTable<FunctorT>::target_const(
    const void* storage) noexcept {
  return *std::launder(static_cast<FunctorT* const*>(storage));
}
#endif

template <typename ReturnT, typename... ArgsT, size_t SboSizeT>
template <typename FunctorT>
inline const typename MoveFunction<ReturnT(ArgsT...), SboSizeT>::VTable*
MoveFunction<ReturnT(ArgsT...), SboSizeT>::get_vtable() noexcept {
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

template <typename ReturnT, typename... ArgsT, size_t SboSizeT>
template <typename FunctorT, typename SourceT>
inline void MoveFunction<ReturnT(ArgsT...), SboSizeT>::construct_from(SourceT&& src) {
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

template <typename ReturnT, typename... ArgsT, size_t SboSizeT>
inline void MoveFunction<ReturnT(ArgsT...), SboSizeT>::move_from(MoveFunction&& other) noexcept {
  if VLIKELY (other.vtable_ != nullptr) {
    other.vtable_->move_construct(&storage_, &other.storage_);
    vtable_ = other.vtable_;
    other.vtable_ = nullptr;
  }
}

template <typename ReturnT, typename... ArgsT, size_t SboSizeT>
inline void MoveFunction<ReturnT(ArgsT...), SboSizeT>::reset() noexcept {
  if VLIKELY (vtable_ != nullptr) {
    vtable_->destroy(&storage_);
    vtable_ = nullptr;
  }
}

template <typename ReturnT, typename... ArgsT, size_t SboSizeT>
inline void swap(MoveFunction<ReturnT(ArgsT...), SboSizeT>& lhs,
                 MoveFunction<ReturnT(ArgsT...), SboSizeT>& rhs) noexcept {
  lhs.swap(rhs);
}

template <typename ReturnT, typename... ArgsT, size_t SboSizeT>
inline bool operator==(const MoveFunction<ReturnT(ArgsT...), SboSizeT>& cb, std::nullptr_t) noexcept {
  return !cb;
}

template <typename ReturnT, typename... ArgsT, size_t SboSizeT>
inline bool operator==(std::nullptr_t, const MoveFunction<ReturnT(ArgsT...), SboSizeT>& cb) noexcept {
  return !cb;
}

template <typename ReturnT, typename... ArgsT, size_t SboSizeT>
inline bool operator!=(const MoveFunction<ReturnT(ArgsT...), SboSizeT>& cb, std::nullptr_t) noexcept {
  return static_cast<bool>(cb);
}

template <typename ReturnT, typename... ArgsT, size_t SboSizeT>
inline bool operator!=(std::nullptr_t, const MoveFunction<ReturnT(ArgsT...), SboSizeT>& cb) noexcept {
  return static_cast<bool>(cb);
}

}  // namespace vlink

#else

namespace vlink {

template <typename SignatureT>
using Function = std::function<SignatureT>;

template <typename SignatureT>
using LargeFunction = std::function<SignatureT>;

template <typename SignatureT>
using function = std::function<SignatureT>;

#if defined(__cpp_lib_move_only_function) && __cpp_lib_move_only_function >= 202110L
[[maybe_unused]] static constexpr bool kIsSupportMoveFunction = true;
template <typename SignatureT>
using MoveFunction = std::move_only_function<SignatureT>;

template <typename SignatureT>
using LargeMoveFunction = std::move_only_function<SignatureT>;

template <typename SignatureT>
using move_only_function = std::move_only_function<SignatureT>;
#else
[[maybe_unused]] static constexpr bool kIsSupportMoveFunction = false;
template <typename SignatureT>
using MoveFunction = std::function<SignatureT>;

template <typename SignatureT>
using LargeMoveFunction = std::function<SignatureT>;

template <typename SignatureT>
using move_only_function = std::function<SignatureT>;
#endif

}  // namespace vlink

#endif
