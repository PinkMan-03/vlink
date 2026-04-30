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
 * @file c_api.h
 * @brief Pure C API for the VLink communication middleware.
 *
 * @details
 * Provides a stable, language-agnostic C binding over the VLink C++ core.
 * Each of the three VLink communication models is exposed through a handle-based
 * interface that wraps the corresponding C++ template class:
 *
 * | Model   | C++ class                         | C handle types                                     |
 * | ------- | --------------------------------- | -------------------------------------------------- |
 * | Event   | Publisher / Subscriber            | vlink_publisher_handle_t / vlink_subscriber_handle_t |
 * | Method  | Server / Client                   | vlink_server_handle_t / vlink_client_handle_t        |
 * | Field   | Setter / Getter                   | vlink_setter_handle_t / vlink_getter_handle_t        |
 *
 * All handles are opaque structs that contain a @c native_handle pointer to the
 * underlying C++ object and a @c reserved array used for internal state
 * (e.g., the @c std::mutex and response buffer used by @c vlink_server_handle_t).
 *
 * @par Return value conventions
 * Every function returns a @c vlink_ret_t integer:
 *
 * | Code                          | Meaning                                          |
 * | ----------------------------- | ------------------------------------------------ |
 * | VLINK_RET_NO_ERROR (0)        | Success                                          |
 * | VLINK_RET_UNEXPECTED_ERROR    | Condition not met (e.g., no subscribers yet)     |
 * | VLINK_RET_INVALID_ERROR       | Null pointer or invalid handle                   |
 * | VLINK_RET_MEMORY_ERROR        | Output buffer too small                          |
 * | VLINK_RET_RUNTIME_ERROR       | Exception thrown during C++ construction         |
 * | VLINK_RET_TRANSFER_ERROR      | Publish / listen / invoke operation failed       |
 * | VLINK_RET_UNKNOWN_ERROR (-1)  | Unclassified error                               |
 *
 * @par Server reply protocol
 * The @c vlink_server_handle_t::reserved array is used to coordinate the
 * synchronous request-reply flow:
 * @code
 * static void on_request(const uint8_t* data, size_t size, void* user_data) {
 *     vlink_server_handle_t* handle = (vlink_server_handle_t*) user_data;
 *     vlink_reply(handle, resp_data, resp_size);
 * }
 *
 * vlink_schema_info_t schema = {"demo.raw.Text", VLINK_SCHEMA_RAW};
 * vlink_create_server(url, &schema, &handle, on_request, &handle);
 * @endcode
 * If @c vlink_reply() is not called, the request completes with an empty
 * response payload.  Calling @c vlink_reply() after the callback returns is
 * undefined behaviour; the internal mutex lock will no longer be held.
 *
 * @par Usage -- publisher / subscriber
 * @code
 * vlink_publisher_handle_t pub;
 * vlink_schema_info_t schema = {"demo.proto.PointCloud", VLINK_SCHEMA_PROTOBUF};
 * vlink_create_publisher("dds://my/topic", &schema, &pub);
 * vlink_wait_for_subscribers(pub, 1000);
 * vlink_publish(pub, data_buf, data_size);
 * vlink_destroy_publisher(&pub);
 *
 * static void on_message(const uint8_t* data, size_t size, void* user_data) {
 *     (void) data;
 *     (void) size;
 *     (void) user_data;
 * }
 *
 * vlink_subscriber_handle_t sub;
 * vlink_create_subscriber("dds://my/topic", &schema, &sub, on_message, user_data);
 * @endcode
 *
 * @par Usage -- getter (polling)
 * @code
 * vlink_getter_handle_t getter;
 * vlink_schema_info_t schema = {"demo.proto.State", VLINK_SCHEMA_PROTOBUF};
 * vlink_create_getter("dds://state/topic", &schema, &getter, NULL, NULL);
 *
 * uint8_t buf[4096];
 * size_t  sz = sizeof(buf);
 * if (vlink_get(getter, buf, &sz) == VLINK_RET_NO_ERROR) {
 *     // buf[0..sz-1] contains the latest value
 * }
 * @endcode
 *
 * @note
 * - The C API uses @c vlink::Publisher<vlink::Bytes> and similar internally.
 *   Pass @c vlink_schema_info_t to configure @c ser + @c schema atomically.
 * - All create/destroy pairs must be balanced.  Handles are not thread-safe
 *   across concurrent create/destroy calls.
 * - @c vlink_get() copies the latest value into the caller-supplied buffer.
 *   Returns @c VLINK_RET_MEMORY_ERROR if @c *size is smaller than the payload.
 * - @c vlink_publish_by_force() publishes even when no subscribers are
 *   matched, useful for transient-local-durability scenarios.
 */

// NOLINTBEGIN

#pragma once

#undef VLINK_C_API_EXPORT
#ifdef VLINK_C_API_LIBRARY_STATIC
#define VLINK_C_API_EXPORT
#elif defined(_WIN32) || defined(__CYGWIN__)
#ifdef VLINK_C_API_LIBRARY
#define VLINK_C_API_EXPORT __declspec(dllexport)
#else
#define VLINK_C_API_EXPORT __declspec(dllimport)
#endif
#else
#define VLINK_C_API_EXPORT __attribute__((visibility("default")))
#endif

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Return code for all VLink C API functions.
 *
 * @details
 * A non-negative value indicates success or an expected condition.
 * Negative values indicate hard errors.  Check the return of every API call.
 */
typedef enum {
  VLINK_RET_UNKNOWN_ERROR = -1,   /**< Unclassified or unexpected internal error. */
  VLINK_RET_NO_ERROR = 0,         /**< Operation succeeded. */
  VLINK_RET_UNEXPECTED_ERROR = 1, /**< Condition not yet met (e.g., no subscribers). */
  VLINK_RET_INVALID_ERROR = 2,    /**< Null pointer argument or invalid handle. */
  VLINK_RET_MEMORY_ERROR = 3,     /**< Caller-provided buffer is too small. */
  VLINK_RET_RUNTIME_ERROR = 4,    /**< C++ exception thrown during node construction. */
  VLINK_RET_TRANSFER_ERROR = 5,   /**< Publish, listen, or invoke operation failed. */
} vlink_ret_t;

/**
 * @brief Coarse runtime schema family used for raw C API nodes.
 *
 * @details
 * Numerical values are intentionally kept in sync with the C++
 * @c vlink::SchemaType enum (see @c include/vlink/impl/types.h).  This
 * allows the C API implementation to cast between the two enums safely.
 * Clients should always reference the symbolic names rather than the raw
 * integer values so the mapping remains opaque at the source level.
 */
typedef enum {
  VLINK_SCHEMA_UNKNOWN = 0,     /**< Schema family is not specified. */
  VLINK_SCHEMA_RAW = 1,         /**< Opaque/raw payload. */
  VLINK_SCHEMA_ZEROCOPY = 2,    /**< VLink zero-copy payload. */
  VLINK_SCHEMA_PROTOBUF = 3,    /**< Protocol Buffers payload. */
  VLINK_SCHEMA_FLATBUFFERS = 4, /**< FlatBuffers payload. */
} vlink_schema_t;

/**
 * @brief Bundled runtime schema metadata for C API node creation.
 *
 * @details
 * This mirrors the C++ pair of @c ser_type + @c schema_type so callers can
 * configure both pieces atomically before the underlying node is initialised.
 * Callers must either provide both fields together or leave both unset
 * (@c ser == NULL / empty and @c schema == VLINK_SCHEMA_UNKNOWN).
 */
typedef struct {
  const char* ser;       /**< Concrete type name / serialization identifier, or @c NULL. */
  vlink_schema_t schema; /**< Coarse schema family. */
} vlink_schema_info_t;

/**
 * @brief Opaque handle for a @c Publisher node.
 *
 * @details
 * Created by @c vlink_create_publisher() and destroyed by
 * @c vlink_destroy_publisher().  @c native_handle points to a heap-allocated
 * @c vlink::Publisher<vlink::Bytes> object.
 */
typedef struct {
  void* native_handle; /**< Internal C++ Publisher object pointer. */
  void* reserved[4];   /**< Reserved for internal use; do not modify. */
} vlink_publisher_handle_t;

/**
 * @brief Opaque handle for a @c Subscriber node.
 *
 * @details
 * Created by @c vlink_create_subscriber() and destroyed by
 * @c vlink_destroy_subscriber().  @c native_handle points to a heap-allocated
 * @c vlink::Subscriber<vlink::Bytes> object.
 */
typedef struct {
  void* native_handle; /**< Internal C++ Subscriber object pointer. */
  void* reserved[4];   /**< Reserved for internal use; do not modify. */
} vlink_subscriber_handle_t;

/**
 * @brief Opaque handle for a @c Server node.
 *
 * @details
 * Created by @c vlink_create_server() and destroyed by
 * @c vlink_destroy_server().  @c native_handle points to a heap-allocated
 * @c vlink::Server<vlink::Bytes, vlink::Bytes> object.
 * The @c reserved array is used internally to coordinate the synchronous
 * request-reply flow:
 * - @c reserved[0]: pointer to a @c std::mutex (lock held during callback).
 * - @c reserved[1]: pointer to the response byte buffer.
 * - @c reserved[2]: response byte count (stored as a pointer-sized integer).
 * - @c reserved[3]: non-null while a request is being processed (cleared by
 *   @c vlink_reply()).
 */
typedef struct {
  void* native_handle; /**< Internal C++ Server object pointer. */
  void* reserved[4];   /**< Internal coordination fields; do not modify. */
} vlink_server_handle_t;

/**
 * @brief Opaque handle for a @c Client node.
 *
 * @details
 * Created by @c vlink_create_client() and destroyed by
 * @c vlink_destroy_client().  @c native_handle points to a heap-allocated
 * @c vlink::Client<vlink::Bytes, vlink::Bytes> object.
 */
typedef struct {
  void* native_handle; /**< Internal C++ Client object pointer. */
  void* reserved[4];   /**< Reserved for internal use; do not modify. */
} vlink_client_handle_t;

/**
 * @brief Opaque handle for a @c Setter node.
 *
 * @details
 * Created by @c vlink_create_setter() and destroyed by
 * @c vlink_destroy_setter().  @c native_handle points to a heap-allocated
 * @c vlink::Setter<vlink::Bytes> object.
 */
typedef struct {
  void* native_handle; /**< Internal C++ Setter object pointer. */
  void* reserved[4];   /**< Reserved for internal use; do not modify. */
} vlink_setter_handle_t;

/**
 * @brief Opaque handle for a @c Getter node.
 *
 * @details
 * Created by @c vlink_create_getter() and destroyed by
 * @c vlink_destroy_getter().  @c native_handle points to a heap-allocated
 * @c vlink::Getter<vlink::Bytes> object.
 */
typedef struct {
  void* native_handle; /**< Internal C++ Getter object pointer. */
  void* reserved[4];   /**< Reserved for internal use; do not modify. */
} vlink_getter_handle_t;

/**
 * @brief Callback invoked when the connection state of a Publisher or Client changes.
 *
 * @param is_connected  @c true when at least one peer has matched, @c false otherwise.
 * @param user_data     Opaque pointer supplied at registration time.
 */
typedef void (*vlink_connect_callback_t)(const bool is_connected, void* user_data);

/**
 * @brief Callback invoked when a Subscriber or Getter receives a message.
 *
 * @param data       Pointer to the received payload bytes.
 * @param size       Number of bytes in @p data.
 * @param user_data  Opaque pointer supplied at creation time.
 */
typedef void (*vlink_msg_callback_t)(const uint8_t* data, const size_t size, void* user_data);

/**
 * @brief Callback invoked when a Server receives an RPC request.
 *
 * @details
 * Called synchronously on the Server's dispatch thread while an internal mutex
 * is held.  Call @c vlink_reply() from within this callback if you want to
 * provide a non-empty response before returning.  If it is not called, the
 * request completes with an empty payload.  Calling it after the callback
 * returns is undefined behaviour.
 *
 * @param data       Pointer to the request payload bytes.
 * @param size       Number of bytes in @p data.
 * @param user_data  Opaque pointer supplied at creation time.
 */
typedef void (*vlink_req_callback_t)(const uint8_t* data, const size_t size, void* user_data);

/**
 * @brief Callback invoked when a Client receives an RPC response.
 *
 * @param data       Pointer to the response payload bytes.  May be null if the
 *                   server did not provide a response.
 * @param size       Number of bytes in @p data.
 * @param user_data  Opaque pointer supplied at invocation time.
 */
typedef void (*vlink_resp_callback_t)(const uint8_t* data, const size_t size, void* user_data);

////////////////////////////////////////////////////////////////
/// Publisher
////////////////////////////////////////////////////////////////

/**
 * @brief Creates a Publisher node and initialises it on the given URL.
 *
 * @details
 * Allocates a @c vlink::Publisher<vlink::Bytes> on the heap and stores the
 * pointer in @p handle.  Pass @p schema_info to set @c ser + @c schema before
 * the underlying node is initialised.  Pass @c NULL to leave both unset.
 *
 * @param url          VLink topic URL (e.g., @c "dds://my/topic").  Must not be @c NULL.
 * @param schema_info  Optional bundled @c ser + @c schema metadata.
 * @param handle       Output handle to fill.  Must not be @c NULL.
 * @return        @c VLINK_RET_NO_ERROR on success, @c VLINK_RET_INVALID_ERROR if
 *                @p url or @p handle is @c NULL, if @p schema_info is only
 *                partially filled, if @p schema_info->schema is invalid, or
 *                @c VLINK_RET_RUNTIME_ERROR if construction throws.
 */
VLINK_C_API_EXPORT int vlink_create_publisher(const char* url, const vlink_schema_info_t* schema_info,
                                              vlink_publisher_handle_t* handle);

/**
 * @brief Destroys a Publisher node and releases all associated resources.
 *
 * @param handle  Publisher handle to destroy.  Must not be @c NULL.
 * @return        @c VLINK_RET_NO_ERROR on success, @c VLINK_RET_INVALID_ERROR if
 *                @p handle or its @c native_handle is @c NULL.
 */
VLINK_C_API_EXPORT int vlink_destroy_publisher(vlink_publisher_handle_t* handle);

/**
 * @brief Checks whether at least one Subscriber has matched this Publisher.
 *
 * @param handle  Publisher handle.
 * @return        @c VLINK_RET_NO_ERROR if subscribers are present,
 *                @c VLINK_RET_UNEXPECTED_ERROR if none are matched yet, or
 *                @c VLINK_RET_INVALID_ERROR on bad handle.
 */
VLINK_C_API_EXPORT int vlink_has_subscribers(const vlink_publisher_handle_t handle);

/**
 * @brief Blocks until at least one Subscriber matches or the timeout elapses.
 *
 * @param handle      Publisher handle.
 * @param timeout_ms  Maximum wait time in milliseconds.
 * @return            @c VLINK_RET_NO_ERROR if a subscriber matched,
 *                    @c VLINK_RET_UNEXPECTED_ERROR if the timeout expired, or
 *                    @c VLINK_RET_INVALID_ERROR on bad handle.
 */
VLINK_C_API_EXPORT int vlink_wait_for_subscribers(const vlink_publisher_handle_t handle, const int timeout_ms);

/**
 * @brief Registers a callback fired whenever the Subscriber connection state changes.
 *
 * @details
 * The callback is invoked from the Publisher's internal event thread.
 *
 * @param handle            Publisher handle.
 * @param connect_callback  Callback to invoke on connection change.
 * @param user_data         Opaque pointer forwarded to @p connect_callback.
 * @return                  @c VLINK_RET_NO_ERROR on success, or
 *                          @c VLINK_RET_INVALID_ERROR on bad handle or
 *                          @c NULL @p connect_callback.
 */
VLINK_C_API_EXPORT int vlink_detect_subscribers(const vlink_publisher_handle_t handle,
                                                const vlink_connect_callback_t connect_callback, void* user_data);

/**
 * @brief Publishes a message to all matched Subscribers.
 *
 * @details
 * Internally wraps @p data in a shallow-copy @c vlink::Bytes (zero-copy when
 * the underlying transport supports it).  Returns @c VLINK_RET_TRANSFER_ERROR
 * if no subscribers are matched and the publisher does not allow forced delivery.
 *
 * @param handle  Publisher handle.
 * @param data    Payload to publish.  Must remain valid until the call returns.
 * @param size    Number of bytes in @p data.
 * @return        @c VLINK_RET_NO_ERROR on success, @c VLINK_RET_TRANSFER_ERROR
 *                if publishing failed, or @c VLINK_RET_INVALID_ERROR on bad handle.
 */
VLINK_C_API_EXPORT int vlink_publish(const vlink_publisher_handle_t handle, const uint8_t* data, const size_t size);

/**
 * @brief Publishes a message even when no Subscribers are currently matched.
 *
 * @details
 * Identical to @c vlink_publish() but passes @c force=true to the underlying
 * publisher, bypassing the subscriber-presence check.  Useful for
 * transient-local-durability or late-joining subscriber scenarios.
 *
 * @param handle  Publisher handle.
 * @param data    Payload to publish.
 * @param size    Number of bytes in @p data.
 * @return        @c VLINK_RET_NO_ERROR on success, @c VLINK_RET_TRANSFER_ERROR
 *                on failure, or @c VLINK_RET_INVALID_ERROR on bad handle.
 */
VLINK_C_API_EXPORT int vlink_publish_by_force(const vlink_publisher_handle_t handle, const uint8_t* data,
                                              const size_t size);

////////////////////////////////////////////////////////////////
/// Subscriber
////////////////////////////////////////////////////////////////

/**
 * @brief Creates a Subscriber node, initialises it, and registers the message callback.
 *
 * @details
 * Allocates a @c vlink::Subscriber<vlink::Bytes> and immediately calls @c listen()
 * with the provided @p msg_callback.  The callback is invoked on the Subscriber's
 * internal receive thread.
 *
 * @param url           VLink topic URL.  Must not be @c NULL.
 * @param schema_info   Optional bundled @c ser + @c schema metadata.
 * @param handle        Output handle.  Must not be @c NULL.
 * @param msg_callback  Callback invoked on each received message.  Must not be @c NULL.
 * @param user_data     Opaque pointer forwarded to @p msg_callback.
 * @return              @c VLINK_RET_NO_ERROR on success, @c VLINK_RET_INVALID_ERROR if
 *                      any required argument is @c NULL, if @p schema_info is only
 *                      partially filled, if @p schema_info->schema is invalid,
 *                      @c VLINK_RET_TRANSFER_ERROR if @c listen() fails, or
 *                      @c VLINK_RET_RUNTIME_ERROR on construction exception.
 */
VLINK_C_API_EXPORT int vlink_create_subscriber(const char* url, const vlink_schema_info_t* schema_info,
                                               vlink_subscriber_handle_t* handle,
                                               const vlink_msg_callback_t msg_callback, void* user_data);

/**
 * @brief Destroys a Subscriber node and releases all associated resources.
 *
 * @param handle  Subscriber handle to destroy.  Must not be @c NULL.
 * @return        @c VLINK_RET_NO_ERROR on success, or @c VLINK_RET_INVALID_ERROR
 *                on bad handle.
 */
VLINK_C_API_EXPORT int vlink_destroy_subscriber(vlink_subscriber_handle_t* handle);

////////////////////////////////////////////////////////////////
/// Server
////////////////////////////////////////////////////////////////

/**
 * @brief Creates a Server node, initialises it, and registers the request callback.
 *
 * @details
 * Allocates a @c vlink::Server<vlink::Bytes, vlink::Bytes> and calls @c listen()
 * with an internal handler that wraps @p req_callback.
 *
 * The internal handler holds a @c std::mutex (stored in @c handle->reserved[0])
 * during each invocation.  Call @c vlink_reply() from within @p req_callback
 * to set a non-empty response before the callback returns.
 *
 * @param url           VLink service URL.  Must not be @c NULL.
 * @param schema_info   Optional bundled @c ser + @c schema metadata.
 * @param handle        Output handle.  Must not be @c NULL.
 * @param req_callback  Request handler.  Must not be @c NULL.  Call
 *                      @c vlink_reply() before returning if a non-empty
 *                      response is required.
 * @param user_data     Opaque pointer forwarded to @p req_callback.
 * @return              @c VLINK_RET_NO_ERROR on success, @c VLINK_RET_INVALID_ERROR if
 *                      @p url or @p handle is @c NULL, if @p req_callback is
 *                      @c NULL, if @p schema_info is only partially filled, if
 *                      @p schema_info->schema is invalid, @c VLINK_RET_TRANSFER_ERROR
 *                      if @c listen() fails, or @c VLINK_RET_RUNTIME_ERROR on
 *                      construction exception.
 */
VLINK_C_API_EXPORT int vlink_create_server(const char* url, const vlink_schema_info_t* schema_info,
                                           vlink_server_handle_t* handle, const vlink_req_callback_t req_callback,
                                           void* user_data);

/**
 * @brief Destroys a Server node and frees all internal resources including the
 *        internal mutex and any pending response buffer.
 *
 * @param handle  Server handle to destroy.  Must not be @c NULL.
 * @return        @c VLINK_RET_NO_ERROR on success, or @c VLINK_RET_INVALID_ERROR
 *                on bad handle.
 */
VLINK_C_API_EXPORT int vlink_destroy_server(vlink_server_handle_t* handle);

/**
 * @brief Provides the response data for the current in-progress RPC request.
 *
 * @details
 * Must be called from within the @c vlink_req_callback_t while the internal
 * mutex lock is still held.  Copies @p data into a heap buffer stored in
 * @c handle->reserved[1] and sets @c handle->reserved[2] to the size.
 * Sets @c handle->reserved[3] to @c NULL to signal that a reply is ready.
 *
 * @param handle  Server handle.  Must not be @c NULL.
 * @param data    Response payload bytes.
 * @param size    Number of bytes in @p data.
 * @return        @c VLINK_RET_NO_ERROR on success, @c VLINK_RET_INVALID_ERROR
 *                on bad handle, or @c VLINK_RET_RUNTIME_ERROR if no pending
 *                request is in progress.
 */
VLINK_C_API_EXPORT int vlink_reply(vlink_server_handle_t* handle, const uint8_t* data, const size_t size);

////////////////////////////////////////////////////////////////
/// Client
////////////////////////////////////////////////////////////////

/**
 * @brief Creates a Client node and initialises it on the given URL.
 *
 * @param url          VLink service URL.  Must not be @c NULL.
 * @param schema_info  Optional bundled @c ser + @c schema metadata.
 * @param handle       Output handle.  Must not be @c NULL.
 * @return        @c VLINK_RET_NO_ERROR on success, @c VLINK_RET_INVALID_ERROR if
 *                @p url or @p handle is @c NULL, if @p schema_info is only
 *                partially filled, if @p schema_info->schema is invalid, or
 *                @c VLINK_RET_RUNTIME_ERROR on construction exception.
 */
VLINK_C_API_EXPORT int vlink_create_client(const char* url, const vlink_schema_info_t* schema_info,
                                           vlink_client_handle_t* handle);

/**
 * @brief Destroys a Client node and releases all associated resources.
 *
 * @param handle  Client handle to destroy.  Must not be @c NULL.
 * @return        @c VLINK_RET_NO_ERROR on success, or @c VLINK_RET_INVALID_ERROR
 *                on bad handle.
 */
VLINK_C_API_EXPORT int vlink_destroy_client(vlink_client_handle_t* handle);

/**
 * @brief Checks whether the Client is connected to a Server.
 *
 * @param handle  Client handle.
 * @return        @c VLINK_RET_NO_ERROR if connected, @c VLINK_RET_UNEXPECTED_ERROR
 *                if not yet connected, or @c VLINK_RET_INVALID_ERROR on bad handle.
 */
VLINK_C_API_EXPORT int vlink_has_server(const vlink_client_handle_t handle);

/**
 * @brief Blocks until a Server is available or the timeout elapses.
 *
 * @param handle      Client handle.
 * @param timeout_ms  Maximum wait time in milliseconds.
 * @return            @c VLINK_RET_NO_ERROR if connected, @c VLINK_RET_UNEXPECTED_ERROR
 *                    on timeout, or @c VLINK_RET_INVALID_ERROR on bad handle.
 */
VLINK_C_API_EXPORT int vlink_wait_for_server(const vlink_client_handle_t handle, const int timeout_ms);

/**
 * @brief Registers a callback fired whenever the Server connection state changes.
 *
 * @param handle            Client handle.
 * @param connect_callback  Callback to invoke on connection change.
 * @param user_data         Opaque pointer forwarded to @p connect_callback.
 * @return                  @c VLINK_RET_NO_ERROR on success, or
 *                          @c VLINK_RET_INVALID_ERROR on bad handle or
 *                          @c NULL @p connect_callback.
 */
VLINK_C_API_EXPORT int vlink_detect_server(const vlink_client_handle_t handle,
                                           const vlink_connect_callback_t connect_callback, void* user_data);

/**
 * @brief Sends an RPC request and registers a callback to receive the response.
 *
 * @details
 * Internally calls @c vlink::Client::invoke() using a shallow-copy @c Bytes
 * wrapping @p data.  The @p resp_callback is invoked asynchronously on the
 * underlying @c vlink::Client callback context when the Server's response
 * arrives.
 * Pass @c NULL for @p resp_callback if the response is not needed.
 *
 * @param handle         Client handle.
 * @param data           Request payload.  Must remain valid until the call returns.
 * @param size           Number of bytes in @p data.
 * @param resp_callback  Callback invoked with the response, or @c NULL.
 * @param user_data      Opaque pointer forwarded to @p resp_callback.
 * @return               @c VLINK_RET_NO_ERROR on success, @c VLINK_RET_TRANSFER_ERROR
 *                       if the invoke failed, or @c VLINK_RET_INVALID_ERROR on bad handle.
 */
VLINK_C_API_EXPORT int vlink_invoke(const vlink_client_handle_t handle, const uint8_t* data, const size_t size,
                                    const vlink_resp_callback_t resp_callback, void* user_data);

////////////////////////////////////////////////////////////////
/// Setter
////////////////////////////////////////////////////////////////

/**
 * @brief Creates a Setter node and initialises it on the given URL.
 *
 * @param url          VLink field URL.  Must not be @c NULL.
 * @param schema_info  Optional bundled @c ser + @c schema metadata.
 * @param handle       Output handle.  Must not be @c NULL.
 * @return        @c VLINK_RET_NO_ERROR on success, @c VLINK_RET_INVALID_ERROR if
 *                @p url or @p handle is @c NULL, if @p schema_info is only
 *                partially filled, if @p schema_info->schema is invalid, or
 *                @c VLINK_RET_RUNTIME_ERROR on construction exception.
 */
VLINK_C_API_EXPORT int vlink_create_setter(const char* url, const vlink_schema_info_t* schema_info,
                                           vlink_setter_handle_t* handle);

/**
 * @brief Destroys a Setter node and releases all associated resources.
 *
 * @param handle  Setter handle to destroy.  Must not be @c NULL.
 * @return        @c VLINK_RET_NO_ERROR on success, or @c VLINK_RET_INVALID_ERROR
 *                on bad handle.
 */
VLINK_C_API_EXPORT int vlink_destroy_setter(vlink_setter_handle_t* handle);

/**
 * @brief Publishes the latest field value.
 *
 * @details
 * The new value overwrites the previous value held by all matched Getters.
 * Internally wraps @p data in a shallow-copy @c vlink::Bytes (zero-copy).
 *
 * @warning
 * The Setter caches the latest value internally so that late-joining Getters
 * can sync.  Because @p data is wrapped without copying, the buffer it points
 * to **must remain valid until either** (a) the next @c vlink_set() call on
 * the same handle (which overrides the cached value), or (b)
 * @c vlink_destroy_setter() is called.  Releasing @p data sooner causes a
 * use-after-free when a late Getter tries to read the cache.  Static
 * storage (string literals, file-scope buffers) or a pinned allocation that
 * lives for the Setter's lifetime is the typical pattern; if that is
 * inconvenient, copy the value into a long-lived buffer before calling
 * @c vlink_set().
 *
 * @param handle  Setter handle.
 * @param data    New field value bytes.  Must remain valid until the next
 *                @c vlink_set() or @c vlink_destroy_setter().
 * @param size    Number of bytes in @p data.
 * @return        @c VLINK_RET_NO_ERROR on success, or @c VLINK_RET_INVALID_ERROR
 *                on bad handle.
 */
VLINK_C_API_EXPORT int vlink_set(const vlink_setter_handle_t handle, const uint8_t* data, const size_t size);

////////////////////////////////////////////////////////////////
/// Getter
////////////////////////////////////////////////////////////////

/**
 * @brief Creates a Getter node, initialises it, and optionally registers a change callback.
 *
 * @details
 * Allocates a @c vlink::Getter<vlink::Bytes>.  If @p msg_callback is non-null,
 * @c listen() is called and the callback is invoked on every value update.
 * If @p msg_callback is @c NULL the Getter operates in polling mode -- use
 * @c vlink_get() to retrieve the latest value.
 *
 * @param url           VLink field URL.  Must not be @c NULL.
 * @param schema_info   Optional bundled @c ser + @c schema metadata.
 * @param handle        Output handle.  Must not be @c NULL.
 * @param msg_callback  Callback for push-mode updates, or @c NULL for poll mode.
 * @param user_data     Opaque pointer forwarded to @p msg_callback.
 * @return              @c VLINK_RET_NO_ERROR on success, @c VLINK_RET_INVALID_ERROR if
 *                      @p url or @p handle is @c NULL, if @p schema_info is only
 *                      partially filled, if @p schema_info->schema is invalid, or
 *                      @c VLINK_RET_RUNTIME_ERROR on construction exception.
 */
VLINK_C_API_EXPORT int vlink_create_getter(const char* url, const vlink_schema_info_t* schema_info,
                                           vlink_getter_handle_t* handle, const vlink_msg_callback_t msg_callback,
                                           void* user_data);

/**
 * @brief Destroys a Getter node and releases all associated resources.
 *
 * @param handle  Getter handle to destroy.  Must not be @c NULL.
 * @return        @c VLINK_RET_NO_ERROR on success, or @c VLINK_RET_INVALID_ERROR
 *                on bad handle.
 */
VLINK_C_API_EXPORT int vlink_destroy_getter(vlink_getter_handle_t* handle);

/**
 * @brief Retrieves the latest field value into a caller-provided buffer.
 *
 * @details
 * Copies the current cached value into @p data.  On entry @c *size must hold
 * the capacity of @p data; on success @c *size is updated to the actual byte
 * count.  When the buffer is too small returns @c VLINK_RET_MEMORY_ERROR and
 * sets @c *size to the required byte count so the caller can allocate and
 * retry.  @p data is left unmodified in the error case.
 *
 * @param handle  Getter handle.
 * @param data    Output buffer.  Must not be @c NULL.
 * @param size    In/out: buffer capacity on entry, actual size on exit (or required
 *                size when the buffer is too small).  Must not be @c NULL.
 * @return        @c VLINK_RET_NO_ERROR on success, @c VLINK_RET_TRANSFER_ERROR if no
 *                value is available yet, @c VLINK_RET_MEMORY_ERROR if @c *size is too
 *                small (the required size is written back into @c *size), or
 *                @c VLINK_RET_INVALID_ERROR on bad arguments.
 */
VLINK_C_API_EXPORT int vlink_get(const vlink_getter_handle_t handle, uint8_t* data, size_t* size);

#ifdef __cplusplus
}
#endif

// NOLINTEND
