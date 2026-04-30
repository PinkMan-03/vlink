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
 * @file calculate_sample.h
 * @brief Per-GUID cumulative sample loss tracker for VLink subscribers.
 *
 * @details
 * @c CalculateSample maintains per-sender (GUID) sequence-number state to
 * detect gaps in the message stream.  It is used by DDS and other transports
 * that carry monotonically-increasing sequence numbers with their messages.
 *
 * @par Algorithm
 * For each GUID, the tracker records:
 * - @c first -- the first sequence number seen from this sender.
 * - @c expected -- the next sequence number expected.
 * - @c lost -- cumulative count of skipped sequence numbers.
 *
 * On each @c update(seq, guid) call:
 * - If @c expected == 0 or the gap looks like a reset, the state is
 *   re-initialised (no loss recorded for the first message).
 * - Otherwise, @c lost is incremented by the difference
 *   @c (seq - expected), and @c expected is set to @c seq + 1.
 *
 * @par Thread Safety
 * All public methods are thread-safe; @c update() uses an exclusive lock and
 * @c get_total() / @c get_lost() use a shared lock.
 *
 * @note
 * - A gap larger than @c UINT32_MAX between consecutive sequence numbers is
 *   treated as a counter reset rather than a loss event.
 * - @c get_total() returns the sum over all GUIDs of
 *   @c (expected - first), i.e. the number of expected messages.
 */

#pragma once

#include <cstdint>
#include <shared_mutex>
#include <unordered_map>

#include "../base/macros.h"

namespace vlink {

/**
 * @class CalculateSample
 * @brief Thread-safe, per-GUID cumulative sample loss counter.
 *
 * @details
 * Instantiated once per @c SubscriberImpl or @c GetterImpl that has
 * latency/loss tracking enabled.  The @c guid parameter allows a single
 * subscriber to track messages from multiple publishers independently.
 */
class VLINK_EXPORT CalculateSample final {
 public:
  /**
   * @brief Default constructor.
   */
  CalculateSample() noexcept;

  /**
   * @brief Destructor.
   */
  ~CalculateSample() noexcept;

  /**
   * @brief Processes an incoming sequence number for the given sender.
   *
   * @details
   * Detects gaps in the sequence and accumulates the loss count.
   * Out-of-order or wrap-around sequences larger than @c UINT32_MAX are
   * treated as resets and do not increment the loss counter.
   *
   * @param seq   Sequence number of the received message.
   * @param guid  Sender identifier (GUID).  Use @c 0 for single-sender streams.
   */
  void update(uint64_t seq, uint64_t guid = 0) noexcept;

  /**
   * @brief Returns the total number of expected samples across all senders.
   *
   * @details
   * Computed as the sum of @c (expected - first) for every tracked GUID.
   * This includes both successfully delivered and lost samples.
   *
   * @return Total expected sample count, or 0 if no data has been received.
   */
  [[nodiscard]] uint64_t get_total() const noexcept;

  /**
   * @brief Returns the cumulative number of lost samples across all senders.
   *
   * @details
   * Accumulated from every gap detected since the last reset.
   *
   * @return Total lost sample count.
   */
  [[nodiscard]] uint64_t get_lost() const noexcept;

 private:
  struct Number final {
    uint64_t first{0};
    uint64_t expected{0};
    uint64_t lost{0};
  };

  std::unordered_map<uint64_t, Number> number_map_;
  mutable std::shared_mutex mtx_;

  VLINK_DISALLOW_COPY_AND_ASSIGN(CalculateSample)
};

}  // namespace vlink
