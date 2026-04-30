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

// NOLINTBEGIN

#include "./impl/ssl_options.h"

#include <doctest/doctest.h>

#include <string>

//
#include "../common_test.h"

using namespace vlink;

// ---------------------------------------------------------------------------
// TEST SUITE: SslOptions - default construction
// ---------------------------------------------------------------------------

TEST_SUITE("impl-SslOptions") {
  TEST_CASE("ssl-options-default-construction") {
    SslOptions opts;

    CHECK(opts.verify_peer == true);
    CHECK(opts.ca_file.empty());
    CHECK(opts.cert_file.empty());
    CHECK(opts.key_file.empty());
    CHECK(opts.key_password.empty());
    CHECK(opts.server_name.empty());
    CHECK(opts.ciphers.empty());
    CHECK_FALSE(opts.is_valid());
  }

  // ---------------------------------------------------------------------------
  // TEST SUITE: SslOptions::is_valid()
  // ---------------------------------------------------------------------------

  TEST_CASE("ssl-options-is-valid") {
    SUBCASE("empty options are not valid") {
      SslOptions opts;
      CHECK_FALSE(opts.is_valid());
    }

    SUBCASE("ca_file alone makes it valid") {
      SslOptions opts;
      opts.ca_file = "/path/to/ca.pem";
      CHECK(opts.is_valid());
    }

    SUBCASE("cert_file alone makes it valid") {
      SslOptions opts;
      opts.cert_file = "/path/to/cert.pem";
      CHECK(opts.is_valid());
    }

    SUBCASE("both ca_file and cert_file make it valid") {
      SslOptions opts;
      opts.ca_file = "/ca.pem";
      opts.cert_file = "/cert.pem";
      CHECK(opts.is_valid());
    }

    SUBCASE("only key_file does not make it valid") {
      SslOptions opts;
      opts.key_file = "/key.pem";
      CHECK_FALSE(opts.is_valid());
    }

    SUBCASE("only verify_peer=false does not make it valid") {
      SslOptions opts;
      opts.verify_peer = false;
      CHECK_FALSE(opts.is_valid());
    }

    SUBCASE("only server_name does not make it valid") {
      SslOptions opts;
      opts.server_name = "example.com";
      CHECK_FALSE(opts.is_valid());
    }

    SUBCASE("only ciphers does not make it valid") {
      SslOptions opts;
      opts.ciphers = "ECDHE-RSA-AES256-GCM-SHA384";
      CHECK_FALSE(opts.is_valid());
    }
  }

  // ---------------------------------------------------------------------------
  // TEST SUITE: SslOptions::parse_from()
  // ---------------------------------------------------------------------------

  TEST_CASE("ssl-parse-from-empty-properties") {
    Conf::PropertiesMap props;
    auto opts = SslOptions::parse_from(props);

    CHECK_FALSE(opts.is_valid());
    CHECK(opts.verify_peer == true);
    CHECK(opts.ca_file.empty());
    CHECK(opts.cert_file.empty());
    CHECK(opts.key_file.empty());
    CHECK(opts.key_password.empty());
    CHECK(opts.server_name.empty());
    CHECK(opts.ciphers.empty());
  }

  TEST_CASE("ssl-parse-from-ca-only") {
    Conf::PropertiesMap props;
    props["ssl.ca"] = "/path/to/ca.pem";

    auto opts = SslOptions::parse_from(props);

    CHECK(opts.is_valid());
    CHECK(opts.ca_file == "/path/to/ca.pem");
    CHECK(opts.cert_file.empty());
    CHECK(opts.verify_peer == true);
  }

  TEST_CASE("ssl-parse-from-cert-only") {
    Conf::PropertiesMap props;
    props["ssl.cert"] = "/path/to/cert.pem";

    auto opts = SslOptions::parse_from(props);

    CHECK(opts.is_valid());
    CHECK(opts.cert_file == "/path/to/cert.pem");
    CHECK(opts.ca_file.empty());
  }

  TEST_CASE("ssl-parse-from-full-config") {
    Conf::PropertiesMap props;
    props["ssl.ca"] = "/ca.pem";
    props["ssl.cert"] = "/cert.pem";
    props["ssl.key"] = "/key.pem";
    props["ssl.key_password"] = "secret";
    props["ssl.verify"] = "0";
    props["ssl.server_name"] = "broker.example.com";
    props["ssl.ciphers"] = "ECDHE-RSA-AES256-GCM-SHA384";

    auto opts = SslOptions::parse_from(props);

    CHECK(opts.is_valid());
    CHECK(opts.ca_file == "/ca.pem");
    CHECK(opts.cert_file == "/cert.pem");
    CHECK(opts.key_file == "/key.pem");
    CHECK(opts.key_password == "secret");
    CHECK(opts.verify_peer == false);
    CHECK(opts.server_name == "broker.example.com");
    CHECK(opts.ciphers == "ECDHE-RSA-AES256-GCM-SHA384");
  }

  TEST_CASE("ssl-parse-from-verify-values") {
    SUBCASE("verify=1 means true") {
      Conf::PropertiesMap props;
      props["ssl.ca"] = "/ca.pem";
      props["ssl.verify"] = "1";
      auto opts = SslOptions::parse_from(props);
      CHECK(opts.verify_peer == true);
    }

    SUBCASE("verify=0 means false") {
      Conf::PropertiesMap props;
      props["ssl.ca"] = "/ca.pem";
      props["ssl.verify"] = "0";
      auto opts = SslOptions::parse_from(props);
      CHECK(opts.verify_peer == false);
    }

    SUBCASE("verify=true is treated as not-zero (true)") {
      Conf::PropertiesMap props;
      props["ssl.verify"] = "true";
      auto opts = SslOptions::parse_from(props);
      CHECK(opts.verify_peer == true);
    }

    SUBCASE("verify absent defaults to true") {
      Conf::PropertiesMap props;
      props["ssl.ca"] = "/ca.pem";
      auto opts = SslOptions::parse_from(props);
      CHECK(opts.verify_peer == true);
    }
  }

  TEST_CASE("ssl-parse-from-ignores-non-ssl-props") {
    Conf::PropertiesMap props;
    props["dds.tcp"] = "1";
    props["mqtt.broker"] = "tcp://localhost:1883";
    props["zenoh.mode"] = "peer";

    auto opts = SslOptions::parse_from(props);

    CHECK_FALSE(opts.is_valid());
    CHECK(opts.ca_file.empty());
  }

  TEST_CASE("ssl-parse-from-mixed-props") {
    Conf::PropertiesMap props;
    props["dds.tcp"] = "1";
    props["ssl.ca"] = "/ca.pem";
    props["zenoh.mode"] = "peer";

    auto opts = SslOptions::parse_from(props);

    CHECK(opts.is_valid());
    CHECK(opts.ca_file == "/ca.pem");
  }

  // ---------------------------------------------------------------------------
  // TEST SUITE: SslOptions::parse_to()
  // ---------------------------------------------------------------------------

  TEST_CASE("ssl-parse-to-empty-options") {
    SslOptions opts;
    Conf::PropertiesMap props;
    opts.parse_to(props);

    CHECK(props.empty());
  }

  TEST_CASE("ssl-parse-to-full-options") {
    SslOptions opts;
    opts.ca_file = "/ca.pem";
    opts.cert_file = "/cert.pem";
    opts.key_file = "/key.pem";
    opts.key_password = "pass";
    opts.verify_peer = false;
    opts.server_name = "sni.test";
    opts.ciphers = "AES256";

    Conf::PropertiesMap props;
    opts.parse_to(props);

    CHECK(props["ssl.ca"] == "/ca.pem");
    CHECK(props["ssl.cert"] == "/cert.pem");
    CHECK(props["ssl.key"] == "/key.pem");
    CHECK(props["ssl.key_password"] == "pass");
    CHECK(props["ssl.verify"] == "0");
    CHECK(props["ssl.server_name"] == "sni.test");
    CHECK(props["ssl.ciphers"] == "AES256");
  }

  TEST_CASE("ssl-parse-to-verify-true-not-written") {
    SslOptions opts;
    opts.verify_peer = true;
    opts.ca_file = "/ca.pem";

    Conf::PropertiesMap props;
    opts.parse_to(props);

    CHECK(props.find("ssl.verify") == props.end());
    CHECK(props["ssl.ca"] == "/ca.pem");
  }

  TEST_CASE("ssl-parse-to-partial-options") {
    SslOptions opts;
    opts.ca_file = "/ca.pem";
    opts.server_name = "example.com";

    Conf::PropertiesMap props;
    opts.parse_to(props);

    CHECK(props.size() == 2);
    CHECK(props["ssl.ca"] == "/ca.pem");
    CHECK(props["ssl.server_name"] == "example.com");
    CHECK(props.find("ssl.cert") == props.end());
    CHECK(props.find("ssl.key") == props.end());
    CHECK(props.find("ssl.key_password") == props.end());
    CHECK(props.find("ssl.ciphers") == props.end());
  }

  TEST_CASE("ssl-parse-to-preserves-existing-props") {
    SslOptions opts;
    opts.ca_file = "/ca.pem";

    Conf::PropertiesMap props;
    props["dds.tcp"] = "1";
    props["zenoh.mode"] = "peer";
    opts.parse_to(props);

    CHECK(props.size() == 3);
    CHECK(props["dds.tcp"] == "1");
    CHECK(props["zenoh.mode"] == "peer");
    CHECK(props["ssl.ca"] == "/ca.pem");
  }

  // ---------------------------------------------------------------------------
  // TEST SUITE: Round-trip parse_to -> parse_from
  // ---------------------------------------------------------------------------

  TEST_CASE("ssl-roundtrip-parse-to-from") {
    SslOptions original;
    original.ca_file = "/ca.pem";
    original.cert_file = "/cert.pem";
    original.key_file = "/key.pem";
    original.key_password = "secret";
    original.verify_peer = false;
    original.server_name = "broker.test";
    original.ciphers = "ECDHE-RSA-AES128";

    Conf::PropertiesMap props;
    original.parse_to(props);

    auto restored = SslOptions::parse_from(props);

    CHECK(restored.ca_file == original.ca_file);
    CHECK(restored.cert_file == original.cert_file);
    CHECK(restored.key_file == original.key_file);
    CHECK(restored.key_password == original.key_password);
    CHECK(restored.verify_peer == original.verify_peer);
    CHECK(restored.server_name == original.server_name);
    CHECK(restored.ciphers == original.ciphers);
    CHECK(restored.is_valid() == original.is_valid());
  }

  TEST_CASE("ssl-roundtrip-verify-true") {
    SslOptions original;
    original.ca_file = "/ca.pem";
    original.verify_peer = true;

    Conf::PropertiesMap props;
    original.parse_to(props);

    auto restored = SslOptions::parse_from(props);

    CHECK(restored.verify_peer == true);
  }
}

// NOLINTEND
