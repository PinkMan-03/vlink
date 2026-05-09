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
 * @file bag_reader.h
 * @brief Abstract base class for VLink bag file playback with time-based seeking and rate control.
 *
 * @details
 * @c BagReader is an abstract @c MessageLoop-based player that reads VLink bag files and
 * replays recorded messages through an @c OutputCallback.  Concrete implementations are
 * @c DatabaseReader (SQLite-backed) and @c McapReader (MCAP-format).
 *
 * Playback features:
 * - Configurable playback rate (e.g., @c rate=2.0 for 2x speed).
 * - Loop playback via the @c times field (@c kInfinite = -1 for endless loop).
 * - Time-range filtering via @c begin_time and @c end_time.
 * - Jump-to-timestamp seeking with optional forced play.
 * - Per-URL output filtering via @c Config::filter_urls whitelist.
 * - Background integrity check, reindex, and fix operations via @c std::future.
 * - Plugin interface for custom URL/type mapping.
 *
 * @par Playback example
 * @code
 * auto reader = vlink::BagReader::create("/data/log.vdb");
 * reader->register_output_callback([](int64_t ts, const std::string& url,
 *                                     vlink::ActionType action, const vlink::Bytes& data) {
 *     VLOG_I("ts=", ts, " url=", url, " size=", data.size());
 * });
 * reader->async_run();
 *
 * vlink::BagReader::Config cfg;
 * cfg.rate = 1.0;
 * cfg.times = vlink::BagReader::kInfinite;
 * reader->play(cfg);
 * @endcode
 *
 * @note
 * - Call @c async_run() before @c play().
 * - @c check(), @c reindex(), and @c fix() run on a background thread and return
 *   a @c std::future<bool> for result polling.
 * - The file format is auto-detected from the extension by @c create()
 *   (@c .vcap / @c .vcapx -> McapReader, otherwise -> DatabaseReader).
 */

#pragma once

#include <cstdint>
#include <future>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "../base/bytes.h"
#include "../base/functional.h"
#include "../base/macros.h"
#include "../base/message_loop.h"
#include "../impl/types.h"
#include "./bag_reader_plugin_interface.h"

namespace vlink {

/**
 * @class BagReader
 * @brief Abstract VLink bag file player with time control, seeking, and integrity tools.
 *
 * @details
 * Inherits @c MessageLoop to drive playback on a dedicated thread.
 * Concrete subclasses (@c DatabaseReader, @c McapReader) implement format-specific I/O.
 */
class VLINK_EXPORT BagReader : public MessageLoop {
 public:
  /**
   * @brief Sentinel value for the @c Config::times field to indicate endless loop playback.
   */
  static constexpr int kInfinite{-1};

  /**
   * @brief Playback state of the reader.
   *
   * | State    | Description                                   |
   * | -------- | --------------------------------------------- |
   * | kStopped  | Not playing; reset to beginning               |
   * | kPaused  | Playback suspended; can be resumed            |
   * | kPlaying | Actively delivering messages to the callback  |
   */
  enum Status : uint8_t {
    kStopped = 0,  ///< Stopped (not playing).
    kPaused = 1,   ///< Paused mid-playback.
    kPlaying = 2,  ///< Actively playing.
  };

  /**
   * @struct Info
   * @brief Metadata extracted from the bag file header and index.
   *
   * @details
   * Available after construction.  Contains file-level metadata and per-URL statistics.
   */
  struct Info final {
    /**
     * @struct UrlMeta
     * @brief Per-URL statistics extracted from the bag index.
     */
    struct VLINK_EXPORT UrlMeta final {
      bool valid{false};     ///< @c true if this UrlMeta was successfully populated.
      int index{0};          ///< Numeric index of this URL in the bag's URL table.
      std::string url;       ///< Full VLink URL string.
      std::string url_type;  ///< Communication model type (e.g., "Event", "Method", "Field").
      std::string ser_type;  ///< Serialisation type string (e.g., "demo.proto.PointCloud", "raw").
      SchemaType schema_type{SchemaType::kUnknown};  ///< Coarse schema family associated with this URL.
      size_t count{0};                               ///< Total number of messages recorded for this URL.
      size_t size{0};                                ///< Total compressed bytes recorded for this URL.
      double freq{0};                                ///< Average message frequency (Hz).
      double loss{0};                                ///< Declared message loss ratio [0.0, 1.0].

      /**
       * @brief Comparison operator for sorting UrlMeta entries.
       *
       * @details
       * Sorts primarily by the URL's transport priority, then by URL string
       * lexicographically, and finally by numeric index as a tie-breaker.
       *
       * @param target  Right-hand side.
       * @return @c true if @c *this should sort before @p target.
       */
      bool operator<(const UrlMeta& target) const noexcept;
    };

    std::string file_name;           ///< Absolute path to the bag file.
    std::string tag_name;            ///< Tag name stored in the bag header.
    std::string version;             ///< Bag format version string.
    std::string storage_type;        ///< Storage backend (e.g., "sqlite", "mcap").
    std::string compression_type;    ///< Compression algorithm used (e.g., "lzav", "zstd").
    std::string time_accuracy;       ///< Timestamp resolution (e.g., "us", "ns").
    std::string process_name;        ///< Name of the recording process.
    std::string date_time;           ///< Recording start date/time string.
    bool has_completed{false};       ///< @c true if the recording was cleanly finalized.
    bool has_idx_elapsed{false};     ///< @c true if an elapsed-time index is present.
    bool has_idx_url{false};         ///< @c true if a URL index is present.
    bool has_schema{false};          ///< @c true if any schemas are embedded.
    int32_t timezone{0};             ///< Timezone offset in minutes from UTC.
    int64_t start_timestamp{0};      ///< Recording start timestamp (milliseconds since epoch).
    int64_t blank_duration{0};       ///< Total blank (gap) duration (milliseconds).
    int64_t total_duration{0};       ///< Total recording duration (milliseconds).
    int64_t file_size{0};            ///< File size in bytes.
    int64_t total_raw_size{0};       ///< Total uncompressed payload size (bytes).
    int64_t message_count{0};        ///< Total number of messages across all URLs.
    int64_t split_count{0};          ///< Number of file splits (0 if single file).
    int64_t split_by_size{0};        ///< Split threshold by size (bytes).
    int64_t split_by_time{0};        ///< Split threshold by time (milliseconds).
    std::vector<UrlMeta> url_metas;  ///< Per-URL statistics, one entry per recorded topic.
  };

  /**
   * @struct Config
   * @brief Playback configuration passed to @c play().
   */
  struct Config final {
    int64_t begin_time{0};                        ///< Playback start timestamp (ms).  0 = from beginning.
    int64_t end_time{0};                          ///< Playback end timestamp (ms).  0 = until end.
    int times{1};                                 ///< Number of loops.  @c kInfinite (-1) = loop forever.
    double rate{1.0};                             ///< Playback rate multiplier.  1.0 = real time.
    bool skip_blank{false};                       ///< If @c true, skip silent gaps between messages.
    int64_t force_delay{-1};                      ///< Override inter-message delay (ms).  -1 = use timestamps.
    bool auto_pause{false};                       ///< If @c true, pause automatically at each message.
    bool auto_quit{false};                        ///< If @c true, quit the loop thread when playback ends.
    std::unordered_set<std::string> filter_urls;  ///< Whitelist of URLs to play.  Empty = all URLs.
  };

  /**
   * @brief Callback type fired for each replayed message.
   *
   * @details
   * Called on the BagReader's loop thread.  The @p data reference is valid only
   * for the duration of the callback.
   *
   * @param microseconds_timestamp    Message timestamp in microseconds.
   * @param url          Topic URL string.
   * @param action_type  Action type (kPublish, kRequest, etc.).
   * @param data         Serialized message payload.
   */
  using OutputCallback = MoveFunction<void(int64_t microseconds_timestamp, const std::string& url,
                                           ActionType action_type, const Bytes& data)>;

  /**
   * @brief Callback fired whenever the playback @c Status changes.
   *
   * @param status  New playback status.
   */
  using StatusCallback = MoveFunction<void(Status status)>;

  /**
   * @brief Callback fired when the reader has opened the file and is ready to start playing.
   */
  using ReadyCallback = MoveFunction<void()>;

  /**
   * @brief Callback fired when playback has finished (or was interrupted).
   *
   * @param is_interrupted  @c true if @c stop() was called before natural end.
   */
  using FinishCallback = MoveFunction<void(bool is_interrupted)>;

  /**
   * @brief Creates a concrete @c BagReader for @p path, selecting the implementation by extension.
   *
   * @details
   * - @c .vcap / @c .vcapx -- @c McapReader (MCAP format)
   * - All other extensions -- @c DatabaseReader (SQLite)
   *
   * @param path        Path to the bag file.
   * @param read_only   If @c true, open in read-only mode (no write operations).
   * @param try_to_fix  If @c true, attempt to repair a corrupt bag on open.
   * @return Shared pointer to the new reader.
   */
  [[nodiscard]] static std::shared_ptr<BagReader> create(const std::string& path, bool read_only = true,
                                                         bool try_to_fix = false);

  /**
   * @brief Constructs the reader for @p path.
   *
   * @param path        Path to the bag file.
   * @param read_only   Open in read-only mode.
   * @param try_to_fix  Attempt repair if the file is corrupt.
   */
  explicit BagReader(const std::string& path, bool read_only = true, bool try_to_fix = false);

  /**
   * @brief Destructor -- stops playback and releases file resources.
   */
  virtual ~BagReader();  // NOLINT(modernize-use-override)

  /**
   * @brief Attaches a @c BagReaderPluginInterface for custom URL/type conversion.
   *
   * @details
   * The plugin's @c convert_url_meta() is called for each URL in the bag to allow
   * remapping before messages are dispatched to @c OutputCallback.
   *
   * @param plugin_interface  Plugin to bind.  May be @c nullptr to detach.
   */
  virtual void bind_plugin_interface(const std::shared_ptr<BagReaderPluginInterface>& plugin_interface);

  /**
   * @brief Registers a callback fired whenever the playback status changes.
   *
   * @param status_callback  Callback receiving the new @c Status value.
   */
  virtual void register_status_callback(StatusCallback&& status_callback);

  /**
   * @brief Registers a callback fired when the reader is ready to start playing.
   *
   * @param ready_callback  Callback invoked once the file is open and parsed.
   */
  virtual void register_ready_callback(ReadyCallback&& ready_callback);

  /**
   * @brief Registers a callback fired when playback ends or is interrupted.
   *
   * @param finish_callback  Callback receiving @c is_interrupted flag.
   */
  virtual void register_finish_callback(FinishCallback&& finish_callback);

  /**
   * @brief Registers the callback that receives replayed messages.
   *
   * @param output_callback  Called for each message during playback.
   */
  virtual void register_output_callback(OutputCallback&& output_callback);

  /**
   * @brief Starts playback with the given @p config.
   *
   * @details
   * Must be called after @c async_run().  The reader transitions to @c kPlaying.
   *
   * @param config  Playback configuration.
   */
  virtual void play(const Config& config) = 0;

  /**
   * @brief Stops playback and resets the reader to the beginning.
   *
   * @details
   * Transitions the reader to @c kStopped.  The @c FinishCallback is fired with
   * @c is_interrupted = @c true.
   */
  virtual void stop() = 0;

  /**
   * @brief Pauses playback at the current position.
   *
   * @details Transitions from @c kPlaying to @c kPaused.
   */
  virtual void pause() = 0;

  /**
   * @brief Resumes a paused playback from the current position.
   *
   * @details Transitions from @c kPaused to @c kPlaying.
   */
  virtual void resume() = 0;

  /**
   * @brief Advances one message while paused, then pauses again.
   *
   * @details Useful for single-stepping through a bag in debug sessions.
   */
  virtual void pause_to_next() = 0;

  /**
   * @brief Seeks to @p begin_time and resumes playback at @p rate with @p times loops.
   *
   * @param begin_time       Seek target timestamp in milliseconds (relative to recording start).
   * @param rate             New playback rate multiplier.
   * @param times            Number of loops after the jump.
   * @param force_to_play    If @c true, forces play state even if currently paused.
   */
  virtual void jump(int64_t begin_time, double rate, int times, bool force_to_play = false) = 0;

  /**
   * @brief Verifies the integrity of the bag file asynchronously.
   *
   * @return @c std::future<bool> that resolves to @c true if the file is intact.
   */
  virtual std::future<bool> check() = 0;

  /**
   * @brief Rebuilds the index tables asynchronously.
   *
   * @return @c std::future<bool> that resolves to @c true on success.
   */
  virtual std::future<bool> reindex() = 0;

  /**
   * @brief Repairs a corrupt bag file asynchronously.
   *
   * @param rebuild  If @c true, rebuilds the entire index from scratch.
   * @return @c std::future<bool> that resolves to @c true if repair succeeded.
   */
  virtual std::future<bool> fix(bool rebuild = false) = 0;

  /**
   * @brief Updates the tag name stored in the bag's metadata.
   *
   * @param tag_name  New tag name string.
   */
  virtual void tag(const std::string& tag_name) = 0;

  /**
   * @brief Returns the current playback position as a recording timestamp.
   *
   * @return Current message timestamp in milliseconds (recording time, relative to start).
   */
  [[nodiscard]] virtual int64_t get_timestamp() const = 0;

  /**
   * @brief Returns the current playback position in real elapsed time.
   *
   * @return Elapsed time since playback started (milliseconds).
   */
  [[nodiscard]] virtual int64_t get_real_timestamp() const = 0;

  /**
   * @brief Returns the current playback status.
   *
   * @return One of @c kStopped, @c kPaused, or @c kPlaying.
   */
  [[nodiscard]] virtual Status get_status() const = 0;

  /**
   * @brief Returns the bag file metadata and per-URL statistics.
   *
   * @return Const reference to the @c Info struct populated at open time.
   */
  [[nodiscard]] virtual const Info& get_info() const = 0;

  /**
   * @brief Scans the bag and returns all embedded schemas.
   *
   * @return Vector of @c SchemaData descriptors found in the bag.
   */
  [[nodiscard]] virtual std::vector<SchemaData> detect_schema() = 0;

  /**
   * @brief Returns the serialisation type string for a given @p url.
   *
   * @param url  Topic URL to look up.
   * @return Serialisation type (e.g., @c "demo.proto.PointCloud"), or an empty string if unknown.
   */
  [[nodiscard]] virtual std::string get_ser_type(const std::string& url) const = 0;

  /**
   * @brief Returns the coarse schema family for a given @p url.
   *
   * @param url  Topic URL to look up.
   * @return Schema family, or @c SchemaType::kUnknown if unavailable.
   */
  [[nodiscard]] virtual SchemaType get_schema_type(const std::string& url) const = 0;

  /**
   * @brief Returns @c true if the bag spans multiple split files.
   *
   * @return @c true when reading a split bag.
   */
  [[nodiscard]] virtual bool is_split_mode() const = 0;

  /**
   * @brief Returns the zero-based index of the current split file being read.
   *
   * @return Current split file index, or 0 for single-file bags.
   */
  [[nodiscard]] virtual int get_split_index() const = 0;

  /**
   * @brief Returns @c true if a jump-to-timestamp seek is currently in progress.
   *
   * @return @c true while seeking.
   */
  [[nodiscard]] virtual bool is_jumping() const = 0;

 protected:
  /**
   * @brief Rebuilds URL metadata lookup maps after plugin remapping.
   *
   * @details
   * Reader implementations call this after @c process_url_metas() mutates the
   * per-URL metadata list, ensuring @c get_ser_type() and @c get_schema_type()
   * both observe the remapped metadata instead of stale pre-plugin entries.
   *
   * @param url_metas         Remapped URL metadata list.
   * @param ser_map           Output lookup map: URL -> serialisation type.
   * @param schema_type_map   Output lookup map: URL -> coarse schema family.
   */
  static void rebuild_url_meta_maps(const std::vector<Info::UrlMeta>& url_metas,
                                    std::unordered_map<std::string, std::string>& ser_map,
                                    std::unordered_map<std::string, SchemaType>& schema_type_map);

  void process_output(int64_t timestamp, const std::string& url, ActionType action_type, const Bytes& data);

  void process_url_metas(std::vector<Info::UrlMeta>& url_metas);

  static ActionType convert_action(std::string_view str);

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;

  VLINK_DISALLOW_COPY_AND_ASSIGN(BagReader)
};

}  // namespace vlink
