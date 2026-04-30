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

#include <doctest/doctest.h>

#include <chrono>
#include <string>
#include <thread>
#include <vector>

#include "./extension/discovery_reporter.h"
#include "./extension/discovery_viewer.h"

//
#include "../common_test.h"

// ---------------------------------------------------------------------------
// TEST SUITE: DiscoveryViewer - FilterType enum
// ---------------------------------------------------------------------------

TEST_SUITE("extension-DiscoveryViewer - FilterType enum") {
  TEST_CASE("kFilterNone == 0") { CHECK(static_cast<uint8_t>(DiscoveryViewer::kFilterNone) == 0); }

  TEST_CASE("kFilterAvailable == 1") { CHECK(static_cast<uint8_t>(DiscoveryViewer::kFilterAvailable) == 1); }

  TEST_CASE("kFilterNative == 2") { CHECK(static_cast<uint8_t>(DiscoveryViewer::kFilterNative) == 2); }
}

// ---------------------------------------------------------------------------
// TEST SUITE: DiscoveryViewer - static methods
// ---------------------------------------------------------------------------

TEST_SUITE("extension-DiscoveryViewer - static methods") {
  TEST_CASE("get_listen_address returns non-empty string") {
    const std::string addr = DiscoveryViewer::get_listen_address();
    CHECK(!addr.empty());
  }

  TEST_CASE("convert_type with empty string returns kUnknownImplType") {
    ImplType t = DiscoveryViewer::convert_type("");
    CHECK(t == kUnknownImplType);
  }

  TEST_CASE("convert_type Ser returns kServer") { CHECK(DiscoveryViewer::convert_type("Ser") == kServer); }

  TEST_CASE("convert_type Cli returns kClient") { CHECK(DiscoveryViewer::convert_type("Cli") == kClient); }

  TEST_CASE("convert_type Pub returns kPublisher") { CHECK(DiscoveryViewer::convert_type("Pub") == kPublisher); }

  TEST_CASE("convert_type Sub returns kSubscriber") { CHECK(DiscoveryViewer::convert_type("Sub") == kSubscriber); }

  TEST_CASE("convert_type Set returns kSetter") { CHECK(DiscoveryViewer::convert_type("Set") == kSetter); }

  TEST_CASE("convert_type Get returns kGetter") { CHECK(DiscoveryViewer::convert_type("Get") == kGetter); }

  TEST_CASE("convert_type unknown returns kUnknownImplType") {
    CHECK(DiscoveryViewer::convert_type("xyz") == kUnknownImplType);
  }

  TEST_CASE("convert_type_to_view kPublisher|kSubscriber") {
    CHECK(DiscoveryViewer::convert_type_to_view(kPublisher | kSubscriber) == "Pub|Sub");
  }

  TEST_CASE("convert_type_to_view kSetter|kGetter") {
    CHECK(DiscoveryViewer::convert_type_to_view(kSetter | kGetter) == "Set|Get");
  }

  TEST_CASE("convert_type_to_view kServer|kClient") {
    CHECK(DiscoveryViewer::convert_type_to_view(kServer | kClient) == "Ser|Cli");
  }

  TEST_CASE("convert_type_to_view kPublisher only") {
    CHECK(DiscoveryViewer::convert_type_to_view(kPublisher) == "Pub|---");
  }

  TEST_CASE("convert_type_to_view kSubscriber only") {
    CHECK(DiscoveryViewer::convert_type_to_view(kSubscriber) == "---|Sub");
  }

  TEST_CASE("convert_type_to_view kSetter only") { CHECK(DiscoveryViewer::convert_type_to_view(kSetter) == "Set|---"); }

  TEST_CASE("convert_type_to_view kGetter only") { CHECK(DiscoveryViewer::convert_type_to_view(kGetter) == "---|Get"); }

  TEST_CASE("convert_type_to_view kServer only") { CHECK(DiscoveryViewer::convert_type_to_view(kServer) == "Ser|---"); }

  TEST_CASE("convert_type_to_view kClient only") { CHECK(DiscoveryViewer::convert_type_to_view(kClient) == "---|Cli"); }

  TEST_CASE("convert_type_to_view kPublisher|kGetter") {
    CHECK(DiscoveryViewer::convert_type_to_view(kPublisher | kGetter) == "Pub|Get");
  }

  TEST_CASE("convert_type_to_view kSetter|kSubscriber") {
    CHECK(DiscoveryViewer::convert_type_to_view(kSetter | kSubscriber) == "Set|Sub");
  }

  TEST_CASE("convert_type_to_view kPublisher|kSetter") {
    CHECK(DiscoveryViewer::convert_type_to_view(kPublisher | kSetter) == "Pub|---");
  }

  TEST_CASE("convert_type_to_view kSubscriber|kGetter") {
    CHECK(DiscoveryViewer::convert_type_to_view(kSubscriber | kGetter) == "---|Sub");
  }

  TEST_CASE("convert_type_to_view unknown combination") {
    CHECK(DiscoveryViewer::convert_type_to_view(0) == "???|???");
  }

  TEST_CASE("convert_type_to_view with process_list - Pub|Sub") {
    std::vector<DiscoveryViewer::Process> procs;
    DiscoveryViewer::Process pub_proc;
    pub_proc.type = kPublisher;
    procs.push_back(pub_proc);

    DiscoveryViewer::Process sub_proc;
    sub_proc.type = kSubscriber;
    procs.push_back(sub_proc);

    std::string result = DiscoveryViewer::convert_type_to_view(kPublisher | kSubscriber, procs);
    CHECK(result == "Pub*1|Sub*1");
  }

  TEST_CASE("convert_type_to_view with process_list - many nodes use ~") {
    std::vector<DiscoveryViewer::Process> procs;
    for (int i = 0; i < 15; ++i) {
      DiscoveryViewer::Process p;
      p.type = kPublisher;
      procs.push_back(p);
    }

    std::string result = DiscoveryViewer::convert_type_to_view(kPublisher, procs);
    CHECK(result == "Pub*~|-----");
  }

  TEST_CASE("convert_type_to_view with process_list - Ser|Cli") {
    std::vector<DiscoveryViewer::Process> procs;
    DiscoveryViewer::Process srv;
    srv.type = kServer;
    procs.push_back(srv);
    procs.push_back(srv);

    DiscoveryViewer::Process cli;
    cli.type = kClient;
    procs.push_back(cli);

    std::string result = DiscoveryViewer::convert_type_to_view(kServer | kClient, procs);
    CHECK(result == "Ser*2|Cli*1");
  }

  TEST_CASE("convert_type_to_view with process_list - Set|Get") {
    std::vector<DiscoveryViewer::Process> procs;
    DiscoveryViewer::Process setter;
    setter.type = kSetter;
    procs.push_back(setter);

    DiscoveryViewer::Process getter;
    getter.type = kGetter;
    procs.push_back(getter);
    procs.push_back(getter);

    std::string result = DiscoveryViewer::convert_type_to_view(kSetter | kGetter, procs);
    CHECK(result == "Set*1|Get*2");
  }

  TEST_CASE("convert_type_to_view with process_list - Sub only") {
    std::vector<DiscoveryViewer::Process> procs;
    DiscoveryViewer::Process sub;
    sub.type = kSubscriber;
    procs.push_back(sub);
    procs.push_back(sub);
    procs.push_back(sub);

    std::string result = DiscoveryViewer::convert_type_to_view(kSubscriber, procs);
    CHECK(result == "-----|Sub*3");
  }

  TEST_CASE("convert_type_to_view with process_list - Setter only") {
    std::vector<DiscoveryViewer::Process> procs;
    DiscoveryViewer::Process setter;
    setter.type = kSetter;
    procs.push_back(setter);

    std::string result = DiscoveryViewer::convert_type_to_view(kSetter, procs);
    CHECK(result == "Set*1|-----");
  }

  TEST_CASE("convert_type_to_view with process_list - Getter only") {
    std::vector<DiscoveryViewer::Process> procs;
    DiscoveryViewer::Process getter;
    getter.type = kGetter;
    procs.push_back(getter);

    std::string result = DiscoveryViewer::convert_type_to_view(kGetter, procs);
    CHECK(result == "-----|Get*1");
  }

  TEST_CASE("convert_type_to_view with process_list - Server only") {
    std::vector<DiscoveryViewer::Process> procs;
    DiscoveryViewer::Process srv;
    srv.type = kServer;
    procs.push_back(srv);

    std::string result = DiscoveryViewer::convert_type_to_view(kServer, procs);
    CHECK(result == "Ser*1|-----");
  }

  TEST_CASE("convert_type_to_view with process_list - Client only") {
    std::vector<DiscoveryViewer::Process> procs;
    DiscoveryViewer::Process cli;
    cli.type = kClient;
    procs.push_back(cli);

    std::string result = DiscoveryViewer::convert_type_to_view(kClient, procs);
    CHECK(result == "-----|Cli*1");
  }

  TEST_CASE("convert_type_to_view with process_list - Pub|Get") {
    std::vector<DiscoveryViewer::Process> procs;
    DiscoveryViewer::Process pub;
    pub.type = kPublisher;
    procs.push_back(pub);

    DiscoveryViewer::Process get;
    get.type = kGetter;
    procs.push_back(get);

    std::string result = DiscoveryViewer::convert_type_to_view(kPublisher | kGetter, procs);
    CHECK(result == "Pub*1|Get*1");
  }

  TEST_CASE("convert_type_to_view with process_list - Set|Sub") {
    std::vector<DiscoveryViewer::Process> procs;
    DiscoveryViewer::Process setter;
    setter.type = kSetter;
    procs.push_back(setter);

    DiscoveryViewer::Process sub;
    sub.type = kSubscriber;
    procs.push_back(sub);

    std::string result = DiscoveryViewer::convert_type_to_view(kSetter | kSubscriber, procs);
    CHECK(result == "Set*1|Sub*1");
  }

  TEST_CASE("convert_type_to_view with process_list - unknown type") {
    std::vector<DiscoveryViewer::Process> procs;
    std::string result = DiscoveryViewer::convert_type_to_view(0, procs);
    CHECK(result == "?????|?????");
  }
}

// ---------------------------------------------------------------------------
// TEST SUITE: DiscoveryViewer::Process - ordering
// ---------------------------------------------------------------------------

TEST_SUITE("extension-DiscoveryViewer::Process - ordering") {
  TEST_CASE("operator< compares by host first") {
    DiscoveryViewer::Process a;
    a.host = "alpha";
    a.pid = 100;

    DiscoveryViewer::Process b;
    b.host = "beta";
    b.pid = 1;

    CHECK(a < b);
    CHECK(!(b < a));
  }

  TEST_CASE("same host: operator< compares by pid") {
    DiscoveryViewer::Process a;
    a.host = "host1";
    a.pid = 10;

    DiscoveryViewer::Process b;
    b.host = "host1";
    b.pid = 20;

    CHECK(a < b);
    CHECK(!(b < a));
  }

  TEST_CASE("default Process fields are zeroed") {
    DiscoveryViewer::Process p;
    CHECK(p.pid == 0);
    CHECK(p.type == 0);
    CHECK(p.profiler == doctest::Approx(-1.0));
    CHECK(p.host.empty());
    CHECK(p.name.empty());
    CHECK(p.ip.empty());
  }
}

// ---------------------------------------------------------------------------
// TEST SUITE: DiscoveryViewer::Info - ordering
// ---------------------------------------------------------------------------

TEST_SUITE("extension-DiscoveryViewer::Info - ordering") {
  TEST_CASE("operator< compares by sort_index first") {
    DiscoveryViewer::Info a;
    a.sort_index = 1;
    a.url = "intra://z";

    DiscoveryViewer::Info b;
    b.sort_index = 2;
    b.url = "intra://a";

    CHECK(a < b);
    CHECK(!(b < a));
  }

  TEST_CASE("same sort_index: operator< compares by url") {
    DiscoveryViewer::Info a;
    a.sort_index = 5;
    a.url = "intra://aaa";

    DiscoveryViewer::Info b;
    b.sort_index = 5;
    b.url = "intra://bbb";

    CHECK(a < b);
    CHECK(!(b < a));
  }

  TEST_CASE("default Info fields are zeroed/empty") {
    DiscoveryViewer::Info info;
    CHECK(info.sort_index == -1);
    CHECK(info.type == 0);
    CHECK(info.url.empty());
    CHECK(info.ser_type.empty());
    CHECK(info.process_list.empty());
  }
}

// ---------------------------------------------------------------------------
// TEST SUITE: DiscoveryViewer - construction and basic operations
// ---------------------------------------------------------------------------

TEST_SUITE("extension-DiscoveryViewer - construction") {
  TEST_CASE("default construction with kFilterNone does not throw") {
    DiscoveryViewer viewer(DiscoveryViewer::kFilterNone);
    CHECK(true);
  }

  TEST_CASE("construction with kFilterAvailable does not throw") {
    DiscoveryViewer viewer(DiscoveryViewer::kFilterAvailable);
    CHECK(true);
  }

  TEST_CASE("construction with kFilterNative does not throw") {
    DiscoveryViewer viewer(DiscoveryViewer::kFilterNative);
    CHECK(true);
  }

  TEST_CASE("get_info_list returns empty vector initially") {
    DiscoveryViewer viewer(DiscoveryViewer::kFilterNone);
    auto list = viewer.get_info_list();
    // Before any reporter has published, the list should be empty
    CHECK(list.empty());
  }

  TEST_CASE("get_ser_type for unknown URL returns empty string") {
    DiscoveryViewer viewer(DiscoveryViewer::kFilterNone);
    const std::string ser = viewer.get_ser_type("intra://nonexistent");
    CHECK(ser.empty());
  }

  TEST_CASE("register_callback does not throw") {
    DiscoveryViewer viewer(DiscoveryViewer::kFilterNone);
    viewer.register_callback([](const std::vector<DiscoveryViewer::Info>&) {});
    CHECK(true);
  }
}

// ---------------------------------------------------------------------------
// TEST SUITE: DiscoveryReporter - construction
// ---------------------------------------------------------------------------

TEST_SUITE("extension-DiscoveryReporter - construction") {
  TEST_CASE("default construction and destruction do not throw") {
    {
      DiscoveryReporter reporter;
    }
    CHECK(true);
  }

  TEST_CASE("global_get returns non-null singleton") {
    DiscoveryReporter* g = DiscoveryReporter::global_get();
    CHECK(g != nullptr);
  }

  TEST_CASE("global_get returns same pointer on repeated calls") {
    DiscoveryReporter* g1 = DiscoveryReporter::global_get();
    DiscoveryReporter* g2 = DiscoveryReporter::global_get();
    CHECK(g1 == g2);
  }
}

// ---------------------------------------------------------------------------
// TEST SUITE: DiscoveryViewer global_get
// ---------------------------------------------------------------------------

TEST_SUITE("extension-DiscoveryViewer - global_get") {
  TEST_CASE("global_get returns non-null singleton") {
    DiscoveryViewer* g = DiscoveryViewer::global_get();
    CHECK(g != nullptr);
  }

  TEST_CASE("global_get returns same pointer on repeated calls") {
    DiscoveryViewer* g1 = DiscoveryViewer::global_get();
    DiscoveryViewer* g2 = DiscoveryViewer::global_get();
    CHECK(g1 == g2);
  }
}

// NOLINTEND
