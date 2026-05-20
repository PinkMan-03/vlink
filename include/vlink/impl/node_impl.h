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
 * @file node_impl.h
 * @brief Abstract transport node base classes used by all VLink node implementations.
 *
 * @details
 * This header defines two base classes that form the backbone of the VLink
 * transport abstraction:
 *
 * - @c AbstractNode -- minimal interface for retrieving the underlying native
 *   transport handle (e.g. a DDS DataWriter pointer).
 * - @c NodeImpl -- the main base class for @c PublisherImpl, @c SubscriberImpl,
 *   @c ServerImpl, @c ClientImpl, @c SetterImpl, and @c GetterImpl.  It
 *   provides common infrastructure:
 *
 * | Feature                | API                                               |
 * | ---------------------- | ------------------------------------------------- |
 * | Transport initialise   | @c init() / @c deinit()                           |
 * | Zero-copy loan         | @c loan() / @c return_loan()                      |
 * | Suspend / resume       | @c suspend() / @c resume()                        |
 * | Interrupt              | @c interrupt() / @c reset_interrupted()           |
 * | Property store         | @c set_property() / @c get_property()             |
 * | Message loop attach    | @c attach() / @c detach() / @c get_message_loop() |
 * | Status callback        | @c register_status_handler() / @c call_status()   |
 * | Data recording         | @c set_record_path() / @c try_record()            |
 * | Discovery registration | @c init_ext() / @c deinit_ext()                   |
 * | Version check          | @c check_version()                                |
 * | Global init            | @c global_init() -- called once per process        |
 *
 * @par Callback Types
 * @c NodeImpl defines the standardised callback signatures used throughout all
 * transport backends:
 *
 * | Type              | Signature                               | Used by                    |
 * | ----------------- | --------------------------------------- | -------------------------- |
 * | ConnectCallback   | @c void(bool)                           | PublisherImpl, ClientImpl, ServerImpl |
 * | StatusCallback    | @c void(const Status::BasePtr&)         | DDS status events          |
 * | SyncCallback      | @c void()                               | SetterImpl sync            |
 * | ReqRespCallback   | @c void(uint64_t, const Bytes&, Bytes*) | ServerImpl listen          |
 * | MsgCallback       | @c void(const Bytes&)                   | SubscriberImpl, GetterImpl |
 * | IntraMsgCallback  | @c void(const IntraData&)               | intra:// subscriber        |
 */

#pragma once

#include <any>
#include <atomic>
#include <memory>
#include <string>

#include "../base/bytes.h"
#include "../base/cpu_profiler.h"
#include "../base/functional.h"
#include "../extension/security.h"
#include "../extension/status.h"
#include "../impl/conf.h"
#include "../impl/types.h"
#include "./intra_data.h"
#include "./ssl_options.h"

namespace vlink {

/**
 * @class AbstractNode
 * @brief Minimal interface for accessing the underlying native transport handle.
 *
 * @details
 * Concrete transport backends (e.g. @c DdsPublisherImpl) inherit from both
 * @c AbstractNode and their corresponding @c NodeImpl subclass.
 * @c get_native_handle() returns an @c std::any wrapping the backend-specific
 * handle, allowing advanced users to access transport internals without
 * breaking the VLink API boundary.
 *
 * @note The base implementation returns an empty @c std::any.
 */
class VLINK_EXPORT AbstractNode {
 public:
  /**
   * @brief Returns the underlying native transport handle wrapped in @c std::any.
   *
   * @details
   * Transport-specific subclasses override this to return, for example, a
   * DDS @c DataWriter or @c DataReader pointer.  Callers must @c std::any_cast
   * to the correct type.  The base implementation returns an empty @c std::any.
   *
   * @return Backend-specific handle, or empty @c std::any in the base class.
   */
  virtual std::any get_native_handle() const;

 protected:
  AbstractNode();
  virtual ~AbstractNode();

 private:
  VLINK_DISALLOW_COPY_AND_ASSIGN(AbstractNode)
};

/**
 * @class NodeImpl
 * @brief Abstract base for all VLink transport backend node implementations.
 *
 * @details
 * Every concrete transport backend (e.g. @c DdsPublisherImpl, @c ShmSubscriberImpl)
 * ultimately derives from @c NodeImpl.  The class provides:
 * - The pure-virtual @c init() / @c deinit() lifecycle interface.
 * - Common property storage shared between the @c Conf layer and the node.
 * - An optional @c MessageLoop attachment for callback dispatch.
 * - An @c interrupt() mechanism to unblock blocking operations.
 * - Hooks for data recording (@c try_record) and discovery reporting
 *   (@c init_ext / @c deinit_ext).
 * - A static @c global_init() for process-wide singletons, invoked automatically
 *   by the constructor and also safe to call explicitly.
 *
 * @note @c suspend() and @c resume() are optional -- the default implementations
 *       log a warning and return @c false if not overridden.
 *
 * @note @c register_status_handler() and @c call_status() are only supported on
 *       DDS-family transports (@c kDds, @c kDdsc, @c kDdsr, @c kDdst).  Calling
 *       them on other transport types logs a warning and is a no-op.
 */
class VLINK_EXPORT NodeImpl {
 public:
  /**
   * @brief Callback invoked when the peer connection state changes.
   *
   * @param bool  @c true when connected; @c false when disconnected.
   */
  using ConnectCallback = Function<void(bool)>;

  /**
   * @brief Callback invoked on DDS status events (e.g. deadline missed).
   *
   * @param ptr  Polymorphic status object; downcast to the concrete type.
   */
  using StatusCallback = Function<void(const Status::BasePtr& ptr)>;

  /**
   * @brief Callback invoked when a @c SetterImpl sync completes.
   */
  using SyncCallback = Function<void()>;

  /**
   * @brief Callback for @c ServerImpl request/response processing.
   *
   * @details
   * Parameters: @c (req_id, request_bytes, response_bytes_ptr).
   * The handler writes its response into @c *response_bytes_ptr; if
   * @c response_bytes_ptr is @c nullptr the server is in fire-and-forget mode.
   */
  using ReqRespCallback = Function<void(uint64_t, const Bytes&, Bytes*)>;

  /**
   * @brief Callback delivering a raw serialised message to a @c SubscriberImpl or @c GetterImpl.
   *
   * @param bytes  Received payload; lifetime is scoped to the callback.
   */
  using MsgCallback = Function<void(const Bytes&)>;

  /**
   * @brief Callback delivering an in-process @c IntraData message.
   *
   * @details
   * Only used on @c intra:// transport.  The @c IntraData is a @c shared_ptr
   * to the payload type, so no copy occurs.
   */
  using IntraMsgCallback = Function<void(const IntraData&)>;

  /**
   * @brief Initialises the underlying transport channel.
   *
   * @details
   * Called by the Node<> template after all properties have been set.
   * Concrete implementations create DDS entities, SHM channels, etc.
   */
  virtual void init() = 0;

  /**
   * @brief Tears down the underlying transport channel.
   *
   * @details
   * Called by the Node<> template destructor.  Releases all transport
   * resources (e.g. DDS entities, SHM segments).
   */
  virtual void deinit() = 0;

  /**
   * @brief Temporarily suspends message delivery without tearing down the channel.
   *
   * @details
   * The base implementation logs a warning and returns @c false.
   * Only a subset of transport backends support this operation.
   *
   * @return @c true if suspended successfully; @c false if unsupported.
   */
  virtual bool suspend();

  /**
   * @brief Resumes message delivery after a @c suspend() call.
   *
   * @details
   * The base implementation logs a warning and returns @c false.
   *
   * @return @c true if resumed successfully; @c false if unsupported.
   */
  virtual bool resume();

  /**
   * @brief Returns @c true when the node is currently suspended.
   *
   * @details
   * The base implementation logs a warning and returns @c false.
   *
   * @return @c true if the node is suspended; @c false otherwise or if unsupported.
   */
  [[nodiscard]] virtual bool is_suspend() const;

  /**
   * @brief Signals any blocking operations to unblock and return immediately.
   *
   * @details
   * Sets the internal interrupted flag and, in derived classes
   * (e.g. @c PublisherImpl, @c ClientImpl), also notifies waiting
   * condition variables to release threads blocked in @c wait_for_*.
   * Call @c reset_interrupted() before resuming normal operations.
   */
  virtual void interrupt();

  /**
   * @brief Returns @c true if the transport supports zero-copy loaning.
   *
   * @details
   * Zero-copy loan support is transport-specific and lets senders write
   * directly into transport-managed memory.  The base returns @c false.
   *
   * @return @c true if @c loan() / @c return_loan() are supported.
   */
  [[nodiscard]] virtual bool is_support_loan() const;

  /**
   * @brief Borrows a write buffer of @p size bytes from the transport.
   *
   * @details
   * Returns an empty @c Bytes in the base implementation.  Transports
   * that support zero-copy override this to return a @c Bytes view into
   * pre-allocated shared memory.
   *
   * @param size  Required buffer size in bytes.
   * @return      Borrowed @c Bytes, or empty on failure.
   */
  [[nodiscard]] virtual Bytes loan(int64_t size);

  /**
   * @brief Returns a previously loaned buffer to the transport.
   *
   * @details
   * Must be called if @c loan() returned a valid buffer and the caller
   * decided not to publish (e.g. serialisation failed).  The base returns
   * @c false.
   *
   * @param bytes  Buffer previously returned by @c loan().
   * @return       @c true on success; @c false if unsupported.
   */
  virtual bool return_loan(const Bytes& bytes);

  /**
   * @brief Configures manual unloan mode for zero-copy transports.
   *
   * @details
   * When @p manual_unloan is @c true the transport does not automatically
   * release received loaned buffers after callback dispatch; the caller must
   * call @c return_loan() explicitly.  The base is a no-op.
   *
   * @param manual_unloan  @c true to enable manual release; @c false for automatic.
   */
  virtual void set_manual_unloan(bool manual_unloan);

  /**
   * @brief Returns a pointer to the associated @c Conf configuration object.
   *
   * @details
   * Concrete backends override this to return their typed @c Conf subclass
   * (e.g. @c DdsConf).  The base returns @c nullptr.
   *
   * @return Pointer to the owning @c Conf, or @c nullptr in the base.
   */
  [[nodiscard]] virtual const struct Conf* get_conf() const;

  /**
   * @brief Returns a pointer to the @c AbstractNode peer (if any).
   *
   * @details
   * Used to access the native handle when the impl is split from the node.
   * The base returns @c nullptr.
   *
   * @return Pointer to the @c AbstractNode, or @c nullptr.
   */
  [[nodiscard]] virtual const AbstractNode* get_abstract_node() const;

  /**
   * @brief Retrieves a transport-specific status object.
   *
   * @details
   * Only supported on DDS-family transports.  The base logs a warning and
   * returns @c Status::Unknown.
   *
   * @param type  The type of status to retrieve (e.g. deadline missed).
   * @return      Polymorphic status object; never @c nullptr.
   */
  [[nodiscard]] virtual Status::BasePtr get_status(Status::Type type) const;

  /**
   * @brief Checks whether @p version matches the runtime VLink library version.
   *
   * @details
   * Compares @p version against @c VLINK_VERSION_MAJOR/MINOR/PATCH.  On the
   * first mismatch a warning is logged (once per process).  Version checks are
   * advisory; mismatches do not prevent node creation.
   *
   * @param version  Compile-time version embedded by the application.
   * @return         @c true if versions match; @c false otherwise.
   */
  virtual bool check_version(const Version& version);

  /**
   * @brief Attaches the node to a @c MessageLoop for callback dispatch.
   *
   * @details
   * Once attached, @c call_status() posts callbacks onto the loop thread.
   * Returns @c false if another loop is already attached.
   *
   * @param message_loop  Loop to attach to.
   * @return              @c true on success; @c false if another loop is already attached.
   */
  virtual bool attach(class MessageLoop* message_loop);

  /**
   * @brief Detaches the node from its @c MessageLoop.
   *
   * @details
   * Clears the attached-loop pointer and then waits for the previous loop to
   * become idle if the call is from a different thread, ensuring no already
   * posted callbacks are in-flight after this call returns.
   *
   * @return @c true on success; @c false if no loop was attached.
   */
  virtual bool detach();

  /**
   * @brief Returns the @c MessageLoop this node is attached to.
   *
   * @return Pointer to the loop, or @c nullptr if not attached.
   */
  [[nodiscard]] class MessageLoop* get_message_loop() const;

  /**
   * @brief Returns a typed pointer to the conf by downcasting to @c T.
   *
   * @details
   * Convenience wrapper around @c get_conf() for transport backends that
   * need to access their own @c Conf subclass.
   *
   * @tparam T  Concrete @c Conf subclass to cast to.
   * @return    @c const T* pointer, or @c nullptr if @c get_conf() returns null.
   */
  template <typename T>
  [[nodiscard]] const T* get_target_conf() const;

  /**
   * @brief Registers a callback for DDS status events.
   *
   * @details
   * Only effective on DDS-family transports (@c kDds, @c kDdsc, @c kDdsr,
   * @c kDdst).  A warning is logged if called on other transport types.
   * If a @c MessageLoop is attached, the callback is dispatched on that thread.
   *
   * @param callback  Handler invoked with each status change.
   */
  void register_status_handler(StatusCallback&& callback);

  /**
   * @brief Returns @c true if a status handler has been registered.
   *
   * @details
   * Only effective on DDS-family transports; logs a warning and returns
   * @c false for other transport types.
   *
   * @return @c true if a non-null status callback is registered.
   */
  [[nodiscard]] bool has_register_status() const;

  /**
   * @brief Dispatches a status event to the registered status handler.
   *
   * @details
   * If a @c MessageLoop is attached the callback is posted onto the loop thread;
   * otherwise it is called directly.  Only effective on DDS-family transports.
   *
   * @param ptr  Status object to deliver.
   */
  void call_status(Status::BasePtr ptr);

  /**
   * @brief Sets a named transport property on this node.
   *
   * @details
   * Properties are key/value strings (e.g. @c "dds.ip" = @c "192.168.1.1").
   * Must be called before @c init() to take effect.  The property map is
   * protected by a shared mutex for thread safety.
   *
   * @param prop   Property name.
   * @param value  Property value.
   */
  void set_property(const std::string& prop, const std::string& value);

  /**
   * @brief Retrieves a named transport property.
   *
   * @param prop  Property name.
   * @return      Property value, or empty string if not set.
   */
  [[nodiscard]] std::string get_property(const std::string& prop) const;

  /**
   * @brief Returns a snapshot of all properties set on this node.
   *
   * @return A copy of the internal @c PropertiesMap.
   */
  [[nodiscard]] Conf::PropertiesMap get_all_properties() const;

  /**
   * @brief Enables or disables discovery reporting for this node.
   *
   * @details
   * When @c false the node is hidden from @c DiscoveryReporter / @c DiscoveryViewer.
   * Proxy-internal channels use this to suppress themselves from topology views.
   * Must be called before @c init_ext().
   *
   * @param enable  @c true to report to discovery (default); @c false to suppress.
   */
  void set_discovery_enabled(bool enable);

  /**
   * @brief Returns @c true if discovery reporting is enabled for this node.
   *
   * @return Current discovery-enabled state.
   */
  [[nodiscard]] bool get_discovery_enabled() const;

  /**
   * @brief Sets the file path for per-node message recording.
   *
   * @details
   * When non-empty a @c BagWriter instance is obtained (or shared with other
   * nodes writing to the same path) and @c try_record() will capture messages.
   * Pass an empty string to disable per-node recording.
   *
   * @param path  File path for the bag; empty or unsupported suffix disables recording.
   */
  void set_record_path(const std::string& path);

  /**
   * @brief Installs application-layer security for this implementation node.
   *
   * @details
   * Called before @c init() by the protected @c Node::enable_security() helper.
   * Rejects unsupported transports, fills an empty AAD context from the node
   * wire metadata, lets @c Security handle the empty-config default, validates
   * the resulting instance against the current node role, then stores it in
   * @c security on success.
   *
   * @param cfg  Security configuration aggregate supplied by the public node wrapper.
   * @return @c true when a usable @c Security instance was installed; otherwise @c false.
   */
  bool enable_security(const Security::Config& cfg);

  /**
   * @brief Installs application-layer security by consuming @p cfg.
   *
   * @details
   * Same validation path as the const-reference overload, but avoids copying
   * callback targets and key material when the caller owns the config.  The
   * method may fill @c cfg.advanced.aad_context before moving it into
   * @c Security; an otherwise empty config is defaulted by @c Security.
   *
   * @param cfg  Security configuration aggregate to consume.
   * @return @c true when a usable @c Security instance was installed; otherwise @c false.
   */
  bool enable_security(Security::Config&& cfg);

  /**
   * @brief Merges SSL/TLS options into the node property map.
   *
   * @details
   * Acquires the helper mutex and calls @c SslOptions::parse_to() to
   * write the non-default fields of @p options as @c ssl.* entries in
   * @c helper_->property_map.  The transport factory reads these entries
   * during connection setup to configure TLS.
   *
   * @param options  The SSL/TLS configuration to merge.
   *
   * @see SslOptions::parse_to(), Node::set_ssl_options()
   */
  void set_ssl_options(const SslOptions& options);

  /**
   * @brief Records a message to the global and/or per-node bag writers.
   *
   * @details
   * Queries the global @c BagWriter (if one is active) and the per-node writer
   * set by @c set_record_path().  DDS CDR messages are skipped; intra messages
   * are skipped only when the internal ignore-intra filter is enabled.
   *
   * @param action_type  The role this message is being recorded for.
   * @param data         Raw serialised message bytes.
   */
  void try_record(ActionType action_type, const Bytes& data);

  /**
   * @brief Clears the interrupted flag set by @c interrupt().
   *
   * @details
   * Must be called before re-using a blocking operation after it has been
   * interrupted, e.g. before calling @c wait_for_subscribers() again.
   */
  void reset_interrupted();

  /**
   * @brief Returns @c true if @c interrupt() has been called and not yet reset.
   *
   * @return @c true when the interrupted flag is set.
   */
  [[nodiscard]] bool is_interrupted() const;

  /**
   * @brief Registers the node with the global @c DiscoveryReporter.
   *
   * @details
   * Called at the end of @c init() by all Node<> template specialisations.
   * Optionally starts the @c CpuProfiler if global profiling is enabled.
   * Skips registration for CDR, security, and (by default) intra nodes.
   */
  void init_ext();

  /**
   * @brief Deregisters the node from the global @c DiscoveryReporter.
   *
   * @details
   * Called after the transport-specific @c deinit() by all Node<> template
   * specialisations.
   * Restarts the @c CpuProfiler if global profiling was running.
   */
  void deinit_ext();

  /**
   * @brief Initialises process-wide VLink singletons.
   *
   * @details
   * Ensures the @c Logger, memory pool, global @c BagWriter, and global
   * @c DiscoveryReporter are initialised.  Safe to call multiple times; only
   * the first call has an effect.  The constructor calls this automatically.
   */
  static void global_init();

  std::string url;                               ///< Full URL string of this node (e.g. @c "dds://my/topic").
  std::string ser_type;                          ///< Serialisation type string (e.g. @c "demo.proto.PointCloud").
  ImplType impl_type{kUnknownImplType};          ///< Role of this implementation node.
  SchemaType schema_type{SchemaType::kUnknown};  ///< Coarse schema family reported to discovery and bag/proxy paths.
  TransportType transport_type{TransportType::kUnknown};  ///< Transport backend of this implementation node.
  bool is_cdr_type{false};                                ///< @c true when using DDS native CDR serialisation.
  bool is_security_type{false};                           ///< @c true when security-authenticated transport is enabled.
  bool is_discovery_enabled{true};                        ///< Whether this node is reported to the discovery layer.
  std::atomic_bool has_suspend{false};    ///< Atomic suspend state flag (currently unused by default impls).
  std::unique_ptr<CpuProfiler> profiler;  ///< Optional per-node CPU profiler (only when global profiling is on).
  std::unique_ptr<Security> security;     ///< Installed per-node message security context, or @c nullptr.

 protected:
  explicit NodeImpl(ImplType type);

  virtual ~NodeImpl();

 private:
  std::unique_ptr<struct NodeImplHelper> helper_;

  VLINK_DISALLOW_COPY_AND_ASSIGN(NodeImpl)
};

////////////////////////////////////////////////////////////////
/// Details
////////////////////////////////////////////////////////////////

template <typename T>
inline const T* NodeImpl::get_target_conf() const {
  return static_cast<const T*>(get_conf());
}

}  // namespace vlink
