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
 * @file ssl_options.h
 * @brief Transport-layer SSL/TLS configuration for VLink communication backends.
 *
 * @details
 * @c SslOptions provides a backend-agnostic way to configure transport-layer
 * TLS encryption.  It works through the @c ssl.* property convention that
 * every transport backend reads during connection setup:
 *
 * | Backend    | Native TLS Mechanism                                         |
 * | ---------- | ------------------------------------------------------------ |
 * | MQTT       | @c MQTTClient_SSLOptions (Paho C), auto @c tcp:// to ssl://  |
 * | DDS        | @c TCPv4TransportDescriptor::tls_config (Fast-DDS)           |
 * | CycloneDDS | @c ddsi_config ssl fields (requires @c DDS_HAS_SSL)          |
 * | Zenoh      | @c transport/link/tls config keys (zenoh-c, not zenoh-pico)  |
 *
 * @par Property Keys
 * | Property Key         | @c SslOptions Field  | Description                                  |
 * | -------------------- | -------------------- | -------------------------------------------- |
 * | @c ssl.ca            | @c ca_file           | CA certificate file path (PEM)               |
 * | @c ssl.cert          | @c cert_file         | Client certificate file path (PEM)           |
 * | @c ssl.key           | @c key_file          | Client private key file path (PEM)           |
 * | @c ssl.key_password  | @c key_password      | Private key passphrase                       |
 * | @c ssl.verify        | @c verify_peer       | @c "0" to skip verification; default verify  |
 * | @c ssl.server_name   | @c server_name       | SNI server name override                     |
 * | @c ssl.ciphers       | @c ciphers           | Cipher suite string (OpenSSL format)         |
 *
 * @par Environment Variable Defaults
 * When a property is not set explicitly, the factory reads these environment
 * variables as lowest-priority defaults (property values always take precedence):
 *
 * | Environment Variable   | Maps to            |
 * | ---------------------- | ------------------ |
 * | @c VLINK_SSL_CA        | @c ssl.ca          |
 * | @c VLINK_SSL_CERT      | @c ssl.cert        |
 * | @c VLINK_SSL_KEY       | @c ssl.key         |
 * | @c VLINK_SSL_KEY_PASS  | @c ssl.key_password|
 * | @c VLINK_SSL_VERIFY    | @c ssl.verify      |
 * | @c VLINK_SSL_SNI       | @c ssl.server_name |
 * | @c VLINK_SSL_CIPHERS   | @c ssl.ciphers     |
 *
 * @par Auto-detection
 * SSL is considered valid (enabled) when @c ca_file or @c cert_file is
 * non-empty.  There is no separate @c ssl.enabled flag.  When SSL is enabled
 * on DDS/CycloneDDS, TCP transport is automatically activated because TLS
 * requires TCP.
 *
 * @par Usage
 * @code
 * // --- Via Node API ---
 * Publisher<MyMsg> pub("mqtt://sensor/data");
 * SslOptions ssl;
 * ssl.ca_file   = "/etc/certs/ca.pem";
 * ssl.cert_file = "/etc/certs/client.pem";
 * ssl.key_file  = "/etc/certs/client-key.pem";
 * pub.set_ssl_options(ssl);
 *
 * // --- Via set_property ---
 * pub.set_property("ssl.ca", "/etc/certs/ca.pem");
 *
 * // --- Via global property ---
 * MqttConf::set_global_property("ssl.ca", "/etc/certs/ca.pem");
 *
 * // --- Via environment variable ---
 * // export VLINK_SSL_CA=/etc/certs/ca.pem
 * @endcode
 *
 * @note
 * - Zenoh-pico (@c VLINK_ENABLE_ZENOH_PICO) does not support TLS; a warning
 *   is logged if SSL properties are present.
 * - CycloneDDS requires @c DDS_HAS_SSL at compile time; a warning is logged
 *   if SSL properties are present but the feature was not compiled in.
 */

#pragma once

#include <string>

#include "../base/macros.h"
#include "./conf.h"

namespace vlink {

/**
 * @struct SslOptions
 * @brief Aggregate of SSL/TLS settings for transport-layer encryption.
 *
 * @details
 * Populate the desired fields and pass to @c Node::set_ssl_options(), or
 * use @c parse_from() / @c parse_to() to convert between @c SslOptions and
 * the @c ssl.* entries in a @c Conf::PropertiesMap.
 *
 * @c is_valid() returns @c true when at least @c ca_file or @c cert_file is
 * set, which the transport backends interpret as "SSL is enabled".
 */
struct VLINK_EXPORT SslOptions final {
  /**
   * @brief Whether to verify the server certificate.
   *
   * @details
   * Defaults to @c true.  Set to @c false to skip peer verification
   * (maps to @c ssl.verify = @c "0").  This is useful for development
   * environments with self-signed certificates, but should remain @c true
   * in production.
   */
  bool verify_peer{true};

  /**
   * @brief Path to the CA certificate file (PEM format).
   *
   * @details
   * Used by all backends to verify the remote peer.  Setting this field
   * (or its corresponding property @c ssl.ca) is one of the two conditions
   * that makes @c is_valid() return @c true.
   */
  std::string ca_file;

  /**
   * @brief Path to the client certificate file (PEM format).
   *
   * @details
   * Required for mutual TLS (mTLS) where the server verifies the client.
   * Setting this field is the other condition that makes @c is_valid()
   * return @c true.
   */
  std::string cert_file;

  /**
   * @brief Path to the client private key file (PEM format).
   *
   * @details
   * Paired with @c cert_file for mutual TLS.  If the key is encrypted,
   * provide the passphrase via @c key_password.
   */
  std::string key_file;

  /**
   * @brief Passphrase for the encrypted private key.
   *
   * @details
   * Only needed when @c key_file is protected by a passphrase.
   * Corresponds to property @c ssl.key_password and environment variable
   * @c VLINK_SSL_KEY_PASS.
   */
  std::string key_password;

  /**
   * @brief Server Name Indication (SNI) override.
   *
   * @details
   * When set, the TLS handshake uses this value as the expected server
   * name instead of the hostname derived from the connection URL.
   * Used by MQTT, DDS, and Zenoh backends.
   */
  std::string server_name;

  /**
   * @brief Cipher suite string (OpenSSL format).
   *
   * @details
   * Overrides the default cipher suite selection.  The format depends on
   * the underlying TLS library (typically OpenSSL).  Leave empty to use
   * the backend default.
   */
  std::string ciphers;

  /**
   * @brief Default constructor; all strings are empty, @c verify_peer is @c true.
   */
  SslOptions() noexcept = default;

  /**
   * @brief Default destructor.
   */
  ~SslOptions() noexcept = default;

  /**
   * @brief Returns @c true when the configuration contains enough data to enable TLS.
   *
   * @details
   * TLS is considered valid when at least @c ca_file or @c cert_file is
   * non-empty.  An empty @c SslOptions (no certificates at all) is not valid
   * and the transport backend will not attempt a TLS connection.
   *
   * @return @c true if TLS should be enabled.
   */
  bool is_valid() const noexcept;

  /**
   * @brief Constructs an @c SslOptions by reading @c ssl.* entries from a property map.
   *
   * @details
   * Resolution order (highest priority first):
   * -# Explicit @c ssl.* entries in @p properties.
   * -# Environment variables (@c VLINK_SSL_CA, @c VLINK_SSL_CERT, etc.).
   *
   * Properties not present in either source retain their default values
   * (@c verify_peer = @c true, all strings empty).
   *
   * @param properties  The property map to read from.
   * @return            A fully-resolved @c SslOptions.
   */
  static SslOptions parse_from(const Conf::PropertiesMap& properties) noexcept;

  /**
   * @brief Writes the non-default fields back into a property map as @c ssl.* entries.
   *
   * @details
   * Only non-empty string fields and a @c false @c verify_peer are written.
   * This is the inverse of @c parse_from() and is used internally by
   * @c Node::set_ssl_options() to merge SSL settings into the node properties.
   *
   * @param properties  The property map to write into.
   */
  void parse_to(Conf::PropertiesMap& properties) const noexcept;
};

}  // namespace vlink
