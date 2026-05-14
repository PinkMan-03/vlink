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
 * @file cancellation.h
 * @brief Cooperative cancellation primitives shared by VLink async building blocks.
 *
 * @details
 * VLink cooperative cancellation is modelled as a producer/observer pair built on
 * a refcounted internal state.  Work-producing code owns a @c CancellationSource
 * and signals cancellation via @c request_cancel(); work-consuming code holds
 * (lightweight, copyable) @c CancellationToken instances obtained from
 * @c CancellationSource::token() and polls them or attaches callbacks.
 *
 * The three public types in this header are:
 *
 * | Type                        | Role                                          | Copy / Move semantics |
 * | --------------------------- | --------------------------------------------- | --------------------- |
 * | @c CancellationSource       | Mutable owner; signals cancellation           | Implicit shared state |
 * | @c CancellationToken        | Read-only observer; poll or callback          | Copyable; shareable   |
 * | @c CancellationRegistration | RAII slot for one fired-callback subscription | Move-only             |
 *
 * @c vlink::Exception::OperationCancelled is re-exported through @c "./exception.h" and is @b not
 * redefined here.  Use it as the canonical thrown type when an operation observes a cancellation
 * request mid-flight.
 *
 * @par Lock ordering
 * The cancellation source's internal state mutex is released before fired
 * callbacks are invoked, so user callbacks may freely cancel sibling sources or
 * register additional callbacks.  The state mutex is never held across user
 * code.
 *
 * @par Example
 * @code
 * vlink::CancellationSource source;
 * auto token = source.token();
 *
 * auto registration = token.register_callback([] {
 *   // runs once, on the thread that calls request_cancel() (or inline if
 *   // cancellation was already requested at registration time)
 * });
 *
 * // Worker thread cooperatively polls:
 * while (!token.is_cancellation_requested()) {
 *   token.throw_if_cancellation_requested();  // throws OperationCancelled
 *   do_unit_of_work();
 * }
 *
 * source.request_cancel();
 * @endcode
 */

#pragma once

#include <cstddef>
#include <memory>

#include "./functional.h"
#include "./macros.h"

namespace vlink {

class CancellationToken;

/**
 * @class CancellationRegistration
 * @brief RAII subscription returned when registering a cancellation callback.
 *
 * @details
 * A @c CancellationRegistration owns exactly one callback slot inside the
 * cancellation source's state.  Destroying or @c reset()-ing the registration
 * removes the callback if cancellation has not fired yet; if cancellation has
 * already fired, the slot is already gone and the registration becomes a no-op
 * sink.
 *
 * The type is move-only: the slot identity must not be aliased.  @c reset() is
 * idempotent — calling it on an empty or already-reset registration is a no-op
 * and is safe to call any number of times.
 */
class VLINK_EXPORT CancellationRegistration final {
 public:
  /**
   * @brief Constructs an empty registration that owns no callback slot.
   */
  CancellationRegistration() noexcept;

  /**
   * @brief Unregisters the callback if it has not yet fired or been removed.
   */
  ~CancellationRegistration();

  /**
   * @brief Move-constructs from @p other, transferring slot ownership.
   *
   * @param other Source registration; left in the empty state.
   */
  CancellationRegistration(CancellationRegistration&& other) noexcept;

  /**
   * @brief Move-assigns from @p other, releasing any currently owned slot first.
   *
   * @param other Source registration; left in the empty state.
   * @return Reference to @c *this.
   */
  CancellationRegistration& operator=(CancellationRegistration&& other) noexcept;

  /**
   * @brief Unregisters the callback and resets this object to the empty state.
   *
   * @details
   * Idempotent: safe to call on an empty registration or to call multiple
   * times in a row.  After @c reset() returns, @c valid() reports @c false.
   */
  void reset() noexcept;

  /**
   * @brief Returns @c true if this object currently owns a live callback slot.
   *
   * @return @c true if a callback slot is still attached to a non-cancelled
   *         source; @c false otherwise.
   */
  [[nodiscard]] bool valid() const noexcept;

 private:
  struct State;

  CancellationRegistration(std::weak_ptr<State> state, size_t id) noexcept;

  std::weak_ptr<State> state_;
  size_t id_{0};

  friend class CancellationToken;
  friend class CancellationSource;

  VLINK_DISALLOW_COPY_AND_ASSIGN(CancellationRegistration)
};

/**
 * @class CancellationToken
 * @brief Read-only handle used by work items to observe cooperative cancellation.
 *
 * @details
 * A @c CancellationToken is a lightweight, shared, read-only view into a
 * @c CancellationSource.  Tokens are cheap to copy and safe to pass across
 * threads; many tokens may simultaneously observe the same source.  A
 * default-constructed token is invalid (@c valid() returns @c false) and never
 * reports cancellation.
 *
 * Consumers typically either poll @c is_cancellation_requested() at safe
 * points, call @c throw_if_cancellation_requested() at structured cancellation
 * points, or attach a one-shot callback via @c register_callback().
 */
class VLINK_EXPORT CancellationToken final {
 public:
  /**
   * @brief Constructs an invalid token that is not bound to any source.
   */
  CancellationToken() noexcept;

  /**
   * @brief Returns @c true if this token is bound to a @c CancellationSource.
   *
   * @return @c true if the token observes a live source; @c false for a
   *         default-constructed token.
   */
  [[nodiscard]] bool valid() const noexcept;

  /**
   * @brief Returns @c true if cancellation has been requested on the source.
   *
   * @return @c true if the bound source's @c request_cancel() has been called;
   *         @c false for an invalid token or an as-yet-uncancelled source.
   */
  [[nodiscard]] bool is_cancellation_requested() const noexcept;

  /**
   * @brief Registers a callback to run when cancellation is requested.
   *
   * @details
   * If cancellation has not yet been requested, @p callback is stored in the
   * source and will be invoked at most once on the thread that later calls
   * @c CancellationSource::request_cancel().  The returned
   * @c CancellationRegistration owns the slot and may be used to unregister
   * the callback before it fires.
   *
   * If cancellation was already requested when @c register_callback is called,
   * the callback is invoked synchronously by this thread before returning, and
   * the returned registration is empty (invalid).
   *
   * Callbacks always run with no internal cancellation locks held, so callback
   * code may freely query the token, cancel sibling sources, or register
   * additional callbacks without risk of self-deadlock.
   *
   * @param callback Move-only nullary functor to invoke on cancellation.  An
   *                 empty (null) @p callback is silently ignored and yields an
   *                 empty registration.
   * @return RAII registration that, while alive, may be used to unregister the
   *         callback; empty if the token is invalid, @p callback is empty, or
   *         cancellation had already fired.
   * @note Exceptions escaping the callback are caught and logged via
   *       @c CLOG_E; they are never propagated to @c request_cancel() or to
   *       the registering thread.
   */
  CancellationRegistration register_callback(MoveFunction<void()>&& callback) const;

  /**
   * @brief Throws @c OperationCancelled if cancellation has been requested.
   *
   * @details
   * Convenience structured cancellation point.  Has no effect on an invalid
   * token or an as-yet-uncancelled source.
   *
   * @throws vlink::Exception::OperationCancelled if @c is_cancellation_requested() is
   *         @c true.
   */
  void throw_if_cancellation_requested() const;

 private:
  using State = CancellationRegistration::State;

  explicit CancellationToken(std::shared_ptr<State> state) noexcept;

  std::shared_ptr<State> state_;

  friend class CancellationSource;
};

/**
 * @class CancellationSource
 * @brief Mutable owner that mints observer tokens and signals cooperative cancellation.
 *
 * @details
 * @c CancellationSource is the writable end of the cancellation primitive.
 * The underlying cancellation state is held through a refcounted handle, so
 * the type is implicitly copyable and movable: copies / moves yield additional
 * handles to the same state, all of which can observe and fire the same
 * cancellation.  @c token() is the cheap canonical way to fan out observers,
 * and multiple tokens may be alive at once, all observing the same source.
 *
 * Cancellation is one-shot: the first @c request_cancel() transitions the
 * source to the cancelled state and fires registered callbacks; subsequent
 * calls are no-ops and return @c false.
 */
class VLINK_EXPORT CancellationSource final {
 public:
  /**
   * @brief Constructs a fresh, uncancelled source with empty callback set.
   */
  CancellationSource();

  /**
   * @brief Returns a lightweight observer token bound to this source.
   *
   * @details
   * Cheap to call repeatedly; the returned token shares the source's state via
   * a refcounted handle.  The caller may freely copy the token and hand it to
   * other threads.
   *
   * @return A valid @c CancellationToken observing this source.
   */
  [[nodiscard]] CancellationToken token() const noexcept;

  /**
   * @brief Returns @c true if cancellation has been requested on this source.
   *
   * @return @c true once @c request_cancel() has succeeded; @c false until
   *         then.
   */
  [[nodiscard]] bool is_cancellation_requested() const noexcept;

  /**
   * @brief Requests cancellation and fires registered callbacks exactly once.
   *
   * @details
   * Transitions the source from not-cancelled to cancelled atomically and
   * invokes every callback that was registered at the moment of the
   * transition.  Callbacks are invoked on the calling thread after the
   * internal state mutex has been released, so they may safely re-enter the
   * cancellation API.
   *
   * @return @c true if this call performed the not-cancelled -> cancelled
   *         transition; @c false if cancellation had already been requested
   *         by a previous call.
   * @note Exceptions thrown by callbacks are caught and logged; they are
   *       never propagated out of @c request_cancel().
   */
  bool request_cancel() const;

 private:
  using State = CancellationRegistration::State;

  std::shared_ptr<State> state_;
};

}  // namespace vlink
