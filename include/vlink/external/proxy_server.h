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
 * @file proxy_server.h
 * @brief VLink proxy server daemon -- singleton per process.
 *
 * @details
 * @c ProxyServer is the server-side half of the VLink proxy subsystem.  It
 * inherits from @c MessageLoop and runs an event loop that:
 *
 * -# Hosts a @c DiscoveryViewer to enumerate all active publishers and
 *    subscribers on the DDS domain.
 * -# Accepts @c Control messages from @c ProxyAPI clients via a
 *    security-authenticated DDS channel.
 * -# Broadcasts a 1-second @c Time heartbeat carrying CPU/memory usage,
 *    version string, hostname, and wall-clock / boot-time.
 * -# Publishes per-topic statistics (@c freq, @c rate, @c loss, @c latency)
 *    once per second via a security-authenticated @c InfoList channel.
 * -# Relays raw message bytes from discovered publishers to connected
 *    @c ProxyAPI listeners when operating in observe, record, or play mode.
 * -# Optionally manages an embedded Iceoryx RouDi daemon when
 *    @c Config::use_iox is @c true.
 * -# Loads and manages @c RunablePluginInterface shared-library plugins
 *    from @c Config::runnable_list.
 *
 * @par Singleton Constraint
 * Only one @c ProxyServer may be constructed per operating-system process.
 * A second construction attempt logs a fatal message and returns without
 * initialising any channels.
 *
 * @par Communication Architecture
 * @code
 *  ProxyAPI (kController)
 *       |--- ControlPub --> [DDS] --> ControlSub ---|
 *       |                                           v
 *       |                               ProxyServer (this)
 *       |                                           |
 *       |<-- TimeSub <--- [DDS] <--- TimePub -------|
 *       |<-- InfoSub <--- [DDS] <--- InfoPub -------|
 *       |<-- DataSub <--- [DDS/SHM] <- DataPub -----|
 * @endcode
 *
 * @par Runnable Plugin Lifecycle
 * Plugins listed in @c Config::runnable_list are loaded in the constructor.
 * When the MessageLoop starts (@c on_begin) each plugin's @c on_init() and
 * @c async_run() are called.  When the loop stops (@c on_end) each plugin's
 * @c on_deinit(), @c quit(), and @c wait_for_quit() are called in order.
 *
 * @par Environment Variables
 * - @c VLINK_INTRA_BIND -- when set (any value), the server subscribes to
 *   @c intra:// topics in addition to DDS/SHM, enabling in-process observation.
 *
 * @par Usage Example
 * @code
 * vlink::ProxyServer::Config cfg;
 * cfg.dds_impl    = "dds";
 * cfg.domain_id   = 0;
 * cfg.reliable    = false;
 * cfg.async       = true;
 * cfg.use_iox     = false;
 *
 * vlink::ProxyServer server(cfg);
 * server.async_run();      // start event loop in a background thread
 * // ... application runs ...
 * server.quit(true);
 * server.wait_for_quit();
 * @endcode
 *
 * @note
 * - The constructor must be called on the main thread before any @c ProxyAPI
 *   clients connect on the same domain.
 * - @c ProxyServer must be destroyed before the process exits; the destructor
 *   stops all timers, waits for the DiscoveryViewer, and releases every DDS
 *   handle in a deterministic order to avoid dangling callbacks.
 */

#pragma once

#undef VLINK_PROXY_SERVER_EXPORT
#ifdef VLINK_PROXY_SERVER_LIBRARY_STATIC
#define VLINK_PROXY_SERVER_EXPORT
#elif defined(_WIN32) || defined(__CYGWIN__)
#ifdef VLINK_PROXY_SERVER_LIBRARY
#define VLINK_PROXY_SERVER_EXPORT __declspec(dllexport)
#else
#define VLINK_PROXY_SERVER_EXPORT __declspec(dllimport)
#endif
#else
#define VLINK_PROXY_SERVER_EXPORT __attribute__((visibility("default")))
#endif

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "../base/message_loop.h"

namespace vlink {

/**
 * @class ProxyServer
 * @brief VLink proxy server daemon backed by a MessageLoop.
 *
 * @details
 * Manages all DDS/SHM channels, a @c DiscoveryViewer, heartbeat timers, and
 * optional runnable plugins.  Only one instance may exist per process.
 * Use @c async_run() (inherited from @c MessageLoop) to start the event loop.
 */
class VLINK_PROXY_SERVER_EXPORT ProxyServer : public MessageLoop {
 public:
  /**
   * @struct Config
   * @brief Construction-time configuration for @c ProxyServer.
   *
   * @details
   * The fields @c reliable, @c enable_tcp, and @c direct are broadcast in every
   * @c Time heartbeat so that connecting @c ProxyAPI clients can verify
   * compatibility.  Mismatches result in the client reporting an error and
   * refusing to connect.
   *
   * @par Field Summary
   *
   * | Field                   | Default  | Description                                                           |
   * | ----------------------- | -------- | --------------------------------------------------------------------- |
   * | async                   | false    | Publish data on the MessageLoop thread; false = inline on subscriber. |
   * | reliable                | false    | Use reliable DDS QoS for data channels.                               |
   * | enable_tcp              | false    | Use TCP transport for data channels.                                  |
   * | direct                  | false    | Use SHM (Iceoryx) instead of DDS for data forwarding.                 |
   * | native_mode             | false    | Restrict all DDS traffic to 127.0.0.1 (loopback).                    |
   * | domain_id               | 0        | DDS domain ID shared with all clients.                                |
   * | buf_size                | 0        | DDS socket buffer size in bytes; 0 = built-in default.               |
   * | mtu_size                | 0        | DDS MTU size in bytes; 0 = built-in default.                         |
   * | max_packet_size         | 0        | Maximum relayed message size in MiB; 0 = unlimited.                  |
   * | security_key            | ""       | Security key for Time, Info, and Control DDS channels.               |
   * | bind_ip                 | ""       | Bind DDS sockets to this IP; empty = any interface.                  |
   * | peer_ip                 | ""       | Unicast peer IP for DDS discovery; empty = multicast.                |
   * | dds_impl                | "dds"    | DDS implementation: "dds", "ddsc", "ddsr", etc.                      |
   * | use_iox                 | false    | Launch an embedded Iceoryx RouDi daemon at startup.                  |
   * | iox_monitoring          | true     | Enable Iceoryx introspection/monitoring.                             |
   * | iox_strategy            | 1        | Iceoryx memory strategy index passed to ShmConf::init_roudi().       |
   * | iox_config              | ""       | Path to a custom Iceoryx TOML configuration file; empty = default.   |
   * | runnable_version_major  | 1        | Required major version for loaded runnable plugins.                  |
   * | runnable_version_minor  | 0        | Required minor version for loaded runnable plugins.                  |
   * | runnable_prefix         | ""       | Library name prefix for plugin shared objects.                       |
   * | runnable_list           | {}       | Names of runnable plugins (@c RunablePluginInterface in the API).    |
   */
  struct Config final {
    bool async{false};                       ///< Async data forwarding on the MessageLoop thread.
    bool reliable{false};                    ///< Use reliable DDS QoS; must match all client configs.
    bool enable_tcp{false};                  ///< Use TCP transport for DDS data channels.
    bool direct{false};                      ///< Use SHM channels for data (requires use_iox or external RouDi).
    bool native_mode{false};                 ///< Restrict all DDS traffic to loopback (127.0.0.1).
    int domain_id{0};                        ///< DDS domain ID.
    uint32_t buf_size{0};                    ///< DDS socket send/receive buffer in bytes; 0 = default.
    uint32_t mtu_size{0};                    ///< DDS fragment MTU in bytes; 0 = default.
    double max_packet_size{0};               ///< Maximum relayed payload in MiB; 0 = no limit.
    std::string security_key;                ///< Security key for authenticated DDS channels.
    std::string bind_ip;                     ///< Local IP to bind DDS sockets; empty = any.
    std::string peer_ip;                     ///< Peer unicast IP for DDS; empty = multicast.
    std::string dds_impl{"dds"};             ///< DDS implementation transport.
    bool use_iox{false};                     ///< Initialise embedded Iceoryx RouDi daemon.
    bool iox_monitoring{true};               ///< Enable Iceoryx introspection monitoring.
    int iox_strategy{1};                     ///< Iceoryx memory allocation strategy.
    std::string iox_config;                  ///< Path to Iceoryx TOML config file; empty = default.
    uint16_t runnable_version_major{1};      ///< Required major ABI version for runnable plugins.
    uint16_t runnable_version_minor{0};      ///< Required minor ABI version for runnable plugins.
    std::string runnable_prefix;             ///< Library filename prefix for plugin discovery.
    std::vector<std::string> runnable_list;  ///< Ordered list of plugin names to load on startup.
  };

  /**
   * @brief Constructs a @c ProxyServer and initialises all subsystems.
   *
   * @details
   * Performs the following steps in order:
   * -# Checks the process-global singleton guard; logs a fatal message and
   *    returns early if another instance already exists.
   * -# Reads the @c VLINK_INTRA_BIND environment variable.
   * -# If @c config.use_iox is @c true, calls @c init_shm_roudi() to start
   *    an embedded Iceoryx RouDi process.
   * -# Calls @c init_server() to create all DDS/SHM channels, subscribe to
   *    @c Control messages, and start the 1-second @c time_timer and
   *    @c info_timer on the @c DiscoveryViewer's loop.
   * -# Calls @c init_runnable() to load all plugins listed in
   *    @c config.runnable_list.
   *
   * @param config  Server configuration.  See @c Config for field details.
   *
   * @note The constructor does not start the MessageLoop; call @c async_run()
   *       (or @c run()) separately.
   */
  explicit ProxyServer(const Config& config);

  /**
   * @brief Destructor.
   *
   * @details
   * Stops and waits for the MessageLoop, all runnable plugins, the
   * @c DiscoveryViewer, and all DDS handles in a deterministic teardown
   * sequence.  Also marks the singleton guard so future constructions in
   * the same process behave correctly.
   */
  ~ProxyServer() override;

 protected:
  size_t get_max_task_count() const override;

  uint32_t get_max_elapsed_time() const override;

  void on_begin() override;

  void on_end() override;

 private:
  void init_shm_roudi();

  void init_server();

  void init_runnable();

  void send_time();

  void send_control(const void* control_data);

  void update_all();

  std::unique_ptr<struct ProxyServerImpl> impl_;

  VLINK_DISALLOW_COPY_AND_ASSIGN(ProxyServer)
};

}  // namespace vlink
