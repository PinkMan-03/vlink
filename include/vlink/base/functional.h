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
 * @brief Type-erased callable wrapper with a 64 byte small-buffer optimisation backed
 *        by @c vlink::MemoryPool for the heap-fallback path.
 *
 * @details
 * @c vlink::Function<R(Args...)> is a drop-in @c std::function -style wrapper tuned
 * for VLink hot paths.  Three design choices shape its behaviour:
 *
 * - @b SBO @b size : @c kSboSize is fixed at @c 64 bytes (one cache line), large
 *   enough to host most realistic capture sets (a handful of @c shared_ptr , a few
 *   pointers, a few small structs) without ever calling into the heap.  @c sizeof
 *   @c (Function) lands at ~72 bytes which is bigger than @c std::function but still
 *   tight enough for typical task-queue use.
 *
 * - @b std::function @b interop : because the converting constructor accepts any
 *   callable that meets @c is_invocable_r , a @c std::function value can be wrapped
 *   into a @c Function (and vice-versa) without explicit casts.  Direct assignment in
 *   either direction is supported.
 *
 * - @b Pooled @b heap : when the target does not fit inline, the storage is allocated
 *   from @c vlink::MemoryPool::global_instance() and released back to the same pool.
 *   This avoids stepping on @c ::operator @c new / @c ::operator @c delete on the hot
 *   path and lets the application benefit from VLink's tiered free-list reuse.
 *
 * Storage layout (per instance):
 * - 64 byte aligned inline buffer (@c kSboSize).
 * - One @c VTable pointer (4 functions: invoke / copy / move / destroy).
 *
 * Performance notes:
 * - Invocation is one indirect call through the cached vtable pointer.  Inline targets
 *   incur zero pointer chasing beyond the vtable; pooled targets add one extra load.
 * - Heap fallback is used only when the target's @c sizeof exceeds @c kSboSize, when
 *   its @c alignof exceeds @c alignof(std::max_align_t) , or when its move constructor
 *   may throw.
 * - The vtable is a per-target singleton instantiated once per @c F at link time.
 * - Pool allocations remember exactly the byte count and alignment used at allocation
 *   time so @c MemoryPool::deallocate routes back to the same tier (no free-list
 *   corruption).
 *
 * Semantics tracked from @c std::function:
 * - @c R operator()(Args...) const re-throws @c std::bad_function_call when empty.
 * - @c operator bool reports non-emptiness.
 * - Copy and move follow the underlying target's constructors.  A target lacking a
 *   public copy constructor is rejected at @c Function's converting constructor.
 * - Function-pointer / member-pointer targets equal to @c nullptr produce an empty
 *   @c Function (no allocation, no vtable).
 *
 * @par Example
 * @code
 * vlink::Function<int(int, int)> add = [](int a, int b) { return a + b; };
 * int x = add(1, 2);                                // 3
 *
 * vlink::Function<void()> noop;                     // empty
 * if (noop) { noop(); }                             // skipped
 *
 * vlink::Function<void()> heavy = [big_capture]() { ... };   // pooled if > 64 B
 *
 * // std::function interop -- both directions supported via converting constructors:
 * std::function<void(int)> stdfn = [](int x) { ... };
 * vlink::Function<void(int)> wrapped = stdfn;       // wrap a std::function
 * std::function<void(int)> back = wrapped;          // unwrap into a std::function
 * @endcode
 */

#pragma once

#include <functional>

#if !defined(VLINK_ENABLE_BASE_FUNCTIONAL) && defined(__unix__) && !defined(__CYGWIN__)
#define VLINK_ENABLE_BASE_FUNCTIONAL
#endif

#ifdef VLINK_ENABLE_BASE_FUNCTIONAL
#include <cstddef>
#include <new>
#include <type_traits>
#include <utility>

#include "./macros.h"
#include "./memory_pool.h"

namespace vlink {

template <typename SignatureT>
class Function;

/**
 * @class Function
 * @brief Type-erased callable wrapper, partial @c std::function replacement.
 *
 * @tparam R     Return type of the wrapped callable.
 * @tparam Args  Parameter types of the wrapped callable.
 */
template <typename R, typename... Args>
class Function<R(Args...)> {
 public:
  /**
   * @brief Inline-storage byte budget (one cache line).
   *
   * @details
   * Set to @c 64 so most realistic capture sets (several @c shared_ptr , a few
   * pointers / small POD members) live entirely inline and never touch the heap
   * fallback path.
   *
   * Targets whose @c sizeof, @c alignof, or noexcept-move properties do not satisfy
   * the inline criteria fall back to a @c vlink::MemoryPool -backed heap path.
   */
  static constexpr std::size_t kSboSize = 64U;

  using result_type = R;

  /**
   * @brief Constructs an empty @c Function.
   */
  Function() noexcept = default;

  /**
   * @brief Constructs an empty @c Function from @c nullptr.
   */
  Function(std::nullptr_t) noexcept {}  // NOLINT(google-explicit-constructor)

  /**
   * @brief Copy-constructs from @p other.
   *
   * @details
   * If @p other holds a target, the target's copy constructor is invoked into
   * inline storage or via @c vlink::MemoryPool, mirroring @p other's storage strategy.
   */
  Function(const Function& other) { copy_from(other); }

  /**
   * @brief Move-constructs from @p other and leaves @p other empty.
   */
  Function(Function&& other) noexcept { move_from(std::move(other)); }

  /**
   * @brief Constructs a @c Function from any compatible callable @p f.
   *
   * @details
   * @p f must be invocable with @c Args... and return a value convertible to @c R.
   * It must also be copy-constructible (matching @c std::function's contract); the
   * trait is enforced at compile time so move-only callables are rejected here.
   */
  template <
      typename F, typename DecayF = std::decay_t<F>,
      typename = std::enable_if_t<!std::is_same_v<DecayF, Function> && !std::is_same_v<DecayF, std::nullptr_t> &&
                                  std::is_copy_constructible_v<DecayF> && std::is_invocable_r_v<R, DecayF&, Args...>>>
  Function(F&& f) {  // NOLINT(google-explicit-constructor)
    if constexpr (kIsPointerLike<DecayF>) {
      if VUNLIKELY (f == nullptr) {
        return;
      }
    }

    construct_from<DecayF>(std::forward<F>(f));
  }

  /**
   * @brief Copy assignment via copy-and-swap.
   */
  Function& operator=(const Function& other) {
    if VLIKELY (this != &other) {
      Function tmp(other);
      swap(tmp);
    }

    return *this;
  }

  /**
   * @brief Move assignment.  Resets @c *this to empty and steals from @p other.
   */
  Function& operator=(Function&& other) noexcept {
    if VLIKELY (this != &other) {
      reset();
      move_from(std::move(other));
    }

    return *this;
  }

  /**
   * @brief Resets to empty state.
   */
  Function& operator=(std::nullptr_t) noexcept {
    reset();
    return *this;
  }

  /**
   * @brief Replaces the wrapped target with @p f via copy-and-swap.
   */
  template <typename F, typename DecayF = std::decay_t<F>,
            typename = std::enable_if_t<!std::is_same_v<DecayF, Function> && std::is_copy_constructible_v<DecayF> &&
                                        std::is_invocable_r_v<R, DecayF&, Args...>>>
  Function& operator=(F&& f) {
    Function tmp(std::forward<F>(f));
    swap(tmp);
    return *this;
  }

  /**
   * @brief Destructor.
   *
   * @details
   * If the @c Function holds a target, runs that target's destructor.  For pool-allocated
   * targets the underlying memory is then returned to @c vlink::MemoryPool .
   * Empty @c Function instances are destroyed at zero cost.
   */
  ~Function() { reset(); }

  /**
   * @brief Invokes the wrapped target with @p args.
   *
   * @throws std::bad_function_call if the @c Function is empty.
   */
  R operator()(Args... args) const {
    if VUNLIKELY (vtable_ == nullptr) {
      throw std::bad_function_call();
    }

    return vtable_->invoke(&storage_, std::forward<Args>(args)...);
  }

  /**
   * @brief Returns @c true iff the @c Function wraps a target.
   */
  explicit operator bool() const noexcept { return vtable_ != nullptr; }

  /**
   * @brief Exchanges the wrapped target with @p other.
   */
  void swap(Function& other) noexcept {
    if VUNLIKELY (this == &other) {
      return;
    }

    Function tmp(std::move(*this));
    *this = std::move(other);
    other = std::move(tmp);
  }

 private:
  template <typename F>
  static constexpr bool kIsPointerLike =
      std::is_pointer_v<F> || std::is_member_pointer_v<F> || std::is_function_v<std::remove_pointer_t<F>>;

  template <typename F>
  static constexpr bool kIsInline =
      sizeof(F) <= kSboSize && alignof(F) <= alignof(std::max_align_t) && std::is_nothrow_move_constructible_v<F>;

  struct VTable final {
    R (*invoke)(const void* storage, Args... args);
    void (*copy_construct)(void* dst, const void* src);
    void (*move_construct)(void* dst, void* src) noexcept;
    void (*destroy)(void* storage) noexcept;
  };

  template <typename F>
  struct InlineVTable {
    static R invoke(const void* storage, Args... args) {
      auto* f = reinterpret_cast<F*>(const_cast<void*>(storage));

      if constexpr (std::is_void_v<R>) {
        std::invoke(*f, std::forward<Args>(args)...);
      } else {
        return std::invoke(*f, std::forward<Args>(args)...);
      }
    }

    static void copy_construct(void* dst, const void* src) {
      const auto* src_f = reinterpret_cast<const F*>(src);
      ::new (dst) F(*src_f);
    }

    static void move_construct(void* dst, void* src) noexcept {
      auto* src_f = reinterpret_cast<F*>(src);
      ::new (dst) F(std::move(*src_f));
      src_f->~F();
    }

    static void destroy(void* storage) noexcept {
      auto* f = reinterpret_cast<F*>(storage);
      f->~F();
    }
  };

  template <typename F>
  struct HeapVTable {
    static R invoke(const void* storage, Args... args) {
      F* f = *static_cast<F* const*>(storage);

      if constexpr (std::is_void_v<R>) {
        std::invoke(*f, std::forward<Args>(args)...);
      } else {
        return std::invoke(*f, std::forward<Args>(args)...);
      }
    }

    static void copy_construct(void* dst, const void* src) {
      F* src_f = *static_cast<F* const*>(src);

      static auto& pool = MemoryPool::global_instance();

      void* mem = pool.allocate(sizeof(F), alignof(F));

      if VUNLIKELY (mem == nullptr) {
        throw std::bad_alloc();
      }

      try {
        F* new_f = ::new (mem) F(*src_f);
        *static_cast<F**>(dst) = new_f;
      } catch (...) {
        pool.deallocate(mem, sizeof(F), alignof(F));
        throw;
      }
    }

    static void move_construct(void* dst, void* src) noexcept {
      F* src_f = *static_cast<F**>(src);
      *static_cast<F**>(dst) = src_f;
      *static_cast<F**>(src) = nullptr;
    }

    static void destroy(void* storage) noexcept {
      F** slot = static_cast<F**>(storage);
      F* f = *slot;

      if VLIKELY (f != nullptr) {
        f->~F();

        static auto& pool = MemoryPool::global_instance();

        pool.deallocate(f, sizeof(F), alignof(F));

        *slot = nullptr;
      }
    }
  };

  template <typename F>
  static const VTable* get_vtable() noexcept {
    if constexpr (kIsInline<F>) {
      static constexpr VTable kVTable = {
          &InlineVTable<F>::invoke,
          &InlineVTable<F>::copy_construct,
          &InlineVTable<F>::move_construct,
          &InlineVTable<F>::destroy,
      };
      return &kVTable;
    } else {
      static constexpr VTable kVTable = {
          &HeapVTable<F>::invoke,
          &HeapVTable<F>::copy_construct,
          &HeapVTable<F>::move_construct,
          &HeapVTable<F>::destroy,
      };
      return &kVTable;
    }
  }

  template <typename F, typename Source>
  void construct_from(Source&& src) {
    if constexpr (kIsInline<F>) {
      ::new (&storage_) F(std::forward<Source>(src));
    } else {
      static auto& pool = MemoryPool::global_instance();

      void* mem = pool.allocate(sizeof(F), alignof(F));

      if VUNLIKELY (mem == nullptr) {
        throw std::bad_alloc();
      }

      try {
        F* new_f = ::new (mem) F(std::forward<Source>(src));
        *reinterpret_cast<F**>(&storage_) = new_f;
      } catch (...) {
        pool.deallocate(mem, sizeof(F), alignof(F));
        throw;
      }
    }

    vtable_ = get_vtable<F>();
  }

  void copy_from(const Function& other) {
    if VLIKELY (other.vtable_ != nullptr) {
      other.vtable_->copy_construct(&storage_, &other.storage_);
      vtable_ = other.vtable_;
    }
  }

  void move_from(Function&& other) noexcept {
    if VLIKELY (other.vtable_ != nullptr) {
      other.vtable_->move_construct(&storage_, &other.storage_);
      vtable_ = other.vtable_;
      other.vtable_ = nullptr;
    }
  }

  void reset() noexcept {
    if (vtable_ != nullptr) {
      vtable_->destroy(&storage_);
      vtable_ = nullptr;
    }
  }

  alignas(std::max_align_t) std::byte storage_[kSboSize]{};
  const VTable* vtable_{nullptr};
};

/**
 * @brief Free-function @c swap for ADL.
 */
template <typename R, typename... Args>
inline void swap(Function<R(Args...)>& lhs, Function<R(Args...)>& rhs) noexcept {
  lhs.swap(rhs);
}

/**
 * @brief Equality with @c nullptr -- empty if and only if the @c Function holds no target.
 */
template <typename R, typename... Args>
inline bool operator==(const Function<R(Args...)>& cb, std::nullptr_t) noexcept {
  return !cb;
}

template <typename R, typename... Args>
inline bool operator==(std::nullptr_t, const Function<R(Args...)>& cb) noexcept {
  return !cb;
}

template <typename R, typename... Args>
inline bool operator!=(const Function<R(Args...)>& cb, std::nullptr_t) noexcept {
  return static_cast<bool>(cb);
}

template <typename R, typename... Args>
inline bool operator!=(std::nullptr_t, const Function<R(Args...)>& cb) noexcept {
  return static_cast<bool>(cb);
}

}  // namespace vlink

#else

namespace vlink {

template <typename SignatureT>
using Function = std::function<SignatureT>;
}

#endif
