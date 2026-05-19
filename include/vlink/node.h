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
 * @file node.h
 * @brief Base CRTP template for all VLink communication nodes.
 *
 * @details
 * @c Node<ImplT, SecT> is the common base class inherited by @c Publisher,
 * @c Subscriber, @c Server, @c Client, @c Setter, and @c Getter.  It owns
 * the transport-specific implementation pointer (@c impl_), drives the node
 * lifecycle, and provides the shared services described below.
 *
 * @par Architecture Overview
 * @code
 * +---------------------------------------------------+
 * |               User Application                    |
 * |  Publisher<T>  Subscriber<T>  Client<Req,Resp>    |
 * |  Server<Req,Resp>  Getter<T>  Setter<T>           |
 * +-------------------+-------------------------------+
 *                     |  inherits
 * +-------------------v-------------------------------+
 * |         Node<ImplT, SecT>                         |
 * |  lifecycle  loans  security  properties  profiler |
 * +-------------------+-------------------------------+
 *                     |  owns (unique_ptr)
 * +-------------------v-------------------------------+
 * |    ImplT  (PublisherImpl / SubscriberImpl / ...)  |
 * +-------------------+-------------------------------+
 *                     |  creates / calls
 * +-------------------v-------------------------------+
 * |         Transport Back-end                        |
 * |   intra   shm   shm2   dds   ddsc   zenoh  ...    |
 * +---------------------------------------------------+
 * @endcode
 *
 * @par Lifecycle
 * | Step               | Method                | Notes                                       |
 * | ------------------ | --------------------- | ------------------------------------------- |
 * | Construction       | constructor           | Parses URL, creates impl via Conf factory.  |
 * | Initialisation     | @c init()             | Calls impl init + init_ext, sets loan flag. |
 * | Use                | publish/listen/invoke | Normal operation.                           |
 * | Interrupt          | @c interrupt()        | Unblocks any blocking wait immediately.     |
 * | De-initialisation  | @c deinit()           | Calls interrupt(), then impl deinit.        |
 * | Destruction        | destructor            | Calls @c deinit() automatically.            |
 *
 * @par Deferred Initialisation
 * @code
 * Publisher<MyMsg> pub("dds://topic", InitType::kWithoutInit);
 * pub.set_ser_type("my.custom.Type");   // configure before init
 * pub.init();
 * @endcode
 *
 * @par Security
 * Enable per-message encryption by using the @c Security* aliases.  The
 * @c Security::Config aggregate is the second constructor argument; there is
 * no runtime setter:
 * @code
 * Security::Config cfg;
 * cfg.key = "my-secret";
 * SecurityPublisher<MyMsg> pub("shm://topic", cfg);
 * @endcode
 * @note @c intra:// and @c dds:// with CDR serialisation do NOT support security.
 *
 * @par Zero-copy Loans
 * On @c shm:// (Iceoryx) and @c shm2:// (Iceoryx2) transports, a loaned
 * buffer avoids extra copies:
 * @code
 * if (pub.is_support_loan()) {
 *     Bytes buf = pub.loan(sizeof(MyStruct));
 *     // fill buf ...
 *     pub.publish(buf);   // loan is returned automatically
 * }
 * @endcode
 *
 * @tparam ImplT  Concrete transport implementation (must inherit @c NodeImpl).
 * @tparam SecT   Security mode: @c kWithoutSecurity (default) or @c kWithSecurity.
 */

#pragma once

#include <atomic>
#include <memory>
#include <mutex>
#include <optional>
#include <string>

#include "./extension/security.h"
#include "./impl/node_impl.h"

namespace vlink {

/**
 * @class Node
 * @brief Transport-agnostic CRTP base for all VLink communication nodes.
 *
 * @details
 * All six VLink communication primitives inherit from this template.
 * It manages the @c ImplT implementation pointer and provides the shared
 * API surface described in the file-level documentation above.
 *
 * @tparam ImplT  Concrete impl class (e.g. @c PublisherImpl, @c GetterImpl).
 * @tparam SecT   Security mode (@c kWithoutSecurity or @c kWithSecurity).
 */
template <typename ImplT, SecurityType SecT>
class Node {
 public:
  /**
   * @brief Callback type for node status-change notifications.
   */
  using StatusCallback = NodeImpl::StatusCallback;

  /**
   * @brief Initialises the node and its transport back-end.
   *
   * @details
   * Uses an atomic compare-exchange to guard against double-initialisation.
   * On success: calls @c impl_->init(), @c impl_->init_ext(), and queries
   * the transport for zero-copy loan support.  Calling @c init() on an
   * already-initialised node is a no-op that returns @c false.
   *
   * @return @c true on first successful initialisation; @c false otherwise.
   */
  virtual bool init();

  /**
   * @brief Shuts down the node and releases all transport resources.
   *
   * @details
   * Uses an atomic compare-exchange to prevent double-deinit.  Calls
   * @c interrupt() first, then @c impl_->deinit() and @c impl_->deinit_ext().
   * When safe-quit mode is active the deinit sequence runs under the quit mutex.
   * The destructor calls @c deinit() automatically, so explicit calls are only
   * needed for early shutdown.
   *
   * @return @c true on first successful deinit; @c false if not initialised.
   */
  virtual bool deinit();

  /**
   * @brief Unblocks any active blocking wait on this node.
   *
   * @details
   * Signals the internal interrupted flag and wakes the condition variable so
   * that calls such as @c wait_for_subscribers(), @c wait_for_connected(), and
   * @c wait_for_value() return immediately with @c false.  @c Getter overrides
   * this to additionally wake its own blocking condition variable used by
   * @c wait_for_value().
   */
  virtual void interrupt();

  /**
   * @brief Returns @c true if @c init() has been successfully called.
   *
   * @return @c true when the node is in the initialised state.
   */
  [[nodiscard]] bool has_inited() const;

  /**
   * @brief Returns @c true if the transport supports zero-copy loaned buffers.
   *
   * @details
   * Currently the @c shm:// (Iceoryx) and @c shm2:// (Iceoryx2) back-ends
   * return @c true here.  When loans are supported, @c publish() / @c set()
   * / @c reply() will automatically use them to avoid an extra memory copy.
   *
   * @return @c true if @c loan() / @c return_loan() are meaningful.
   */
  [[nodiscard]] bool is_support_loan() const;

  /**
   * @brief Allocates a loaned buffer from the transport memory pool.
   *
   * @details
   * Returns a @c Bytes backed by transport-managed memory of @p size bytes.
   * The caller must either pass it to a publish/write/reply call (which
   * returns the loan automatically) or call @c return_loan() explicitly.
   * Returns an empty @c Bytes on failure or if the transport has no loan pool.
   *
   * @param size  Requested size in bytes (@c 0 is valid for empty messages).
   * @return      Loaned @c Bytes, or empty @c Bytes on failure.
   */
  [[nodiscard]] Bytes loan(int64_t size);

  /**
   * @brief Returns a previously loaned buffer back to the transport pool.
   *
   * @details
   * Must be called if a loaned buffer obtained via @c loan() is not consumed by
   * a publish/write call.  Failing to return a loan can exhaust the shared
   * memory pool.
   *
   * @param bytes  The loaned @c Bytes to return.
   * @return       @c true on success; @c false if the buffer is not a valid loan.
   */
  bool return_loan(const Bytes& bytes);

  /**
   * @brief Enables or disables manual-unloan mode for zero-copy receives.
   *
   * @details
   * In manual-unloan mode the user is responsible for calling @c return_loan()
   * after consuming received data.  The base implementation logs a warning;
   * only @c Subscriber and @c Getter override this method.
   *
   * @param manual_unloan  @c true to enable; @c false for automatic (default).
   */
  virtual void set_manual_unloan(bool manual_unloan);

  /**
   * @brief Returns @c true if manual-unloan mode is active.
   *
   * @return @c true if @c set_manual_unloan(true) was called.
   */
  [[nodiscard]] virtual bool is_manual_unloan() const;

  /**
   * @brief Suspends message delivery on this node.
   *
   * @details
   * Behaviour is transport-dependent: some back-ends buffer incoming messages
   * while suspended; others drop them.  Pair with @c resume() to re-enable.
   *
   * @return @c true if suspension succeeded; @c false on error.
   */
  bool suspend();

  /**
   * @brief Resumes message delivery after a @c suspend().
   *
   * @return @c true if resumption succeeded; @c false on error.
   */
  bool resume();

  /**
   * @brief Returns @c true if the node is currently suspended.
   *
   * @return @c true while @c suspend() is in effect.
   */
  [[nodiscard]] bool is_suspend() const;

  /**
   * @brief Attaches the node to a @c MessageLoop for callback dispatching.
   *
   * @details
   * After attachment, incoming-message callbacks are posted to @p message_loop
   * rather than invoked on the transport thread.  This serialises delivery to
   * the loop's thread, which is useful for single-threaded application code.
   *
   * @param message_loop  Non-null pointer to the target @c MessageLoop.
   * @return              @c true on success; @c false if a @c MessageLoop is
   *                      already attached.
   */
  bool attach(class MessageLoop* message_loop);

  /**
   * @brief Detaches the node from its current @c MessageLoop.
   *
   * @details
   * After detachment, callbacks are again invoked on the transport thread.
   *
   * @return @c true on success; @c false if no loop was attached.
   */
  bool detach();

  /**
   * @brief Returns the @c MessageLoop this node is attached to.
   *
   * @return Pointer to the attached @c MessageLoop, or @c nullptr.
   */
  [[nodiscard]] class MessageLoop* get_message_loop() const;

  /**
   * @brief Returns the abstract node handle for graph introspection.
   *
   * @details
   * The @c AbstractNode pointer can be used with @c AbstractFactory to query
   * peer nodes in the same transport graph, or passed to the proxy monitoring
   * API for runtime topology inspection.
   *
   * @return Non-owning pointer to the @c AbstractNode, or @c nullptr if the
   *         transport back-end does not expose an @c AbstractNode.
   */
  [[nodiscard]] const AbstractNode* get_abstract_node() const;

  /**
   * @brief Returns the current status object for the specified status type.
   *
   * @details
   * Returns a polymorphic status shared pointer.  The concrete type and set of
   * available types depend on the active transport.  If the transport does not
   * support the requested @p type, a @c Status::Unknown instance is returned
   * and a warning is logged.
   *
   * @param type  Category of status to retrieve.
   * @return      Shared pointer to status data; returns @c Status::Unknown
   *              when the transport does not support the query.
   */
  [[nodiscard]] Status::BasePtr get_status(Status::Type type) const;

  /**
   * @brief Registers a handler called when the node's status changes.
   *
   * @details
   * Only one handler can be registered; subsequent calls replace the previous
   * one.  The handler is invoked with a @c Status::BasePtr describing the new
   * state (e.g. connected, disconnected, error).
   *
   * @param callback  Callable @c void(const Status::BasePtr&) invoked on status changes.
   */
  void register_status_handler(StatusCallback&& callback);

  /**
   * @brief Sets a transport-specific key-value property on the node.
   *
   * @details
   * Provides an extensibility mechanism for back-end-specific tuning knobs
   * that do not have dedicated API methods.  Recognised keys depend on the
   * active transport.
   *
   * @param prop   Property key string.
   * @param value  Property value string.
   */
  void set_property(const std::string& prop, const std::string& value);

  /**
   * @brief Retrieves a transport-specific property value.
   *
   * @param prop  Property key string.
   * @return      Property value string; empty if key is not recognised.
   */
  [[nodiscard]] std::string get_property(const std::string& prop) const;

  /**
   * @brief Returns the @c TransportType of the transport this node is bound to.
   *
   * @return Enumerator such as @c kDds, @c kShm, @c kIntra, etc.
   */
  [[nodiscard]] TransportType get_transport_type() const;

  /**
   * @brief Returns the URL string used to construct this node.
   *
   * @details
   * Non-empty only when the node was constructed via a URL string or @c Url
   * object; empty for @c ConfT-based construction.
   *
   * @return Reference to the URL (e.g. @c "dds://vehicle/speed").
   */
  [[nodiscard]] const std::string& get_url() const;

  /**
   * @brief Sets the filesystem path for message bag recording.
   *
   * @details
   * Enables recording of each published/received message to a bag file.
   * Not supported on @c intra:// or @c dds:// CDR nodes (triggers fatal log).
   *
   * @param path  Bag file path. Supported suffixes are @c .vdb, @c .vdbx,
   *              @c .vcap and @c .vcapx; unsupported suffixes disable recording.
   */
  void set_record_path(const std::string& path);

  /**
   * @brief Overrides the runtime wire metadata for this node.
   *
   * @details
   * @p ser_type stores the concrete runtime type identifier, while
   * @p schema_type stores the coarse decoder family used by discovery, proxy,
   * and bag metadata. When @p schema_type is @c SchemaType::kUnknown (the
   * default), the node does not explicitly override the current family; in
   * that mode it keeps the existing protobuf / flatbuffers family unless the
   * new @p ser_type itself clearly implies @c kRaw or @c kZeroCopy. Passing
   * an empty @p ser_type clears both fields.
   *
   * If called after @c init(), the transport extension is restarted once so
   * external metadata stays in sync.
   *
   * @param ser_type     Concrete serialisation type identifier, or empty to clear the current metadata.
   * @param schema_type  Explicit coarse schema family to expose; default
   *                     @c kUnknown preserves the current family unless
   *                     @p ser_type implies a different one.
   */
  void set_ser_type(const std::string& ser_type, SchemaType schema_type = SchemaType::kUnknown);

  /**
   * @brief Returns the current serialisation type string.
   *
   * @return Reference to the type string stored in the impl.
   */
  [[nodiscard]] const std::string& get_ser_type() const;

  /**
   * @brief Returns the current coarse schema family.
   *
   * @return The schema family stored in the node implementation.
   */
  [[nodiscard]] SchemaType get_schema_type() const;

  /**
   * @brief Enables or disables peer-discovery on this node.
   *
   * @details
   * Disabling discovery reduces CPU and network overhead for nodes that never
   * need to locate peers.  If called after @c init(), the transport extension
   * is automatically reinitialised to apply the change.
   *
   * @param enable  @c true (default) to enable discovery; @c false to disable.
   */
  void set_discovery_enabled(bool enable);

  /**
   * @brief Returns @c true if peer-discovery is currently enabled.
   *
   * @return @c true if discovery is active.
   */
  [[nodiscard]] bool get_discovery_enabled() const;

  /**
   * @brief Binds a Protobuf Arena for arena-allocated message objects.
   *
   * @details
   * Required when @c MsgT is a raw Protobuf pointer type (e.g. @c MyProto*).
   * The arena must outlive this node.  Forgetting to bind an arena for
   * proto-pointer types causes a fatal log on the first received message.
   *
   * @param proto_arena  Pointer to a @c google::protobuf::Arena instance (as @c void*).
   */
  void bind_proto_arena(void* proto_arena);

  /**
   * @brief Returns the cumulative CPU usage ratio for this node.
   *
   * @details
   * Returns the percentage of wall-clock time that this node has spent in
   * active publish/receive operations since the profiler was started.
   * Available only when the CPU profiler is built in (i.e. @c VLINK_DISABLE_PROFILER
   * is not defined) and global profiling is enabled via the @c VLINK_PROFILER_ENABLE
   * environment variable.  Returns @c -1.0 if no profiler is attached to the impl.
   *
   * @return CPU usage percentage [0.0, 100.0], or @c -1.0 if unavailable.
   */
  [[nodiscard]] double get_cpu_usage() const;

  /**
   * @brief Returns @c true if safe-quit mode is currently active.
   *
   * @details
   * Safe-quit mode holds a @c std::mutex around all user callbacks and around
   * @c deinit(), preventing use-after-free races when a node is destroyed while
   * a callback is in flight.
   *
   * @return @c true if the safe-quit mutex is engaged.
   */
  [[nodiscard]] bool get_safety_quit() const;

  /**
   * @brief Enables or disables safe-quit mode.
   *
   * @details
   * When @p safety_quit is @c true, an internal @c std::mutex is allocated and
   * locked around every callback invocation and around @c deinit().  Enable
   * this when the node's lifetime is shorter than the callback's scope.  There
   * is a small synchronisation overhead; avoid enabling it on hot paths.
   *
   * @param safety_quit  @c true to enable; @c false to disable (default).
   */
  void set_safety_quit(bool safety_quit);

  /**
   * @brief Configures transport-layer SSL/TLS encryption for this node.
   *
   * @details
   * Merges the fields of @p options into the node's internal property map
   * via @c SslOptions::parse_to().  The transport back-end reads the
   * resulting @c ssl.* properties during @c init() to set up a TLS
   * connection.  This method must be called **before** @c init() for the
   * settings to take effect.
   *
   * SSL is considered enabled when @c SslOptions::is_valid() returns
   * @c true (i.e. at least @c ca_file or @c cert_file is non-empty).
   * Not all back-ends support TLS; see the @c SslOptions file-level
   * documentation for the per-backend compatibility table.
   *
   * This call is thread-safe; the property map is updated under a mutex.
   *
   * @par Example
   * @code
   * Publisher<MyMsg> pub("mqtt://sensor/data", InitType::kWithoutInit);
   * SslOptions ssl;
   * ssl.ca_file   = "/etc/certs/ca.pem";
   * ssl.cert_file = "/etc/certs/client.pem";
   * ssl.key_file  = "/etc/certs/client-key.pem";
   * pub.set_ssl_options(ssl);
   * pub.init();
   * @endcode
   *
   * @param options  The SSL/TLS configuration to apply.
   *
   * @see SslOptions, set_property()
   */
  void set_ssl_options(const SslOptions& options);

 protected:
  Node();

  virtual ~Node();

  /**
   * @brief Installs a @c Security configuration before transport initialisation.
   *
   * @details
   * Internal helper used by @c SecurityPublisher / @c SecuritySubscriber /
   * @c SecurityServer / @c SecurityClient / @c SecuritySetter /
   * @c SecurityGetter constructors after @c impl_ is created and before
   * @c init().  Delegates validation and storage to @c NodeImpl::enable_security().
   *
   * @param cfg  Security configuration aggregate.
   * @return     @c true when @p cfg is usable for this node role and transport.
   */
  bool enable_security(const Security::Config& cfg);

  /**
   * @brief Move-overload for construction-time security installation.
   *
   * @details
   * Used when an internal caller owns the config and can pass it through to
   * @c NodeImpl::enable_security(Security::Config&&) without an extra copy.
   *
   * @param cfg  Security configuration aggregate to consume.
   * @return     @c true when @p cfg is usable for this node role and transport.
   */
  bool enable_security(Security::Config&& cfg);

  template <typename CallbackT, typename... ArgsT>
  void invoke_callback(const CallbackT& callback, ArgsT&&... args);

  template <typename TypeT>
  TypeT get_default_value();

  void* proto_arena_{nullptr};
  bool is_support_loan_{false};
  bool is_manual_unloan_{false};

  std::atomic_bool has_inited_{false};
  std::optional<std::mutex> quit_mtx_;  ///< Optional safe-quit mutex guarding callbacks and teardown.

  std::unique_ptr<ImplT> impl_;

 private:
  VLINK_DISALLOW_COPY_AND_ASSIGN(Node)
};

}  // namespace vlink

#include "./internal/node-inl.h"
