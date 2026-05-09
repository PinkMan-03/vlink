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

/*
 * ConditionVariable implementation to fix monotonic_clock.
 *
 * There is a problem with std::condition_variable in lower versions of gcc,
 * which actually uses std::chrono::system_clock instead of std::chrono::steady_clock.
 * Bug 41861 (DR887) - [DR887][C++0x] <condition_variable> does not use monotonic_clock
 *
 * Since new versions of gcc cannot be used, vlink::condition_variable can only be
 * manually implemented instead of C++11's std::condition_variable.
 *
 * This problem is mainly solved by using the POSIX function pthread_condattr_setclock to
 * set the attribute of the condition variable to CLOCK_MONOTONIC.
 */

/**
 * @file condition_variable.h
 * @brief POSIX monotonic-clock condition variable replacing std::condition_variable.
 *
 * @details
 * On older versions of GCC (and some libc implementations) @c std::condition_variable
 * internally uses @c CLOCK_REALTIME for timed waits, which means that NTP adjustments
 * or system-clock changes can cause spurious wakeups or missed timeouts.  This header
 * provides @c vlink::ConditionVariable and @c vlink::ConditionVariableAny as drop-in
 * replacements that explicitly configure the underlying @c pthread_cond_t to use
 * @c CLOCK_MONOTONIC via @c pthread_condattr_setclock.
 *
 * On non-POSIX platforms (Windows) the types are aliased to their @c std:: counterparts
 * because that bug does not apply there.
 *
 * Type aliases for convenience:
 * @code
 * vlink::condition_variable      == vlink::ConditionVariable
 * vlink::condition_variable_any  == vlink::ConditionVariableAny
 * @endcode
 *
 * The public API is identical to @c std::condition_variable and
 * @c std::condition_variable_any respectively, so existing code can
 * substitute the types without further changes.
 *
 * @note
 * - On POSIX the @c ConditionVariable is backed by a raw @c pthread_cond_t
 *   initialised with @c CLOCK_MONOTONIC.  It is not copyable or movable.
 * - @c ConditionVariableAny uses a shared internal @c ConditionVariable plus
 *   a @c std::mutex, making it compatible with any @c BasicLockable type.
 * - The @c ceil() / @c ceil_impl() helper functions ensure that duration
 *   conversions always round up, preventing premature timeouts.
 *
 * @par Example
 * @code
 * std::mutex mtx;
 * vlink::ConditionVariable cv;
 * bool ready = false;
 *
 * // Consumer thread:
 * {
 *   std::unique_lock lock(mtx);
 *   cv.wait_for(lock, std::chrono::milliseconds(200),
 *               [&]{ return ready; });
 * }
 *
 * // Producer thread:
 * {
 *   std::lock_guard lock(mtx);
 *   ready = true;
 * }
 * cv.notify_one();
 * @endcode
 */

#pragma once

#include <condition_variable>

#if !defined(VLINK_ENABLE_BASE_CONDITION) && defined(__unix__) && !defined(__CYGWIN__)
#define VLINK_ENABLE_BASE_CONDITION
#endif

#ifdef VLINK_ENABLE_BASE_CONDITION
#include <pthread.h>

#include <cerrno>
#include <chrono>
#include <memory>
#include <mutex>
#include <utility>

namespace vlink {

/**
 * @class ConditionVariable
 * @brief POSIX monotonic-clock condition variable (pthread-backed).
 *
 * @details
 * Drop-in replacement for @c std::condition_variable that uses
 * @c CLOCK_MONOTONIC for all timed waits, ensuring correctness in the
 * presence of NTP adjustments.
 *
 * The interface mirrors @c std::condition_variable exactly.
 */
class ConditionVariable final {
 public:
  /**
   * @brief The underlying native handle type.
   */
  using native_handle_type = pthread_cond_t*;

  ConditionVariable(const ConditionVariable&) noexcept = delete;

  ConditionVariable& operator=(const ConditionVariable&) noexcept = delete;

  /**
   * @brief Constructs and initialises the condition variable with CLOCK_MONOTONIC.
   */
  ConditionVariable() noexcept;

  /**
   * @brief Destroys the condition variable.
   */
  ~ConditionVariable() noexcept;

  /**
   * @brief Wakes one thread waiting on this condition variable.
   */
  void notify_one() noexcept;

  /**
   * @brief Wakes all threads waiting on this condition variable.
   */
  void notify_all() noexcept;

  /**
   * @brief Atomically releases @p lock and waits for a notification.
   *
   * @param lock  A held @c unique_lock<mutex> that is released during the wait.
   */
  void wait(std::unique_lock<std::mutex>& lock) noexcept;

  /**
   * @brief Waits until @p p returns @c true, using a spurious-wakeup loop.
   *
   * @tparam PredicateT  A callable returning @c bool with no arguments.
   * @param lock  A held @c unique_lock<mutex>.
   * @param p     Predicate checked after every wakeup.
   */
  template <typename PredicateT>
  void wait(std::unique_lock<std::mutex>& lock, PredicateT p) noexcept;

  /**
   * @brief Waits until @p atime or notification, using @c steady_clock.
   *
   * @tparam DurationT  The duration type of the time point.
   * @param lock   A held @c unique_lock<mutex>.
   * @param atime  Absolute time point (steady_clock).
   * @return @c cv_status::timeout if the deadline was reached, otherwise
   *         @c cv_status::no_timeout.
   */
  template <typename DurationT>
  std::cv_status wait_until(std::unique_lock<std::mutex>& lock,
                            const std::chrono::time_point<std::chrono::steady_clock, DurationT>& atime) noexcept;

  /**
   * @brief Waits until @p atime or notification, using @c system_clock.
   *
   * @details
   * Internally converts the @c system_clock time point to the @c steady_clock
   * equivalent to avoid NTP sensitivity.
   *
   * @tparam DurationT  The duration type of the time point.
   * @param lock   A held @c unique_lock<mutex>.
   * @param atime  Absolute time point (system_clock).
   * @return @c cv_status::timeout or @c cv_status::no_timeout.
   */
  template <typename DurationT>
  std::cv_status wait_until(std::unique_lock<std::mutex>& lock,
                            const std::chrono::time_point<std::chrono::system_clock, DurationT>& atime) noexcept;

  /**
   * @brief Waits until @p atime or notification, for any clock type.
   *
   * @details
   * Converts the given clock time point to a @c steady_clock deadline at the
   * moment of the call to eliminate clock drift during the wait.
   *
   * @tparam ClockT     The clock type.
   * @tparam DurationT  The duration type of the time point.
   * @param lock   A held @c unique_lock<mutex>.
   * @param atime  Absolute time point in @c ClockT.
   * @return @c cv_status::timeout or @c cv_status::no_timeout.
   */
  template <typename ClockT, typename DurationT>
  std::cv_status wait_until(std::unique_lock<std::mutex>& lock,
                            const std::chrono::time_point<ClockT, DurationT>& atime) noexcept;

  /**
   * @brief Waits until @p atime, a notification, or @p p returns @c true.
   *
   * @tparam ClockT     The clock type.
   * @tparam DurationT  The duration type of the time point.
   * @tparam PredicateT A callable returning @c bool.
   * @param lock   A held @c unique_lock<mutex>.
   * @param atime  Absolute deadline.
   * @param p      Predicate checked after every wakeup.
   * @return The final value of @p p when either the deadline passes or the
   *         predicate becomes @c true.
   */
  template <typename ClockT, typename DurationT, typename PredicateT>
  bool wait_until(std::unique_lock<std::mutex>& lock, const std::chrono::time_point<ClockT, DurationT>& atime,
                  PredicateT p) noexcept;

  /**
   * @brief Waits for at most @p rtime duration or until notified.
   *
   * @tparam RepT    The representation type of the duration.
   * @tparam PeriodT The period of the duration.
   * @param lock   A held @c unique_lock<mutex>.
   * @param rtime  Maximum wait duration.
   * @return @c cv_status::timeout or @c cv_status::no_timeout.
   */
  template <typename RepT, typename PeriodT>
  std::cv_status wait_for(std::unique_lock<std::mutex>& lock,
                          const std::chrono::duration<RepT, PeriodT>& rtime) noexcept;

  /**
   * @brief Waits for at most @p rtime or until @p p returns @c true.
   *
   * @tparam RepT       The representation type of the duration.
   * @tparam PeriodT    The period of the duration.
   * @tparam PredicateT A callable returning @c bool.
   * @param lock   A held @c unique_lock<mutex>.
   * @param rtime  Maximum wait duration.
   * @param p      Predicate checked after every wakeup.
   * @return The final value of @p p.
   */
  template <typename RepT, typename PeriodT, typename PredicateT>
  bool wait_for(std::unique_lock<std::mutex>& lock, const std::chrono::duration<RepT, PeriodT>& rtime,
                PredicateT p) noexcept;

  /**
   * @brief Returns the native @c pthread_cond_t* handle.
   *
   * @return Pointer to the internal @c pthread_cond_t.
   */
  [[nodiscard]] native_handle_type native_handle() noexcept;

 private:
  template <typename ToDurT, typename RepT, typename PeriodT>
  static constexpr ToDurT ceil(const std::chrono::duration<RepT, PeriodT>& d) noexcept;

  template <typename TpT, typename UpT>
  static constexpr TpT ceil_impl(const TpT& t, const UpT& u) noexcept;

  template <typename DurationT>
  std::cv_status wait_until_impl(std::unique_lock<std::mutex>& lock,
                                 const std::chrono::time_point<std::chrono::steady_clock, DurationT>& atime) noexcept;

  pthread_cond_t cond_{};
};

/**
 * @class ConditionVariableAny
 * @brief POSIX monotonic-clock condition variable compatible with any @c BasicLockable.
 *
 * @details
 * Drop-in replacement for @c std::condition_variable_any that uses
 * @c CLOCK_MONOTONIC internally.  Accepts any lockable type (not just
 * @c std::unique_lock).
 *
 * Internally uses a shared @c ConditionVariable wrapped in a @c std::mutex
 * to provide the @c BasicLockable compatibility layer.
 */
class ConditionVariableAny final {
 public:
  ConditionVariableAny(const ConditionVariableAny&) noexcept = delete;

  ConditionVariableAny& operator=(const ConditionVariableAny&) noexcept = delete;

  /**
   * @brief Constructs and initialises the condition variable.
   */
  ConditionVariableAny() noexcept;

  /**
   * @brief Destructor.
   */
  ~ConditionVariableAny() noexcept;

  /**
   * @brief Wakes one thread waiting on this condition variable.
   */
  void notify_one() noexcept;

  /**
   * @brief Wakes all threads waiting on this condition variable.
   */
  void notify_all() noexcept;

  /**
   * @brief Atomically releases @p lock and waits for a notification.
   *
   * @tparam LockT  Any @c BasicLockable type.
   * @param lock  The lock to release during the wait.
   */
  template <typename LockT>
  void wait(LockT& lock) noexcept;

  /**
   * @brief Waits until @p p returns @c true, using a spurious-wakeup loop.
   *
   * @tparam LockT      Any @c BasicLockable type.
   * @tparam PredicateT A callable returning @c bool.
   * @param lock  The lock to release during the wait.
   * @param p     Predicate checked after every wakeup.
   */
  template <typename LockT, typename PredicateT>
  void wait(LockT& lock, PredicateT p) noexcept;

  /**
   * @brief Waits until @p atime or notification.
   *
   * @tparam LockT      Any @c BasicLockable type.
   * @tparam ClockT     Clock type.
   * @tparam DurationT  Duration type.
   * @param lock   The lock to release during the wait.
   * @param atime  Absolute deadline.
   * @return @c cv_status::timeout or @c cv_status::no_timeout.
   */
  template <typename LockT, typename ClockT, typename DurationT>
  std::cv_status wait_until(LockT& lock, const std::chrono::time_point<ClockT, DurationT>& atime) noexcept;

  /**
   * @brief Waits until @p atime, notification, or @p p returns @c true.
   *
   * @tparam LockT      Any @c BasicLockable type.
   * @tparam ClockT     Clock type.
   * @tparam DurationT  Duration type.
   * @tparam PredicateT A callable returning @c bool.
   * @param lock   The lock to release during the wait.
   * @param atime  Absolute deadline.
   * @param p      Predicate.
   * @return The final value of @p p.
   */
  template <typename LockT, typename ClockT, typename DurationT, typename PredicateT>
  bool wait_until(LockT& lock, const std::chrono::time_point<ClockT, DurationT>& atime, PredicateT p) noexcept;

  /**
   * @brief Waits for at most @p rtime or until notified.
   *
   * @tparam LockT   Any @c BasicLockable type.
   * @tparam RepT    Duration representation type.
   * @tparam PeriodT Duration period.
   * @param lock   The lock to release during the wait.
   * @param rtime  Maximum wait duration.
   * @return @c cv_status::timeout or @c cv_status::no_timeout.
   */
  template <typename LockT, typename RepT, typename PeriodT>
  std::cv_status wait_for(LockT& lock, const std::chrono::duration<RepT, PeriodT>& rtime) noexcept;

  /**
   * @brief Waits for at most @p rtime or until @p p returns @c true.
   *
   * @tparam LockT      Any @c BasicLockable type.
   * @tparam RepT       Duration representation type.
   * @tparam PeriodT    Duration period.
   * @tparam PredicateT A callable returning @c bool.
   * @param lock   The lock to release during the wait.
   * @param rtime  Maximum wait duration.
   * @param p      Predicate.
   * @return The final value of @p p.
   */
  template <typename LockT, typename RepT, typename PeriodT, typename PredicateT>
  bool wait_for(LockT& lock, const std::chrono::duration<RepT, PeriodT>& rtime, PredicateT p) noexcept;

 private:
  template <typename ToDurT, typename RepT, typename PeriodT>
  static constexpr ToDurT ceil(const std::chrono::duration<RepT, PeriodT>& d) noexcept;

  template <typename TpT, typename UpT>
  static constexpr TpT ceil_impl(const TpT& t, const UpT& u) noexcept;

  template <typename LockT, typename DurationT>
  std::cv_status wait_until_impl(LockT& lock,
                                 const std::chrono::time_point<std::chrono::steady_clock, DurationT>& atime) noexcept;

  struct SharedState final {
    std::mutex mtx;
    ConditionVariable cv;
  };

  std::shared_ptr<SharedState> shared_state_;
};

////////////////////////////////////////////////////////////////
/// Details
////////////////////////////////////////////////////////////////

inline ConditionVariable::ConditionVariable() noexcept {
  pthread_condattr_t attr;

  pthread_condattr_init(&attr);
  pthread_condattr_setclock(&attr, CLOCK_MONOTONIC);
  pthread_cond_init(&cond_, &attr);
  pthread_condattr_destroy(&attr);
}

inline ConditionVariable::~ConditionVariable() noexcept { pthread_cond_destroy(&cond_); }

inline void ConditionVariable::notify_one() noexcept { pthread_cond_signal(&cond_); }

inline void ConditionVariable::notify_all() noexcept { pthread_cond_broadcast(&cond_); }

inline void ConditionVariable::wait(std::unique_lock<std::mutex>& lock) noexcept {
  pthread_cond_wait(&cond_, lock.mutex()->native_handle());
}

template <typename PredicateT>
inline void ConditionVariable::wait(std::unique_lock<std::mutex>& lock, PredicateT p) noexcept {
  while (!p()) {
    wait(lock);
  }
}

template <typename DurationT>
inline std::cv_status ConditionVariable::wait_until(
    std::unique_lock<std::mutex>& lock,
    const std::chrono::time_point<std::chrono::steady_clock, DurationT>& atime) noexcept {
  return wait_until_impl(lock, atime);
}

template <typename DurationT>
inline std::cv_status ConditionVariable::wait_until(
    std::unique_lock<std::mutex>& lock,
    const std::chrono::time_point<std::chrono::system_clock, DurationT>& atime) noexcept {
  return wait_until<std::chrono::system_clock, DurationT>(lock, atime);
}

template <typename ClockT, typename DurationT>
inline std::cv_status ConditionVariable::wait_until(std::unique_lock<std::mutex>& lock,
                                                    const std::chrono::time_point<ClockT, DurationT>& atime) noexcept {
  const typename ClockT::time_point c_entry = ClockT::now();
  const std::chrono::steady_clock::time_point s_entry = std::chrono::steady_clock::now();
  const auto delta = atime - c_entry;
  const auto s_atime = s_entry + ceil<std::chrono::steady_clock::duration>(delta);

  if (wait_until_impl(lock, s_atime) == std::cv_status::no_timeout) {
    return std::cv_status::no_timeout;
  }

  if (ClockT::now() < atime) {
    return std::cv_status::no_timeout;
  }

  return std::cv_status::timeout;
}

template <typename ClockT, typename DurationT, typename PredicateT>
inline bool ConditionVariable::wait_until(std::unique_lock<std::mutex>& lock,
                                          const std::chrono::time_point<ClockT, DurationT>& atime,
                                          PredicateT p) noexcept {
  while (!p()) {
    if (wait_until(lock, atime) == std::cv_status::timeout) {
      return p();
    }
  }

  return true;
}

template <typename RepT, typename PeriodT>
inline std::cv_status ConditionVariable::wait_for(std::unique_lock<std::mutex>& lock,
                                                  const std::chrono::duration<RepT, PeriodT>& rtime) noexcept {
  return wait_until(lock, std::chrono::steady_clock::now() + ceil<std::chrono::steady_clock::duration>(rtime));
}

template <typename RepT, typename PeriodT, typename PredicateT>
inline bool ConditionVariable::wait_for(std::unique_lock<std::mutex>& lock,
                                        const std::chrono::duration<RepT, PeriodT>& rtime, PredicateT p) noexcept {
  return wait_until(lock, std::chrono::steady_clock::now() + ceil<std::chrono::steady_clock::duration>(rtime),
                    std::move(p));
}

inline ConditionVariable::native_handle_type ConditionVariable::native_handle() noexcept { return &cond_; }

template <typename ToDurT, typename RepT, typename PeriodT>
inline constexpr ToDurT ConditionVariable::ceil(const std::chrono::duration<RepT, PeriodT>& d) noexcept {
  return ceil_impl(std::chrono::duration_cast<ToDurT>(d), d);
}

template <typename TpT, typename UpT>
inline constexpr TpT ConditionVariable::ceil_impl(const TpT& t, const UpT& u) noexcept {
  return (t < u) ? (t + TpT{1}) : t;
}

template <typename DurationT>
inline std::cv_status ConditionVariable::wait_until_impl(
    std::unique_lock<std::mutex>& lock,
    const std::chrono::time_point<std::chrono::steady_clock, DurationT>& atime) noexcept {
  if (std::chrono::steady_clock::now() >= atime) {
    return std::cv_status::timeout;
  }

  auto s = std::chrono::time_point_cast<std::chrono::seconds>(atime);
  auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(atime - s);

  struct timespec ts = {static_cast<std::time_t>(s.time_since_epoch().count()),
                        static_cast<long>(ns.count())};  // NOLINT(runtime/int, google-runtime-int)

  int ret = pthread_cond_timedwait(&cond_, lock.mutex()->native_handle(), &ts);

  return (ret == ETIMEDOUT) ? std::cv_status::timeout : std::cv_status::no_timeout;
}

inline ConditionVariableAny::ConditionVariableAny() noexcept : shared_state_(std::make_shared<SharedState>()) {}

inline ConditionVariableAny::~ConditionVariableAny() noexcept = default;

inline void ConditionVariableAny::notify_one() noexcept {
  std::shared_ptr<SharedState> state = shared_state_;
  std::lock_guard lock(state->mtx);
  state->cv.notify_one();
}

inline void ConditionVariableAny::notify_all() noexcept {
  std::shared_ptr<SharedState> state = shared_state_;
  std::lock_guard lock(state->mtx);
  state->cv.notify_all();
}

template <typename LockT>
inline void ConditionVariableAny::wait(LockT& lock) noexcept {
  std::shared_ptr<SharedState> state = shared_state_;
  std::unique_lock internal_lock(state->mtx);
  lock.unlock();

  struct UnlockGuard final {
    LockT& lock_ref;

    ~UnlockGuard() noexcept {
      try {
        lock_ref.lock();
      } catch (std::exception&) {
      }
    }
  } guard{lock};

  state->cv.wait(internal_lock);
}

template <typename LockT, typename PredicateT>
inline void ConditionVariableAny::wait(LockT& lock, PredicateT p) noexcept {
  while (!p()) {
    wait(lock);
  }
}

template <typename LockT, typename ClockT, typename DurationT>
inline std::cv_status ConditionVariableAny::wait_until(
    LockT& lock, const std::chrono::time_point<ClockT, DurationT>& atime) noexcept {
  if constexpr (std::is_same_v<ClockT, std::chrono::steady_clock>) {
    return wait_until_impl(lock, atime);
  } else {
    const typename ClockT::time_point c_entry = ClockT::now();
    const std::chrono::steady_clock::time_point s_entry = std::chrono::steady_clock::now();
    const auto delta = atime - c_entry;
    const auto s_atime = s_entry + ceil<std::chrono::steady_clock::duration>(delta);

    if (wait_until_impl(lock, s_atime) == std::cv_status::no_timeout) {
      return std::cv_status::no_timeout;
    }

    if (ClockT::now() < atime) {
      return std::cv_status::no_timeout;
    }

    return std::cv_status::timeout;
  }
}

template <typename LockT, typename ClockT, typename DurationT, typename PredicateT>
inline bool ConditionVariableAny::wait_until(LockT& lock, const std::chrono::time_point<ClockT, DurationT>& atime,
                                             PredicateT p) noexcept {
  while (!p()) {
    if (wait_until(lock, atime) == std::cv_status::timeout) {
      return p();
    }
  }

  return true;
}

template <typename LockT, typename RepT, typename PeriodT>
inline std::cv_status ConditionVariableAny::wait_for(LockT& lock,
                                                     const std::chrono::duration<RepT, PeriodT>& rtime) noexcept {
  return wait_until(lock, std::chrono::steady_clock::now() + ceil<std::chrono::steady_clock::duration>(rtime));
}

template <typename LockT, typename RepT, typename PeriodT, typename PredicateT>
inline bool ConditionVariableAny::wait_for(LockT& lock, const std::chrono::duration<RepT, PeriodT>& rtime,
                                           PredicateT p) noexcept {
  return wait_until(lock, std::chrono::steady_clock::now() + ceil<std::chrono::steady_clock::duration>(rtime),
                    std::move(p));
}

template <typename ToDurT, typename RepT, typename PeriodT>
inline constexpr ToDurT ConditionVariableAny::ceil(const std::chrono::duration<RepT, PeriodT>& d) noexcept {
  return ceil_impl(std::chrono::duration_cast<ToDurT>(d), d);
}

template <typename TpT, typename UpT>
inline constexpr TpT ConditionVariableAny::ceil_impl(const TpT& t, const UpT& u) noexcept {
  return (t < u) ? (t + TpT{1}) : t;
}

template <typename LockT, typename DurationT>
inline std::cv_status ConditionVariableAny::wait_until_impl(
    LockT& lock, const std::chrono::time_point<std::chrono::steady_clock, DurationT>& atime) noexcept {
  if (std::chrono::steady_clock::now() >= atime) {
    return std::cv_status::timeout;
  }

  std::shared_ptr<SharedState> state = shared_state_;
  std::unique_lock internal_lock(state->mtx);
  lock.unlock();

  struct UnlockGuard final {
    LockT& lock_ref;

    ~UnlockGuard() noexcept {
      try {
        lock_ref.lock();
      } catch (std::exception&) {
      }
    }
  } guard{lock};

  auto s = std::chrono::time_point_cast<std::chrono::seconds>(atime);
  auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(atime - s);

  struct timespec ts = {static_cast<std::time_t>(s.time_since_epoch().count()),
                        static_cast<long>(ns.count())};  // NOLINT(runtime/int, google-runtime-int)

  int ret = pthread_cond_timedwait(state->cv.native_handle(), internal_lock.mutex()->native_handle(), &ts);

  return (ret == ETIMEDOUT) ? std::cv_status::timeout : std::cv_status::no_timeout;
}

/**
 * @typedef condition_variable
 * @brief Alias for @c ConditionVariable (monotonic-clock based).
 */
using condition_variable = ConditionVariable;

/**
 * @typedef condition_variable_any
 * @brief Alias for @c ConditionVariableAny (monotonic-clock based).
 */
using condition_variable_any = ConditionVariableAny;

}  // namespace vlink

#else

namespace vlink {

using ConditionVariable = std::condition_variable;
using ConditionVariableAny = std::condition_variable_any;
using condition_variable = ConditionVariable;
using condition_variable_any = ConditionVariableAny;

}  // namespace vlink

#endif
