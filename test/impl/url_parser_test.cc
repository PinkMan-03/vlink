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

#include "./impl/url_parser.h"

#include <doctest/doctest.h>

#include <map>
#include <stdexcept>
#include <string>

//
#include "../common_test.h"

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static const UrlParser::Category kH = UrlParser::Category::kHierarchical;
static const UrlParser::Category kN = UrlParser::Category::kNonHierarchical;
static const UrlParser::Separator kAmp = UrlParser::Separator::kAmpersand;
static const UrlParser::Separator kSemi = UrlParser::Separator::kSemicolon;

// ---------------------------------------------------------------------------
// TEST SUITE: Basic hierarchical parsing
// ---------------------------------------------------------------------------

TEST_SUITE("impl-UrlParser - basic hierarchical") {
  TEST_CASE("shm://vehicle/speed") {
    UrlParser p("shm://vehicle/speed");

    CHECK(p.get_transport() == "shm");
    CHECK(p.get_host() == "vehicle");
    CHECK(p.get_path() == "speed");
    CHECK(p.get_port() == 0);  // 0 = absent
    CHECK(p.get_query().empty());
    CHECK(p.get_fragment().empty());
    CHECK(p.get_username().empty());
    CHECK(p.get_password().empty());
    CHECK(p.get_category() == kH);
  }

  TEST_CASE("dds://topic/sub/path?key=value&key2=val2") {
    UrlParser p("dds://topic/sub/path?key=value&key2=val2");

    CHECK(p.get_transport() == "dds");
    CHECK(p.get_host() == "topic");
    CHECK(p.get_path() == "sub/path");

    const auto& dict = p.get_query_dictionary();
    REQUIRE(dict.count("key") == 1);
    REQUIRE(dict.count("key2") == 1);
    CHECK(dict.at("key") == "value");
    CHECK(dict.at("key2") == "val2");
  }

  TEST_CASE("someip://127.0.0.1:30490/0x1/method - port parsed correctly") {
    // Port appears after the colon following the host: transport://host:port/path
    UrlParser p("someip://127.0.0.1:30490/0x1/method");

    CHECK(p.get_transport() == "someip");
    CHECK(p.get_host() == "127.0.0.1");
    CHECK(p.get_port() == 30490);
    CHECK(!p.get_path().empty());
  }

  TEST_CASE("intra://test1?event=hello - query dict with single param") {
    UrlParser p("intra://test1?event=hello");

    CHECK(p.get_transport() == "intra");
    CHECK(p.get_host() == "test1");

    const auto& dict = p.get_query_dictionary();
    REQUIRE(dict.count("event") == 1);
    CHECK(dict.at("event") == "hello");
  }

  TEST_CASE("zenoh://domain/some/topic with no query or fragment") {
    UrlParser p("zenoh://domain/some/topic");

    CHECK(p.get_transport() == "zenoh");
    CHECK(p.get_host() == "domain");
    CHECK(p.get_path() == "some/topic");
    CHECK(p.get_query().empty());
    CHECK(p.get_fragment().empty());
    CHECK(p.get_query_dictionary().empty());
  }

  TEST_CASE("fdbus://my_service - host only, no path") {
    UrlParser p("fdbus://my_service");

    CHECK(p.get_transport() == "fdbus");
    CHECK(p.get_host() == "my_service");
  }
}

// ---------------------------------------------------------------------------
// TEST SUITE: Fragment and credential components
// ---------------------------------------------------------------------------

TEST_SUITE("impl-UrlParser - fragment and credentials") {
  TEST_CASE("url://host/path#fragment - fragment extracted correctly") {
    UrlParser p("url://host/path#fragment");

    CHECK(p.get_transport() == "url");
    CHECK(p.get_host() == "host");
    CHECK(p.get_path() == "path");
    CHECK(p.get_fragment() == "fragment");
  }

  TEST_CASE("transport://user:pass@host/path - username and password") {
    UrlParser p("transport://user:pass@host/path");

    CHECK(p.get_transport() == "transport");
    CHECK(p.get_username() == "user");
    CHECK(p.get_password() == "pass");
    CHECK(p.get_host() == "host");
    CHECK(p.get_path() == "path");
  }

  TEST_CASE("transport://user@host - username without password throws") {
    // The parser requires a colon-delimited password after the username;
    // a bare user@host (no colon before '@') is rejected with RuntimeError.
    CHECK_THROWS_AS(UrlParser("transport://user@host"), std::runtime_error);
  }

  TEST_CASE("full URL with all components") {
    UrlParser p("dds://admin:secret@192.168.1.1:7400/vehicle/speed?qos=reliable#section1");

    CHECK(p.get_transport() == "dds");
    CHECK(p.get_username() == "admin");
    CHECK(p.get_password() == "secret");
    CHECK(p.get_host() == "192.168.1.1");
    CHECK(p.get_port() == 7400);
    CHECK(p.get_path() == "vehicle/speed");
    CHECK(!p.get_query().empty());
    CHECK(p.get_fragment() == "section1");

    const auto& dict = p.get_query_dictionary();
    REQUIRE(dict.count("qos") == 1);
    CHECK(dict.at("qos") == "reliable");
  }
}

// ---------------------------------------------------------------------------
// TEST SUITE: Port behaviour
// ---------------------------------------------------------------------------

TEST_SUITE("impl-UrlParser - port handling") {
  TEST_CASE("get_port() returns 0 when no port specified") {
    UrlParser p("shm://topicname/path");
    CHECK(p.get_port() == 0);
  }

  TEST_CASE("port 0 is parsed as 0") {
    UrlParser p("someip://host:0/path");
    CHECK(p.get_port() == 0);
  }

  TEST_CASE("port 65535 boundary") {
    UrlParser p("someip://host:65535/path");
    CHECK(p.get_port() == 65535);
  }

  TEST_CASE("port 30490 as used in SOME/IP") {
    UrlParser p("someip://127.0.0.1:30490/my_service");
    CHECK(p.get_port() == 30490);
    CHECK(p.get_host() == "127.0.0.1");
  }
}

// ---------------------------------------------------------------------------
// TEST SUITE: Query separator variants
// ---------------------------------------------------------------------------

TEST_SUITE("impl-UrlParser - query separators") {
  TEST_CASE("kAmpersand separator (default): a=1&b=2") {
    UrlParser p("url://host/path?a=1&b=2", kH, kAmp);

    const auto& dict = p.get_query_dictionary();
    REQUIRE(dict.count("a") == 1);
    REQUIRE(dict.count("b") == 1);
    CHECK(dict.at("a") == "1");
    CHECK(dict.at("b") == "2");
  }

  TEST_CASE("kSemicolon separator: a=1;b=2") {
    UrlParser p("url://host/path?a=1;b=2", kH, kSemi);

    const auto& dict = p.get_query_dictionary();
    REQUIRE(dict.count("a") == 1);
    REQUIRE(dict.count("b") == 1);
    CHECK(dict.at("a") == "1");
    CHECK(dict.at("b") == "2");
  }

  TEST_CASE("kSemicolon separator with three params") {
    UrlParser p("url://host/path?x=10;y=20;z=30", kH, kSemi);

    const auto& dict = p.get_query_dictionary();
    CHECK(dict.size() == 3);
    CHECK(dict.at("x") == "10");
    CHECK(dict.at("y") == "20");
    CHECK(dict.at("z") == "30");
  }

  TEST_CASE("ampersand separator does not split on semicolon") {
    UrlParser p("url://host/path?a=1;b=2", kH, kAmp);

    const auto& dict = p.get_query_dictionary();
    // With ampersand separator, "a=1;b=2" is one token; key "a" maps to "1;b=2"
    CHECK(dict.size() == 1);
  }

  TEST_CASE("query key without value stored with empty string") {
    UrlParser p("url://host/path?flag", kH, kAmp);

    const auto& dict = p.get_query_dictionary();
    REQUIRE(dict.count("flag") == 1);
    CHECK(dict.at("flag").empty());
  }
}

// ---------------------------------------------------------------------------
// TEST SUITE: Non-hierarchical (opaque) URIs
// ---------------------------------------------------------------------------

TEST_SUITE("impl-UrlParser - non-hierarchical") {
  TEST_CASE("mailto:user@example.com") {
    UrlParser p("mailto:user@example.com", kN);

    CHECK(p.get_transport() == "mailto");
    CHECK(p.get_category() == kN);
    CHECK(!p.get_content().empty());
  }

  TEST_CASE("urn:isbn:978-3-16-148410-0") {
    UrlParser p("urn:isbn:978-3-16-148410-0", kN);

    CHECK(p.get_transport() == "urn");
    CHECK(p.get_category() == kN);
  }

  TEST_CASE("category is stored correctly for hierarchical") {
    UrlParser p("shm://topic", kH);
    CHECK(p.get_category() == kH);
  }

  TEST_CASE("category is stored correctly for non-hierarchical") {
    UrlParser p("data:text/plain,hello", kN);
    CHECK(p.get_category() == kN);
  }
}

// ---------------------------------------------------------------------------
// TEST SUITE: to_string round-trip
// ---------------------------------------------------------------------------

TEST_SUITE("impl-UrlParser - to_string round-trip") {
  TEST_CASE("simple URL reconstructed") {
    const std::string url = "shm://vehicle/speed";
    UrlParser p(url);
    const std::string reconstructed = p.to_string();

    // Transport and path must survive round-trip
    CHECK(!reconstructed.empty());
    CHECK(reconstructed.find("shm") != std::string::npos);
    CHECK(reconstructed.find("vehicle") != std::string::npos);
    CHECK(reconstructed.find("speed") != std::string::npos);
  }

  TEST_CASE("URL with query reconstructed") {
    UrlParser p("dds://host/path?key=value");
    const std::string s = p.to_string();

    CHECK(s.find("dds") != std::string::npos);
    CHECK(s.find("key") != std::string::npos);
    CHECK(s.find("value") != std::string::npos);
  }

  TEST_CASE("URL with fragment reconstructed") {
    UrlParser p("url://host/path#frag");
    const std::string s = p.to_string();

    CHECK(s.find("frag") != std::string::npos);
  }

  TEST_CASE("to_string returns non-empty for minimal URL") {
    UrlParser p("intra://topic");
    CHECK(!p.to_string().empty());
  }
}

// ---------------------------------------------------------------------------
// TEST SUITE: Component-map constructor
// ---------------------------------------------------------------------------

TEST_SUITE("impl-UrlParser - component-map constructor") {
  TEST_CASE("construct from explicit component map") {
    std::map<UrlParser::Component, std::string> comps;
    comps[UrlParser::Component::kTransport] = "dds";
    comps[UrlParser::Component::kHost] = "my_host";
    comps[UrlParser::Component::kPath] = "my/path";

    UrlParser p(comps, kH, false);

    CHECK(p.get_transport() == "dds");
    CHECK(p.get_host() == "my_host");
  }

  TEST_CASE("construct with replacement - transport override") {
    UrlParser original("shm://vehicle/speed");
    std::map<UrlParser::Component, std::string> repl;
    repl[UrlParser::Component::kTransport] = "dds";

    UrlParser modified(original, repl);
    CHECK(modified.get_transport() == "dds");
    CHECK(modified.get_host() == original.get_host());
    CHECK(modified.get_path() == original.get_path());
  }
}

// ---------------------------------------------------------------------------
// TEST SUITE: std::string constructor
// ---------------------------------------------------------------------------

TEST_SUITE("impl-UrlParser - string constructor") {
  TEST_CASE("construct from std::string") {
    std::string url = "zenoh://robot/lidar/scan";
    UrlParser p(url);

    CHECK(p.get_transport() == "zenoh");
    CHECK(p.get_host() == "robot");
    CHECK(p.get_path() == "lidar/scan");
  }

  TEST_CASE("construct from const char*") {
    UrlParser p("intra://test_topic");

    CHECK(p.get_transport() == "intra");
    CHECK(p.get_host() == "test_topic");
  }
}

// ---------------------------------------------------------------------------
// TEST SUITE: Edge cases
// ---------------------------------------------------------------------------

TEST_SUITE("impl-UrlParser - edge cases") {
  TEST_CASE("multiple path segments") {
    UrlParser p("dds://ns/a/b/c/d");

    CHECK(p.get_transport() == "dds");
    CHECK(p.get_host() == "ns");
    CHECK(p.get_path() == "a/b/c/d");
  }

  TEST_CASE("URL with only transport and host - empty path") {
    UrlParser p("intra://mytopic");

    CHECK(p.get_transport() == "intra");
    CHECK(p.get_host() == "mytopic");
    CHECK(p.get_port() == 0);
  }

  TEST_CASE("query dict is empty when no query string") {
    UrlParser p("shm://host/path");
    CHECK(p.get_query_dictionary().empty());
  }

  TEST_CASE("fragment is empty when not present") {
    UrlParser p("shm://host/path?q=1");
    CHECK(p.get_fragment().empty());
  }

  TEST_CASE("IP address as host") {
    UrlParser p("dds://192.168.0.1/topic");

    CHECK(p.get_transport() == "dds");
    CHECK(p.get_host() == "192.168.0.1");
  }

  TEST_CASE("host:port without path") {
    UrlParser p("someip://localhost:8080");

    CHECK(p.get_host() == "localhost");
    CHECK(p.get_port() == 8080);
  }

  TEST_CASE("transport only - no authority") {
    UrlParser p("intra://");
    CHECK(p.get_transport() == "intra");
    CHECK(p.get_host().empty());
  }

  TEST_CASE("path with dots") {
    UrlParser p("dds://ns/path.with.dots");
    CHECK(p.get_transport() == "dds");
    CHECK(p.get_host() == "ns");
    CHECK(p.get_path() == "path.with.dots");
  }

  TEST_CASE("path with hyphens and underscores") {
    UrlParser p("shm://host/my-topic_name");
    CHECK(p.get_path() == "my-topic_name");
  }

  TEST_CASE("query with multiple values for same separator") {
    UrlParser p("dds://host/path?a=1&b=2&c=3&d=4", kH, kAmp);
    const auto& dict = p.get_query_dictionary();
    CHECK(dict.size() == 4);
    CHECK(dict.at("a") == "1");
    CHECK(dict.at("d") == "4");
  }

  TEST_CASE("query and fragment together") {
    UrlParser p("dds://host/path?key=val#frag");
    CHECK(p.get_query_dictionary().at("key") == "val");
    CHECK(p.get_fragment() == "frag");
  }

  TEST_CASE("very long path") {
    std::string long_path;
    for (int i = 0; i < 50; ++i) {
      if (i > 0) {
        long_path += "/";
      }
      long_path += "segment" + std::to_string(i);
    }
    UrlParser p("dds://host/" + long_path);
    CHECK(p.get_transport() == "dds");
    CHECK(p.get_host() == "host");
    CHECK(!p.get_path().empty());
  }
}

// ---------------------------------------------------------------------------
// TEST SUITE: UrlParser - content accessor
// ---------------------------------------------------------------------------

TEST_SUITE("impl-UrlParser - content") {
  TEST_CASE("get_content throws for hierarchical URL") {
    UrlParser p("dds://host/path");
    CHECK_THROWS((void)p.get_content());
  }

  TEST_CASE("get_content for non-hierarchical URL") {
    UrlParser p("mailto:user@example.com", kN);
    CHECK(!p.get_content().empty());
  }
}

// ---------------------------------------------------------------------------
// TEST SUITE: UrlParser - replacement constructor additional
// ---------------------------------------------------------------------------

TEST_SUITE("impl-UrlParser - replacement constructor") {
  TEST_CASE("replace host") {
    UrlParser original("dds://old_host/path");
    std::map<UrlParser::Component, std::string> repl;
    repl[UrlParser::Component::kHost] = "new_host";

    UrlParser modified(original, repl);
    CHECK(modified.get_host() == "new_host");
    CHECK(modified.get_transport() == "dds");
  }

  TEST_CASE("replace query") {
    UrlParser original("dds://host/path?old=value");
    std::map<UrlParser::Component, std::string> repl;
    repl[UrlParser::Component::kQuery] = "new=data";

    UrlParser modified(original, repl);
    CHECK(modified.get_query() == "new=data");
  }

  TEST_CASE("replace fragment") {
    UrlParser original("dds://host/path#old_frag");
    std::map<UrlParser::Component, std::string> repl;
    repl[UrlParser::Component::kFragment] = "new_frag";

    UrlParser modified(original, repl);
    CHECK(modified.get_fragment() == "new_frag");
  }
}

// ---------------------------------------------------------------------------
// TEST SUITE: UrlParser - component-map constructor additional
// ---------------------------------------------------------------------------

TEST_SUITE("impl-UrlParser - component-map additional") {
  TEST_CASE("construct with query component") {
    std::map<UrlParser::Component, std::string> comps;
    comps[UrlParser::Component::kTransport] = "intra";
    comps[UrlParser::Component::kHost] = "host";
    comps[UrlParser::Component::kPath] = "/topic";
    comps[UrlParser::Component::kQuery] = "key=value";

    UrlParser p(comps, kH, false);
    CHECK(p.get_transport() == "intra");
    CHECK(p.get_query() == "key=value");
  }

  TEST_CASE("construct with port component") {
    std::map<UrlParser::Component, std::string> comps;
    comps[UrlParser::Component::kTransport] = "someip";
    comps[UrlParser::Component::kHost] = "127.0.0.1";
    comps[UrlParser::Component::kPort] = "9090";
    comps[UrlParser::Component::kPath] = "/service";

    UrlParser p(comps, kH, false);
    CHECK(p.get_transport() == "someip");
    CHECK(p.get_host() == "127.0.0.1");
    CHECK(p.get_port() == 9090);
  }

  TEST_CASE("construct with fragment component") {
    std::map<UrlParser::Component, std::string> comps;
    comps[UrlParser::Component::kTransport] = "fdbus";
    comps[UrlParser::Component::kHost] = "svc";
    comps[UrlParser::Component::kPath] = "/path";
    comps[UrlParser::Component::kFragment] = "ipc";

    UrlParser p(comps, kH, false);
    CHECK(p.get_fragment() == "ipc");
  }
}

// NOLINTEND
