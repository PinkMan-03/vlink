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

#include "./common_test.h"

#if defined(VLINK_SUPPORT_SOMEIP)

#include <atomic>
#include <future>
#include <string>
#include <thread>

#include "./modules/someip_conf.h"

// ---------------------------------------------------------------------------
// Someip - init
// ---------------------------------------------------------------------------

TEST_SUITE("someip-init") {
  TEST_CASE("rpc-conf-defaults") {
    SomeipConf conf(0x0001, 0x0001, 0x0001);

    CHECK(conf.service == 0x0001);
    CHECK(conf.instance == 0x0001);
    CHECK(conf.method == 0x0001);
    CHECK(conf.groups.empty());
    CHECK(conf.event == 0);
    CHECK(conf.field == false);
    CHECK(conf.get_transport_type() == TransportType::kSomeip);
  }

  TEST_CASE("event-conf-defaults") {
    SomeipConf conf(0x0002, 0x0001, SomeipConf::Groups{0x0001}, 0x0010);

    CHECK(conf.service == 0x0002);
    CHECK(conf.instance == 0x0001);
    CHECK(conf.method == 0);
    CHECK(conf.groups == SomeipConf::Groups{0x0001});
    CHECK(conf.event == 0x0010);
    CHECK(conf.field == false);
  }

  TEST_CASE("field-conf-flag") {
    SomeipConf conf(0x0003, 0x0001, SomeipConf::Groups{0x0001}, 0x0010, true);

    CHECK(conf.field == true);
  }

  TEST_CASE("multi-group-conf") {
    SomeipConf::Groups grps{0x0001, 0x0002, 0x0003};
    SomeipConf conf(0x0004, 0x0001, grps, 0x0020);

    CHECK(conf.groups.size() == 3);
    CHECK(conf.groups.count(0x0002) == 1);
  }

  TEST_CASE("rpc-conf-equality") {
    SomeipConf a(0x1234, 0x5678, 0x0001);
    SomeipConf b(0x1234, 0x5678, 0x0001);
    SomeipConf c(0x1234, 0x5678, 0x0002);

    CHECK(a == b);
    CHECK(a != c);
  }

  TEST_CASE("event-conf-equality") {
    SomeipConf a(0x1234, 0x5678, SomeipConf::Groups{0x0001}, 0x0010);
    SomeipConf b(0x1234, 0x5678, SomeipConf::Groups{0x0001}, 0x0010);
    SomeipConf c(0x1234, 0x5678, SomeipConf::Groups{0x0001}, 0x0020);

    CHECK(a == b);
    CHECK(a != c);
  }

  TEST_CASE("field-equality-differs-from-event") {
    SomeipConf a(0x1234, 0x5678, SomeipConf::Groups{0x0001}, 0x0010, false);
    SomeipConf b(0x1234, 0x5678, SomeipConf::Groups{0x0001}, 0x0010, true);

    CHECK(a != b);
  }

  TEST_CASE("url-parse-rpc-server") {
    // service=0x1234(4660), instance=0x5678(22136), method=1
    Url url("someip://4660/22136?method=1");

    CHECK(url.parse(kServer));
    CHECK(url.parse(kClient));
  }

  TEST_CASE("url-parse-event-pub-sub") {
    // service=4660, instance=22136, groups=1, event=16
    Url url("someip://4660/22136?groups=1&event=16");

    CHECK(url.parse(kPublisher));
    CHECK(url.parse(kSubscriber));
  }

  TEST_CASE("url-parse-field") {
    Url url("someip://4660/22136?groups=1&event=16&field=1");

    CHECK(url.parse(kSetter));
    CHECK(url.parse(kGetter));
  }

  TEST_CASE("invalid-transport-throws") { CHECK_THROWS(Publisher<int>("someip1://bad/url")); }
}

// ---------------------------------------------------------------------------
// Someip - event
// ---------------------------------------------------------------------------

TEST_SUITE("someip-event") {
  TEST_CASE("someip-event-pub-sub") {
    std::atomic<bool> received{false};
    Bytes captured;

    // service=0x1001(4097), instance=1, groups={1}, event=1
    Publisher<Bytes> pub(SomeipConf(0x1001, 0x0001, SomeipConf::Groups{0x0001}, 0x0001));
    Subscriber<Bytes> sub("someip://4097/1?groups=1&event=1");

    sub.listen([&](const Bytes& data) {
      captured = data;
      received.store(true, std::memory_order_release);
    });

    CHECK(pub.wait_for_subscribers(5s));
    CHECK(pub.has_subscribers());

    std::this_thread::sleep_for(10ms);

    Bytes payload{0xAB, 0xCD, 0xEF};
    CHECK(pub.publish(payload));

    for (int i = 0; i < 100 && !received.load(std::memory_order_acquire); ++i) {
      std::this_thread::sleep_for(50ms);
    }

    CHECK(received.load(std::memory_order_acquire));
    REQUIRE(captured.size() == 3);
    CHECK(captured[0] == 0xAB);
    CHECK(captured[2] == 0xEF);
  }

  TEST_CASE("someip-event-string") {
    std::atomic<bool> received{false};
    std::string captured;

    Publisher<std::string> pub(SomeipConf(0x1002, 0x0001, SomeipConf::Groups{0x0001}, 0x0001));
    Subscriber<std::string> sub("someip://4098/1?groups=1&event=1");

    sub.listen([&](const std::string& val) {
      captured = val;
      received.store(true, std::memory_order_release);
    });

    CHECK(pub.wait_for_subscribers(5s));

    std::this_thread::sleep_for(10ms);

    CHECK(pub.publish(std::string("hello_someip")));

    for (int i = 0; i < 100 && !received.load(std::memory_order_acquire); ++i) {
      std::this_thread::sleep_for(50ms);
    }

    CHECK(received.load(std::memory_order_acquire));
    CHECK(captured == "hello_someip");
  }

  TEST_CASE("someip-event-multi-pub") {
    std::atomic<int> count{0};

    Publisher<int> pub(SomeipConf(0x1003, 0x0001, SomeipConf::Groups{0x0001}, 0x0001));
    Subscriber<int> sub("someip://4099/1?groups=1&event=1");

    sub.listen([&](const int& /*v*/) { count.fetch_add(1, std::memory_order_relaxed); });

    CHECK(pub.wait_for_subscribers(5s));

    std::this_thread::sleep_for(10ms);

    for (int i = 0; i < 10; ++i) {
      pub.publish(i);
      std::this_thread::sleep_for(20ms);
    }

    std::this_thread::sleep_for(1000ms);

    CHECK(count.load() >= 10);
  }

  TEST_CASE("someip-event-multi-sub") {
    std::atomic<int> count1{0};
    std::atomic<int> count2{0};

    Publisher<Bytes> pub(SomeipConf(0x1004, 0x0001, SomeipConf::Groups{0x0001}, 0x0001));
    Subscriber<Bytes> sub1("someip://4100/1?groups=1&event=1");
    Subscriber<Bytes> sub2("someip://4100/1?groups=1&event=1");

    sub1.listen([&](const Bytes& /*d*/) { count1.fetch_add(1, std::memory_order_relaxed); });
    sub2.listen([&](const Bytes& /*d*/) { count2.fetch_add(1, std::memory_order_relaxed); });

    CHECK(pub.wait_for_subscribers(5s));

    std::this_thread::sleep_for(10ms);

    for (int i = 0; i < 3; ++i) {
      pub.publish(Bytes{static_cast<uint8_t>(i)});
      std::this_thread::sleep_for(30ms);
    }

    std::this_thread::sleep_for(300ms);

    CHECK(count1.load() >= 3);
    CHECK(count2.load() >= 3);
  }

  TEST_CASE("someip-event-force-publish") {
    Publisher<Bytes> pub(SomeipConf(0x1005, 0x0001, SomeipConf::Groups{0x0001}, 0x0001));

    CHECK(!pub.has_subscribers());

    for (int i = 0; i < 5; ++i) {
      CHECK(pub.publish(Bytes{static_cast<uint8_t>(i)}, true));
    }
  }

  TEST_CASE("someip-event-detect-subscribers") {
    std::atomic<int> connected_count{0};

    Publisher<Bytes> pub(SomeipConf(0x1006, 0x0001, SomeipConf::Groups{0x0001}, 0x0001));

    pub.detect_subscribers([&](bool connected) {
      if (connected) {
        connected_count.fetch_add(1, std::memory_order_relaxed);
      }
    });

    {
      Subscriber<Bytes> sub("someip://4102/1?groups=1&event=1");
      sub.listen([](const Bytes& /*d*/) {});

      std::this_thread::sleep_for(500ms);
      CHECK(pub.has_subscribers());
    }

    std::this_thread::sleep_for(500ms);
    CHECK(!pub.has_subscribers());
  }
}

// ---------------------------------------------------------------------------
// Someip - method
// ---------------------------------------------------------------------------

TEST_SUITE("someip-method") {
  TEST_CASE("someip-method-send") {
    std::atomic<int> counter{0};

    // service=0x2001(8193), instance=1, method=1
    Server<std::string> server(SomeipConf(0x2001, 0x0001, 0x0001));
    server.listen([&](const std::string& /*req*/) { counter.fetch_add(1, std::memory_order_relaxed); });

    Client<std::string> client("someip://8193/1?method=1");
    CHECK(client.wait_for_connected(5s));
    CHECK(client.is_connected());

    CHECK(client.send("fire1"));
    std::this_thread::sleep_for(200ms);
    CHECK(counter.load() == 1);

    CHECK(client.send("fire2"));
    std::this_thread::sleep_for(200ms);
    CHECK(counter.load() == 2);
  }

  TEST_CASE("someip-method-invoke") {
    Server<std::string, std::string> server(SomeipConf(0x2002, 0x0001, 0x0001));
    server.listen([](const std::string& req, std::string& resp) { resp = "someip:" + req; });

    Client<std::string, std::string> client("someip://8194/1?method=1");
    CHECK(client.wait_for_connected(5s));

    SUBCASE("sync-optional") {
      auto resp = client.invoke("ping");
      CHECK(resp.has_value());
      CHECK(*resp == "someip:ping");
    }

    SUBCASE("sync-ref-overload") {
      std::string out;
      CHECK(client.invoke("pong", out, 5s));
      CHECK(out == "someip:pong");
    }

    SUBCASE("async-future") {
      auto fut = client.async_invoke("async");
      REQUIRE(fut.wait_for(5s) == std::future_status::ready);
      CHECK(fut.get() == "someip:async");
    }

    // SUBCASE("multiple-sequential") {
    //   for (int i = 0; i < 5; ++i) {
    //     auto resp = client.invoke("r" + std::to_string(i));
    //     CHECK(resp.has_value());
    //     CHECK(*resp == "someip:r" + std::to_string(i));
    //   }
    // }
  }

  // TEST_CASE("someip-method-async-reply") {
  //   std::atomic<uint64_t> saved_id{0};
  //   std::atomic<bool> req_received{false};

  //   Server<std::string, std::string> server(SomeipConf(0x2003, 0x0001, 0x0001));
  //   server.listen_for_reply([&](uint64_t req_id, const std::string& /*req*/) {
  //     saved_id.store(req_id, std::memory_order_release);
  //     req_received.store(true, std::memory_order_release);
  //   });

  //   Client<std::string, std::string> client("someip://8195/1?method=1");
  //   CHECK(client.wait_for_connected(5s));

  //   auto fut = client.async_invoke("defer");

  //   for (int i = 0; i < 100 && !req_received.load(std::memory_order_acquire); ++i) {
  //     std::this_thread::sleep_for(50ms);
  //   }

  //   REQUIRE(req_received.load(std::memory_order_acquire));
  //   CHECK(server.reply(saved_id.load(), std::string("deferred_someip")));

  //   REQUIRE(fut.wait_for(5s) == std::future_status::ready);
  //   CHECK(fut.get() == "deferred_someip");
  // }

  TEST_CASE("someip-method-async-callback") {
    Server<std::string, std::string> server(SomeipConf(0x2004, 0x0001, 0x0001));
    server.listen([](const std::string& /*req*/, std::string& resp) { resp = "someip_cb"; });

    Client<std::string, std::string> client("someip://8196/1?method=1");
    CHECK(client.wait_for_connected(5s));

    std::atomic<bool> got{false};
    std::string resp_val;

    bool ok = client.invoke("msg", [&](const std::string& resp) {
      resp_val = resp;
      got.store(true, std::memory_order_release);
    });

    CHECK(ok);

    for (int i = 0; i < 100 && !got.load(std::memory_order_acquire); ++i) {
      std::this_thread::sleep_for(50ms);
    }

    CHECK(got.load(std::memory_order_acquire));
    CHECK(resp_val == "someip_cb");
  }

  TEST_CASE("someip-method-detect-connected") {
    std::atomic<bool> conn_flag{false};

    Server<std::string, std::string> server(SomeipConf(0x2005, 0x0001, 0x0001));
    server.listen([](const std::string& /*req*/, std::string& resp) { resp = "ok"; });

    Client<std::string, std::string> client("someip://8197/1?method=1");
    client.detect_connected([&](bool connected) { conn_flag.store(connected, std::memory_order_release); });

    CHECK(client.wait_for_connected(5s));
    std::this_thread::sleep_for(200ms);
    CHECK(conn_flag.load(std::memory_order_acquire));
  }
}

// ---------------------------------------------------------------------------
// Someip - field
// ---------------------------------------------------------------------------

TEST_SUITE("someip-field") {
  TEST_CASE("someip-field-setter-getter") {
    SUBCASE("polling-get") {
      // service=0x3001(12289), instance=1, groups={1}, event=0x0010(16), field=true
      Setter<Bytes> setter(SomeipConf(0x3001, 0x0001, SomeipConf::Groups{0x0001}, 0x0010, true));
      Getter<Bytes> getter("someip://12289/1?groups=1&event=16&field=1");

      setter.set(Bytes{0x13, 0x37});
      std::this_thread::sleep_for(300ms);

      auto v = getter.get();
      REQUIRE(v.has_value());
      REQUIRE(v->size() == 2);
      CHECK((*v)[0] == 0x13);
      CHECK((*v)[1] == 0x37);
    }

    SUBCASE("wait-for-value") {
      Setter<Bytes> setter(SomeipConf(0x3002, 0x0001, SomeipConf::Groups{0x0001}, 0x0010, true));
      Getter<Bytes> getter("someip://12290/1?groups=1&event=16&field=1");

      std::thread writer([&] {
        std::this_thread::sleep_for(100ms);
        setter.set(Bytes{0xF0, 0x0D});
      });

      CHECK(getter.wait_for_value(5s));
      auto v = getter.get();
      REQUIRE(v.has_value());
      CHECK((*v)[0] == 0xF0);

      writer.join();
    }

    SUBCASE("listen-callback") {
      std::atomic<bool> notified{false};

      Setter<Bytes> setter(SomeipConf(0x3003, 0x0001, SomeipConf::Groups{0x0001}, 0x0010, true));
      Getter<Bytes> getter("someip://12291/1?groups=1&event=16&field=1");

      getter.listen([&](const Bytes& /*val*/) { notified.store(true, std::memory_order_release); });

      std::this_thread::sleep_for(100ms);
      setter.set(Bytes{0x42});

      for (int i = 0; i < 100 && !notified.load(std::memory_order_acquire); ++i) {
        std::this_thread::sleep_for(50ms);
      }

      CHECK(notified.load(std::memory_order_acquire));
    }

    SUBCASE("change-reporting") {
      std::atomic<int> cb_count{0};

      Setter<Bytes> setter(SomeipConf(0x3004, 0x0001, SomeipConf::Groups{0x0001}, 0x0010, true));
      Getter<Bytes> getter("someip://12292/1?groups=1&event=16&field=1");

      getter.set_change_reporting(true);
      getter.listen([&](const Bytes& /*v*/) { cb_count.fetch_add(1, std::memory_order_relaxed); });

      std::this_thread::sleep_for(100ms);

      setter.set(Bytes{0x5A});
      std::this_thread::sleep_for(200ms);
      setter.set(Bytes{0x5A});
      std::this_thread::sleep_for(200ms);

      CHECK(cb_count.load() <= 1);
    }

    SUBCASE("late-getter-receives-cached-value") {
      Setter<Bytes> setter(SomeipConf(0x3005, 0x0001, SomeipConf::Groups{0x0001}, 0x0010, true));
      setter.set(Bytes{0xC0, 0xDE});
      std::this_thread::sleep_for(200ms);

      Getter<Bytes> late_getter("someip://12293/1?groups=1&event=16&field=1");
      CHECK(late_getter.wait_for_value(5s));
      auto v = late_getter.get();
      REQUIRE(v.has_value());
      CHECK((*v)[0] == 0xC0);
    }

    SUBCASE("multi-set-sequence") {
      std::atomic<int> update_count{0};

      Setter<Bytes> setter(SomeipConf(0x3006, 0x0001, SomeipConf::Groups{0x0001}, 0x0010, true));
      Getter<Bytes> getter("someip://12294/1?groups=1&event=16&field=1");

      getter.listen([&](const Bytes& /*v*/) { update_count.fetch_add(1, std::memory_order_relaxed); });

      std::this_thread::sleep_for(100ms);

      for (int i = 0; i < 5; ++i) {
        setter.set(Bytes{static_cast<uint8_t>(i), static_cast<uint8_t>(i + 1)});
        std::this_thread::sleep_for(50ms);
      }

      std::this_thread::sleep_for(200ms);
      CHECK(update_count.load() >= 5);

      auto v = getter.get();
      REQUIRE(v.has_value());
      CHECK(v->size() == 2);
    }
  }
}

// ---------------------------------------------------------------------------
// Someip - identity
// ---------------------------------------------------------------------------

TEST_SUITE("someip-identity") {
  TEST_CASE("someip-node-identity") {
    Publisher<int> pub1(SomeipConf(0x5001, 0x0001, SomeipConf::Groups{0x0001}, 0x0001));
    Publisher<int> pub2(SomeipConf(0x5002, 0x0001, SomeipConf::Groups{0x0001}, 0x0001));
    Subscriber<int> sub("someip://20481/1?groups=1&event=1");

    CHECK(pub1.get_abstract_node() != nullptr);
    CHECK(pub2.get_abstract_node() != nullptr);
    CHECK(pub1.get_abstract_node() != pub2.get_abstract_node());
    CHECK(pub1.get_abstract_node() != sub.get_abstract_node());
  }
}

#endif  // VLINK_SUPPORT_SOMEIP

// NOLINTEND
