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
 * @file vdb_reader.h
 * @brief Concrete BagReader implementation for the SQLite-backed VLink bag format.
 *
 * @details
 * @c VDBReader reads bag files in the SQLite-backed @c .vdb / @c .vdbx format and inherits all
 * playback, seeking, fix, and reindex capabilities from @c BagReader.
 *
 * The SQLite bag format stores messages in structured @c .vdb databases; @c .vdbx
 * files are JSON manifests that reference split @c .vdb files.  This enables efficient
 * random-access queries, indexed playback, and in-place repair with @c fix().
 *
 * @par Usage
 * @code
 * auto reader = vlink::BagReader::create("/data/recording.vdb");
 * // or explicitly:
 * auto reader = std::make_shared<vlink::VDBReader>("/data/recording.vdb");
 * reader->register_output_callback([](int64_t ts, const std::string& url,
 *                                     vlink::ActionType action, const vlink::Bytes& data) {
 *     // process message
 * });
 * reader->async_run();
 * reader->play({});
 * @endcode
 *
 * @see BagReader, VCAPReader
 */

#pragma once

#include <future>
#include <memory>
#include <string>
#include <vector>

#include "./bag_reader.h"

namespace vlink {

/**
 * @class VDBReader
 * @brief Concrete SQLite-backed bag file player.
 *
 * @details
 * All virtual methods from @c BagReader are implemented.  Prefer using
 * @c BagReader::create() for format-agnostic construction.
 */
class VLINK_EXPORT VDBReader final : public BagReader {
 public:
  /**
   * @brief Constructs a @c VDBReader for the given @p path.
   *
   * @param path        Path to the @c .vdb / @c .vdbx file.
   * @param read_only   Open in read-only mode (no write operations allowed).
   * @param try_to_fix  Reserved for table-rebuild recovery when table checks are enabled.
   */
  explicit VDBReader(const std::string& path, bool read_only = true, bool try_to_fix = false);

  /**
   * @brief Destructor -- stops playback and closes the SQLite database handle.
   */
  ~VDBReader() override;

  /**
   * @brief Attaches a @c BagReaderPluginInterface for custom URL/type conversion.
   *
   * @param plugin_interface  Plugin to bind.  May be @c nullptr to detach.
   */
  void bind_plugin_interface(const std::shared_ptr<BagReaderPluginInterface>& plugin_interface) override;

  /**
   * @brief Registers a callback fired whenever the playback status changes.
   *
   * @param status_callback  Callback receiving the new @c Status value.
   */
  void register_status_callback(StatusCallback&& status_callback) override;

  /**
   * @brief Registers a callback fired when the reader is ready to start playing.
   *
   * @param ready_callback  Callback invoked once the file is open and parsed.
   */
  void register_ready_callback(ReadyCallback&& ready_callback) override;

  /**
   * @brief Registers a callback fired when playback ends or is interrupted.
   *
   * @param finish_callback  Callback receiving the @c is_interrupted flag.
   */
  void register_finish_callback(FinishCallback&& finish_callback) override;

  /**
   * @brief Registers the callback that receives replayed messages.
   *
   * @param output_callback  Called for each message during playback.
   */
  void register_output_callback(OutputCallback&& output_callback) override;

  /**
   * @brief Starts playback with the given configuration.
   *
   * @param config  Playback configuration (rate, times, filters, etc.).
   */
  void play(const Config& config) override;

  /**
   * @brief Stops playback and resets the reader to the beginning.
   */
  void stop() override;

  /**
   * @brief Pauses playback at the current position.
   */
  void pause() override;

  /**
   * @brief Resumes a paused playback from the current position.
   */
  void resume() override;

  /**
   * @brief Advances one message while paused, then pauses again.
   */
  void pause_to_next() override;

  /**
   * @brief Seeks to @p begin_time and resumes playback.
   *
   * @param begin_time       Seek target timestamp in milliseconds (relative to recording start).
   * @param rate             New playback rate multiplier.
   * @param times            Number of loops after the jump.
   * @param force_to_play    If @c true, forces play state even if currently paused.
   */
  void jump(int64_t begin_time, double rate, int times, bool force_to_play = false) override;

  /**
   * @brief Verifies the integrity of the SQLite bag file asynchronously.
   *
   * @return @c std::future<bool> that resolves to @c true if the file is intact.
   */
  std::future<bool> check() override;

  /**
   * @brief Rebuilds the SQLite index tables asynchronously.
   *
   * @return @c std::future<bool> that resolves to @c true on success.
   */
  std::future<bool> reindex() override;

  /**
   * @brief Repairs a corrupt SQLite bag file asynchronously.
   *
   * @param rebuild  If @c true, rebuilds the entire index from scratch.
   * @return @c std::future<bool> that resolves to @c true if repair succeeded.
   */
  std::future<bool> fix(bool rebuild = false) override;

  /**
   * @brief Updates the tag name stored in the bag metadata.
   *
   * @param tag_name  New tag name string.
   */
  void tag(const std::string& tag_name) override;

  /**
   * @brief Returns the current playback position as a recording-relative timestamp.
   *
   * @return Current position in milliseconds relative to the recording start.
   */
  [[nodiscard]] int64_t get_timestamp() const override;

  /**
   * @brief Returns the last actual playback timestamp reached by delivered data.
   *
   * @return Last data timestamp in milliseconds relative to the recording start, or 0 when stopped.
   */
  [[nodiscard]] int64_t get_real_timestamp() const override;

  /**
   * @brief Returns the current playback status.
   *
   * @return One of @c kStopped, @c kPaused, or @c kPlaying.
   */
  [[nodiscard]] Status get_status() const override;

  /**
   * @brief Returns the bag file metadata and per-URL statistics.
   *
   * @return Const reference to the @c Info struct populated at open time.
   */
  [[nodiscard]] const Info& get_info() const override;

  /**
   * @brief Scans the SQLite bag and returns all embedded schemas.
   *
   * @return Vector of @c SchemaData descriptors found in the bag.
   */
  [[nodiscard]] std::vector<SchemaData> detect_schema() override;

  /**
   * @brief Returns the serialisation type string for a given URL.
   *
   * @param url  Topic URL to look up.
   * @return Serialisation type (e.g., @c "demo.proto.PointCloud"), or empty if unknown.
   */
  [[nodiscard]] std::string get_ser_type(const std::string& url) const override;

  /**
   * @brief Returns the coarse schema family for a given URL.
   *
   * @param url  Topic URL to look up.
   * @return Schema family, or @c SchemaType::kUnknown if unavailable.
   */
  [[nodiscard]] SchemaType get_schema_type(const std::string& url) const override;

  /**
   * @brief Returns @c true if the bag spans multiple split files.
   */
  [[nodiscard]] bool is_split_mode() const override;

  /**
   * @brief Returns the zero-based index of the current split file being read.
   */
  [[nodiscard]] int get_split_index() const override;

  /**
   * @brief Returns @c true if a jump-to-timestamp seek is currently in progress.
   */
  [[nodiscard]] bool is_jumping() const override;

 protected:
  size_t get_max_task_count() const override;

  void on_begin() override;

  void on_end() override;

 private:
  void update_status(Status status);

  void do_stop();

  void do_pause();

  void prepare_file(void* file);

  void open(const std::string& path);

  void close();

  int get_reset_index(const Config& config);

  void read(const Config& config);

  struct Impl;
  std::unique_ptr<Impl> impl_;

  VLINK_DISALLOW_COPY_AND_ASSIGN(VDBReader)
};

}  // namespace vlink
