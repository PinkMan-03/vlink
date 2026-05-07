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
 * @file proxy_api.h
 * @brief Client-side VLink proxy monitoring and control API.
 *
 * @details
 * @c ProxyAPI provides a C++ interface for connecting to a running @c ProxyServer
 * daemon.  It inherits from @c MessageLoop, but incoming DDS/SHM callbacks may
 * execute on transport-managed receive threads.  The inherited @c MessageLoop is
 * used for posted tasks and timers when @c run() or @c async_run() is started.
 *
 * @par Roles
 * A @c ProxyAPI instance operates in one of two roles:
 *
 * | Role          | Description                                                              |
 * | ------------- | ------------------------------------------------------------------------ |
 * | kController   | Can send Control messages to direct the server's observation/playback.   |
 * | kListener     | Passive observer only; send_control() and send_data() are rejected.      |
 *
 * @par Operation Modes
 * The proxy server supports eight operation modes, selectable by @c kController:
 *
 * | Mode              | Value | Description                                             |
 * | ----------------- | ----- | ------------------------------------------------------- |
 * | kOffline          |   0   | Disconnected; server releases all subscriptions.        |
 * | kObserveOne       |   1   | Observe a single selected topic.                        |
 * | kObserveAll       |   2   | Observe all discovered topics.                          |
 * | kRecord           |   3   | Record all topics matching the subscription URL list.   |
 * | kPlay             |   4   | Replay previously recorded data via the server.         |
 * | kEdit             |   5   | Edit mode: injects data through the server.             |
 * | kAuto             |   6   | Auto mode: observe specified topics.                    |
 * | kAutoAndObserveAll|   7   | Auto + observe all topics simultaneously.               |
 *
 * @par Error Codes
 *
 * | Error                 | Value | Description                                       |
 * | --------------------- | ----- | ------------------------------------------------- |
 * | kNoError              |   0   | No error.                                         |
 * | kModeError            |   1   | Unsupported mode requested.                       |
 * | kControlError         |   2   | Control ID mismatch with the server.              |
 * | kReliableCompError    |   3   | reliable setting mismatch between client/server.  |
 * | kTcpCompError         |   4   | enable_tcp setting mismatch.                      |
 * | kDirectCompError      |   5   | direct setting mismatch.                          |
 * | kMultiProxyError      |   7   | Multiple proxy servers detected on the network.   |
 * | kVersionCompError     |   8   | VLink version mismatch between client and server. |
 * | kUnknownError         |   9   | Unknown error.                                    |
 *
 * @par Connectivity and Heartbeat
 * Internally the API subscribes to a 1-second Time heartbeat published by
 * @c ProxyServer over a security-authenticated DDS channel.  If no heartbeat is
 * received for 5 consecutive seconds the connection is declared lost and
 * @c ConnectCallback is invoked with @c connected = @c false.  A @c kController
 * client also sends an initial @c Control message at construction and re-sends
 * the last control automatically when the server reconnects.
 *
 * @par Communication Channels
 * The transport channels are determined by @c Config::direct:
 *
 * | direct | Data path                   | Control/Info/Time path     |
 * | ------ | --------------------------- | -------------------------- |
 * | false  | DDS (reliable or best-effort) | DDS (security-enabled)   |
 * | true   | SHM (Iceoryx)               | DDS (security-enabled)     |
 *
 * @par Version Matching
 * When @c Config::match_version is @c true (default) the client checks that
 * the server's reported @c VLINK_VERSION string matches its own at connection
 * time.  A mismatch triggers @c kVersionCompError.
 *
 * @par Usage Example (Controller)
 * @code
 * vlink::ProxyAPI::Config cfg;
 * cfg.role        = vlink::ProxyAPI::kController;
 * cfg.dds_impl    = "dds";
 * cfg.domain_id   = 0;
 * cfg.reliable    = false;
 * cfg.match_version = true;
 *
 * vlink::ProxyAPI api(cfg);
 *
 * api.register_connect_callback([](bool connected) {
 *   if (connected) {
 *     // server is online
 *   }
 * });
 *
 * api.register_info_callback([](const std::vector<vlink::ProxyAPI::Info>& list) {
 *   for (const auto& info : list) {
 *     // info.url, info.freq, info.status, ...
 *   }
 * });
 *
 * // Start observing a specific topic
 * vlink::ProxyAPI::Control ctrl;
 * ctrl.mode = vlink::ProxyAPI::kObserveOne;
 * ctrl.url_meta_list.emplace_back(
 *     {"dds://my/topic", "demo.proto.PointCloud", vlink::SchemaType::kProtobuf, vlink::kSubscriber});
 * api.send_control(ctrl);
 *
 * api.async_run();      // optional: runs the inherited MessageLoop on a background thread
 * @endcode
 *
 * @note
 * - Only one @c ProxyServer should exist on a given DDS domain/channel at a time;
 *   connecting to two will trigger @c kMultiProxyError.
 * - @c send_control() and @c send_data() return @c false immediately when the
 *   role is @c kListener.
 */

#pragma once

#undef VLINK_PROXY_API_EXPORT
#ifdef VLINK_PROXY_API_LIBRARY_STATIC
#define VLINK_PROXY_API_EXPORT
#elif defined(_WIN32) || defined(__CYGWIN__)
#ifdef VLINK_PROXY_API_LIBRARY
#define VLINK_PROXY_API_EXPORT __declspec(dllexport)
#else
#define VLINK_PROXY_API_EXPORT __declspec(dllimport)
#endif
#else
#define VLINK_PROXY_API_EXPORT __attribute__((visibility("default")))
#endif

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_set>
#include <vector>

#include "../base/bytes.h"
#include "../base/functional.h"
#include "../base/message_loop.h"
#include "../impl/types.h"

namespace vlink {

/**
 * @class ProxyAPI
 * @brief Client-side proxy monitoring and control API backed by a MessageLoop.
 *
 * @details
 * Connects to a @c ProxyServer daemon over DDS (or SHM in direct mode) and
 * exposes callbacks for connection state, error codes, heartbeat timestamps,
 * topic statistics, and raw data payloads.  Inherits @c MessageLoop; start the
 * loop explicitly if you need queued tasks, timers, or async control posts to
 * execute on that loop.
 */
class VLINK_PROXY_API_EXPORT ProxyAPI : public MessageLoop {
 public:
  /**
   * @enum Mode
   * @brief Proxy operation modes sent via @c send_control().
   *
   * @details
   * The mode governs what the @c ProxyServer observes or replays.
   * Only a @c kController client may change the mode.
   */
  enum Mode : uint8_t {
    kOffline = 0,            ///< Disconnected; server releases all subscriptions.
    kObserveOne = 1,         ///< Observe a single topic from the URL list.
    kObserveAll = 2,         ///< Observe all discovered topics on the network.
    kRecord = 3,             ///< Record data from topics in the URL list.
    kPlay = 4,               ///< Replay: inject previously recorded data.
    kEdit = 5,               ///< Edit: forward data injected by the controller.
    kAuto = 6,               ///< Auto: observe specified URLs and forward to subscribers.
    kAutoAndObserveAll = 7,  ///< Auto + observe every discovered topic.
  };

  /**
   * @enum Error
   * @brief Compatibility and protocol error codes reported via @c ErrorCallback.
   *
   * @details
   * Errors are detected when the client parses an incoming @c Time heartbeat.
   * The callback fires only when the error state changes.
   */
  enum Error : uint8_t {
    kNoError = 0,            ///< No error; connection is healthy.
    kModeError = 1,          ///< Unsupported mode requested.
    kControlError = 2,       ///< Server responded with a different control_id.
    kReliableCompError = 3,  ///< Client and server have mismatched reliable setting.
    kTcpCompError = 4,       ///< Client and server have mismatched enable_tcp setting.
    kDirectCompError = 5,    ///< Client and server have mismatched direct setting.
    kMultiProxyError = 7,    ///< Multiple proxy servers detected on the same domain.
    kVersionCompError = 8,   ///< VLINK_VERSION string differs between client and server.
    kUnknownError = 9,       ///< Unknown or unclassified error.
  };

  /**
   * @enum Status
   * @brief Per-topic activity status reported in @c Info.
   */
  enum Status : uint8_t {
    kActive = 0,    ///< Topic is actively receiving data.
    kInActive = 1,  ///< Topic exists but has not received data recently.
    kPending = 2,   ///< Topic was just discovered; statistics are still accumulating.
    kInvalid = 3,   ///< Topic type does not support observation (e.g., subscriber-only).
  };

  /**
   * @enum Role
   * @brief Role of this @c ProxyAPI instance.
   *
   * @details
   * - @c kController can call @c send_control() and @c send_data().
   * - @c kListener is a passive observer; control/data send calls return @c false.
   */
  enum Role : uint8_t { kController = 0, kListener };

  /**
   * @struct Process
   * @brief Describes a VLink node process attached to a topic endpoint.
   */
  struct Process final {
    uint32_t type{0};  ///< Node type bitmask (kPublisher / kSubscriber / kServer / kClient / kSetter / kGetter).
    std::string host;  ///< Hostname of the machine running the process.
    uint32_t pid{0};   ///< Operating-system process ID.
    std::string name;  ///< Human-readable process name.
    std::string ip;    ///< IP address of the network interface.
  };

  /**
   * @struct Info
   * @brief Statistics and metadata for a single discovered topic endpoint.
   *
   * @details
   * Delivered in batches via @c InfoCallback once per second.  The fields
   * @c freq, @c rate, @c loss, and @c latency are weighted moving averages
   * computed over the last two 1-second collection intervals.
   */
  struct Info final {
    uint32_t type{0};                         ///< Node type bitmask for this endpoint.
    std::string url;                          ///< Full topic URL, e.g. @c "dds://my/topic".
    std::string ser;                          ///< Serialisation type, e.g. @c "demo.proto.PointCloud".
    SchemaType schema{SchemaType::kUnknown};  ///< Coarse schema family of the payload.
    Status status{kInvalid};                  ///< Activity status of the topic.
    float freq{0};                            ///< Observed message frequency in messages/s.
    uint64_t rate{0};                         ///< Observed throughput in bytes/s.
    float loss{0};                            ///< Sample loss ratio in the range [0, 1].
    float latency{0};                         ///< End-to-end latency in milliseconds.
    std::vector<Process> process_list;        ///< List of connected publisher/subscriber processes.
  };

  /**
   * @struct UrlMeta
   * @brief Associates a topic URL with its serialisation type and node role.
   *
   * @details
   * Used in @c Control::url_meta_list to instruct the server which topics to
   * subscribe to or publish on.
   */
  struct UrlMeta final {
    std::string url;                          ///< Full topic URL.
    std::string ser;                          ///< Required serialisation type carried on this proxy route.
    SchemaType schema{SchemaType::kUnknown};  ///< Required coarse schema family carried on this proxy route.
    ImplType type{kSubscriber};  ///< Whether the server should act as publisher or subscriber for this URL.
  };

  /**
   * @struct Control
   * @brief Control message sent from a @c kController client to @c ProxyServer.
   *
   * @details
   * Sets the server's operating mode and the list of topics to observe or play.
   * The @c filter_str is a space-separated list of substrings; topics (or process
   * names when @c filter_by_process is @c true) must contain at least one
   * substring to be included.
   */
  struct Control final {
    Mode mode{kOffline};                 ///< Target operation mode.
    std::vector<UrlMeta> url_meta_list;  ///< Topics to observe / inject (mode-dependent).
    bool filter_by_process{false};       ///< When true, filter_str matches process names; otherwise matches URLs.
    std::string filter_str;              ///< Space-separated filter keywords (case-insensitive).
    uint32_t filter_type{0};             ///< Type filter: 0=all, 1=pub+sub pair, 2=srv+cli pair, etc.
  };

  /**
   * @struct Data
   * @brief Raw message payload delivered via @c DataCallback or sent via @c send_data().
   *
   * @details
   * In record/observe mode the server relays raw serialised bytes together with
   * routing metadata.  @c timestamp is the elapsed time in microseconds since the
   * current proxy data session / control generation started; @c seq is the proxy
   * relay sequence number for the URL.
   */
  struct Data final {
    std::string url;                          ///< Topic URL the data was captured on.
    std::string ser;                          ///< Serialisation type of the payload.
    SchemaType schema{SchemaType::kUnknown};  ///< Coarse schema family of the payload.
    Bytes raw;                                ///< Raw serialised message bytes.
    int64_t timestamp{-1};                    ///< Elapsed time in microseconds since session start; -1 if unset.
    int64_t seq{0};                           ///< Publisher sequence number.
  };

  /**
   * @struct Config
   * @brief Construction-time configuration for @c ProxyAPI.
   *
   * @details
   * All fields must be set consistently with the @c ProxyServer::Config they
   * connect to; a mismatch on @c reliable, @c enable_tcp, or @c direct will
   * trigger the corresponding @c Error code once the client receives the first
   * heartbeat.
   */
  struct Config final {
    Role role{kController};       ///< Role of this client instance.
    int domain_id{0};             ///< DDS domain ID; must match the server's domain_id.
    std::string dds_impl{"dds"};  ///< DDS implementation transport: "dds", "ddsc", "ddsr", etc.
    std::string security_key;     ///< Optional security key for encrypted DDS channels; must match the server key.
    bool native{false};           ///< When true, restricts all DDS traffic to 127.0.0.1 (loopback only).
    bool reliable{false};         ///< Use reliable DDS QoS; must match the server's reliable setting.
    bool direct{false};           ///< Use direct SHM channels for data; must match the server's direct setting.
    bool enable_tcp{false};       ///< Use TCP transport for data channels; must match the server's enable_tcp.
    bool match_version{true};     ///< Reject connections when the server's VLINK_VERSION differs from the client.
    std::string allow_ip;         ///< Bind DDS sockets to this IP address (empty = any).
    std::string peer_ip;          ///< Unicast peer IP for DDS discovery (empty = multicast).
    int buf_size{0};              ///< Socket send/receive buffer size in bytes; 0 uses the built-in default.
    int mtu_size{0};              ///< DDS MTU size in bytes; 0 uses the built-in default.
  };

  /**
   * @brief Callback invoked when the connection state with @c ProxyServer changes.
   *
   * @details
   * @c connected is @c true when the first valid heartbeat is received; @c false
   * when 5 seconds pass without a heartbeat or when the control channel disconnects.
   */
  using ConnectCallback = vlink::MoveFunction<void(bool connected)>;

  /**
   * @brief Callback invoked when an error or error-clear event is detected.
   *
   * @details
   * Only fired when the @c Error state transitions (e.g. @c kNoError to
   * @c kVersionCompError, or back to @c kNoError).
   */
  using ErrorCallback = vlink::MoveFunction<void(Error error)>;

  /**
   * @brief Callback delivering the server's wall-clock and boot-time from each heartbeat.
   *
   * @param sys_time   Server system time in microseconds since the Unix epoch.
   * @param boot_time  Server uptime in microseconds since boot.
   */
  using TimeCallback = vlink::MoveFunction<void(uint64_t sys_time, uint64_t boot_time)>;

  /**
   * @brief Callback delivering the per-topic statistics list once per second.
   *
   * @param info_list  List of @c Info records for all currently observed topics.
   */
  using InfoCallback = vlink::MoveFunction<void(const std::vector<Info>& info_list)>;

  /**
   * @brief Callback delivering raw message data relayed by @c ProxyServer.
   *
   * @details
   * Fired for every message forwarded by the server in observe, record, or
   * play mode.  The @c Data::raw bytes are shallow-borrowed; copy if you need
   * to retain them beyond the callback.
   */
  using DataCallback = vlink::MoveFunction<void(const Data& data)>;

  /**
   * @brief Constructs a @c ProxyAPI with the given configuration.
   *
   * @details
   * Initialises all DDS/SHM channels based on @c config.  A unique @c control_id
   * is derived from the CPU timestamp at construction time so that the server can
   * distinguish simultaneous controllers.  The inherited @c MessageLoop is not
   * started automatically; call @c run() or @c async_run() yourself if you need
   * timer-driven reconnect logic or queued control posts to execute.
   *
   * @param config  Configuration for the proxy connection.
   */
  explicit ProxyAPI(const Config& config);

  /**
   * @brief Destructor.
   *
   * @details
   * Quits the inherited @c MessageLoop (if it is running), waits for it to stop,
   * and then releases DDS/SHM handles.  Destruction does not publish an extra
   * @c kOffline control message.
   */
  ~ProxyAPI() override;

  /**
   * @brief Registers a callback for connection state changes.
   *
   * @details
   * If the API is already connected when this method is called the callback
   * is invoked immediately with @c connected = @c true inside this call.
   * The callback is replaced atomically; only one callback is active at a time.
   *
   * @param callback  Callable with signature @c void(bool connected).
   */
  void register_connect_callback(ConnectCallback&& callback);

  /**
   * @brief Registers a callback for error state transitions.
   *
   * @details
   * If a non-zero error is already active when this method is called the
   * callback is invoked immediately with the current error inside this call.
   *
   * @param callback  Callable with signature @c void(Error error).
   */
  void register_error_callback(ErrorCallback&& callback);

  /**
   * @brief Registers a callback for heartbeat timestamp delivery.
   *
   * @details
   * If timestamps have already been received (timers are active) the callback
   * is invoked immediately with the latest extrapolated times inside this call.
   *
   * @param callback  Callable with signature @c void(uint64_t sys_time, uint64_t boot_time).
   */
  void register_time_callback(TimeCallback&& callback);

  /**
   * @brief Registers a callback for per-topic statistics updates.
   *
   * @details
   * Invoked once per second with the full @c Info list from the server.
   * No immediate invocation occurs at registration; data arrives on the next
   * server broadcast cycle.
   *
   * @param callback  Callable with signature @c void(const std::vector<Info>& info_list).
   */
  void register_info_callback(InfoCallback&& callback);

  /**
   * @brief Registers a callback for raw message data relayed by the server.
   *
   * @details
   * No immediate invocation occurs at registration.  Data arrives when the
   * server is in a mode that forwards messages (kObserveOne, kObserveAll,
   * kRecord, kAuto, kAutoAndObserveAll).
   *
   * @param callback  Callable with signature @c void(const Data& data).
   */
  void register_data_callback(DataCallback&& callback);

  /**
   * @brief Sends a @c Control message to the @c ProxyServer.
   *
   * @details
   * Only valid when the role is @c kController; returns @c false immediately
   * for @c kListener.  The control is cached internally and automatically
   * re-sent if the server reconnects after a dropout.
   *
   * When @c async is @c true (default) the DDS publish is posted to the
   * MessageLoop thread; when @c false it is executed synchronously on the
   * calling thread.
   *
   * Every entry in @c control.url_meta_list must provide both @c ser and a
   * known @c schema value.  Proxy no longer back-fills missing routing
   * metadata from discovery caches because that would hide broken
   * schema-propagation paths.
   *
   * If @c direct mode is configured, corresponding SHM publishers are created
   * or destroyed to match publisher entries in @c url_meta_list.  Direct
   * subscribers are synchronised either from subscriber entries in
   * @c url_meta_list or, for @c kObserveAll / @c kAutoAndObserveAll, from the
   * latest @c Info list reported by the server.
   *
   * @param control  Control message to send.
   * @param async    @c true to post asynchronously (default); @c false to block.
   * @return         @c true on success; @c false if role is @c kListener or
   *                 the API is shutting down.
   */
  bool send_control(const Control& control, bool async = true);

  /**
   * @brief Sends raw message data to the @c ProxyServer for injection.
   *
   * @details
   * Only valid when the role is @c kController; returns @c false for @c kListener.
   * In direct mode the data is published directly on the SHM publisher
   * corresponding to @c data.url.  In non-direct mode it is wrapped in a
   * @c ProxyData envelope and forwarded over the DDS data channel.
   *
   * Returns @c false if no subscribers are listening on the target channel.
   *
   * The caller must provide both @c data.ser and a known @c data.schema.
   * Proxy no longer guesses the decode stack from cached discovery metadata.
   *
   * @param data  Data to inject, including URL, serialisation type, schema
   *              family, raw bytes, timestamp, and sequence number.
   * @return      @c true if data was published; @c false otherwise.
   */
  bool send_data(const Data& data);

  /**
   * @brief Returns the configuration passed at construction.
   *
   * @return Const reference to the internal @c Config.
   */
  [[nodiscard]] const Config& get_current_config() const;

  /**
   * @brief Returns the current proxy operation mode.
   *
   * @details
   * Reflects the mode most recently set by @c send_control().  Updated
   * immediately on the calling thread before any async DDS publish.
   *
   * @return Current @c Mode value.
   */
  [[nodiscard]] Mode get_current_mode() const;

  /**
   * @brief Returns the current error state.
   *
   * @return Current @c Error value; @c kNoError when no error is active.
   */
  [[nodiscard]] Error get_current_error() const;

  /**
   * @brief Returns the hostname of the connected @c ProxyServer.
   *
   * @details
   * Populated from the server's @c Time heartbeat.  Returns an empty string
   * before the first heartbeat is received or after disconnection.
   *
   * @return Hostname string, or empty if not yet connected.
   */
  [[nodiscard]] std::string get_current_hostname() const;

  /**
   * @brief Returns the machine ID of the connected @c ProxyServer.
   *
   * @details
   * Populated from the server's @c Time heartbeat.  Returns an empty string
   * before the first heartbeat is received.
   *
   * @return Machine ID string, or empty if not yet connected.
   */
  [[nodiscard]] std::string get_current_machine_id() const;

  /**
   * @brief Returns the best estimate of the server's current wall-clock time.
   *
   * @details
   * Computed as the last received @c sys_time plus the elapsed microseconds
   * since that heartbeat, extrapolated via an @c ElapsedTimer.  Returns the
   * raw @c sys_time field if the timer is not yet running.
   *
   * @return Estimated server system time in microseconds since the Unix epoch.
   */
  [[nodiscard]] uint64_t get_current_sys_time() const;

  /**
   * @brief Returns the best estimate of the server's current boot time.
   *
   * @details
   * Computed as the last received @c boot_time plus the elapsed microseconds
   * since that heartbeat, extrapolated via an @c ElapsedTimer.
   *
   * @return Estimated server uptime in microseconds since boot.
   */
  [[nodiscard]] uint64_t get_current_boot_time() const;

  /**
   * @brief Returns the server's most recently reported CPU utilisation.
   *
   * @return CPU usage as a percentage in the range [0, 100].  Returns 0 when
   *         disconnected.
   */
  [[nodiscard]] double get_current_cpu_usage() const;

  /**
   * @brief Returns the server's most recently reported memory utilisation.
   *
   * @return Memory usage as a percentage in the range [0, 100].  Returns 0 when
   *         disconnected.
   */
  [[nodiscard]] double get_current_memory_usage() const;

  /**
   * @brief Returns the measured round-trip latency on the data channel.
   *
   * @details
   * In direct (SHM) mode this always returns 0 because latency measurement is
   * not available on the SHM data channel.  In DDS mode it delegates to the
   * underlying data subscriber's latency tracker.
   *
   * @return Latency in microseconds, or 0 in direct mode.
   */
  [[nodiscard]] int64_t get_latency() const;

  /**
   * @brief Returns the sample loss statistics on the data channel.
   *
   * @details
   * In direct (SHM) mode returns a default-constructed (zero) @c SampleLostInfo.
   * In DDS mode it delegates to the underlying data subscriber.
   *
   * @return @c SampleLostInfo with total and lost sample counts.
   */
  [[nodiscard]] SampleLostInfo get_lost() const;

  /**
   * @brief Returns @c true when a valid connection to @c ProxyServer exists.
   *
   * @details
   * Set to @c true on receipt of a valid @c Time heartbeat; set to @c false
   * when 5 seconds elapse without a heartbeat or when the control channel
   * reports disconnection.
   *
   * @return @c true if connected, @c false otherwise.
   */
  [[nodiscard]] bool is_connected() const;

  /**
   * @brief Returns the @c VLINK_VERSION string reported by the server.
   *
   * @details
   * Populated from the first valid heartbeat.  Returns an empty string before
   * connection or after disconnection.
   *
   * @return Server VLink version string, e.g. @c "2.0.0".
   */
  [[nodiscard]] std::string get_proxy_version() const;

  /**
   * @brief Returns the set of all server hostnames seen during this session.
   *
   * @details
   * Hostnames are accumulated over the lifetime of the connection; they are
   * NOT cleared on disconnection, only when a new @c reset_handle() is triggered.
   *
   * @return Unordered set of hostname strings.
   */
  [[nodiscard]] std::unordered_set<std::string> get_proxy_hostnames() const;

  /**
   * @brief Returns the set of all server machine IDs seen during this session.
   *
   * @details
   * Analogous to @c get_proxy_hostnames(); accumulated over the lifetime of
   * the connection.
   *
   * @return Unordered set of machine ID strings.
   */
  [[nodiscard]] std::unordered_set<std::string> get_proxy_machine_ids() const;

  /**
   * @brief Returns @c true if the build includes SHM (Iceoryx) support.
   *
   * @details
   * Determined at compile time by the @c VLINK_SUPPORT_SHM macro.  Useful for
   * checking whether @c Config::direct can be used.
   *
   * @return @c true when SHM support is compiled in.
   */
  [[nodiscard]] static bool is_support_shm();

  /**
   * @brief Returns @c true if topic filtering support is compiled in.
   *
   * @details
   * Determined at compile time by @c VLINK_PROXY_ENABLE_FILTER.  When @c false
   * the @c filter_str and @c filter_by_process fields in @c Control are ignored
   * by the server.
   *
   * @return @c true when filtering is enabled.
   */
  [[nodiscard]] static bool is_enable_filter();

  /**
   * @brief Formats a microsecond wall-clock timestamp as a human-readable string.
   *
   * @details
   * Output format: @c "YYYY/MM/DD HH:MM:SS:mmm" where @c mmm is milliseconds.
   * Uses @c localtime_r by default; @c gmtime_r when @c enable_utc is @c true.
   *
   * @param time        Microseconds since the Unix epoch.
   * @param enable_utc  @c true for UTC; @c false for local time (default).
   * @return            Formatted timestamp string, or empty on conversion error.
   */
  [[nodiscard]] static std::string get_format_sys_time(uint64_t time, bool enable_utc = false);

  /**
   * @brief Formats a microsecond boot-time duration as a human-readable string.
   *
   * @details
   * Converts @p time (microseconds since boot) to a formatted elapsed-time
   * string, e.g. @c "0d 01:23:45.678".
   *
   * @param time  Microseconds since boot.
   * @return      Formatted elapsed-time string.
   */
  [[nodiscard]] static std::string get_format_boot_time(uint64_t time);

 protected:
  size_t get_max_task_count() const override;

  uint32_t get_max_elapsed_time() const override;

  void on_begin() override;

  void on_end() override;

 private:
  bool send_control_sync(const Control& control);

  void sync_direct_maps(const Control& control);

  void reset_handle();

  void process_connected(bool connected);

  void process_time(uint64_t sys_time, uint64_t boot_time);

  void process_error(Error error);

  struct Impl;
  std::unique_ptr<Impl> impl_;

  VLINK_DISALLOW_COPY_AND_ASSIGN(ProxyAPI)
};

}  // namespace vlink
