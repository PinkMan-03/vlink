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
 * @file vdb_writer.h
 * @brief SQLite-backed recorder that produces @c .vdb / @c .vdbx bag files with batched WAL commits.
 *
 * @details
 * @c VDBWriter materialises @c BagWriter on top of SQLite.  Messages are accumulated in an
 * in-memory cache and flushed to disk in WAL-mode transactions so that the write amplification
 * remains bounded even at high publish rates; outstanding writes are committed during @c close()
 * and an optional @c VACUUM step compacts the file on exit.
 *
 * @par VDB schema
 *
 * | Table       | Purpose                                                             |
 * | ----------- | ------------------------------------------------------------------- |
 * | @c messages | (id, url_id, ts_us, action, data) -- one row per recorded message   |
 * | @c urls     | (id, url, ser_type, schema_type) -- topic dictionary                |
 * | @c schemas  | (id, ser_type, schema_type, blob) -- embedded schema descriptors    |
 * | @c metadata | (key, value) -- tag, machine name, vlink version, capture time      |
 * | @c stats    | (url_id, count, bytes, loss) -- per-URL aggregates updated on flush |
 *
 * @par Writer states
 * @code
 *   +----------+   on_begin()       +---------+   push()/push_schema()   +-----------+
 *   |  closed  | -----------------> | opening | -----------------------> | recording |
 *   +----------+                    +---------+                          +-----------+
 *        ^                                                                     |
 *        |                                                                     v cache full / split
 *        |                                                              +------------+
 *        |                                                              | committing |
 *        |                                                              | WAL flush  |
 *        |                                                              +------------+
 *        |                                                                     |
 *        +-------------------- on_end() / dtor ----------------------- finalising
 *                                                                              |
 *                                                                              v
 *                                                                          (optional)
 *                                                                          vacuum
 * @endcode
 *
 * @par Example
 * @code
 *   vlink::BagWriter::Config cfg;
 *   cfg.compress = vlink::BagWriter::kCompressLzav;
 *   cfg.wal_mode = true;
 *
 *   auto writer = vlink::BagWriter::create("/data/recording.vdb", cfg);
 *
 *   writer->async_run();
 *   writer->push("dds://lidar/front", "demo.proto.PointCloud",
 *                vlink::SchemaType::kProtobuf,
 *                vlink::ActionType::kPublish,
 *                serialized_bytes);
 * @endcode
 *
 * @see BagWriter, VCAPWriter
 */

#pragma once

#include <memory>
#include <string>

#include "./bag_writer.h"

namespace vlink {

/**
 * @class VDBWriter
 * @brief Concrete SQLite-backed @c BagWriter implementation with WAL caching and batch commits.
 *
 * @details
 * Prefer @c BagWriter::create() for format-agnostic instantiation; instantiate this class
 * directly only when a SQLite-specific feature is required.
 */
class VLINK_EXPORT VDBWriter final : public BagWriter {
 public:
  /**
   * @brief Opens or creates a SQLite bag file for recording.
   *
   * @param path    Filesystem path of the @c .vdb or @c .vdbx target.
   * @param config  Recording configuration (split policy, compression, cache thresholds).
   */
  explicit VDBWriter(const std::string& path, const Config& config = {});

  /**
   * @brief Commits any cached writes and closes the SQLite database.
   */
  ~VDBWriter() override;

  /**
   * @brief Registers a callback invoked at each file-split boundary.
   *
   * @param callback  Receives (split_index, filename) before or after the split.
   * @param before    @c true fires before the new file is opened; @c false fires after.
   */
  void register_split_callback(SplitCallback&& callback, bool before) override;

  /**
   * @brief Registers a resolver that maps a serialisation-type string to @c SchemaData.
   *
   * @param callback  Function consulted before inserting a URL row.
   */
  void register_schema_callback(SchemaCallback&& callback) override;

  /**
   * @brief Embeds @p schema_data in the @c schemas table for offline introspection.
   *
   * @param schema_data  Schema descriptor to embed.
   * @param immediate    @c true merges synchronously; @c false enqueues the write.
   * @return @c false only when an immediate merge failed.
   */
  bool push_schema(const SchemaData& schema_data, bool immediate = false) override;

  /**
   * @brief Records one message to the SQLite bag file.
   *
   * @param url                    Topic URL identifying the channel.
   * @param ser_type               Serialisation type string.
   * @param schema_type            Coarse schema family for the payload.
   * @param action_type            Action category (@c kPublish, @c kRequest, etc.).
   * @param data                   Serialised payload bytes.
   * @param microseconds_timestamp Optional caller-provided timestamp in microseconds.
   * @param immediate              @c true writes synchronously bypassing the cache.
   * @return Timestamp recorded in microseconds; negative on error.
   */
  int64_t push(const std::string& url, const std::string& ser_type, SchemaType schema_type, ActionType action_type,
               const Bytes& data, int64_t* microseconds_timestamp = nullptr, bool immediate = false) override;

  /**
   * @brief Returns the current value of the internal dumping flag.
   *
   * @return @c true while messages are actively being persisted.
   */
  [[nodiscard]] bool is_dumping() const override;

  /**
   * @brief Reports whether split-file recording is active.
   *
   * @return @c true when emitting a @c .vdbx manifest with multiple parts.
   */
  [[nodiscard]] bool is_split_mode() const override;

  /**
   * @brief Returns the index of the split part currently being written.
   *
   * @return Zero-based split index.
   */
  [[nodiscard]] int get_split_index() const override;

  /**
   * @brief Records the expected message-loss ratio for @p url.
   *
   * @param url   Topic URL.
   * @param loss  Loss ratio; values greater than @c 1.0 are stored as @c -1.
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

  struct Impl;
  std::unique_ptr<Impl> impl_;

  VLINK_DISALLOW_COPY_AND_ASSIGN(VDBWriter)
};

}  // namespace vlink
