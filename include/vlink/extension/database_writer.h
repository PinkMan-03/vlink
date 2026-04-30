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
 * @file database_writer.h
 * @brief Concrete BagWriter implementation for the SQLite-backed VLink bag format.
 *
 * @details
 * @c DatabaseWriter records VLink messages to a SQLite @c .vdb file.  It extends
 * @c BagWriter with transactional write caching (WAL mode, batch commit), optional
 * VACUUM optimisation on exit, and in-place schema embedding for Protobuf introspection.
 *
 * Internally, messages are accumulated in a memory cache and committed in batches to
 * reduce SQLite write overhead.  Cache parameters are configurable via @c BagWriter::Config.
 *
 * @par Usage
 * @code
 * vlink::BagWriter::Config cfg;
 * cfg.compress = vlink::BagWriter::kCompressLzav;
 * cfg.wal_mode = true;
 *
 * auto writer = vlink::BagWriter::create("/data/recording.vdb", cfg);
 * // or explicitly:
 * auto writer = std::make_shared<vlink::DatabaseWriter>("/data/recording.vdb", cfg);
 * writer->async_run();
 * writer->push("dds://my/topic", "demo.proto.PointCloud", vlink::SchemaType::kProtobuf,
 *              vlink::ActionType::kPublish, data);
 * @endcode
 *
 * @see BagWriter, McapWriter
 */

#pragma once

#include <memory>
#include <string>

#include "./bag_writer.h"

namespace vlink {

/**
 * @class DatabaseWriter
 * @brief Concrete SQLite-backed bag file recorder with transactional write caching.
 *
 * @details
 * All virtual methods from @c BagWriter are implemented.  Prefer using
 * @c BagWriter::create() for format-agnostic construction.
 */
class VLINK_EXPORT DatabaseWriter final : public BagWriter {
 public:
  /**
   * @brief Constructs a @c DatabaseWriter for the given @p path.
   *
   * @param path    Path to the output @c .vdb file.  Created if it does not exist.
   * @param config  Recording configuration.
   */
  explicit DatabaseWriter(const std::string& path, const Config& config = {});

  /**
   * @brief Destructor -- commits remaining cached writes and closes the SQLite database.
   */
  ~DatabaseWriter() override;

  /**
   * @brief Registers a callback invoked when a file split occurs.
   *
   * @param callback  Called with (split_index, new_filename) on each split.
   * @param before    If @c true, fires before the new file is opened; otherwise after.
   */
  void register_split_callback(SplitCallback&& callback, bool before) override;

  /**
   * @brief Registers a callback that resolves serialisation type strings to @c SchemaData.
   *
   * @param callback  Function mapping (@c ser_type, @c schema_type) to @c SchemaData.
   */
  void register_schema_callback(SchemaCallback&& callback) override;

  /**
   * @brief Embeds a @c SchemaData into the SQLite bag for offline introspection.
   *
   * @param schema_data  Schema descriptor to store.
   * @param immediate    If @c true, merges synchronously; otherwise enqueues.
   * @return             @c false only when the immediate merge fails.
   */
  bool push_schema(const SchemaData& schema_data, bool immediate = false) override;

  /**
   * @brief Records one message to the SQLite bag file.
   *
   * @param url                    VLink URL of the topic.
   * @param ser_type               Serialisation type string.
   * @param schema_type            Coarse schema family for the payload.
   * @param action_type            Action type (@c kPublish, @c kRequest, etc.).
   * @param data                   Serialized payload bytes.
   * @param microseconds_timestamp Optional custom timestamp in microseconds.
   *                               @c nullptr means use the current system time.
   * @param immediate              If @c true, writes synchronously bypassing the queue.
   * @return Sequence number of the recorded message, or a negative value on error.
   */
  int64_t push(const std::string& url, const std::string& ser_type, SchemaType schema_type, ActionType action_type,
               const Bytes& data, int64_t* microseconds_timestamp = nullptr, bool immediate = false) override;

  /**
   * @brief Returns @c true if the writer is actively recording to disk.
   */
  [[nodiscard]] bool is_dumping() const override;

  /**
   * @brief Returns @c true if split-file mode is active.
   */
  [[nodiscard]] bool is_split_mode() const override;

  /**
   * @brief Returns the zero-based index of the current split file.
   */
  [[nodiscard]] int get_split_index() const override;

  /**
   * @brief Sets the expected message loss ratio for a given URL.
   *
   * @param url   Topic URL.
   * @param loss  Loss ratio in the range [0.0, 1.0].
   */
  void set_url_loss(const std::string& url, double loss) override;

 protected:
  size_t get_max_task_count() const override;

  void on_begin() override;

  void on_end() override;

 private:
  void open(const std::string& path);

  void close();

  bool write(const std::string& url, const std::string& ser_type, SchemaType schema_type, ActionType action_type,
             const Bytes& data, int64_t microseconds_timestamp);

  bool write_filex(bool complete = true);

  bool begin_cache();

  bool sync_cache();

  bool rollback_cache();

  bool merge_schema(SchemaData& schema_data);

  bool load_schema(const std::string& ser_type, SchemaType& schema_type, SchemaData& schema_data);

  bool insert_schema(const SchemaData& schema_data);

  std::unique_ptr<struct DatabaseWriterImpl> impl_;

  VLINK_DISALLOW_COPY_AND_ASSIGN(DatabaseWriter)
};

}  // namespace vlink
