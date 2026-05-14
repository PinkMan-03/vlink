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
 * @file coroutine.h
 * @brief C++20 stackless coroutine support layered on top of @c MessageLoop.
 *
 * @details
 * Additive integration layer.  @c MessageLoop itself is not modified; all
 * coroutine machinery is built on top of the existing @c MessageLoop::post_task,
 * @c MessageLoop::exec_task, @c Timer::call_once and @c GraphTask APIs.
 *
 * Enabled only when @c VLINK_ENABLE_COROUTINE is defined.  The macro is
 * auto-enabled when @c VLINK_ENABLE_CXX_STD_20 is set AND both
 * @c __cpp_impl_coroutine and @c __cpp_lib_coroutine are advertised by the
 * compiler / standard library.  When unavailable this header is empty.
 *
 * @par Public API map
 * ```
 *   [Core types]
 *     Task<TypeT>            -- coroutine return type, lazily started, awaitable
 *
 *   [Awaiters & helpers]
 *     schedule(loop, prio)   -- hop execution onto loop's thread
 *     yield(loop, prio)      -- re-post coroutine to back of the queue
 *     delay_ms(loop, ms, prio) -- suspend N milliseconds via Timer
 *     await_future(loop, fut)  -- suspend until std::future<T> ready
 *
 *   [Top-level entry points]
 *     co_spawn(loop, task)             -- fire-and-forget Task<void>
 *     co_spawn(loop, task, on_done)    -- with completion callback
 *     co_spawn_with_priority(...)      -- explicit priority variants
 *
 *   [vlink primitive bridges]
 *     exec(loop, config, fn)   -- co_await Schedule::Config envelope
 *     await_graph(loop, graph) -- co_await a GraphTask DAG to finish
 *
 *   [Orchestration]
 *     when_all(loop, vector<Task<T>>)  -> Task<vector<T>>
 *     when_any(loop, vector<Task<T>>)  -> Task<{idx, T}>
 *     sequence(loop, vector<Task<void>>) -> Task<void>
 *
 *   [Frame allocator]
 *     (none) -- coroutine frames are always allocated through
 *               vlink::MemoryPool::global_instance() via TaskPromise::operator
 *               new / delete; throws std::bad_alloc on pool exhaustion.
 * ```
 *
 * Two namespace names are provided: @c vlink::Coroutine (canonical) and
 * @c vlink::Co (short alias).  Both refer to the same namespace.
 *
 * @par Example
 * @code
 * vlink::MessageLoop loop;
 * loop.async_run();
 *
 * // Recommended pattern: a free function whose parameters carry the state.
 * vlink::Co::Task<> my_routine(vlink::MessageLoop& loop, int* counter) {
 *   co_await vlink::Co::delay_ms(loop, 100);
 *   ++(*counter);
 *   co_await vlink::Co::yield(loop);
 *   ++(*counter);
 * }
 *
 * int counter = 0;
 * vlink::Co::co_spawn(loop, my_routine(loop, &counter));
 * @endcode
 *
 * @warning Do NOT use a coroutine lambda as a temporary, like
 *          @code co_spawn(loop, []() -> Task<> { co_await ... ; }()) @endcode
 *          The lambda object is destroyed at the end of the full-expression,
 *          long before the coroutine body actually runs.  Any captured state
 *          (including @c [&] and @c [=]) becomes a dangling reference and
 *          access is undefined behaviour.  Always pass state through the
 *          coroutine function's parameters (which the compiler copies into
 *          the coroutine frame and keeps alive for the frame's lifetime).
 */

#pragma once

#include "../version.h"

#ifdef VLINK_ENABLE_CXX_STD_20
#if __has_include(<coroutine>)
#include <coroutine>
#endif
#if defined(__cpp_lib_coroutine)
#ifndef VLINK_ENABLE_COROUTINE
#define VLINK_ENABLE_COROUTINE
#endif
#endif
#endif

#ifdef VLINK_ENABLE_COROUTINE

#include <atomic>
#include <chrono>
#include <coroutine>
#include <cstdint>
#include <exception>
#include <future>
#include <memory>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <type_traits>
#include <utility>
#include <vector>

#include "./functional.h"
#include "./graph_task.h"
#include "./macros.h"
#include "./message_loop.h"
#include "./schedule.h"

namespace vlink {

namespace Coroutine {  // NOLINT(readability-identifier-naming)

template <typename TypeT = void>
class Task;

namespace detail {

template <typename TypeT>
struct TaskPromise;

VLINK_EXPORT void* allocate_frame(size_t size);

VLINK_EXPORT void deallocate_frame(void* ptr, size_t size) noexcept;

struct FinalAwaiter final {
  // NOLINTNEXTLINE(readability-convert-member-functions-to-static)
  bool await_ready() const noexcept;

  template <typename PromiseT>
  std::coroutine_handle<> await_suspend(std::coroutine_handle<PromiseT> handle) noexcept;

  // NOLINTNEXTLINE(readability-convert-member-functions-to-static)
  void await_resume() const noexcept;
};

template <typename TypeT>
struct TaskPromiseBase {
  std::coroutine_handle<> continuation;
  std::exception_ptr exception;

  std::suspend_always initial_suspend() noexcept;

  FinalAwaiter final_suspend() noexcept;

  void unhandled_exception() noexcept;
};

template <typename TypeT>
struct TaskPromise final : TaskPromiseBase<TypeT> {
  std::optional<TypeT> result;

  static void* operator new(size_t size) { return detail::allocate_frame(size); }

  static void operator delete(void* ptr, size_t size) noexcept { detail::deallocate_frame(ptr, size); }

  Task<TypeT> get_return_object() noexcept;

  template <typename UpT>
  void return_value(UpT&& v) noexcept(std::is_nothrow_constructible_v<TypeT, UpT&&>);
};

template <>
struct TaskPromise<void> final : TaskPromiseBase<void> {
  static void* operator new(size_t size) { return detail::allocate_frame(size); }

  static void operator delete(void* ptr, size_t size) noexcept { detail::deallocate_frame(ptr, size); }

  Task<void> get_return_object() noexcept;

  void return_void() noexcept;
};

struct VLINK_EXPORT DetachedTask final {
  struct VLINK_EXPORT promise_type final {  // NOLINT(readability-identifier-naming)
    static void* operator new(size_t size) { return detail::allocate_frame(size); }

    static void operator delete(void* ptr, size_t size) noexcept { detail::deallocate_frame(ptr, size); }

    DetachedTask get_return_object() noexcept;

    std::suspend_always initial_suspend() noexcept;

    std::suspend_never final_suspend() noexcept;

    void return_void() noexcept;

    void unhandled_exception() noexcept;
  };

  using Handle = std::coroutine_handle<promise_type>;

  DetachedTask() noexcept = default;

  explicit DetachedTask(Handle h) noexcept : handle(h) {}

  DetachedTask(DetachedTask&& other) noexcept : handle(std::exchange(other.handle, {})) {}

  DetachedTask& operator=(DetachedTask&& other) noexcept {
    handle = std::exchange(other.handle, {});
    return *this;
  }

  ~DetachedTask() = default;

  Handle handle;

  VLINK_DISALLOW_COPY_AND_ASSIGN(DetachedTask)
};

VLINK_EXPORT DetachedTask co_spawn_void_impl(Task<void> task);

template <typename TypeT, typename CallbackT>
DetachedTask co_spawn_value_impl(Task<TypeT> task, CallbackT on_complete);

template <typename CallbackT>
DetachedTask co_spawn_void_with_cb_impl(Task<void> task, CallbackT on_complete);

VLINK_EXPORT bool post_resume(MessageLoop& loop, std::coroutine_handle<> handle, uint16_t priority = 0);

VLINK_EXPORT void co_spawn_detached_handle(MessageLoop& loop, DetachedTask::Handle handle, uint16_t priority = 0);

VLINK_EXPORT void register_future_wait(MoveFunction<bool()>&& poll);

}  // namespace detail

/**
 * @class Task
 * @brief Coroutine return type; lazily started, awaitable from another coroutine.
 *
 * @details
 * The coroutine body does not run until the @c Task is either awaited from
 * another coroutine or passed to @c co_spawn().  Moving the @c Task transfers
 * ownership; destroying a still-suspended @c Task destroys the frame.
 *
 * @tparam TypeT  Result type produced by @c co_return.  Use @c void for tasks
 *                that produce no value.
 */
template <typename TypeT>
class Task final {
 public:
  using promise_type = detail::TaskPromise<TypeT>;
  using Handle = std::coroutine_handle<promise_type>;

  /// @brief Default-constructs an invalid (no-frame) task.
  Task() noexcept = default;

  /// @brief Adopts an existing coroutine handle.
  explicit Task(Handle handle) noexcept;

  /// @brief Move-construct; transfers frame ownership and leaves @p other empty.
  Task(Task&& other) noexcept;

  /// @brief Move-assign; destroys the prior frame (if any) and adopts @p other's.
  Task& operator=(Task&& other) noexcept;

  /// @brief Destroys the owned coroutine frame if still valid.
  ~Task();

  /// @brief Returns @c true if this task owns a coroutine frame.
  [[nodiscard]] bool valid() const noexcept;

  /// @brief Returns @c true if the owned frame has finished executing.
  [[nodiscard]] bool done() const noexcept;

  /// @brief Coroutine protocol hook: ready iff the body already finished.
  bool await_ready() const noexcept;

  /// @brief Coroutine protocol hook: links @p awaiter into the promise and tail-calls into the body.
  std::coroutine_handle<> await_suspend(std::coroutine_handle<> awaiter) noexcept;

  /// @brief Coroutine protocol hook: rethrows any captured exception or returns the result.
  TypeT await_resume();

  /// @brief Releases ownership of the frame without destroying it; returns the raw handle.
  Handle release() noexcept;

  /// @brief Returns the raw coroutine handle without transferring ownership.
  [[nodiscard]] Handle native_handle() const noexcept;

 private:
  Handle handle_;

  VLINK_DISALLOW_COPY_AND_ASSIGN(Task)
};

/**
 * @class ScheduleAwaiter
 * @brief Awaiter that resumes the coroutine on @p loop's worker thread.
 *
 * @details
 * Uses @c MessageLoop::post_task internally.  After @c co_await schedule(loop),
 * the rest of the coroutine runs on @p loop's thread.
 *
 * @warning If @p loop is already quitting and @c post_task fails, the awaiting
 *          coroutine's frame is destroyed and no continuation is resumed -- a
 *          parent coroutine awaiting this one via nested @c co_await will be
 *          left permanently suspended.  Same caveat applies to @c YieldAwaiter,
 *          @c DelayAwaiter, and the @c GraphAwaiter resume path.
 */
struct VLINK_EXPORT ScheduleAwaiter final {
  MessageLoop* loop{nullptr};  ///< Target loop on which to resume.
  uint16_t priority{0};        ///< Schedule priority (honoured only on @c kPriorityType loops).
  bool failed{false};          ///< Set to @c true if @c post_task to @c loop failed.

  /// @brief Always @c false — this awaiter always suspends to perform the hop.
  static bool await_ready() noexcept;

  /// @brief Posts a resume of @p handle onto @c loop with @c priority.
  /// @return @c true to stay suspended on success; @c false to resume immediately on post failure.
  bool await_suspend(std::coroutine_handle<> handle);

  /// @brief No-op on success; throws @c std::runtime_error if the post failed.
  void await_resume();
};

/**
 * @class YieldAwaiter
 * @brief Awaiter that re-posts the coroutine to the end of @p loop's queue.
 *
 * @details
 * Useful for cooperative yielding inside a long-running coroutine running on
 * the loop thread, so other queued tasks get a chance to run.
 */
struct VLINK_EXPORT YieldAwaiter final {
  MessageLoop* loop{nullptr};  ///< Target loop to re-post onto.
  uint16_t priority{0};        ///< Schedule priority for the re-post.
  bool failed{false};          ///< Set to @c true if @c post_task to @c loop failed.

  /// @brief Always @c false — this awaiter always suspends to yield.
  static bool await_ready() noexcept;

  /// @brief Re-posts @p handle to the end of @c loop's queue.
  /// @return @c true to stay suspended on success; @c false to resume immediately on post failure.
  bool await_suspend(std::coroutine_handle<> handle);

  /// @brief No-op on success; throws @c std::runtime_error if the re-post failed.
  void await_resume();
};

/**
 * @class DelayAwaiter
 * @brief Awaiter that suspends for @p ms milliseconds via @c Timer::call_once.
 *
 * @details
 * Always suspends and resumes on @p loop's thread, even for @p ms == 0
 * (so that the loop-thread invariant after the resume point is preserved).
 */
struct VLINK_EXPORT DelayAwaiter final {
  MessageLoop* loop{nullptr};  ///< Target loop to resume on after the delay.
  uint32_t ms{0};              ///< Delay in milliseconds (0 still routes through the loop).
  uint16_t priority{0};        ///< Schedule priority for the resume post.
  bool failed{false};          ///< Set to @c true if @c Timer::call_once failed.

  /// @brief Always @c false — this awaiter always suspends to schedule the timer.
  static bool await_ready() noexcept;

  /// @brief Schedules a one-shot timer that resumes @p handle on @c loop.
  /// @return @c true to stay suspended on success; @c false to resume immediately on schedule failure.
  bool await_suspend(std::coroutine_handle<> handle);

  /// @brief No-op on success; throws @c std::runtime_error if the schedule failed.
  void await_resume();
};

/**
 * @class FutureAwaiter
 * @brief Awaiter that suspends until a @c std::future becomes ready.
 *
 * @details
 * Backed by a process-wide shared waiter pool (see @c register_future_wait):
 * a single long-lived background thread services all pending @c await_future
 * calls.  The waiter polls each registered future at a short cadence using
 * non-blocking @c wait_for(0) and posts the resume back to @p loop as soon as
 * the future becomes ready.  The future itself remains in the coroutine frame
 * and is read by @c await_resume() on the loop thread.
 *
 * @note No per-await thread is spawned.  Latency is bounded by the pool's
 *       polling interval (≈ 1 ms) plus the @p loop's task dispatch delay.
 *
 * @warning Lifetime contract: @p loop and the awaiting coroutine frame MUST
 *          remain alive until the future becomes ready AND the waiter has
 *          finished posting the resume.  Specifically, callers MUST NOT
 *          destroy a still-suspended @c Task<> that owns a coroutine awaiting
 *          this awaiter (e.g. holding a @c Task<T> as a named lvalue and
 *          letting it leave scope while suspended), and MUST NOT destroy
 *          @p loop while any @c await_future call is pending.  Violating the
 *          contract races the waiter against frame / loop destruction and
 *          results in undefined behaviour.
 */
template <typename TypeT>
class FutureAwaiter final {
 public:
  /// @brief Constructs an awaiter that bridges @p fut into @p loop's coroutine model.
  FutureAwaiter(MessageLoop* loop, std::future<TypeT> fut) noexcept;

  /// @brief Move-construct; transfers the future and loop pointer.
  FutureAwaiter(FutureAwaiter&&) noexcept = default;

  /// @brief Move-assign; transfers the future and loop pointer.
  FutureAwaiter& operator=(FutureAwaiter&&) noexcept = default;

  /// @brief Destructor; the helper thread, if any, completes independently.
  ~FutureAwaiter() = default;

  /// @brief Ready iff the future is already in a ready state (short-circuit).
  bool await_ready() const noexcept;

  /// @brief Spawns the helper thread that will resume @p handle once @c fut_ is ready.
  void await_suspend(std::coroutine_handle<> handle);

  /// @brief Returns @c fut_.get() (or @c void); rethrows any future-stored exception.
  TypeT await_resume();

 private:
  MessageLoop* loop_{nullptr};
  std::future<TypeT> fut_;

  VLINK_DISALLOW_COPY_AND_ASSIGN(FutureAwaiter)
};

/**
 * @brief Returns an awaiter that hops the coroutine to @p loop's thread.
 *
 * @param loop      Target loop to resume on.
 * @param priority  Schedule priority -- values from @c MessageLoop::Priority
 *                  (@c kNoPriority = FIFO, @c kNormalPriority = 100, etc.).
 *                  Only honoured on @c kPriorityType loops.
 */
VLINK_EXPORT ScheduleAwaiter schedule(MessageLoop& loop, uint16_t priority = MessageLoop::kNoPriority) noexcept;

/**
 * @brief Returns an awaiter that yields once back to @p loop's queue.
 *
 * @param loop      Loop owning the coroutine.
 * @param priority  Re-post priority from @c MessageLoop::Priority
 *                  (only honoured on @c kPriorityType loops).
 */
VLINK_EXPORT YieldAwaiter yield(MessageLoop& loop, uint16_t priority = MessageLoop::kNoPriority) noexcept;

/**
 * @brief Returns an awaiter that suspends for @p ms milliseconds.
 *
 * @param loop      Loop on which the timer is registered.
 * @param ms        Delay in milliseconds.
 * @param priority  Resume priority from @c MessageLoop::Priority,
 *                  forwarded to @c Timer::call_once.
 */
VLINK_EXPORT DelayAwaiter delay_ms(MessageLoop& loop, uint32_t ms,
                                   uint16_t priority = MessageLoop::kNoPriority) noexcept;

/**
 * @brief Returns an awaiter that suspends until @p fut is ready, resuming on @p loop.
 */
template <typename TypeT>
FutureAwaiter<TypeT> await_future(MessageLoop& loop, std::future<TypeT> fut) noexcept;

/**
 * @brief Schedules a top-level @c Task<void> on @p loop (fire-and-forget).
 *
 * @details
 * The coroutine begins executing on @p loop's thread.  Ownership of the task
 * frame is transferred to an internal detached coroutine that destroys it on
 * completion.  Exceptions thrown from the task are swallowed (logged via
 * @c DetachedTask::unhandled_exception).
 *
 * @par Joining from synchronous code
 * Use the three-argument @c co_spawn overload with a callback to bridge to a
 * @c std::promise / @c std::future, then wait on the future:
 * @code
 *   std::promise<void> p;
 *   auto fut = p.get_future();
 *   Co::co_spawn(loop, my_void_task(), [&p]() { p.set_value(); });
 *   fut.wait();
 * @endcode
 * Note: if @c my_void_task throws, the callback is NOT invoked and @c fut
 * never becomes ready.  Catch inside @c my_void_task or use a value-returning
 * task that converts errors into a sentinel result.
 */
VLINK_EXPORT void co_spawn(MessageLoop& loop, Task<void>&& task);

/**
 * @brief Same as @c co_spawn(), with an explicit dispatch priority.
 *
 * @param priority  Schedule priority (only honoured on @c kPriorityType loops).
 */
VLINK_EXPORT void co_spawn_with_priority(MessageLoop& loop, Task<void>&& task, uint16_t priority);

/**
 * @brief Schedules a top-level @c Task<TypeT> with a completion callback.
 *
 * @details
 * The coroutine begins executing on @p loop's thread.  When the task completes
 * successfully, @p on_complete is invoked on the loop thread with the result.
 *
 * @warning **Exceptions thrown by the task are SWALLOWED** — they are logged
 *          via @c DetachedTask::unhandled_exception and @p on_complete is NOT
 *          invoked.  This API is unsuitable for code that must observe task
 *          failure.  To observe failure either:
 *          - Catch inside the task body and convert to a sentinel result, or
 *          - Bridge with @c std::promise + @c await_future:
 *            @code
 *              auto pp = std::make_shared<std::promise<TypeT>>();
 *              auto fut = pp->get_future();
 *              Co::co_spawn(loop, task_that_may_throw(),
 *                           [pp](TypeT v) { pp->set_value(std::move(v)); });
 *              try {
 *                TypeT result = co_await Co::await_future(loop, std::move(fut));
 *              } catch (...) { // still does not see the original exception
 *              }
 *            @endcode
 *            (Note: the future never resolves on task exception either; this
 *            pattern still only handles the success path.)  For full
 *            error observability, use the @c when_all / @c when_any /
 *            @c sequence orchestrators, which capture the original exception
 *            via the runner+guard machinery, or @c co_await the @c Task
 *            directly from a parent coroutine.
 */
template <typename TypeT, typename CallbackT,
          // NOLINTNEXTLINE(modernize-use-constraints)
          typename = std::enable_if_t<!std::is_integral_v<std::remove_reference_t<CallbackT>>>>
void co_spawn(MessageLoop& loop, Task<TypeT>&& task, CallbackT&& on_complete);

/**
 * @brief Same as the value-returning @c co_spawn(), with an explicit dispatch priority.
 */
template <typename TypeT, typename CallbackT,
          // NOLINTNEXTLINE(modernize-use-constraints)
          typename = std::enable_if_t<!std::is_integral_v<std::remove_reference_t<CallbackT>>>>
void co_spawn_with_priority(MessageLoop& loop, Task<TypeT>&& task, CallbackT&& on_complete, uint16_t priority);

/**
 * @class GraphAwaiter
 * @brief Awaiter that suspends until a @c GraphTask reaches @c kStatusDone.
 *
 * @details
 * Installs a status callback on the graph; on @c kStatusDone the coroutine is
 * posted back onto @p loop.  If the graph is already done at the await point,
 * @c await_ready returns @c true.  Cancelled or never-started graphs leave the
 * coroutine suspended forever -- caller must ensure the graph eventually runs.
 *
 * @details
 * Each @c await_graph call appends a single-shot subscription; the
 * subscription is unregistered automatically once the awaiter resumes.
 * Multiple coroutines awaiting the same graph are safe.
 */
class VLINK_EXPORT GraphAwaiter final {
 public:
  /// @brief Constructs an awaiter monitoring @p graph; resume scheduled on @p loop.
  GraphAwaiter(MessageLoop* loop, GraphTaskPtr graph) noexcept;

  /// @brief Move-construct; transfers loop pointer and graph reference.
  GraphAwaiter(GraphAwaiter&&) noexcept = default;

  /// @brief Move-assign; transfers loop pointer and graph reference.
  GraphAwaiter& operator=(GraphAwaiter&&) noexcept = default;

  /// @brief Destructor; outstanding status_callback subscription is removed automatically on fire.
  ~GraphAwaiter() = default;

  /// @brief Ready iff @c graph_ is null or already @c kStatusDone.
  [[nodiscard]] bool await_ready() const noexcept;

  /// @brief Registers a one-shot status callback that resumes @p handle on Done.
  void await_suspend(std::coroutine_handle<> handle);

  /// @brief No-op; the resume was triggered by the status callback.
  void await_resume() const noexcept;

 private:
  MessageLoop* loop_{nullptr};
  GraphTaskPtr graph_;

  VLINK_DISALLOW_COPY_AND_ASSIGN(GraphAwaiter)
};

/**
 * @brief Returns an awaiter that suspends until @p graph reaches a terminal status.
 *
 * @param loop   Loop on which to resume the coroutine.
 * @param graph  Graph task to monitor.
 */
VLINK_EXPORT GraphAwaiter await_graph(MessageLoop& loop, GraphTaskPtr graph);

/**
 * @brief Coroutine wrapper around @c MessageLoop::exec_task().
 *
 * @details
 * Posts @p callback under the given @c Schedule::Config envelope and suspends
 * until the callback has run.  Exceptions thrown by @p callback propagate
 * through the returned task and surface from the awaiting @c co_await.
 *
 * @tparam CallbackT  Callable returning @c void.
 * @param loop      Loop on which the callback is scheduled.
 * @param config    Schedule envelope (delay, priority, timeouts).
 * @param callback  Callable to execute.
 * @return @c Task<void> that completes after @p callback returns.
 */
template <typename CallbackT>
Task<void> exec(MessageLoop& loop, const Schedule::Config& config, CallbackT&& callback);

/**
 * @brief Awaits all @c Task<TypeT> in @p tasks to complete; returns their results.
 *
 * @details
 * Each task is dispatched via @c co_spawn on @p loop.  Results are stored in
 * the order of the input vector.  @c when_all waits for **every** sub-task to
 * finish (success or failure) before completing.  If any sub-task throws, the
 * first exception observed is rethrown from the awaiting @c co_await; the
 * exception type and identity match what the sub-task threw.  Subsequent
 * exceptions from sibling tasks are dropped.
 *
 * @note @p TypeT must be default-constructible (slot for each task is
 *       pre-sized via @c std::vector<TypeT>(count)).
 */
template <typename TypeT>
Task<std::vector<TypeT>> when_all(MessageLoop& loop, std::vector<Task<TypeT>> tasks);

/**
 * @brief Awaits all @c Task<void> in @p tasks to complete.
 *
 * @details
 * Waits for every sub-task to finish.  If any sub-task throws, the first
 * exception observed is rethrown from the awaiting @c co_await; subsequent
 * exceptions are dropped.
 */
VLINK_EXPORT Task<void> when_all(MessageLoop& loop, std::vector<Task<void>> tasks);

/**
 * @brief Records the index and result of the first @c Task<TypeT> in @p tasks
 * to complete successfully; waits for all remaining tasks to finish before
 * returning.
 *
 * @details
 * The "winner" (first successful completion) is captured by an atomic
 * compare-exchange; losing tasks continue running until completion and their
 * results are dropped.  @c when_any does not return until **every** sub-task
 * has finished -- this is required to avoid leaking the orphaned sub-task
 * frames (there is no cross-coroutine cancellation in this layer).  If you
 * need an "abandon losers" semantics with bounded latency, ensure each
 * sub-task itself respects a deadline.  If every sub-task throws, the first
 * exception observed is rethrown from the awaiting @c co_await.
 *
 * @throws std::invalid_argument if @p tasks is empty.  Because @c when_any is
 *         itself a coroutine the exception is observed at the awaiting
 *         @c co_await site (not synchronously at the @c when_any() call site).
 *
 * @return @c {winning_index, winning_result}
 */
template <typename TypeT>
Task<std::pair<size_t, TypeT>> when_any(MessageLoop& loop, std::vector<Task<TypeT>> tasks);

/**
 * @brief Records the index of the first @c Task<void> in @p tasks to complete
 * successfully; waits for all remaining tasks to finish before returning.
 *
 * @details
 * The "winner" (first successful completion) is captured by an atomic
 * compare-exchange; losing tasks continue running until completion.
 * @c when_any does not return until **every** sub-task has finished -- this is
 * required to avoid leaking the orphaned sub-task frames (there is no
 * cross-coroutine cancellation in this layer).  If every sub-task throws, the
 * first exception observed is rethrown from the awaiting @c co_await.
 *
 * @throws std::invalid_argument if @p tasks is empty.  Surfaces through the
 *         awaiting @c co_await (when_any is itself a coroutine).
 */
VLINK_EXPORT Task<size_t> when_any(MessageLoop& loop, std::vector<Task<void>> tasks);

/**
 * @brief Runs @p tasks sequentially, awaiting each one before starting the next.
 *
 * @details
 * Convenience for composing a linear pipeline of @c Task<void>.  Each task is
 * spawned on @p loop via the same runner+guard machinery used by @c when_all,
 * and joined through @c await_future, so both the success path and any
 * exception thrown by a sub-task resume the caller on @p loop's thread.  If a
 * task throws, the exception is rethrown from the awaiting @c co_await with
 * its original type preserved and remaining tasks are skipped.
 */
VLINK_EXPORT Task<void> sequence(MessageLoop& loop, std::vector<Task<void>> tasks);

////////////////////////////////////////////////////////////////
/// Details
////////////////////////////////////////////////////////////////

namespace detail {

// NOLINTNEXTLINE(readability-convert-member-functions-to-static)
inline bool FinalAwaiter::await_ready() const noexcept { return false; }

template <typename PromiseT>
inline std::coroutine_handle<> FinalAwaiter::await_suspend(std::coroutine_handle<PromiseT> handle) noexcept {
  auto next = handle.promise().continuation;
  return next ? next : std::noop_coroutine();
}

inline void FinalAwaiter::await_resume() const noexcept {}

template <typename TypeT>
inline std::suspend_always TaskPromiseBase<TypeT>::initial_suspend() noexcept {
  return {};
}

template <typename TypeT>
inline FinalAwaiter TaskPromiseBase<TypeT>::final_suspend() noexcept {
  return {};
}

template <typename TypeT>
inline void TaskPromiseBase<TypeT>::unhandled_exception() noexcept {
  exception = std::current_exception();
}

template <typename TypeT>
inline Task<TypeT> TaskPromise<TypeT>::get_return_object() noexcept {
  return Task<TypeT>{std::coroutine_handle<TaskPromise<TypeT>>::from_promise(*this)};
}

template <typename TypeT>
template <typename UpT>
inline void TaskPromise<TypeT>::return_value(UpT&& v) noexcept(std::is_nothrow_constructible_v<TypeT, UpT&&>) {
  result.emplace(std::forward<UpT>(v));
}

inline Task<void> TaskPromise<void>::get_return_object() noexcept {
  return Task<void>{std::coroutine_handle<TaskPromise<void>>::from_promise(*this)};
}

inline void TaskPromise<void>::return_void() noexcept {}

template <typename TypeT, typename CallbackT>
inline DetachedTask co_spawn_value_impl(Task<TypeT> task, CallbackT on_complete) {
  on_complete(co_await std::move(task));
}

template <typename CallbackT>
inline DetachedTask co_spawn_void_with_cb_impl(Task<void> task, CallbackT on_complete) {
  co_await std::move(task);
  on_complete();
}

}  // namespace detail

template <typename TypeT>
inline Task<TypeT>::Task(Handle handle) noexcept : handle_(handle) {}

template <typename TypeT>
inline Task<TypeT>::Task(Task&& other) noexcept : handle_(std::exchange(other.handle_, {})) {}

template <typename TypeT>
inline Task<TypeT>& Task<TypeT>::operator=(Task&& other) noexcept {
  if VLIKELY (this != &other) {
    if (handle_) {
      handle_.destroy();
    }
    handle_ = std::exchange(other.handle_, {});
  }
  return *this;
}

template <typename TypeT>
inline Task<TypeT>::~Task() {
  if (handle_) {
    handle_.destroy();
  }
}

template <typename TypeT>
inline bool Task<TypeT>::valid() const noexcept {
  return handle_ != nullptr;
}

template <typename TypeT>
inline bool Task<TypeT>::done() const noexcept {
  return handle_ && handle_.done();
}

template <typename TypeT>
inline bool Task<TypeT>::await_ready() const noexcept {
  if VUNLIKELY (!handle_) {
    return true;
  }

  return handle_.done();
}

template <typename TypeT>
inline std::coroutine_handle<> Task<TypeT>::await_suspend(std::coroutine_handle<> awaiter) noexcept {
  handle_.promise().continuation = awaiter;
  return handle_;
}

template <typename TypeT>
inline TypeT Task<TypeT>::await_resume() {
  if VUNLIKELY (!handle_) {
    throw std::logic_error("vlink::Coroutine::Task::await_resume on invalid Task (moved-from or default-constructed)");
  }

  if VUNLIKELY (handle_.promise().exception) {
    std::rethrow_exception(handle_.promise().exception);
  }

  if constexpr (!std::is_void_v<TypeT>) {
    return std::move(*handle_.promise().result);
  }
}

template <typename TypeT>
inline typename Task<TypeT>::Handle Task<TypeT>::release() noexcept {
  return std::exchange(handle_, {});
}

template <typename TypeT>
inline typename Task<TypeT>::Handle Task<TypeT>::native_handle() const noexcept {
  return handle_;
}

template <typename TypeT>
inline FutureAwaiter<TypeT>::FutureAwaiter(MessageLoop* loop, std::future<TypeT> fut) noexcept
    : loop_(loop), fut_(std::move(fut)) {}

template <typename TypeT>
inline bool FutureAwaiter<TypeT>::await_ready() const noexcept {
  return !fut_.valid() || fut_.wait_for(std::chrono::nanoseconds::zero()) == std::future_status::ready;
}

template <typename TypeT>
inline void FutureAwaiter<TypeT>::await_suspend(std::coroutine_handle<> handle) {
  auto* loop_ptr = loop_;
  auto* fut_ptr = &fut_;

  detail::register_future_wait(MoveFunction<bool()>([loop_ptr, handle, fut_ptr]() -> bool {
    if VUNLIKELY (!fut_ptr->valid()) {
      (void)detail::post_resume(*loop_ptr, handle);
      return true;
    }

    if (fut_ptr->wait_for(std::chrono::nanoseconds::zero()) != std::future_status::ready) {
      return false;
    }

    (void)detail::post_resume(*loop_ptr, handle);
    return true;
  }));
}

template <typename TypeT>
inline TypeT FutureAwaiter<TypeT>::await_resume() {
  if constexpr (std::is_void_v<TypeT>) {
    fut_.get();
  } else {
    return fut_.get();
  }
}

template <typename TypeT>
inline FutureAwaiter<TypeT> await_future(MessageLoop& loop, std::future<TypeT> fut) noexcept {
  return FutureAwaiter<TypeT>{&loop, std::move(fut)};
}

template <typename TypeT, typename CallbackT, typename>
inline void co_spawn(MessageLoop& loop, Task<TypeT>&& task, CallbackT&& on_complete) {
  co_spawn_with_priority<TypeT, CallbackT>(loop, std::move(task), std::forward<CallbackT>(on_complete), 0);
}

template <typename TypeT, typename CallbackT, typename>
inline void co_spawn_with_priority(MessageLoop& loop, Task<TypeT>&& task, CallbackT&& on_complete, uint16_t priority) {
  if constexpr (std::is_void_v<TypeT>) {
    auto detached = detail::co_spawn_void_with_cb_impl<std::decay_t<CallbackT>>(std::move(task),
                                                                                std::forward<CallbackT>(on_complete));
    auto handle = detached.handle;
    detached.handle = {};
    detail::co_spawn_detached_handle(loop, handle, priority);
  } else {
    auto detached = detail::co_spawn_value_impl<TypeT, std::decay_t<CallbackT>>(std::move(task),
                                                                                std::forward<CallbackT>(on_complete));
    auto handle = detached.handle;
    detached.handle = {};
    detail::co_spawn_detached_handle(loop, handle, priority);
  }
}

template <typename CallbackT>
inline Task<void> exec(MessageLoop& loop, const Schedule::Config& config, CallbackT&& callback) {
  auto promise_ptr = std::make_shared<std::promise<void>>();
  auto fut = promise_ptr->get_future();

  auto status = loop.exec_task(config, [cb = std::forward<CallbackT>(callback), promise_ptr]() mutable {
    try {
      cb();
      promise_ptr->set_value();
    } catch (...) {
      promise_ptr->set_exception(std::current_exception());
    }
  });

  if VUNLIKELY (!status.is_valid()) {
    promise_ptr->set_exception(std::make_exception_ptr(std::runtime_error("Coroutine::exec: exec_task post failed")));
  }

  co_await await_future(loop, std::move(fut));

  co_return;
}

namespace detail {

template <typename TypeT>
struct WhenAllState final {
  std::vector<TypeT> results;
  std::atomic<size_t> remaining{0};
  std::mutex exc_mtx;
  std::exception_ptr first_exc;
  std::promise<void> promise;
};

template <typename TypeT>
struct WhenAllGuard final {
  std::shared_ptr<WhenAllState<TypeT>> state;
  bool completed_normally{false};

  explicit WhenAllGuard(std::shared_ptr<WhenAllState<TypeT>> s) noexcept : state(std::move(s)) {}

  WhenAllGuard(WhenAllGuard&& other) noexcept
      : state(std::move(other.state)), completed_normally(other.completed_normally) {}

  WhenAllGuard(const WhenAllGuard&) = delete;
  WhenAllGuard& operator=(const WhenAllGuard&) = delete;
  WhenAllGuard& operator=(WhenAllGuard&&) = delete;

  ~WhenAllGuard() {
    if (!state) {
      return;
    }

    if (!completed_normally) {
      std::lock_guard lock(state->exc_mtx);

      if (!state->first_exc) {
        state->first_exc = std::make_exception_ptr(
            std::runtime_error("vlink::Coroutine::when_all: sub-task aborted before completion"));
      }
    }

    if (state->remaining.fetch_sub(1, std::memory_order_acq_rel) == 1) {
      if (state->first_exc) {
        state->promise.set_exception(state->first_exc);
      } else {
        state->promise.set_value();
      }
    }
  }
};

template <typename TypeT>
inline Task<void> when_all_runner(Task<TypeT> task, size_t i, WhenAllGuard<TypeT> guard) {
  try {
    auto value = co_await std::move(task);
    guard.state->results[i] = std::move(value);
  } catch (...) {
    std::lock_guard lock(guard.state->exc_mtx);

    if (!guard.state->first_exc) {
      guard.state->first_exc = std::current_exception();
    }
  }

  guard.completed_normally = true;
}

template <typename TypeT>
struct WhenAnyState final {
  std::atomic_bool fired{false};
  std::atomic<size_t> remaining{0};
  std::mutex exc_mtx;
  std::exception_ptr first_exc;
  std::optional<std::pair<size_t, TypeT>> winner;
  std::promise<std::pair<size_t, TypeT>> promise;
};

template <typename TypeT>
struct WhenAnyGuard final {
  std::shared_ptr<WhenAnyState<TypeT>> state;
  bool completed_normally{false};

  explicit WhenAnyGuard(std::shared_ptr<WhenAnyState<TypeT>> s) noexcept : state(std::move(s)) {}

  WhenAnyGuard(WhenAnyGuard&& other) noexcept
      : state(std::move(other.state)), completed_normally(other.completed_normally) {}

  WhenAnyGuard(const WhenAnyGuard&) = delete;
  WhenAnyGuard& operator=(const WhenAnyGuard&) = delete;
  WhenAnyGuard& operator=(WhenAnyGuard&&) = delete;

  ~WhenAnyGuard() {
    if (!state) {
      return;
    }

    if (!completed_normally) {
      std::lock_guard lock(state->exc_mtx);

      if (!state->first_exc) {
        state->first_exc = std::make_exception_ptr(
            std::runtime_error("vlink::Coroutine::when_any: sub-task aborted before completion"));
      }
    }

    if (state->remaining.fetch_sub(1, std::memory_order_acq_rel) == 1) {
      if (state->winner) {
        state->promise.set_value(std::move(*state->winner));
      } else {
        state->promise.set_exception(state->first_exc);
      }
    }
  }
};

template <typename TypeT>
inline Task<void> when_any_runner(Task<TypeT> task, size_t i, WhenAnyGuard<TypeT> guard) {
  try {
    auto value = co_await std::move(task);
    bool expected = false;

    if (guard.state->fired.compare_exchange_strong(expected, true, std::memory_order_acq_rel)) {
      guard.state->winner.emplace(i, std::move(value));
    }
  } catch (...) {
    std::lock_guard lock(guard.state->exc_mtx);

    if (!guard.state->first_exc) {
      guard.state->first_exc = std::current_exception();
    }
  }

  guard.completed_normally = true;
}

}  // namespace detail

template <typename TypeT>
inline Task<std::vector<TypeT>> when_all(MessageLoop& loop, std::vector<Task<TypeT>> tasks) {
  static_assert(std::is_default_constructible_v<TypeT>,
                "Co::when_all<T>: T must be default-constructible (results vector pre-sized).");

  if VUNLIKELY (tasks.empty()) {
    co_return std::vector<TypeT>{};
  }

  const size_t count = tasks.size();
  auto state = std::make_shared<detail::WhenAllState<TypeT>>();
  state->results.resize(count);
  state->remaining.store(count, std::memory_order_relaxed);
  auto fut = state->promise.get_future();

  for (size_t i = 0; i < count; ++i) {
    co_spawn(loop, detail::when_all_runner<TypeT>(std::move(tasks[i]), i, detail::WhenAllGuard<TypeT>{state}));
  }

  co_await await_future(loop, std::move(fut));

  co_return std::move(state->results);
}

template <typename TypeT>
inline Task<std::pair<size_t, TypeT>> when_any(MessageLoop& loop, std::vector<Task<TypeT>> tasks) {
  if VUNLIKELY (tasks.empty()) {
    throw std::invalid_argument("vlink::Coroutine::when_any: tasks must not be empty");
  }

  auto state = std::make_shared<detail::WhenAnyState<TypeT>>();
  state->remaining.store(tasks.size(), std::memory_order_relaxed);
  auto fut = state->promise.get_future();

  for (size_t i = 0; i < tasks.size(); ++i) {
    co_spawn(loop, detail::when_any_runner<TypeT>(std::move(tasks[i]), i, detail::WhenAnyGuard<TypeT>{state}));
  }

  co_return co_await await_future(loop, std::move(fut));
}

}  // namespace Coroutine

/**
 * @brief Short alias for the @c Coroutine namespace.
 *
 * @details
 * Lets user code write @c vlink::Co::Task / @c vlink::Co::yield(loop) etc.
 * @c vlink::Co and @c vlink::Coroutine refer to the same namespace.
 */
namespace Co = Coroutine;  // NOLINT(readability-identifier-naming)

}  // namespace vlink

#endif  // VLINK_ENABLE_COROUTINE
