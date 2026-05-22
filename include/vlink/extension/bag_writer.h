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
 * @brief Abstract VLink bag recorder with split, compression, schema embedding and a global hook.
 *
 * @details
 * @c BagWriter is the polymorphic base for VLink offline recording.  It exposes a
 * non-blocking @c push() entry point that enqueues serialised messages onto a private
 * @c MessageLoop, which then persists them through the concrete backend.  Two backends
 * ship with VLink:
 *
 * - @c VDBWriter for SQLite-backed @c .vdb / @c .vdbx containers; default codec is LZAV
 *   for @c kCompressAuto and @c kCompressLzav selectors.
 * - @c VCAPWriter for MCAP-format @c .vcap / @c .vcapx containers; @c kCompressAuto and
 *   @c kCompressZstd select Zstandard when Zstd support is compiled in.
 *
 * Writer state machine:
 *
 * @verbatim
 *                async_run()                push()              flush()/dtor
 *   +---------+ ----------> +-----------+ ---------> +---------+ ----------> +---------+
 *   |  Open   |             |  Running  | <--------- | Pending |             |  Closed |
 *   +---------+             +-----------+   ack      +---------+             +---------+
 *                                ^                                              ^
 *                                |                                              |
 *                                +--- split_by_size / split_by_time -- rotate --+
 * @endverbatim
 *
 * On-disk layout produced by the writers:
 *
 * @verbatim
 *   +---------+----------------+---------------+----------------+--------+
 *   | Header  |  URL index     |  Schema index | Message stream | Footer |
 *   +---------+----------------+---------------+----------------+--------+
 *      tag        url_metas       schema_data       payloads      finalisation
 *      app
 *      timezone
 * @endverbatim
 *
 * Feature highlights:
 * - Asynchronous record path with optional synchronous @c immediate writes.
 * - File splitting by byte size and/or by wall-clock interval.
 * - Optional WAL mode for SQLite crash resilience.
 * - URL-level loss reporting via @c set_url_loss().
 * - Schema embedding through @c push_schema() for offline introspection.
 * - Process-global writer triggered by the @c VLINK_BAG_PATH environment variable.
 *
 * @par Example
 * @code
 * vlink::BagWriter::Config cfg;
 * cfg.compress      = vlink::BagWriter::kCompressAuto;
 * cfg.split_by_size = 1024LL * 1024LL * 512;            // 512 MiB per split
 *
 * auto writer = vlink::BagWriter::create("/data/drive_log.vdb", cfg);
 * writer->async_run();
 * writer->push("dds://camera/front", "demo.proto.Image",
 *              vlink::SchemaType::kProtobuf, vlink::ActionType::kPublish, bytes);
 * @endcode
 *
 * @par Global writer
 * @code
 * // Set VLINK_BAG_PATH=/data/global.vdb before process launch.
 * if (auto* gw = vlink::BagWriter::global_get(); gw != nullptr) {
 *   gw->push("intra://debug", "raw", vlink::SchemaType::kRaw,
 *            vlink::ActionType::kPublish, bytes);
 * }
 * @endcode
 *
 * @note @c push() is thread-safe.  @c immediate=true bypasses the queue and writes on the
 * caller's thread; this should be reserved for finalisation or test code because it
 * can block long enough to violate real-time deadlines.
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
 * @brief Asynchronous VLink message recorder built on top of @c MessageLoop.
 *
 * @details
 * Construct via @c create() (or directly) and call @c async_run() to start the recording
 * thread, then push messages with @c push().  Concrete subclasses implement every virtual
 * persistence operation; the base class owns the shared bookkeeping and the loop wiring.
 */
class VLINK_EXPORT BagWriter : public MessageLoop {
 public:
  /**
   * @brief Compression codec selector understood by the writer backends.
   *
   * | Value           | Algorithm | Notes                                                  |
   * | --------------- | --------- | ------------------------------------------------------ |
   * | kCompressNone   | none      | Payloads stored as raw bytes                           |
   * | kCompressAuto   | backend   | Uses the backend default (LZAV for VDB, Zstd for MCAP) |
   * | kCompressZstd   | Zstandard | Active for MCAP when Zstd support is available         |
   * | kCompressLz4    | LZ4       | Reserved selector; not currently used by built-ins     |
   * | kCompressLzav   | LZAV      | Active for SQLite-backed VDB recordings                |
   */
  enum CompressType : uint8_t {
    kCompressNone = 0,  ///< Store payloads uncompressed.
    kCompressAuto = 1,  ///< Defer codec choice to the active backend.
    kCompressZstd = 2,  ///< Force Zstandard codec where supported.
    kCompressLz4 = 3,   ///< Reserved selector; no built-in writer emits LZ4 today.
    kCompressLzav = 4,  ///< Force LZAV codec where supported.
  };

  /**
   * @struct Config
   * @brief Recording behaviour, split policy and resource budgets.
   *
   * @details
   * Sizes are expressed in bytes and durations in milliseconds unless explicitly stated.
   */
  struct Config final {
    std::string tag_name;                                      ///< Optional tag stored in the bag header.
    CompressType compress{CompressType::kCompressNone};        ///< Compression codec selector.
    bool wal_mode{false};                                      ///< Enable SQLite WAL for crash resilience.
    bool enable_limit{false};                                  ///< When true, evict oldest rows at the row/byte limit.
    bool split_name_by_time{false};                            ///< Append a timestamp suffix to split filenames.
    bool sync_mode{false};                                     ///< Disable periodic cache-flush timer for VDB writes.
    bool optimize_on_exit{false};                              ///< Run VACUUM/OPTIMIZE while closing the file.
    int64_t max_row_count{5'000'000'000LL};                    ///< SQLite row cap; either evicts or fails new writes.
    int64_t max_bytes_size{1024LL * 1024LL * 1024LL * 512LL};  ///< SQLite byte cap; either evicts or fails new writes.
    int64_t split_by_size{1024LL * 1024LL * 1024LL * 1LL};     ///< Split threshold in bytes (0 disables).
    int64_t split_by_time{0};                                  ///< Split interval in milliseconds (0 disables).
    int64_t begin_time{0};                                     ///< Anchor (ms) used by time-based splits.
    int64_t cache_size{1024LL * 1024LL * 4};                   ///< VDB commit chunk / MCAP chunk size in bytes.
    int64_t compress_start_size{128};                          ///< Minimum payload size eligible for compression.
    int64_t compress_level{3};                                 ///< Codec-specific compression level.
    int64_t max_task_depth{20000};                             ///< Maximum pending writes in the loop queue.
    int64_t max_memory_size{1024LL * 1024LL * 1024LL * 2LL};   ///< Maximum in-memory cache size in bytes.
    int64_t start_timestamp{0};                                ///< Override for the wall-clock start timestamp (ms).
    std::unordered_set<std::string> ignore_compress_urls;      ///< URLs whose payloads must never be compressed.

    Config() {}  // NOLINT(modernize-use-equals-default)
  };

  /**
   * @brief Notification fired when the writer rotates to a new split file.
   *
   * @details
   * Called with the zero-based split index and the new file path.  The @c before flag of
   * @c register_split_callback() chooses whether the hook runs before or after the
   * rotation is committed.
   */
  using SplitCallback = MoveFunction<void(int split_index, const std::string& split_filename)>;

  /**
   * @brief Schema resolver used by the writer when a previously unseen URL is recorded.
   *
   * @details
   * The writer passes the requested serialisation type together with a coarse schema
   * family hint so that families sharing a single type name (e.g. Protobuf vs Arrow) can
   * still be disambiguated.
   */
  using SchemaCallback = MoveFunction<SchemaData(const std::string& ser_type, SchemaType schema_type)>;

  /**
   * @brief System clock alias used when formatting timestamps into split file names.
   */
  using SystemClock = std::chrono::time_point<std::chrono::system_clock, std::chrono::milliseconds>;

  /**
   * @brief Builds the concrete writer matching the extension of @p path.
   *
   * @details
   * Suffix dispatch: @c .vdb / @c .vdbx select @c VDBWriter, @c .vcap / @c .vcapx select
   * @c VCAPWriter; other suffixes return @c nullptr.  The returned writer is open but
   * idle until @c async_run() starts its loop.
   *
   * @param path   Output file path.
   * @param config Recording configuration.
   * @return Shared pointer to the new writer, or @c nullptr on unsupported suffix.
   */
  [[nodiscard]] static std::shared_ptr<BagWriter> create(const std::string& path, const Config& config = {});

  /**
   * @brief Returns the cached writer for @p path, lazily creating and starting one.
   *
   * @details
   * Looks up the process-wide writer registry.  When no entry exists, a writer is built
   * by @c create(), its loop is started with @c async_run(), and it is registered for
   * reuse.  The registry releases the entry automatically when the last shared owner
   * goes away.  Unsupported suffixes return @c nullptr and are not registered.
   *
   * @param path Output file path.
   * @return Shared pointer to a started writer, or @c nullptr on unsupported suffix.
   */
  [[nodiscard]] static std::shared_ptr<BagWriter> filter_get(const std::string& path);

  /**
   * @brief Returns the singleton writer driven by the @c VLINK_BAG_PATH environment variable.
   *
   * @details
   * On first call, the writer is created from @c VLINK_BAG_PATH and started.  Returns
   * @c nullptr when the environment variable is absent or carries an unsupported suffix.
   *
   * @return Raw pointer to the global writer, or @c nullptr.
   */
  static BagWriter* global_get();

  /**
   * @brief Constructs the base writer and opens the output file.
   *
   * @details
   * The recording loop is not yet running; call @c async_run() before any @c push().
   *
   * @param path   Output file path.
   * @param config Recording configuration.
   */
  explicit BagWriter(const std::string& path, const Config& config = {});

  /**
   * @brief Halts the loop, flushes pending writes and closes the file.
   */
  virtual ~BagWriter();  // NOLINT(modernize-use-override)

  /**
   * @brief Installs a hook fired around split rotation.
   *
   * @param callback Receives the new split index and the new file path.
   * @param before   When true, the hook fires before the new file is opened; otherwise after.
   */
  virtual void register_split_callback(SplitCallback&& callback, bool before) = 0;

  /**
   * @brief Installs the resolver invoked when an unseen serialisation type is recorded.
   *
   * @param callback Function mapping (ser_type, schema_type) to @c SchemaData.
   */
  virtual void register_schema_callback(SchemaCallback&& callback) = 0;

  /**
   * @brief Embeds a schema descriptor into the bag for downstream introspection.
   *
   * @param schema_data Schema descriptor to persist.
   * @param immediate   When true, performs a synchronous merge on the caller's thread.
   * @return @c false only when an immediate merge fails; queued merges always return @c true.
   */
  virtual bool push_schema(const SchemaData& schema_data, bool immediate = false) = 0;

  /**
   * @brief Records a single message to the bag.
   *
   * @details
   * Enqueues a write task onto the recording loop; the actual disk write happens on the
   * loop thread.  When @p microseconds_timestamp is @c nullptr, the writer assigns a
   * recording-relative timestamp from its elapsed clock.
   *
   * @param url                    VLink URL identifying the topic.
   * @param ser_type               Serialisation type name (e.g. Protobuf message name).
   * @param schema_type            Coarse schema family for the payload.
   * @param action_type            Recorded action kind.
   * @param data                   Serialised payload bytes.
   * @param microseconds_timestamp Optional caller-supplied timestamp in microseconds.
   * @param immediate              When true, bypasses the queue and writes synchronously.
   * @return Assigned timestamp in microseconds, or a negative value on failure.
   */
  virtual int64_t push(const std::string& url, const std::string& ser_type, SchemaType schema_type,
                       ActionType action_type, const Bytes& data, int64_t* microseconds_timestamp = nullptr,
                       bool immediate = false) = 0;

  /**
   * @brief Returns the backend-specific "dump in progress" flag.
   */
  [[nodiscard]] virtual bool is_dumping() const = 0;

  /**
   * @brief Returns whether split mode is currently in effect.
   *
   * @return @c true when either @c split_by_size or @c split_by_time is non-zero.
   */
  [[nodiscard]] virtual bool is_split_mode() const = 0;

  /**
   * @brief Returns the zero-based index of the active split file.
   *
   * @return Active split index, or 0 outside split mode.
   */
  [[nodiscard]] virtual int get_split_index() const = 0;

  /**
   * @brief Records the expected loss ratio for @p url as bag metadata.
   *
   * @details
   * Loss values feed offline diagnostics so that intentional drops can be distinguished
   * from unexpected loss.
   *
   * @param url  Topic URL.
   * @param loss Loss ratio; values greater than 1.0 are normalised to -1.
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
