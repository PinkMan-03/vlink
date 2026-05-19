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
 * @file url.h
 * @brief URL-based transport configuration dispatcher for VLink nodes.
 *
 * @details
 * This header provides two types that together implement the "one API, multiple
 * transports" core promise of VLink:
 *
 * @par Protocol
 * A plain-data struct that holds the parsed components of a VLink URL (transport,
 * host, path, query dictionary, fragment).  It is constructed from a URL string
 * and is used by @c Url::init_target_internal() to select the correct @c Conf
 * subclass.  @c Protocol is only constructible by @c Url (private constructor,
 * @c friend struct Url).
 *
 * @par Url
 * A concrete @c Conf subclass that wraps a @c Protocol and delegates all
 * @c Conf virtual methods to the underlying transport @c Conf (@c target_).
 * On construction it calls @c init_target_internal() which selects the correct
 * @c *Conf class based on the transport prefix in the URL and compile-time feature flags.
 *
 * @par Transport Selection at Construction
 * @code
 *   Url url("dds://vehicle/speed?domain_id=1");
 *   // Internally creates a DdsConf and calls:
 *   //   url.parse(kSubscriber)  -> target_->parse(kSubscriber)
 *   //   url.create_subscriber() -> target_->create_subscriber()
 * @endcode
 *
 * @par URL Remapping
 * When the @c VLINK_URL_USE_REMAP preprocessor flag is set, the @c Protocol
 * constructor checks the @c VLINK_URL_REMAP environment variable and rewrites
 * the URL before transport detection, enabling zero-code transport switching.
 *
 * @par Dynamic Plugin Transport Loading
 * When the @c VLINK_URL_USE_PLUGIN flag is set, @c Url::load_for_plugin()
 * searches dynamically loaded @c ConfPluginInterface plugins (from
 * @c VLINK_URL_PLUGINS env var) for a matching transport when built-in transports are
 * not found.
 *
 * @par Transport Enable Flags
 * The @c TransportEnableFlag bitmask controls which transports are initialised via
 * @c global_init() and @c init_plugins().  This allows embedding environments
 * (e.g. Android, QNX) to exclude unsupported transports at runtime.
 *
 * @note This file includes all @c *_conf.h module headers and all @c *_impl.h
 *       headers.  It is the single aggregation point for the VLink transport
 *       abstraction layer.
 */

#pragma once

#include <map>
#include <memory>
#include <string>
#include <utility>

#include "../base/logger.h"
#include "./conf.h"

// NOLINTBEGIN
#include "../modules/dds_conf.h"
#include "../modules/ddsc_conf.h"
#include "../modules/ddsr_conf.h"
#include "../modules/ddst_conf.h"
#include "../modules/fdbus_conf.h"
#include "../modules/intra_conf.h"
#include "../modules/mqtt_conf.h"
#include "../modules/qnx_conf.h"
#include "../modules/shm2_conf.h"
#include "../modules/shm_conf.h"
#include "../modules/someip_conf.h"
#include "../modules/zenoh_conf.h"
//
#include "./client_impl.h"
#include "./getter_impl.h"
#include "./publisher_impl.h"
#include "./server_impl.h"
#include "./setter_impl.h"
#include "./subscriber_impl.h"
// NOLINTEND

namespace vlink {

/**
 * @struct Protocol
 * @brief Parsed URL components used to select and configure a transport @c Conf.
 *
 * @details
 * Produced by @c Protocol(const std::string& address) which runs the URL through
 * @c UrlParser, applies any @c VLINK_URL_REMAP remapping, and resolves the
 * @c TransportType from the transport string.  Only @c Url may construct a @c Protocol
 * (private constructor, friend @c Url).
 *
 * @note The @c str field holds the input URL after any remap has been applied;
 *       it is not rebuilt from parsed components.
 */
struct VLINK_EXPORT Protocol final {
  std::string str;                                ///< URL string after remap, if any.
  TransportType transport;                        ///< Resolved transport backend.
  std::string host;                               ///< Hostname or IP address component.
  std::string path;                               ///< Topic path component.
  std::map<std::string, std::string> dictionary;  ///< Parsed query key-value pairs.
  std::string fragment;                           ///< Fragment identifier (after @c #).

 private:
  friend struct Url;
  explicit Protocol(const std::string& address);
};

/**
 * @struct Url
 * @brief URL-based @c Conf dispatcher that routes to the correct transport backend.
 *
 * @details
 * @c Url is the user-facing transport configuration type.  It is constructed from a
 * URL string, parses it into a @c Protocol, then creates the matching transport
 * @c Conf (@c target_) in @c init_target_internal().  All @c Conf virtual methods
 * are forwarded to @c target_.
 *
 * @par Full Lifecycle
 * @code
 *   // 1. Construct with URL string:
 *   Url url("dds://vehicle/speed");
 *   //    -> Protocol("dds://vehicle/speed") -> transport = kDds
 *   //    -> init_target_internal() -> target_ = make_unique<DdsConf>()
 *
 *   // 2. Parse for a specific node type:
 *   url.parse(kSubscriber);
 *   //    -> target_->parse(kSubscriber)
 *   //    -> target_->parse_protocol(&protocol_)
 *
 *   // 3. Create transport implementation:
 *   auto impl = url.create_subscriber();
 *   //    -> target_->create_subscriber()
 * @endcode
 */
struct Url final : public Conf {
  /**
   * @enum TransportEnableFlag
   * @brief Bitmask controlling which transport backends are active at runtime.
   *
   * @details
   * Passed to @c global_init() and @c init_plugins() to selectively initialise
   * only the transports that are compiled in and needed for the current process.
   * Each bit enables one transport family; the bit positions are listed below
   * and are independent of the numeric @c TransportType values.
   *
   * | Flag           | Bit Position | Transport      |
   * | -------------- | ------------ | -------------- |
   * | kEnableIntra   | 15           | intra://       |
   * | kEnableShm     | 14           | shm://         |
   * | kEnableShm2    | 13           | shm2://        |
   * | kEnableZenoh   | 12           | zenoh://       |
   * | kEnableDds     | 11           | dds://         |
   * | kEnableDdsc    | 10           | ddsc://        |
   * | kEnableDdsr    |  9           | ddsr://        |
   * | kEnableDdst    |  8           | ddst://        |
   * | kEnableSomeip  |  7           | someip://      |
   * | kEnableMqtt    |  6           | mqtt://        |
   * | kEnableFdbus   |  5           | fdbus://       |
   * | kEnableQnx     |  4           | qnx://         |
   * | kEnableAll     | all bits set | all transports |
   */
  enum TransportEnableFlag : uint16_t {
    kEnableEmpty = 0b0000'0000'0000'0000,   ///< No transport enabled.
    kEnableIntra = 0b1000'0000'0000'0000,   ///< Enable intra:// transport.
    kEnableShm = 0b0100'0000'0000'0000,     ///< Enable shm:// (Iceoryx) transport.
    kEnableShm2 = 0b0010'0000'0000'0000,    ///< Enable shm2:// (Iceoryx2) transport.
    kEnableZenoh = 0b0001'0000'0000'0000,   ///< Enable zenoh:// transport.
    kEnableDds = 0b0000'1000'0000'0000,     ///< Enable dds:// (Fast-DDS) transport.
    kEnableDdsc = 0b0000'0100'0000'0000,    ///< Enable ddsc:// (CycloneDDS) transport.
    kEnableDdsr = 0b0000'0010'0000'0000,    ///< Enable ddsr:// (RTI DDS) transport.
    kEnableDdst = 0b0000'0001'0000'0000,    ///< Enable ddst:// (TravoDDS) transport.
    kEnableSomeip = 0b0000'0000'1000'0000,  ///< Enable someip:// transport.
    kEnableMqtt = 0b0000'0000'0100'0000,    ///< Enable mqtt:// transport.
    kEnableFdbus = 0b0000'0000'0010'0000,   ///< Enable fdbus:// transport.
    kEnableQnx = 0b0000'0000'0001'0000,     ///< Enable qnx:// transport (QNX only).
    kEnableAll = 0b1111'1111'1111'1111,     ///< Enable all transports.
  };

  /**
   * @brief Constructs a @c Url from a transport address string.
   *
   * @details
   * Parses @p str into a @c Protocol, then calls @c init_target_internal() to
   * create the matching @c Conf subclass.  Logs a fatal error if no transport
   * backend matches the URL.
   *
   * @param str  VLink URL string, e.g. @c "dds://vehicle/speed".
   */
  explicit Url(const std::string& str);

  /**
   * @brief Copy constructor.
   *
   * @details
   * Copies the @c Protocol from @p url and rebuilds @c target_ via
   * @c init_target_internal().  The new object gets a fresh transport @c Conf
   * rather than sharing the same instance.
   *
   * @param url  Source @c Url to copy.
   */
  Url(const Url& url);

  /**
   * @brief Move constructor.
   *
   * @details
   * Transfers @c protocol_ and @c target_ from @p url without rebuilding.
   *
   * @param url  Source @c Url to move from.
   */
  Url(Url&& url) noexcept;

  /**
   * @brief Destructor.
   */
  ~Url() override;

  /**
   * @brief Copy-assignment operator.
   *
   * @details
   * Copies @c protocol_ and reinitialises @c target_ via @c init_target_internal().
   *
   * @param url  Source @c Url.
   * @return     Reference to @c *this.
   */
  Url& operator=(const Url& url);

  /**
   * @brief Move-assignment operator.
   *
   * @param url  Source @c Url.
   * @return     Reference to @c *this.
   */
  Url& operator=(Url&& url) noexcept;

  /**
   * @brief Returns the stored URL string after any remap.
   *
   * @return Const reference to the URL string stored in @c Protocol::str.
   */
  [[nodiscard]] const std::string& get_str() const;

  /**
   * @brief Returns a pointer to the underlying transport @c Conf, or @c nullptr.
   *
   * @details
   * Allows callers to inspect or downcast the concrete transport configuration
   * (e.g. to a @c DdsConf to set DDS-specific QoS).
   *
   * @return Pointer to the active transport @c Conf; @c nullptr if the URL was invalid.
   */
  [[nodiscard]] const Conf* get_target() const;

  /**
   * @brief Parses the URL for the given node type, delegating to @c target_.
   *
   * @details
   * Calls @c Conf::parse(impl_type), @c target_->parse(impl_type), and
   * @c target_->parse_protocol() in sequence.  Returns @c false if @c target_
   * is null or any step fails.
   *
   * @param impl_type  Bitmask of @c ImplType roles (e.g. @c kSubscriber).
   * @return           @c true if all parse steps succeed; @c false otherwise.
   */
  bool parse(ImplType impl_type) const override;

  /**
   * @brief Returns @c true if the underlying @c target_ @c Conf is valid.
   *
   * @details
   * Delegates to @c target_->is_valid().  Returns @c false if @c target_ is null.
   *
   * @return @c true if the transport conf is valid and ready for use.
   */
  [[nodiscard]] bool is_valid() const override;

  /**
   * @brief Returns the @c ImplType resolved by the underlying @c target_ @c Conf.
   *
   * @details
   * Delegates to @c target_->get_impl_type().  Returns @c kUnknownImplType if
   * @c target_ is null or @c parse() has not been called yet.
   *
   * @return The @c ImplType for this URL, or @c kUnknownImplType.
   */
  [[nodiscard]] ImplType get_impl_type() const override;

  /**
   * @brief Returns the transport backend resolved from the URL.
   *
   * @details
   * Delegates to @c target_->get_transport_type().  Returns @c TransportType::kUnknown
   * if @c target_ is null.
   *
   * @return The @c TransportType for this URL.
   */
  [[nodiscard]] TransportType get_transport_type() const override;

  /**
   * @brief Loads and registers external @c ConfPluginInterface transport plugins.
   *
   * @details
   * Searches for plugin shared libraries listed in the @c VLINK_URL_PLUGINS
   * environment variable.  The @p transport_enable_flags argument is the set of
   * built-in transports already available in this process; matching plugin names
   * are skipped so linked modules take precedence over external plugins.  This
   * is called automatically when a @c Url is first constructed; explicit calls
   * are only needed in unusual initialisation order.
   *
   * @param transport_enable_flags  Bitmask of built-in @c TransportEnableFlag
   *                                values to skip; default @c 0 skips none.
   */
  VLINK_EXPORT static void init_plugins(uint16_t transport_enable_flags = 0);

  /**
   * @brief Searches loaded plugins for a @c Conf factory matching @p type.
   *
   * @details
   * Looks up a previously loaded @c ConfPluginInterface for @p type and calls
   * @c create() on it.  Returns @c nullptr if no matching plugin is registered.
   *
   * @param type  Transport backend to search for.
   * @return      A new @c Conf instance from the plugin, or @c nullptr.
   */
  [[nodiscard]] VLINK_EXPORT static std::unique_ptr<Conf> load_for_plugin(TransportType type);

  /**
   * @brief Returns a numeric sort index for the transport backend used by a URL.
   *
   * @details
   * Used to order multiple URLs by transport priority.  Local transports
   * (@c intra://, @c shm://) return lower indices than network transports.
   *
   * @param url  URL string to classify.
   * @return     Sort index; lower values = higher priority, @c -1 for an empty URL.
   */
  [[nodiscard]] VLINK_EXPORT static int get_sort_index(std::string_view url);

  /**
   * @brief Returns @c true if the URL refers to a local (same-machine) transport.
   *
   * @details
   * A URL is considered local if its transport is @c intra://, @c shm://, or
   * @c shm2://.
   *
   * @param url  URL string to classify.
   * @return     @c true for local transports; @c false for network transports.
   */
  [[nodiscard]] VLINK_EXPORT static bool is_local_type(std::string_view url);

  /**
   * @brief Returns @c true if the URL uses the @c intra:// in-process transport.
   *
   * @param url  URL string to classify.
   * @return     @c true only for @c intra:// URLs.
   */
  [[nodiscard]] VLINK_EXPORT static bool is_intra_type(std::string_view url);

  /**
   * @brief Returns @c true if the URL uses a shared-memory transport (@c shm:// or @c shm2://).
   *
   * @param url  URL string to classify.
   * @return     @c true for @c shm:// and @c shm2:// URLs.
   */
  [[nodiscard]] VLINK_EXPORT static bool is_shm_type(std::string_view url);

  /**
   * @brief Initialises the global state for all enabled transport backends.
   *
   * @details
   * Calls @c NodeImpl::global_init() and then @c *Conf::global_init() for each
   * transport whose bit is set in @p transport_enable_flags.  Must be called once
   * before creating any @c Url objects if fine-grained transport control is needed.
   * If not called explicitly, transports are lazily initialised on first use.
   *
   * @param transport_enable_flags  Bitmask of @c TransportEnableFlag values.  Passing
   *                                @c 0 (the default) expands to all compiled-in transports.
   */
  static void global_init(uint16_t transport_enable_flags = 0);

  /**
   * @brief Returns a bitmask of all compile-time-enabled transport backends.
   *
   * @details
   * Built at compile time from the @c VLINK_SUPPORT_* preprocessor flags.  The
   * result can be passed to @c global_init() to initialise exactly the available
   * transports.
   *
   * @return @c TransportEnableFlag bitmask of supported transports.
   */
  [[nodiscard]] static uint16_t get_transport_enable_flags();

  /**
   * @brief Creates the @c target_ @c Conf for a given @c Protocol.
   *
   * @details
   * Called by constructors and assignment operators.  Switches on
   * @c Protocol::transport and instantiates the corresponding @c *Conf class.
   * Falls back to @c load_for_plugin() if no built-in transport matches.  Logs a
   * fatal error if neither path succeeds.
   *
   * @param protocol  Parsed URL information used to select the transport.
   * @param target    Output: receives the newly created @c Conf instance.
   */
  static void init_target_internal(const Protocol& protocol, std::unique_ptr<Conf>& target);

 private:
  std::unique_ptr<ServerImpl> create_server() const override;

  std::unique_ptr<ClientImpl> create_client() const override;

  std::unique_ptr<PublisherImpl> create_publisher() const override;

  std::unique_ptr<SubscriberImpl> create_subscriber() const override;

  std::unique_ptr<SetterImpl> create_setter() const override;

  std::unique_ptr<GetterImpl> create_getter() const override;

  VLINK_EXPORT friend std::ostream& operator<<(std::ostream& ostream, const Url& conf) noexcept;

  mutable Protocol protocol_;
  std::unique_ptr<Conf> target_;
  VLINK_DECLARE_CONF_FRIEND()
  VLINK_ALLOW_IMPL_TYPE(kServer | kClient | kPublisher | kSubscriber | kSetter | kGetter);
};

////////////////////////////////////////////////////////////////
/// Details
////////////////////////////////////////////////////////////////

inline Url::Url(const std::string& str) : protocol_(str) { init_target_internal(protocol_, target_); }

// NOLINTNEXTLINE(bugprone-copy-constructor-init)
inline Url::Url(const Url& url) : protocol_(url.protocol_) { init_target_internal(protocol_, target_); }

inline Url::Url(Url&& url) noexcept : protocol_(std::move(url.protocol_)), target_(std::move(url.target_)) {}

inline Url::~Url() = default;

inline Url& Url::operator=(const Url& url) {
  if VUNLIKELY (this == &url) {
    return *this;
  }

  protocol_ = url.protocol_;

  init_target_internal(protocol_, target_);

  return *this;
}

inline Url& Url::operator=(Url&& url) noexcept {
  if VUNLIKELY (this == &url) {
    return *this;
  }

  protocol_ = std::move(url.protocol_);
  target_ = std::move(url.target_);

  return *this;
}

inline const std::string& Url::get_str() const { return protocol_.str; }

inline const Conf* Url::get_target() const { return target_.get(); }

inline bool Url::parse(ImplType impl_type) const {
  if VUNLIKELY (!target_) {
    return false;
  }

  if VUNLIKELY (!Conf::parse(impl_type) || !target_->parse(impl_type)) {
    return false;
  }

  return target_->parse_protocol(&protocol_);
}

inline bool Url::is_valid() const {
  if VUNLIKELY (!target_) {
    return false;
  }

  return target_->is_valid();
}

inline ImplType Url::get_impl_type() const {
  if VUNLIKELY (!target_) {
    return kUnknownImplType;
  }

  return target_->get_impl_type();
}

inline TransportType Url::get_transport_type() const {
  if VUNLIKELY (!target_) {
    return TransportType::kUnknown;
  }

  return target_->get_transport_type();
}

inline void Url::global_init(uint16_t transport_enable_flags) {
  if (transport_enable_flags == 0) {
    transport_enable_flags = get_transport_enable_flags();
  }

  (void)transport_enable_flags;

  NodeImpl::global_init();

#ifndef VLINK_ENABLE_C_INTERFACE

#ifdef VLINK_SUPPORT_INTRA
  if (transport_enable_flags & kEnableIntra) {
    IntraConf::global_init();
  }
#endif

#ifdef VLINK_SUPPORT_SHM
  if (transport_enable_flags & kEnableShm) {
    ShmConf::global_init();
  }
#endif

#ifdef VLINK_SUPPORT_SHM2
  if (transport_enable_flags & kEnableShm2) {
    Shm2Conf::global_init();
  }
#endif

#ifdef VLINK_SUPPORT_ZENOH
  if (transport_enable_flags & kEnableZenoh) {
    ZenohConf::global_init();
  }
#endif

#ifdef VLINK_SUPPORT_DDS
  if (transport_enable_flags & kEnableDds) {
    DdsConf::global_init();
  }
#endif

#ifdef VLINK_SUPPORT_DDSC
  if (transport_enable_flags & kEnableDdsc) {
    DdscConf::global_init();
  }
#endif

#ifdef VLINK_SUPPORT_DDSR
  if (transport_enable_flags & kEnableDdsr) {
    DdsrConf::global_init();
  }
#endif

#ifdef VLINK_SUPPORT_DDST
  if (transport_enable_flags & kEnableDdst) {
    DdstConf::global_init();
  }
#endif

#ifdef VLINK_SUPPORT_SOMEIP
  if (transport_enable_flags & kEnableSomeip) {
    SomeipConf::global_init();
  }
#endif

#ifdef VLINK_SUPPORT_MQTT
  if (transport_enable_flags & kEnableMqtt) {
    MqttConf::global_init();
  }
#endif

#ifdef VLINK_SUPPORT_FDBUS
  if (transport_enable_flags & kEnableFdbus) {
    FdbusConf::global_init();
  }
#endif

#ifdef VLINK_SUPPORT_QNX
  if (transport_enable_flags & kEnableQnx) {
    QnxConf::global_init();
  }
#endif

#endif
}

inline uint16_t Url::get_transport_enable_flags() {
  uint16_t flags = 0;

#ifdef VLINK_SUPPORT_INTRA
  flags |= kEnableIntra;
#endif

#ifdef VLINK_SUPPORT_SHM
  flags |= kEnableShm;
#endif

#ifdef VLINK_SUPPORT_SHM2
  flags |= kEnableShm2;
#endif

#ifdef VLINK_SUPPORT_ZENOH
  flags |= kEnableZenoh;
#endif

#ifdef VLINK_SUPPORT_DDS
  flags |= kEnableDds;
#endif

#ifdef VLINK_SUPPORT_DDSC
  flags |= kEnableDdsc;
#endif

#ifdef VLINK_SUPPORT_DDSR
  flags |= kEnableDdsr;
#endif

#ifdef VLINK_SUPPORT_DDST
  flags |= kEnableDdst;
#endif

#ifdef VLINK_SUPPORT_SOMEIP
  flags |= kEnableSomeip;
#endif

#ifdef VLINK_SUPPORT_MQTT
  flags |= kEnableMqtt;
#endif

#ifdef VLINK_SUPPORT_FDBUS
  flags |= kEnableFdbus;
#endif

#ifdef VLINK_SUPPORT_QNX
  flags |= kEnableQnx;
#endif

  return flags;
}

inline void Url::init_target_internal(const Protocol& protocol, std::unique_ptr<Conf>& target) {
  static auto transport_enable_flags = get_transport_enable_flags();

  Url::init_plugins(transport_enable_flags);

  // NOLINTBEGIN
  switch (protocol.transport) {
#ifdef VLINK_SUPPORT_INTRA
    case TransportType::kIntra:
      target = std::make_unique<IntraConf>();
      break;
#endif

#ifdef VLINK_SUPPORT_SHM
    case TransportType::kShm:
      target = std::make_unique<ShmConf>();
      break;
#endif

#ifdef VLINK_SUPPORT_SHM2
    case TransportType::kShm2:
      target = std::make_unique<Shm2Conf>();
      break;
#endif

#ifdef VLINK_SUPPORT_ZENOH
    case TransportType::kZenoh:
      target = std::make_unique<ZenohConf>();
      break;
#endif

#ifdef VLINK_SUPPORT_DDS
    case TransportType::kDds:
      target = std::make_unique<DdsConf>();
      break;
#endif

#ifdef VLINK_SUPPORT_DDSC
    case TransportType::kDdsc:
      target = std::make_unique<DdscConf>();
      break;
#endif

#ifdef VLINK_SUPPORT_DDSR
    case TransportType::kDdsr:
      target = std::make_unique<DdsrConf>();
      break;
#endif

#ifdef VLINK_SUPPORT_DDST
    case TransportType::kDdst:
      target = std::make_unique<DdstConf>();
      break;
#endif

#ifdef VLINK_SUPPORT_SOMEIP
    case TransportType::kSomeip:
      target = std::make_unique<SomeipConf>();
      break;
#endif

#ifdef VLINK_SUPPORT_MQTT
    case TransportType::kMqtt:
      target = std::make_unique<MqttConf>();
      break;
#endif

#ifdef VLINK_SUPPORT_FDBUS
    case TransportType::kFdbus:
      target = std::make_unique<FdbusConf>();
      break;
#endif

#ifdef VLINK_SUPPORT_QNX
    case TransportType::kQnx:
      target = std::make_unique<QnxConf>();
      break;
#endif

    default:
      break;
  }
  // NOLINTEND

  if (!target) {
    target = Url::load_for_plugin(protocol.transport);
  }

  if VUNLIKELY (!target) {
    CLOG_F("Unsupported url[%s].", protocol.str.c_str());
  }
}

inline std::unique_ptr<ServerImpl> Url::create_server() const {
  if VUNLIKELY (!target_) {
    return nullptr;
  }

  return target_->create_server();
}

inline std::unique_ptr<ClientImpl> Url::create_client() const {
  if VUNLIKELY (!target_) {
    return nullptr;
  }

  return target_->create_client();
}

inline std::unique_ptr<PublisherImpl> Url::create_publisher() const {
  if VUNLIKELY (!target_) {
    return nullptr;
  }

  return target_->create_publisher();
}

inline std::unique_ptr<SubscriberImpl> Url::create_subscriber() const {
  if VUNLIKELY (!target_) {
    return nullptr;
  }

  return target_->create_subscriber();
}

inline std::unique_ptr<SetterImpl> Url::create_setter() const {
  if VUNLIKELY (!target_) {
    return nullptr;
  }

  return target_->create_setter();
}

inline std::unique_ptr<GetterImpl> Url::create_getter() const {
  if VUNLIKELY (!target_) {
    return nullptr;
  }

  return target_->create_getter();
}

}  // namespace vlink
