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
 * @file bag_writer.h
 * @brief Abstract base class for VLink bag file recording with split, compression and global writer support.
 *
 * @details
 * @c BagWriter is an abstract @c MessageLoop-based recorder that captures VLink messages
 * (URL + serialisation type + payload) to a bag file.  Concrete implementations are
 * @c DatabaseWriter (SQLite-backed) and @c McapWriter (MCAP-format).
 *
 * Key features:
 * - Asynchronous recording via the inherited @c MessageLoop queue.
 * - Pluggable compression: none, auto, Zstd, LZ4, LZAV.
 * - File splitting by size or by time, with optional time-stamped names.
 * - WAL (Write-Ahead Log) mode for crash resilience.
 * - Global singleton writer activated by the @c VLINK_BAG_PATH environment variable.
 * - Schema embedding for offline introspection.
 *
 * @par Creating a writer
 * @code
 * auto writer = vlink::BagWriter::create("/data/log.vdb");
 * writer->async_run();
 * writer->push("dds://my/topic", "demo.proto.PointCloud", vlink::SchemaType::kProtobuf,
 *              vlink::ActionType::kPublish, data);
 * @endcode
 *
 * @par Global writer
 * @code
 * // Set VLINK_BAG_PATH=/data/log.vdb before launching the process.
 * // Then retrieve the global instance anywhere:
 * auto* gw = vlink::BagWriter::global_get();
 * if (gw) {
 *     gw->push("intra://my/topic", "raw", vlink::SchemaType::kRaw, vlink::ActionType::kPublish, data);
 * }
 * @endcode
 *
 * @note
 * - @c create() selects the concrete implementation based on the file extension
 *   (@c .vcap / @c .vcapx -> McapWriter, otherwise -> DatabaseWriter).
 * - @c push() is thread-safe and non-blocking; recording is done on the loop thread.
 * - The @c immediate flag bypasses the task queue and writes synchronously (use with care).
 */

#pragma once

#include <chrono>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_set>

#include "../base/bytes.h"
#include "../base/functional.h"
#include "../base/macros.h"
#include "../base/message_loop.h"
#include "../impl/types.h"

namespace vlink {

class SchemaPluginInterface;

/**
 * @class BagWriter
 * @brief Abstract asynchronous message recorder backed by a @c MessageLoop event queue.
 *
 * @details
 * Must be constructed via @c create() (for managed lifetime) or directly (for custom ownership).
 * After construction call @c async_run() to start the recording loop, then use @c push() to
 * record messages.
 */
class VLINK_EXPORT BagWriter : public MessageLoop {
 public:
  /**
   * @brief Compression algorithm applied to each recorded payload.
   *
   * | Kind            | Algorithm    | Notes                                   |
   * | --------------- | ------------ | --------------------------------------- |
   * | kCompressNone   | No compress  | Raw bytes stored as-is                  |
   * | kCompressAuto   | Auto select  | Picks best algorithm per payload        |
   * | kCompressZstd   | Zstandard    | Good ratio, moderate speed              |
   * | kCompressLz4    | LZ4          | Fast compression/decompression          |
   * | kCompressLzav   | LZAV         | Fast, lightweight, built-in             |
   */
  enum CompressType : uint8_t {
    kCompressNone = 0,  ///< No compression.
    kCompressAuto = 1,  ///< Automatic algorithm selection.
    kCompressZstd = 2,  ///< Zstandard compression.
    kCompressLz4 = 3,   ///< LZ4 compression.
    kCompressLzav = 4,  ///< LZAV built-in compression.
  };

  /**
   * @struct Config
   * @brief Configuration for recording behaviour, splitting, compression, and limits.
   *
   * @details
   * All size fields are in bytes; all time fields are in milliseconds unless noted otherwise.
   */
  struct Config final {
    std::string tag_name;                                      ///< Optional tag name stored in the bag header.
    CompressType compress{CompressType::kCompressNone};        ///< Compression algorithm.
    bool wal_mode{false};                                      ///< Enable SQLite WAL mode for crash resilience.
    bool enable_limit{false};                                  ///< Enable max_row_count / max_bytes_size limits.
    bool split_name_by_time{false};                            ///< Append timestamp to split file names.
    bool sync_mode{false};                                     ///< Enable synchronous writes to disk.
    bool optimize_on_exit{false};                              ///< Run VACUUM/optimise on file close.
    int64_t max_row_count{5'000'000'000LL};                    ///< Max rows before splitting (if enable_limit).
    int64_t max_bytes_size{1024LL * 1024LL * 1024LL * 512LL};  ///< Max file bytes before splitting (if enable_limit).
    int64_t split_by_size{1024LL * 1024LL * 1024LL * 1LL};     ///< Split file when it reaches this size (bytes).
    int64_t split_by_time{0};                                  ///< Split file every N milliseconds.  0 = disabled.
    int64_t begin_time{0};                                     ///< Recording start timestamp (ms).  0 = now.
    int64_t cache_size{1024LL * 1024LL * 4};                   ///< SQLite page cache size (bytes).
    int64_t compress_start_size{128};                          ///< Minimum payload size (bytes) to compress.
    int64_t compress_level{3};                                 ///< Compression level (codec-specific).
    int64_t max_task_depth{20000};                             ///< Max pending write tasks in the queue.
    int64_t max_memory_size{1024LL * 1024LL * 1024LL * 2LL};   ///< Max in-memory cache size (bytes).
    int64_t start_timestamp{0};                                ///< Override the bag start timestamp (ms since epoch).
    std::unordered_set<std::string> ignore_compress_urls;      ///< URLs whose payloads are never compressed.

    Config() {}  // NOLINT(modernize-use-equals-default)
  };

  /**
   * @brief Callback fired when a split occurs.
   *
   * @details
   * Called with the zero-based split index and the path of the newly created file.
   * The @c before parameter of @c register_split_callback() controls whether the
   * callback fires before or after the new file is opened.
   */
  using SplitCallback = vlink::MoveFunction<void(int split_index, const std::string& split_filename)>;

  /**
   * @brief Callback that resolves a serialisation type string to a @c SchemaData.
   *
   * @details
   * When a new URL with an unknown @c ser_type appears, this callback is invoked to
   * retrieve the corresponding schema for embedding in the bag. The extra
   * @c schema_type hint lets callers distinguish schema families that share
   * the same concrete type name.
   */
  using SchemaCallback = vlink::MoveFunction<SchemaData(const std::string& ser_type, SchemaType schema_type)>;

  /**
   * @brief System clock type used for file-name timestamp generation.
   */
  using SystemClock = std::chrono::time_point<std::chrono::system_clock, std::chrono::milliseconds>;

  /**
   * @brief Creates a concrete @c BagWriter instance for @p path.
   *
   * @details
   * Selects the implementation based on the file extension:
   * - @c .vcap / @c .vcapx -- @c McapWriter (MCAP format)
   * - All other extensions -- @c DatabaseWriter (SQLite)
   * The returned writer has not yet started its event loop; call @c async_run().
   *
   * @param path    Output file path.
   * @param config  Recording configuration.  Defaults to @c Config{}.
   * @return Shared pointer to the new writer.
   */
  [[nodiscard]] static std::shared_ptr<BagWriter> create(const std::string& path, const Config& config = {});

  /**
   * @brief Returns an existing writer for @p path, or creates and starts a new one.
   *
   * @details
   * Searches the global writer registry.  If a writer matching @p path is alive,
   * returns a shared pointer to it.  Otherwise creates a new writer for @p path,
   * calls @c async_run() on it, registers it in the global registry, and returns it.
   * The writer is automatically removed from the registry when the last shared
   * pointer to it is released.
   *
   * @param path  Output file path.
   * @return Shared pointer to the writer (never @c nullptr).
   */
  [[nodiscard]] static std::shared_ptr<BagWriter> filter_get(const std::string& path);

  /**
   * @brief Returns the process-global @c BagWriter activated by the @c VLINK_BAG_PATH environment variable.
   *
   * @details
   * The global writer is created automatically on first access if @c VLINK_BAG_PATH is set.
   * Returns @c nullptr if the environment variable is not set.
   *
   * @return Raw pointer to the global writer, or @c nullptr.
   */
  static BagWriter* global_get();

  /**
   * @brief Constructs a @c BagWriter for @p path with the given @p config.
   *
   * @details
   * Opens or creates the output file.  Must call @c async_run() before writing.
   *
   * @param path    Output file path.
   * @param config  Recording configuration.
   */
  explicit BagWriter(const std::string& path, const Config& config = {});

  /**
   * @brief Destructor -- stops the recording loop and flushes pending writes.
   */
  virtual ~BagWriter();  // NOLINT(modernize-use-override)

  /**
   * @brief Registers a callback invoked when a file split occurs.
   *
   * @param callback  Called with (split_index, new_filename) on each split.
   * @param before    If @c true, the callback fires before the new file is opened;
   *                  if @c false, it fires after.
   */
  virtual void register_split_callback(SplitCallback&& callback, bool before) = 0;

  /**
   * @brief Registers a callback that resolves serialisation type strings to @c SchemaData.
   *
   * @details
   * Called when a @c push() introduces a URL with an unknown @c ser_type.
   *
   * @param callback  Function mapping ser_type string to @c SchemaData.
   */
  virtual void register_schema_callback(SchemaCallback&& callback) = 0;

  /**
   * @brief Embeds a @c SchemaData into the bag for later offline introspection.
   *
   * @param schema_data  Schema descriptor to store.
   * @param immediate    If @c true, merges synchronously; otherwise enqueues.
   * @return             @c false only when the immediate merge fails.
   */
  virtual bool push_schema(const SchemaData& schema_data, bool immediate = false) = 0;

  /**
   * @brief Records one message to the bag.
   *
   * @details
   * The message is enqueued on the recording loop and written asynchronously.
   * If @p microseconds_timestamp is @c nullptr, the current system time is used.
   *
   * @param url                    VLink URL of the topic (e.g., @c "dds://my/topic").
   * @param ser_type               Serialisation type string (e.g., @c "demo.proto.PointCloud", @c "raw").
   * @param schema_type            Coarse schema family for the payload.
   * @param action_type            Action type (@c kPublish, @c kRequest, etc.).
   * @param data                   Serialized payload bytes.
   * @param microseconds_timestamp Optional pointer to a custom timestamp (microseconds).
   * @param immediate              If @c true, writes synchronously bypassing the queue.
   * @return Sequence number (monotonically increasing) of the recorded message,
   *         or a negative value on error.
   */
  virtual int64_t push(const std::string& url, const std::string& ser_type, SchemaType schema_type,
                       ActionType action_type, const Bytes& data, int64_t* microseconds_timestamp = nullptr,
                       bool immediate = false) = 0;

  /**
   * @brief Returns @c true if the writer is actively recording to disk.
   *
   * @return @c true if the backing file is open and being written.
   */
  [[nodiscard]] virtual bool is_dumping() const = 0;

  /**
   * @brief Returns @c true if the writer is in split-file mode.
   *
   * @return @c true when @c Config::split_by_size or @c Config::split_by_time is non-zero.
   */
  [[nodiscard]] virtual bool is_split_mode() const = 0;

  /**
   * @brief Returns the zero-based index of the current split file.
   *
   * @details
   * Returns 0 if split mode is not active.
   *
   * @return Current split file index.
   */
  [[nodiscard]] virtual int get_split_index() const = 0;

  /**
   * @brief Sets the expected message loss ratio for a given URL.
   *
   * @details
   * Stored as metadata in the bag.  Used for post-processing diagnostics to
   * distinguish intentional drops from unexpected loss.
   *
   * @param url   Topic URL.
   * @param loss  Loss ratio in the range [0.0, 1.0].
   */
  virtual void set_url_loss(const std::string& url, double loss) = 0;

 protected:
  void get_url_meta(const std::string& url, const std::string& ser, int& url_index, int& ser_index) const;

  void get_url_meta(int url_index, int ser_index, std::string& url, std::string& ser) const;

  static const std::string& get_default_tag_name();

  static const std::string& get_default_app_name();

  static SchemaPluginInterface* get_schema_interface();

  static int32_t get_default_timezone_diff();

  static std::string_view convert_action(ActionType type);

  static std::string get_format_date(SystemClock* current = nullptr, bool file_format = false);

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;

  VLINK_DISALLOW_COPY_AND_ASSIGN(BagWriter)
};

}  // namespace vlink
