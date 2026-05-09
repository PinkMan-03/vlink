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
 * @file discovery_viewer.h
 * @brief Real-time view of all active VLink endpoints discovered on the current host or network.
 *
 * @details
 * @c DiscoveryViewer subscribes to @c DiscoveryReporter broadcasts and maintains a live
 * list of all known VLink processes and their endpoints.  It is used by the @c vlink-cli
 * tool and by monitoring applications.
 *
 * Each entry in the @c Info list describes one topic URL and the set of processes that
 * publish or subscribe to it.  Entries are sorted for stable display.
 *
 * @par Filter modes
 * | FilterType       | Shows                                                   |
 * | ---------------- | ------------------------------------------------------- |
 * | kFilterNone      | All discovered endpoints                                |
 * | kFilterAvailable | Only endpoints that have at least one live process      |
 * | kFilterNative    | Only endpoints from the same host                       |
 *
 * @par Usage
 * @code
 * vlink::DiscoveryViewer viewer(vlink::DiscoveryViewer::kFilterAvailable);
 * viewer.register_callback([](const std::vector<vlink::DiscoveryViewer::Info>& list) {
 *     for (auto& info : list) {
 *         VLOG_I("URL: ", info.url, " type: ", info.type);
 *     }
 * });
 * viewer.async_run();
 * @endcode
 *
 * @note
 * - The callback is invoked on the viewer's event loop thread.
 * - Use @c global_get() to share one viewer across the application.
 * - Endpoints that do not send a heartbeat within a timeout are removed automatically.
 */

#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "../base/functional.h"
#include "../base/macros.h"
#include "../base/message_loop.h"
#include "../impl/types.h"

namespace vlink {

/**
 * @class DiscoveryViewer
 * @brief Background @c MessageLoop that aggregates live VLink endpoint discovery data.
 */
class VLINK_EXPORT DiscoveryViewer : public MessageLoop {
 public:
  /**
   * @brief Controls which endpoints are included in the discovery view.
   *
   * | Value            | Meaning                                              |
   * | ---------------- | ---------------------------------------------------- |
   * | kFilterNone      | All discovered endpoints                             |
   * | kFilterAvailable | Endpoints with at least one live process             |
   * | kFilterNative    | Endpoints from the local host only                   |
   */
  enum FilterType : uint8_t {
    kFilterNone = 0,       ///< Show all endpoints.
    kFilterAvailable = 1,  ///< Show only endpoints with live processes.
    kFilterNative = 2,     ///< Show only local-host endpoints.
  };

  /**
   * @struct Process
   * @brief Information about one process that hosts a VLink endpoint.
   */
  struct VLINK_EXPORT Process final {
    uint32_t type{0};     ///< ImplType bitmask of all communication types in this process.
    std::string host;     ///< Hostname of the process.
    uint32_t pid{0};      ///< Process ID.
    std::string name;     ///< Process name.
    std::string ip;       ///< IP address of the process host.
    double profiler{-1};  ///< CPU utilisation reported by the process (-1 if unavailable).

    /**
     * @brief Sorts processes for stable display (by host, pid).
     *
     * @param target  Right-hand side.
     * @return @c true if @c *this should sort before @p target.
     */
    bool operator<(const Process& target) const noexcept;
  };

  /**
   * @struct Info
   * @brief Aggregated discovery entry for one VLink URL.
   *
   * @details
   * Holds the URL, communication type bitmask, serialisation type, coarse schema family,
   * and the list of processes that have registered an endpoint for this URL.
   */
  struct VLINK_EXPORT Info final {
    int sort_index{-1};                            ///< Stable sort key (assigned by the viewer).
    uint32_t type{0};                              ///< ImplType bitmask.
    std::string url;                               ///< Full VLink URL string.
    std::string ser_type;                          ///< Serialisation type (e.g., "demo.proto.PointCloud").
    SchemaType schema_type{SchemaType::kUnknown};  ///< Coarse schema family derived from discovery metadata.
    std::vector<Process> process_list;             ///< Processes hosting this endpoint.

    /**
     * @brief Sorts entries for stable display (by sort_index, url).
     *
     * @param target  Right-hand side.
     * @return @c true if @c *this should sort before @p target.
     */
    bool operator<(const Info& target) const noexcept;
  };

  /**
   * @brief Callback fired whenever the discovery information is updated.
   *
   * @details
   * Invoked on the viewer's event loop thread.  The vector is a snapshot of the
   * current live endpoint list.
   */
  using Callback = Function<void(const std::vector<Info>& info_list)>;

  /**
   * @brief Converts a transport string to the corresponding @c ImplType value.
   *
   * @param str  Transport string (e.g., @c "dds", @c "shm").
   * @return Corresponding @c ImplType, or 0 if unknown.
   */
  [[nodiscard]] static ImplType convert_type(std::string_view str);

  /**
   * @brief Returns a display string for an @c ImplType bitmask.
   *
   * @param type  ImplType bitmask.
   * @return Human-readable type string.
   */
  [[nodiscard]] static std::string convert_type_to_view(uint32_t type);

  /**
   * @brief Returns a combined type-and-process display string.
   *
   * @param type          ImplType bitmask.
   * @param process_list  List of processes to include in the display.
   * @return Combined display string.
   */
  [[nodiscard]] static std::string convert_type_to_view(uint32_t type, const std::vector<Process>& process_list);

  /**
   * @brief Returns the intra-process address used by the discovery subsystem.
   *
   * @return Discovery listen address string.
   */
  [[nodiscard]] static std::string get_listen_address();

  /**
   * @brief Returns the process-global @c DiscoveryViewer singleton.
   *
   * @details
   * Created with @c kFilterNone on first call.  The singleton is destroyed on process exit.
   *
   * @return Raw pointer to the global @c DiscoveryViewer.
   */
  static DiscoveryViewer* global_get();

  /**
   * @brief Constructs a @c DiscoveryViewer with the given filter type.
   *
   * @param type  Controls which endpoints are included.  Default: @c kFilterNone.
   */
  explicit DiscoveryViewer(FilterType type = kFilterNone);

  /**
   * @brief Destructor -- stops the viewer loop.
   */
  ~DiscoveryViewer() override;

  /**
   * @brief Registers the callback invoked when the endpoint list changes.
   *
   * @details
   * Replaces any previously registered callback.  The callback is invoked on the
   * viewer's loop thread.
   *
   * @param callback  Function receiving the updated @c Info list.
   */
  void register_callback(Callback&& callback);

  /**
   * @brief Returns a snapshot of the current discovery information.
   *
   * @return Copy of the live @c Info list at the time of the call.
   */
  [[nodiscard]] std::vector<Info> get_info_list();

  /**
   * @brief Returns the serialisation type string for a given URL.
   *
   * @param url  Topic URL to look up.
   * @return Serialisation type (e.g., @c "demo.proto.PointCloud"), or empty if not known.
   */
  [[nodiscard]] std::string get_ser_type(const std::string& url) const;

  /**
   * @brief Returns the coarse schema family for a given URL.
   *
   * @param url  Topic URL to look up.
   * @return Schema family, or @c SchemaType::kUnknown if not known.
   */
  [[nodiscard]] SchemaType get_schema_type(const std::string& url) const;

 protected:
  size_t get_max_task_count() const override;

  uint32_t get_max_elapsed_time() const override;

  void on_begin() override;

  void on_end() override;

 private:
  void process_timeout();

  void process_offline(std::string_view hostname, uint32_t pid, std::string_view process_name);

  void sort_url();

  void report_list();

  struct Impl;
  std::unique_ptr<Impl> impl_;

  VLINK_DISALLOW_COPY_AND_ASSIGN(DiscoveryViewer)
};

}  // namespace vlink
