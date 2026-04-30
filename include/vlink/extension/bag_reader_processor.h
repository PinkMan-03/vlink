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
 * @brief Time-ordered message buffer for smoothing bag playback across split files.
 *
 * @details
 * @c BagReaderProcessor accepts messages pushed from one or more @c BagReader instances
 * and delivers them to an @c OutputCallback in ascending timestamp order.  It uses a
 * time-windowed cache to absorb out-of-order messages that can arise when reading
 * consecutive split files concurrently.
 *
 * The processor maintains an internal sorted queue.  Messages are held for at least
 * @c Config::min_cache_time milliseconds before being flushed, allowing late-arriving
 * messages from the current split to be sorted before delivery.
 *
 * @par Typical usage
 * @code
 * vlink::BagReaderProcessor processor;
 * processor.register_output_callback([](int64_t ts, const std::string& url,
 *                                       vlink::ActionType action, const vlink::Bytes& data) {
 *     // process in order
 * });
 *
 * // Feed from multiple readers:
 * reader_a->register_output_callback([&](auto ts, auto& url, auto action, auto& data) {
 *     processor.push(ts, url, action, data);
 * });
 * reader_b->register_output_callback([&](auto ts, auto& url, auto action, auto& data) {
 *     processor.push(ts, url, action, data);
 * });
 * @endcode
 */

#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>

#include "../base/bytes.h"
#include "../impl/types.h"

namespace vlink {

/**
 * @class BagReaderProcessor
 * @brief Time-sorted message relay that buffers and orders messages before delivery.
 *
 * @details
 * Thread-safe.  Multiple threads may call @c push() concurrently.
 */
class VLINK_EXPORT BagReaderProcessor {
 public:
  /**
   * @brief Callback type fired for each message in timestamp order.
   *
   * @details
   * Called from an internal processing thread after the cache window has elapsed.
   */
  using OutputCallback =
      std::function<void(int64_t timestamp, const std::string& url, ActionType action_type, const Bytes& data)>;

  /**
   * @struct Config
   * @brief Configuration for the time-ordered message cache.
   */
  struct Config final {
    int64_t min_cache_time{500};                    ///< Minimum cache time in milliseconds before flushing.
    int64_t max_cache_size{1024LL * 1024LL * 256};  ///< Maximum cache size in bytes (256 MiB).

    Config() {}  // NOLINT(modernize-use-equals-default)
  };

  /**
   * @brief Constructs the processor with the given @p config.
   *
   * @param config  Cache time and size limits.
   */
  explicit BagReaderProcessor(const Config& config = Config());

  /**
   * @brief Destructor -- flushes remaining cached messages and stops the processing thread.
   */
  ~BagReaderProcessor();

  /**
   * @brief Registers the callback that receives time-ordered messages.
   *
   * @details
   * Only one callback may be registered.  A subsequent call replaces the previous one.
   *
   * @param output_callback  Callback invoked for each message in order.
   */
  void register_output_callback(OutputCallback&& output_callback);

  /**
   * @brief Pushes a message into the time-ordered cache.
   *
   * @details
   * Thread-safe.  The message is inserted into an internal sorted queue keyed by
   * @p timestamp.  Messages are delivered to the @c OutputCallback after the
   * @c min_cache_time window has elapsed.
   *
   * @param timestamp    Message timestamp in microseconds.
   * @param url          Topic URL string.
   * @param action_type  Action type.
   * @param data         Serialized payload bytes.
   */
  void push(int64_t timestamp, const std::string& url, ActionType action_type, const Bytes& data);

 private:
  bool on_check();

  void on_output(std::unique_lock<std::mutex>& lock);

  void on_run();

  void on_exec(bool at_end);

  std::unique_ptr<struct BagReaderProcessorImpl> impl_;
};

}  // namespace vlink
