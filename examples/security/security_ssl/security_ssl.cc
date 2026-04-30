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

// Security SSL/TLS Example
// Demonstrates transport-layer TLS encryption via SslOptions:
//   1. SslOptions struct configuration (ca_file, cert_file, key_file)
//   2. Setting SSL via set_ssl_options() API
//   3. Setting SSL via set_property() key-value pairs
//   4. VLINK_SSL_* environment variables
//   5. MQTT with TLS example

#include <vlink/base/logger.h>
#include <vlink/impl/ssl_options.h>
#include <vlink/vlink.h>

#include <cstdlib>
#include <iostream>
#include <thread>

using namespace std::chrono_literals;  // NOLINT(build/namespaces, google-build-using-namespace)

int main() {
  // ======== Section 1: SslOptions Struct Configuration ========
  // SslOptions provides a backend-agnostic way to configure transport-layer TLS.
  // SSL is auto-detected as enabled when ca_file or cert_file is non-empty.
  {
    std::cout << "\n[1] SslOptions Struct Configuration" << std::endl;

    vlink::SslOptions ssl;

    // CA certificate - used to verify the remote peer
    ssl.ca_file = "/etc/certs/ca.pem";

    // Client certificate - for mutual TLS (mTLS)
    ssl.cert_file = "/etc/certs/client.pem";

    // Client private key - paired with cert_file
    ssl.key_file = "/etc/certs/client-key.pem";

    // Private key passphrase (if the key is encrypted)
    ssl.key_password = "my_key_passphrase";

    // Server Name Indication (SNI) override
    ssl.server_name = "broker.example.com";

    // Cipher suite string (OpenSSL format)
    ssl.ciphers = "ECDHE-RSA-AES256-GCM-SHA384:ECDHE-RSA-AES128-GCM-SHA256";

    // Peer verification (default: true)
    ssl.verify_peer = true;

    // Check if the SSL configuration is valid (enabled)
    std::cout << "  SslOptions is_valid: " << (ssl.is_valid() ? "true" : "false") << std::endl;
    std::cout << "  ca_file:      " << ssl.ca_file << std::endl;
    std::cout << "  cert_file:    " << ssl.cert_file << std::endl;
    std::cout << "  key_file:     " << ssl.key_file << std::endl;
    std::cout << "  key_password: " << ssl.key_password << std::endl;
    std::cout << "  server_name:  " << ssl.server_name << std::endl;
    std::cout << "  ciphers:      " << ssl.ciphers << std::endl;
    std::cout << "  verify_peer:  " << (ssl.verify_peer ? "true" : "false") << std::endl;
  }

  // ======== Section 2: set_ssl_options() API ========
  // Apply SslOptions to a node via set_ssl_options() before init().
  // The SslOptions are internally converted to ssl.* properties.
  {
    std::cout << "\n[2] Applying SSL via set_ssl_options() API" << std::endl;

    // NOTE: SSL/TLS requires a network transport such as mqtt://, dds://, or zenoh://.
    // intra:// does NOT support TLS (messages stay in-process).
    vlink::Publisher<std::string> pub("mqtt://security_ssl/demo", vlink::InitType::kWithoutInit);

    vlink::SslOptions ssl;
    ssl.ca_file = "/etc/certs/ca.pem";
    ssl.cert_file = "/etc/certs/client.pem";
    ssl.key_file = "/etc/certs/client-key.pem";

    // set_ssl_options() must be called BEFORE init()
    pub.set_ssl_options(ssl);

    // Now init() will pick up the SSL configuration
    pub.init();

    VLOG_I("Publisher created with SSL options (set_ssl_options)");
    VLOG_I("  SSL/TLS is active with mqtt:// transport.");
    VLOG_I("  Other supported transports: dds://, ddsc://, zenoh://.");
  }

  // ======== Section 3: SSL via set_property() ========
  // SSL can also be configured via individual property key-value pairs.
  // This is useful when loading configuration from files or command-line arguments.
  {
    std::cout << "\n[3] SSL via set_property() Key-Value Pairs" << std::endl;

    vlink::Publisher<std::string> pub("mqtt://security_ssl/props", vlink::InitType::kWithoutInit);

    // Set SSL properties individually
    pub.set_property("ssl.ca", "/etc/certs/ca.pem");
    pub.set_property("ssl.cert", "/etc/certs/client.pem");
    pub.set_property("ssl.key", "/etc/certs/client-key.pem");
    pub.set_property("ssl.key_password", "my_passphrase");
    pub.set_property("ssl.verify", "1");  // "0" to skip verification
    pub.set_property("ssl.server_name", "broker.example.com");
    pub.set_property("ssl.ciphers", "AES256-GCM-SHA384");

    pub.init();

    VLOG_I("Publisher created with SSL properties (set_property)");

    std::cout << "\n  Property Key Mapping:" << std::endl;
    std::cout << "  +---------------------+---------------------+" << std::endl;
    std::cout << "  | Property Key        | SslOptions Field    |" << std::endl;
    std::cout << "  +---------------------+---------------------+" << std::endl;
    std::cout << "  | ssl.ca              | ca_file             |" << std::endl;
    std::cout << "  | ssl.cert            | cert_file           |" << std::endl;
    std::cout << "  | ssl.key             | key_file            |" << std::endl;
    std::cout << "  | ssl.key_password    | key_password        |" << std::endl;
    std::cout << "  | ssl.verify          | verify_peer         |" << std::endl;
    std::cout << "  | ssl.server_name     | server_name         |" << std::endl;
    std::cout << "  | ssl.ciphers         | ciphers             |" << std::endl;
    std::cout << "  +---------------------+---------------------+" << std::endl;
  }

  // ======== Section 4: VLINK_SSL_* Environment Variables ========
  // SSL settings can be provided via environment variables as lowest-priority defaults.
  // Property values always take precedence over environment variables.
  {
    std::cout << "\n[4] VLINK_SSL_* Environment Variables" << std::endl;

    // Check current environment variables
    const char* env_vars[] = {"VLINK_SSL_CA",  "VLINK_SSL_CERT",    "VLINK_SSL_KEY",   "VLINK_SSL_KEY_PASS",
                              "VLINK_SSL_SNI", "VLINK_SSL_CIPHERS", "VLINK_SSL_VERIFY"};

    const char* descriptions[] = {"CA certificate file path",     "Client certificate file path",
                                  "Client private key file path", "Private key passphrase",
                                  "SNI server name override",     "Cipher suite string",
                                  "Peer verification (0=skip)"};

    for (int i = 0; i < 7; ++i) {
      const char* val = std::getenv(env_vars[i]);
      std::cout << "  " << env_vars[i] << " = " << (val ? val : "(not set)") << std::endl;
      std::cout << "    -> " << descriptions[i] << std::endl;
    }

    std::cout << "\n  Environment Variable Mapping:" << std::endl;
    std::cout << "  +------------------------+---------------------+" << std::endl;
    std::cout << "  | Environment Variable   | Maps to Property    |" << std::endl;
    std::cout << "  +------------------------+---------------------+" << std::endl;
    std::cout << "  | VLINK_SSL_CA           | ssl.ca              |" << std::endl;
    std::cout << "  | VLINK_SSL_CERT         | ssl.cert            |" << std::endl;
    std::cout << "  | VLINK_SSL_KEY          | ssl.key             |" << std::endl;
    std::cout << "  | VLINK_SSL_KEY_PASS     | ssl.key_password    |" << std::endl;
    std::cout << "  | VLINK_SSL_SNI          | ssl.server_name     |" << std::endl;
    std::cout << "  | VLINK_SSL_CIPHERS      | ssl.ciphers         |" << std::endl;
    std::cout << "  | VLINK_SSL_VERIFY       | ssl.verify          |" << std::endl;
    std::cout << "  +------------------------+---------------------+" << std::endl;
    std::cout << "\n  Priority: set_property() > set_ssl_options() > Environment Variables" << std::endl;
  }

  // ======== Section 5: MQTT with TLS Example ========
  // MQTT transport automatically upgrades tcp:// to ssl:// when SSL is configured.
  {
    std::cout << "\n[5] MQTT with TLS Example (conceptual)" << std::endl;
    std::cout << "   When using mqtt:// transport with SSL configured," << std::endl;
    std::cout << "   the connection is automatically upgraded to TLS." << std::endl;
    std::cout << std::endl;
    std::cout << "   Example code:" << std::endl;
    std::cout << "     Publisher<MyMsg> pub(\"mqtt://sensor/data\", InitType::kWithoutInit);" << std::endl;
    std::cout << "     vlink::SslOptions ssl;" << std::endl;
    std::cout << "     ssl.ca_file   = \"/etc/certs/ca.pem\";" << std::endl;
    std::cout << "     ssl.cert_file = \"/etc/certs/client.pem\";" << std::endl;
    std::cout << "     ssl.key_file  = \"/etc/certs/client-key.pem\";" << std::endl;
    std::cout << "     pub.set_ssl_options(ssl);" << std::endl;
    std::cout << "     pub.init();  // Connection uses TLS" << std::endl;
    std::cout << std::endl;
    std::cout << "   Backend TLS Support:" << std::endl;
    std::cout << "   +------------+--------------------------------------------+" << std::endl;
    std::cout << "   | Backend    | TLS Mechanism                              |" << std::endl;
    std::cout << "   +------------+--------------------------------------------+" << std::endl;
    std::cout << "   | MQTT       | MQTTClient_SSLOptions (Paho C)             |" << std::endl;
    std::cout << "   | DDS        | TCPv4TransportDescriptor::tls_config        |" << std::endl;
    std::cout << "   | CycloneDDS | ddsi_config ssl fields (DDS_HAS_SSL)        |" << std::endl;
    std::cout << "   | Zenoh      | transport/link/tls config keys              |" << std::endl;
    std::cout << "   +------------+--------------------------------------------+" << std::endl;
  }

  VLOG_I("Security SSL example complete.");
  return 0;
}
