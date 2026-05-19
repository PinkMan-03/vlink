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
 * @file conf.h
 * @brief Abstract transport configuration base class and associated helper macros.
 *
 * @details
 * @c Conf is the pure-virtual base for every transport backend configuration
 * object (e.g. @c DdsConf, @c ShmConf, @c IntraConf).  It acts as a bridge
 * between the URL parsing layer and the concrete @c NodeImpl factories:
 *
 * -# A @c Url object parses the transport from the URL string and constructs
 *    the corresponding @c Conf subclass.
 * -# During @c Publisher<T> / @c Subscriber<T> / etc. construction, the
 *    node template calls @c Conf::parse(impl_type) followed by
 *    @c Conf::create_publisher() / @c create_subscriber() / etc. to obtain a
 *    transport-specific @c NodeImpl instance.
 * -# The @c NodeImpl carries out the actual IPC/DDS/SHM operations.
 *
 * @par Macro Overview
 * Several helper macros reduce boilerplate in concrete @c Conf subclasses:
 *
 * | Macro                          | Purpose                                                        |
 * | ------------------------------ | -------------------------------------------------------------- |
 * | VLINK_DECLARE_CONF_FRIEND()    | Grants friendship to all six Node<> template specialisations.  |
 * | VLINK_CONF_IMPL(classname)     | Declares the private Conf interface inside a concrete class.   |
 * | VLINK_ALLOW_IMPL_TYPE(type)    | Declares which ImplType bitmask the Conf supports.             |
 * | VLINK_DECLARE_GLOBAL_PROPERTY()| Declares per-class thread count and global property storage.   |
 * | VLINK_DEFINE_GLOBAL_PROPERTY() | Defines the static members declared by the above macro.        |
 */

#pragma once

#include <map>
#include <memory>
#include <shared_mutex>
#include <string>
#include <utility>

#include "../base/macros.h"
#include "./types.h"

namespace vlink {

/**
 * @struct Conf
 * @brief Abstract base class for VLink transport configuration objects.
 *
 * @details
 * Each supported transport backend has a corresponding @c Conf subclass that
 * implements the virtual factory methods (@c create_publisher, @c create_server,
 * etc.) to produce transport-specific @c NodeImpl instances.  The base class
 * provides default implementations that return @c nullptr / @c false, so
 * subclasses only need to override the methods they support.
 *
 * @c Conf objects are held exclusively by @c Url and the six Node<> template
 * classes; they are not intended for direct use by application code.
 *
 * @note The @c parse() method caches the @c ImplType so that subsequent
 *       factory calls know which node role is being requested.
 */
struct VLINK_EXPORT Conf {
  /**
   * @brief Key/value property map type.
   *
   * @details
   * Used by transport conf subclasses and @c NodeImpl to store DDS QoS
   * strings, IP addresses, buffer sizes, and other per-channel tuning
   * parameters (e.g. @c "dds.ip" = @c "127.0.0.1").
   */
  using PropertiesMap = std::map<std::string, std::string>;

  /**
   * @brief Virtual destructor.
   */
  virtual ~Conf();

  /**
   * @brief Validates the conf for the given node role and caches the type.
   *
   * @details
   * Called by the Node<> template before invoking any @c create_*() factory.
   * The base implementation logs a fatal message and returns @c false for
   * @c kUnknownImplType; otherwise it caches @p impl_type and returns @c true.
   * Concrete subclasses typically call this base and then validate their own
   * fields.
   *
   * @param impl_type  The role being requested (e.g. @c kPublisher).
   * @return           @c true on success; @c false if the type is unknown.
   */
  [[nodiscard]] virtual bool parse(ImplType impl_type) const;

  /**
   * @brief Returns @c true when the configuration holds valid, usable data.
   *
   * @details
   * The base implementation always returns @c false.  Subclasses override
   * this to perform transport-specific validation.
   *
   * @return @c true if the configuration is ready to create @c NodeImpl objects.
   */
  [[nodiscard]] virtual bool is_valid() const;

  /**
   * @brief Returns the most recently parsed @c ImplType.
   *
   * @return @c ImplType cached by the last call to @c parse(); @c kUnknownImplType
   *         before @c parse() is called.
   */
  [[nodiscard]] virtual ImplType get_impl_type() const;

  /**
   * @brief Returns the transport backend this configuration represents.
   *
   * @details
   * The base implementation returns @c TransportType::kUnknown.
   * Each subclass overrides this to return its own @c TransportType value.
   *
   * @return @c TransportType identifier for this conf.
   */
  [[nodiscard]] virtual TransportType get_transport_type() const;

  uint32_t hash_code{0};  ///< Transport-specific channel/topic hash set by concrete implementations.

 protected:
  Conf();

  [[nodiscard]] virtual bool parse_protocol(struct Protocol* protocol);

  [[nodiscard]] virtual std::unique_ptr<class ServerImpl> create_server() const;

  [[nodiscard]] virtual std::unique_ptr<class ClientImpl> create_client() const;

  [[nodiscard]] virtual std::unique_ptr<class PublisherImpl> create_publisher() const;

  [[nodiscard]] virtual std::unique_ptr<class SubscriberImpl> create_subscriber() const;

  [[nodiscard]] virtual std::unique_ptr<class SetterImpl> create_setter() const;

  [[nodiscard]] virtual std::unique_ptr<class GetterImpl> create_getter() const;

 private:
  friend struct Url;
  template <typename, typename, SecurityType>
  friend class Server;
  template <typename, typename, SecurityType>
  friend class Client;
  template <typename, SecurityType>
  friend class Publisher;
  template <typename, SecurityType>
  friend class Subscriber;
  template <typename, SecurityType>
  friend class Setter;
  template <typename, SecurityType>
  friend class Getter;

  mutable ImplType impl_type_{kUnknownImplType};
};

}  // namespace vlink

////////////////////////////////////////////////////////////////
/// Macro Definitions
////////////////////////////////////////////////////////////////

/**
 * @def VLINK_DECLARE_CONF_FRIEND
 * @brief Declares all six VLink Node template specialisations as friends.
 *
 * @details
 * Placed inside a concrete @c Conf subclass declaration to allow
 * @c Server<>, @c Client<>, @c Publisher<>, @c Subscriber<>, @c Setter<>,
 * and @c Getter<> to access the protected @c create_*() factory methods and
 * @c parse_protocol().  Use @c VLINK_CONF_IMPL(classname) which already
 * expands this macro; only include this macro directly when you need friend
 * access without the full @c VLINK_CONF_IMPL boilerplate.
 */
#define VLINK_DECLARE_CONF_FRIEND()           \
  template <typename, typename, SecurityType> \
  friend class Server;                        \
  template <typename, typename, SecurityType> \
  friend class Client;                        \
  template <typename, SecurityType>           \
  friend class Publisher;                     \
  template <typename, SecurityType>           \
  friend class Subscriber;                    \
  template <typename, SecurityType>           \
  friend class Setter;                        \
  template <typename, SecurityType>           \
  friend class Getter;

/**
 * @def VLINK_CONF_IMPL
 * @brief Standard boilerplate for concrete @c Conf subclass declarations.
 *
 * @details
 * Adds to the class:
 * - @c VLINK_DECLARE_CONF_FRIEND() -- friend access for Node<> templates.
 * - Overrides for all six protected @c Conf factory methods.
 * - @c operator<<(ostream, classname) for debug printing.
 * - A public default constructor.
 * - @c bool is_valid() const override -- subclass must define the body.
 *
 * @param classname  The name of the concrete @c Conf subclass.
 */
#define VLINK_CONF_IMPL(classname)                                                                     \
 private:                                                                                              \
  VLINK_DECLARE_CONF_FRIEND()                                                                          \
                                                                                                       \
  [[nodiscard]] bool parse_protocol(struct Protocol* protocol) override;                               \
                                                                                                       \
  [[nodiscard]] std::unique_ptr<class ServerImpl> create_server() const override;                      \
                                                                                                       \
  [[nodiscard]] std::unique_ptr<class ClientImpl> create_client() const override;                      \
                                                                                                       \
  [[nodiscard]] std::unique_ptr<class PublisherImpl> create_publisher() const override;                \
                                                                                                       \
  [[nodiscard]] std::unique_ptr<class SubscriberImpl> create_subscriber() const override;              \
                                                                                                       \
  [[nodiscard]] std::unique_ptr<class SetterImpl> create_setter() const override;                      \
                                                                                                       \
  [[nodiscard]] std::unique_ptr<class GetterImpl> create_getter() const override;                      \
                                                                                                       \
  VLINK_EXPORT friend std::ostream& operator<<(std::ostream& ostream, const classname& conf) noexcept; \
                                                                                                       \
 public:                                                                                               \
  classname() = default;                                                                               \
                                                                                                       \
  ~classname() = default;                                                                              \
                                                                                                       \
  [[nodiscard]] bool is_valid() const override;

/**
 * @def VLINK_ALLOW_IMPL_TYPE
 * @brief Declares a static constexpr bitmask of supported @c ImplType values.
 *
 * @details
 * Expands to a public @c get_allow_impl_type() that returns @p type, allowing
 * the Node<> template to assert at compile time that the conf supports the
 * requested node role.  Use bitwise-OR to combine multiple roles, e.g.:
 * @code
 * VLINK_ALLOW_IMPL_TYPE(kServer | kClient | kPublisher | kSubscriber | kSetter | kGetter)
 * @endcode
 *
 * @param type  Bitmask of @c ImplType values this conf supports.
 */
#define VLINK_ALLOW_IMPL_TYPE(type) \
 public:                            \
  [[nodiscard]] static constexpr int get_allow_impl_type() { return type; }

/**
 * @def VLINK_DECLARE_GLOBAL_PROPERTY
 * @brief Declares per-transport global state: thread count and property storage.
 *
 * @details
 * Intended for use inside a concrete @c Conf subclass body.  Declares:
 * - @c thread_count_  -- number of I/O threads for this transport.
 * - @c global_properties_ -- default properties applied to every node of this transport.
 * - @c global_mtx_ -- shared mutex protecting @c global_properties_.
 *
 * Also injects:
 * - @c get_thread_count() -- retrieve current thread count.
 * - @c set_thread_count() -- set thread count (must be called before @c global_init()).
 * - @c set_global_property(prop, value) -- set a default property for all nodes.
 * - @c get_global_property(prop) -- retrieve a default property.
 * - @c get_global_all_properties() -- retrieve a snapshot of all properties.
 * - @c global_init() declaration -- must be defined by the subclass.
 *
 * Pair with @c VLINK_DEFINE_GLOBAL_PROPERTY(classname) in the @c .cc file.
 */
#define VLINK_DECLARE_GLOBAL_PROPERTY()                                                \
 private:                                                                              \
  static size_t thread_count_;                                                         \
  static PropertiesMap global_properties_;                                             \
  static std::shared_mutex global_mtx_;                                                \
                                                                                       \
 public:                                                                               \
  [[nodiscard]] static size_t get_thread_count() { return thread_count_; }             \
                                                                                       \
  static void set_thread_count(size_t thread_count) { thread_count_ = thread_count; }  \
                                                                                       \
  static void set_global_property(const std::string& prop, const std::string& value) { \
    std::lock_guard lock(global_mtx_);                                                 \
    global_properties_[prop] = value;                                                  \
  }                                                                                    \
                                                                                       \
  [[nodiscard]] static std::string get_global_property(const std::string& prop) {      \
    std::shared_lock lock(global_mtx_);                                                \
    auto iter = global_properties_.find(prop);                                         \
    return iter != global_properties_.end() ? iter->second : std::string();            \
  }                                                                                    \
                                                                                       \
  [[nodiscard]] static PropertiesMap get_global_all_properties() {                     \
    std::shared_lock lock(global_mtx_);                                                \
    return global_properties_;                                                         \
  }                                                                                    \
                                                                                       \
  static void global_init();

/**
 * @def VLINK_DEFINE_GLOBAL_PROPERTY
 * @brief Provides definitions for the static members declared by @c VLINK_DECLARE_GLOBAL_PROPERTY.
 *
 * @details
 * Place once in the @c .cc file of the corresponding @c Conf subclass.
 * Initialises @c thread_count_ to @c 1, @c global_properties_ to an empty map,
 * and default-constructs @c global_mtx_.
 *
 * @param classname  The name of the concrete @c Conf subclass.
 */
#define VLINK_DEFINE_GLOBAL_PROPERTY(classname)      \
  size_t classname::thread_count_{1};                \
  Conf::PropertiesMap classname::global_properties_; \
  std::shared_mutex classname::global_mtx_;
