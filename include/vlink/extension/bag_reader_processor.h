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
 * @file bag_reader_processor.h
 * @brief Bounded reorder buffer that merges multiple @c BagReader streams into one ordered flow.
 *
 * @details
 * When several bag readers are played in parallel (typically across split files or several
 * recordings being merged), their frames arrive interleaved and are not guaranteed to be
 * globally sorted by timestamp.  @c BagReaderProcessor accepts every frame through
 * @c push() and re-emits them through a single @c OutputCallback in timestamp order, while
 * keeping latency bounded so the pipeline cannot stall on a slow stream.
 *
 * Pipeline overview:
 *
 * @verbatim
 *     bag_reader_a ---\
 *                      \   push()      sorted cache (>= min_cache_time)
 *     bag_reader_b -----+--------->  +-----------------------------+
 *                      /             |  timestamp-ordered queue    |  on_output()
 *     bag_reader_c ---/              +-----------------------------+ ----------> OutputCallback
 * @endverbatim
 *
 * The processor flushes the oldest frame only after the time span between the oldest and
 * newest cached frames reaches @c Config::min_cache_time, giving late arrivals a chance
 * to slot in ahead of more recent frames.  A wall-clock fallback drains the cache at the
 * tail of a session so playback can terminate even when one input goes silent.
 *
 * @par Example
 * @code
 * vlink::BagReaderProcessor processor;
 * processor.register_output_callback([](int64_t ts, const std::string& url,
 *                                       vlink::ActionType action, const vlink::Bytes& data) {
 *   (void)action;
 *   VLOG_I("ordered ts=", ts, " url=", url, " bytes=", data.size());
 * });
 *
 * auto fan_in = [&processor](int64_t ts, const std::string& url,
 *                            vlink::ActionType action, const vlink::Bytes& data) {
 *   processor.push(ts, url, action, data);
 * };
 * reader_a->register_output_callback(fan_in);
 * reader_b->register_output_callback(fan_in);
 * @endcode
 */

#pragma once

#include <cstdint>
#include <memory>
#include <mutex>
#include <string>

#include "../base/bytes.h"
#include "../base/functional.h"
#include "../impl/types.h"

namespace vlink {

/**
 * @class BagReaderProcessor
 * @brief Time-sorted relay buffer that smooths interleaved bag-reader outputs.
 *
 * @details
 * Thread-safe in the sense that @c push() may be called concurrently from multiple
 * reader threads; delivery to the @c OutputCallback happens on a dedicated worker
 * thread owned by the processor.
 */
class VLINK_EXPORT BagReaderProcessor {
 public:
  /**
   * @brief Callback signature receiving one ordered frame.
   *
   * @details
   * Invoked on the processor's internal worker thread once @c min_cache_time of
   * timestamps have accumulated ahead of the candidate frame.
   */
  using OutputCallback =
      MoveFunction<void(int64_t timestamp, const std::string& url, ActionType action_type, const Bytes& data)>;

  /**
   * @struct Config
   * @brief Tunables controlling the reorder buffer behaviour.
   */
  struct Config final {
    int64_t min_cache_time{500};                    ///< Minimum head-to-tail timestamp span (ms) before flushing.
    int64_t max_cache_size{1024LL * 1024LL * 256};  ///< Maximum total bytes held in the cache (default 256 MiB).

    Config() {}  // NOLINT(modernize-use-equals-default)
  };

  /**
   * @brief Builds the processor and spawns its worker thread.
   *
   * @param config Cache time-window and memory-budget tunables.
   */
  explicit BagReaderProcessor(const Config& config = Config());

  /**
   * @brief Drains remaining frames and joins the worker thread.
   */
  ~BagReaderProcessor();

  /**
   * @brief Sets the single sink receiving ordered frames.
   *
   * @details
   * Replaces any previously registered callback; only the most recent registration
   * remains effective.
   *
   * @param output_callback Sink invoked once per frame on the worker thread.
   */
  void register_output_callback(OutputCallback&& output_callback);

  /**
   * @brief Inserts a frame into the time-sorted cache.
   *
   * @details
   * Safe to call from any thread.  Frames are inserted in timestamp order; an entry
   * leaves the cache only after the @c Config::min_cache_time window is reached or
   * after a wall-clock timeout when the producer side has gone quiet.
   *
   * @param timestamp   Frame timestamp in microseconds.
   * @param url         Source VLink URL.
   * @param action_type Recorded action kind.
   * @param data        Serialised payload bytes.
   */
  void push(int64_t timestamp, const std::string& url, ActionType action_type, const Bytes& data);

 private:
  bool on_check();

  void on_output(std::unique_lock<std::mutex>& lock, bool at_end);

  void on_run();

  void on_exec(bool at_end);

  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace vlink
