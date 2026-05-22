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
 * @brief Abstract player for VLink bag recordings with seek, loop and rate control.
 *
 * @details
 * @c BagReader is the polymorphic base for VLink offline log playback.  It owns a private
 * @c MessageLoop thread, opens a bag file produced by @c BagWriter, and replays the stored
 * messages back to user-supplied callbacks honouring their original timing.  Concrete
 * subclasses provide format-specific I/O: @c VDBReader for the SQLite-backed @c .vdb
 * container and @c VCAPReader for the MCAP-based @c .vcap container.  @c create() chooses
 * the right one from the file suffix.
 *
 * Supported formats and behaviour summary:
 *
 * | Suffix              | Concrete reader | Storage    | Index source                    |
 * | ------------------- | --------------- | ---------- | ------------------------------- |
 * | @c .vdb / @c .vdbx  | @c VDBReader    | SQLite     | SQLite tables (elapsed/URL)     |
 * | @c .vcap / @c .vcapx| @c VCAPReader   | MCAP       | MCAP summary + chunk index      |
 *
 * Internal playback state machine:
 *
 * @verbatim
 *                     play(cfg)              pause()
 *      +------------+ -------> +-----------+ -------> +----------+
 *      |  kStopped  |          |  kPlaying |          |  kPaused |
 *      +------------+ <------- +-----------+ <------- +----------+
 *                    stop() /         resume() / pause_to_next()
 *                  end-of-bag
 * @endverbatim
 *
 * Playback features:
 * - Rate multiplier, loop count and time-window filtering through @c Config.
 * - @c jump() seeks to an arbitrary recording timestamp and may force-resume playback.
 * - URL whitelist via @c Config::filter_urls applies after plugin URL remapping.
 * - @c check() / @c reindex() / @c fix() run asynchronously and report their outcome
 *   through @c std::future<bool>.
 * - A bound @c BagReaderPluginInterface may rename URLs, override serialisation types
 *   and intercept individual replayed messages.
 *
 * @par Example
 * @code
 * auto reader = vlink::BagReader::create("/data/drive_log.vdb");
 * reader->register_ready_callback([] { VLOG_I("bag ready"); });
 * reader->register_output_callback([](int64_t us, const std::string& url,
 *                                     vlink::ActionType action, const vlink::Bytes& data) {
 *     // replay each frame in real-time order
 *     (void)action;
 *     VLOG_I("us=", us, " url=", url, " bytes=", data.size());
 * });
 * reader->async_run();
 *
 * vlink::BagReader::Config cfg;
 * cfg.rate  = 2.0;                              // play at 2x speed
 * cfg.times = vlink::BagReader::kInfinite;      // loop forever
 * reader->play(cfg);
 * @endcode
 *
 * @note Always call @c async_run() before @c play(); the loop thread must be alive to
 * dispatch frames.  Output callback timestamps are in microseconds, while
 * @c Config::begin_time and @c Config::end_time are expressed in milliseconds.
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
 * @brief Format-agnostic VLink bag player driven by an internal @c MessageLoop.
 *
 * @details
 * Inherits @c MessageLoop so playback runs on a dedicated worker thread.  Construction
 * opens the target file and parses its index, but no frames are emitted until
 * @c async_run() starts the loop and @c play() supplies a @c Config.  All virtual
 * operations are implemented by @c VDBReader and @c VCAPReader; the base class only
 * carries shared plumbing (callback storage, plugin binding, URL filtering helpers).
 */
class VLINK_EXPORT BagReader : public MessageLoop {
 public:
  /**
   * @brief Sentinel for @c Config::times that requests endless loop playback.
   */
  static constexpr int kInfinite{-1};

  /**
   * @brief Coarse playback state observable through @c get_status().
   *
   * | Value     | Meaning                                                      |
   * | --------- | ------------------------------------------------------------ |
   * | kStopped  | No active session; position is reset to the start of the bag |
   * | kPaused   | A play session is open but the dispatcher is suspended       |
   * | kPlaying  | The dispatcher is actively delivering frames                 |
   */
  enum Status : uint8_t {
    kStopped = 0,  ///< Idle; no playback in progress.
    kPaused = 1,   ///< Playback temporarily suspended; position retained.
    kPlaying = 2,  ///< Actively forwarding frames to the output callback.
  };

  /**
   * @struct Info
   * @brief Aggregated metadata extracted from the bag header, summary and URL index.
   *
   * @details
   * Populated when the reader opens the file and is stable thereafter unless a
   * destructive operation such as @c reindex() or @c fix() rewrites the index.
   */
  struct Info final {
    /**
     * @struct UrlMeta
     * @brief Per-URL accounting entry recorded inside @c Info::url_metas.
     */
    struct VLINK_EXPORT UrlMeta final {
      bool valid{false};                                   ///< True when the entry is fully populated.
      int index{0};                                        ///< Bag-local numeric URL identifier.
      std::string url;                                     ///< Full VLink URL string.
      std::string url_type;                                ///< Communication model: Event / Method / Field.
      ActionType action_type{ActionType::kUnknownAction};  ///< Stored action when known.
      std::string ser_type;                                ///< Serialisation type name.
      SchemaType schema_type{SchemaType::kUnknown};        ///< Coarse schema family for this URL.
      size_t count{0};                                     ///< Number of recorded messages.
      size_t size{0};                                      ///< Total stored bytes (compressed when applicable).
      double freq{0};                                      ///< Average publication frequency in Hertz.
      double loss{0};                                      ///< Declared loss ratio in the range [0, 1].

      /**
       * @brief Defines a stable ordering between two URL metadata entries.
       *
       * @details
       * Sort key is the URL transport priority first, then the URL string itself and
       * finally the numeric index as a deterministic tie-breaker.
       *
       * @param target Right-hand operand.
       * @return @c true when @c *this should appear before @p target.
       */
      bool operator<(const UrlMeta& target) const noexcept;
    };

    std::string file_name;           ///< Absolute path to the opened bag file.
    std::string tag_name;            ///< Free-form tag persisted in the header.
    std::string version;             ///< Bag format version string.
    std::string storage_type;        ///< Storage backend label (e.g. @c "sqlite", @c "mcap").
    std::string compression_type;    ///< Default compression codec applied to payloads.
    std::string time_accuracy;       ///< Timestamp resolution token (e.g. @c "us", @c "ns").
    std::string process_name;        ///< Name of the recording process.
    std::string date_time;           ///< Human-readable recording start date and time.
    bool has_completed{false};       ///< True when the recording was cleanly finalised.
    bool has_idx_elapsed{false};     ///< True when an elapsed-time index is present.
    bool has_idx_url{false};         ///< True when a URL index is present.
    bool has_schema{false};          ///< True when at least one embedded schema is available.
    int32_t timezone{0};             ///< Recording timezone offset in minutes from UTC.
    int64_t start_timestamp{0};      ///< Wall-clock recording start (milliseconds since epoch).
    int64_t blank_duration{0};       ///< Cumulative silent-gap duration in milliseconds.
    int64_t total_duration{0};       ///< Total recording duration in milliseconds.
    int64_t file_size{0};            ///< On-disk file size in bytes.
    int64_t total_raw_size{0};       ///< Sum of uncompressed payload bytes.
    int64_t message_count{0};        ///< Total recorded message count across every URL.
    int64_t split_count{0};          ///< Number of split files (0 for a single-file bag).
    int64_t split_by_size{0};        ///< Split threshold in bytes when split mode is active.
    int64_t split_by_time{0};        ///< Split threshold in milliseconds when split mode is active.
    std::vector<UrlMeta> url_metas;  ///< One entry per recorded URL.
  };

  /**
   * @struct Config
   * @brief Playback parameters consumed by @c play().
   */
  struct Config final {
    int64_t begin_time{0};                        ///< Playback window start in milliseconds (0 means file start).
    int64_t end_time{0};                          ///< Playback window end in milliseconds (0 means file end).
    int times{1};                                 ///< Loop count; values <= 0 request endless loop playback.
    double rate{1.0};                             ///< Speed multiplier relative to the recorded clock.
    bool skip_blank{false};                       ///< When true, collapses long silent gaps between frames.
    int64_t force_delay{-1};                      ///< >0 fixed delay (ms), 0 no delay, <0 use recorded timing.
    bool auto_pause{false};                       ///< When true, pauses automatically after every emitted frame.
    bool auto_quit{false};                        ///< When true, stops the loop thread at the end of playback.
    std::unordered_set<std::string> filter_urls;  ///< Whitelist of playback URLs; empty means all URLs pass.
  };

  /**
   * @brief Callback signature receiving one replayed message.
   *
   * @details
   * Invoked on the reader's @c MessageLoop thread.  The @p data reference is only valid
   * for the duration of the call; copy it if it needs to outlive the callback.
   *
   * @param microseconds_timestamp Frame timestamp relative to the recording start, in microseconds.
   * @param url                    Fully-qualified VLink URL of the recorded topic.
   * @param action_type            Recorded action kind (publish, request, response, ...).
   * @param data                   Serialised payload bytes.
   *
   * @note Multiply @c Config::begin_time and @c Config::end_time by 1000 before comparing
   *       them against @p microseconds_timestamp.
   */
  using OutputCallback = MoveFunction<void(int64_t microseconds_timestamp, const std::string& url,
                                           ActionType action_type, const Bytes& data)>;

  /**
   * @brief Callback fired on every transition of @c Status.
   *
   * @param status The new playback state.
   */
  using StatusCallback = MoveFunction<void(Status status)>;

  /**
   * @brief Callback fired once after the bag has been opened and indexed.
   */
  using ReadyCallback = MoveFunction<void()>;

  /**
   * @brief Callback fired when the current play session ends.
   *
   * @param is_interrupted True if termination was caused by @c stop(); false on natural end.
   */
  using FinishCallback = MoveFunction<void(bool is_interrupted)>;

  /**
   * @brief Builds the concrete reader matching the extension of @p path.
   *
   * @details
   * Suffix dispatch: @c .vdb / @c .vdbx select @c VDBReader, @c .vcap / @c .vcapx select
   * @c VCAPReader; any other suffix returns @c nullptr.
   *
   * @param path        Bag file path on disk.
   * @param read_only   When true, opens the backend read-only and rejects mutating calls.
   * @param try_to_fix  When true, allows backends to attempt a recovery pass while opening.
   * @return Shared pointer to the freshly built reader, or @c nullptr for an unknown suffix.
   */
  [[nodiscard]] static std::shared_ptr<BagReader> create(const std::string& path, bool read_only = true,
                                                         bool try_to_fix = false);

  /**
   * @brief Constructs the base reader and stores construction-time options.
   *
   * @param path        Bag file path passed to the concrete subclass.
   * @param read_only   When true, prevents the backend from acquiring write access.
   * @param try_to_fix  When true, allows the subclass to run recovery while opening.
   */
  explicit BagReader(const std::string& path, bool read_only = true, bool try_to_fix = false);

  /**
   * @brief Stops the loop, closes the file and releases backend resources.
   */
  virtual ~BagReader();  // NOLINT(modernize-use-override)

  /**
   * @brief Attaches a custom URL/type/message rewrite plugin to this reader.
   *
   * @details
   * The plugin's @c convert_url_meta() runs once per URL discovered in the bag and may
   * rename topics, override serialisation types or filter URLs out.  Its @c push() hook
   * sees every replayed frame before it reaches @c OutputCallback.
   *
   * @param plugin_interface Plugin instance, or @c nullptr to detach the current binding.
   */
  virtual void bind_plugin_interface(const std::shared_ptr<BagReaderPluginInterface>& plugin_interface);

  /**
   * @brief Installs a state-change observer.
   *
   * @param status_callback Function invoked with the new @c Status on every transition.
   */
  virtual void register_status_callback(StatusCallback&& status_callback);

  /**
   * @brief Installs the "open complete" observer.
   *
   * @param ready_callback Function invoked once the bag is open and the index is parsed.
   */
  virtual void register_ready_callback(ReadyCallback&& ready_callback);

  /**
   * @brief Installs the "play session ended" observer.
   *
   * @param finish_callback Function invoked at the end of a play session.
   */
  virtual void register_finish_callback(FinishCallback&& finish_callback);

  /**
   * @brief Installs the per-frame data sink.
   *
   * @param output_callback Function called for every replayed message.
   */
  virtual void register_output_callback(OutputCallback&& output_callback);

  /**
   * @brief Starts (or restarts) a play session with the supplied configuration.
   *
   * @details
   * Transitions the reader into @c kPlaying.  Requires that @c async_run() has already
   * been called so that the loop thread can dispatch frames.
   *
   * @param config Playback window, rate, loop count and URL filter.
   */
  virtual void play(const Config& config) = 0;

  /**
   * @brief Aborts the active session and rewinds to the start of the bag.
   *
   * @details
   * Drives the reader into @c kStopped and invokes the @c FinishCallback with
   * @c is_interrupted set to true.
   */
  virtual void stop() = 0;

  /**
   * @brief Suspends frame dispatch while preserving the current position.
   */
  virtual void pause() = 0;

  /**
   * @brief Resumes dispatch from the paused position.
   */
  virtual void resume() = 0;

  /**
   * @brief Emits exactly one frame from the paused position, then pauses again.
   */
  virtual void pause_to_next() = 0;

  /**
   * @brief Seeks playback to @p begin_time and applies updated rate and loop settings.
   *
   * @param begin_time    Target recording timestamp in milliseconds.
   * @param rate          New playback speed multiplier.
   * @param times         Loop count to apply after the seek.
   * @param force_to_play When true, transitions to @c kPlaying even if currently paused.
   */
  virtual void jump(int64_t begin_time, double rate, int times, bool force_to_play = false) = 0;

  /**
   * @brief Runs an asynchronous integrity verification pass.
   *
   * @return Future resolving to @c true when the bag is structurally intact.
   */
  virtual std::future<bool> check() = 0;

  /**
   * @brief Rebuilds backend index tables in the background where supported.
   *
   * @return Future resolving to @c true on success.
   */
  virtual std::future<bool> reindex() = 0;

  /**
   * @brief Attempts to recover a corrupted bag in the background where supported.
   *
   * @param rebuild When true, also forces a full index rebuild.
   * @return Future resolving to @c true when recovery succeeded.
   */
  virtual std::future<bool> fix(bool rebuild = false) = 0;

  /**
   * @brief Overwrites the human-readable tag stored in the bag header.
   *
   * @param tag_name New tag value.
   */
  virtual void tag(const std::string& tag_name) = 0;

  /**
   * @brief Returns the timestamp targeted by the playback cursor.
   *
   * @return Current playback time in milliseconds, relative to the recording start.
   */
  [[nodiscard]] virtual int64_t get_timestamp() const = 0;

  /**
   * @brief Returns the timestamp of the most recently emitted frame.
   *
   * @return Real delivered timestamp in milliseconds, or 0 when no frame is in flight.
   */
  [[nodiscard]] virtual int64_t get_real_timestamp() const = 0;

  /**
   * @brief Returns the current playback state.
   *
   * @return One of @c kStopped, @c kPaused or @c kPlaying.
   */
  [[nodiscard]] virtual Status get_status() const = 0;

  /**
   * @brief Returns the cached header/summary metadata.
   *
   * @return Constant reference to the @c Info populated at open time.
   */
  [[nodiscard]] virtual const Info& get_info() const = 0;

  /**
   * @brief Scans the bag and collects every embedded schema descriptor.
   *
   * @return Vector of @c SchemaData entries.
   */
  [[nodiscard]] virtual std::vector<SchemaData> detect_schema() = 0;

  /**
   * @brief Resolves the serialisation type associated with @p url.
   *
   * @param url Fully-qualified URL to look up.
   * @return Stored serialisation type, or an empty string when @p url is unknown.
   */
  [[nodiscard]] virtual std::string get_ser_type(const std::string& url) const = 0;

  /**
   * @brief Resolves the schema family associated with @p url.
   *
   * @param url Fully-qualified URL to look up.
   * @return Coarse @c SchemaType, or @c SchemaType::kUnknown when unavailable.
   */
  [[nodiscard]] virtual SchemaType get_schema_type(const std::string& url) const = 0;

  /**
   * @brief Returns whether the opened bag spans multiple split files.
   */
  [[nodiscard]] virtual bool is_split_mode() const = 0;

  /**
   * @brief Returns the zero-based index of the split file currently being consumed.
   *
   * @return Active split index, or 0 for a single-file bag.
   */
  [[nodiscard]] virtual int get_split_index() const = 0;

  /**
   * @brief Returns whether a @c jump() seek is still in progress.
   */
  [[nodiscard]] virtual bool is_jumping() const = 0;

 protected:
  /**
   * @brief Rebuilds the URL-to-metadata lookup tables after plugin remapping.
   *
   * @details
   * Concrete readers call this after applying @c process_url_metas() so that
   * @c get_ser_type() and @c get_schema_type() observe the post-plugin view
   * instead of stale pre-plugin entries.
   *
   * @param url_metas       Remapped URL metadata list.
   * @param ser_map         Output map keyed by URL string holding the serialisation type.
   * @param schema_type_map Output map keyed by URL string holding the schema family.
   */
  static void rebuild_url_meta_maps(const std::vector<Info::UrlMeta>& url_metas,
                                    std::unordered_map<std::string, std::string>& ser_map,
                                    std::unordered_map<std::string, SchemaType>& schema_type_map);

  void process_output(int64_t timestamp, const std::string& url, ActionType action_type, const Bytes& data);

  void process_url_metas(std::vector<Info::UrlMeta>& url_metas);

  /**
   * @brief Resolves the URL that will be exposed to the output callback for a stored URL.
   *
   * @return @c false when the bound plugin asked to exclude @p input_url from playback.
   */
  [[nodiscard]] bool convert_playback_url(const std::string& input_url, std::string& output_url) const;

  /**
   * @brief Tests a stored URL against the active playback URL whitelist.
   *
   * @details When a plugin is bound, @p filter_urls is matched against post-rewrite URLs
   * rather than the raw bag URL.
   */
  [[nodiscard]] bool match_playback_url_filter(std::string_view input_url,
                                               const std::unordered_set<std::string>& filter_urls) const;

  static ActionType convert_action(std::string_view str);

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;

  VLINK_DISALLOW_COPY_AND_ASSIGN(BagReader)
};

}  // namespace vlink
