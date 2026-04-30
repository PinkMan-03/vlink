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

#if defined(VLINK_SUPPORT_DDST)

#include <atomic>
#include <future>
#include <string>
#include <thread>

#include "./modules/ddst_conf.h"

// ---------------------------------------------------------------------------
// Ddst - init
// ---------------------------------------------------------------------------

TEST_SUITE("ddst-init") {
  TEST_CASE("conf-defaults") {
    DdstConf conf("vehicle/speed");

    CHECK(conf.topic == "vehicle/speed");
    CHECK(conf.domain == 0);
    CHECK(conf.depth == 0);
    CHECK(conf.qos.empty());
    CHECK(conf.get_transport_type() == TransportType::kDdst);
  }

  TEST_CASE("conf-with-domain-depth") {
    DdstConf conf("my/topic", 4, 8);

    CHECK(conf.domain == 4);
    CHECK(conf.depth == 8);
  }

  TEST_CASE("conf-with-named-qos") {
    DdstConf conf("my/topic", 0, 0, "travo_qos");

    CHECK(conf.qos == "travo_qos");
  }

  TEST_CASE("conf-equality") {
    DdstConf a("topic/a", 2, 10, "qos");
    DdstConf b("topic/a", 2, 10, "qos");
    DdstConf c("topic/b", 2, 10, "qos");

    CHECK(a == b);
    CHECK(a != c);
  }

  TEST_CASE("url-parse-all-impl-types") {
    Url url("ddst://ddst/init/parse1");

    CHECK(url.parse(kPublisher));
    CHECK(url.parse(kSubscriber));
    CHECK(url.parse(kServer));
    CHECK(url.parse(kClient));
    CHECK(url.parse(kSetter));
    CHECK(url.parse(kGetter));
  }

  TEST_CASE("unknown-impl-type-throws") {
    Url url("ddst://ddst/init/parse2");

    CHECK_THROWS_AS(url.parse(kUnknownImplType), std::runtime_error);
  }

  TEST_CASE("invalid-transport-throws") { CHECK_THROWS(Publisher<int>("ddst1://bad/url")); }
}

// ---------------------------------------------------------------------------
// Ddst - event
// ---------------------------------------------------------------------------

TEST_SUITE("ddst-event") {
  TEST_CASE("ddst-event-pub-sub") {
    std::atomic<bool> received{false};
    Bytes captured;

    Publisher<Bytes> pub(DdstConf("ddst/evt/pubsub1"));
    Subscriber<Bytes> sub("ddst://ddst/evt/pubsub1");

    sub.listen([&](const Bytes& data) {
      captured = data;
      received.store(true, std::memory_order_release);
    });

    CHECK(pub.wait_for_subscribers(5s));
    CHECK(pub.has_subscribers());

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

  TEST_CASE("ddst-event-string") {
    std::atomic<bool> received{false};
    std::string captured;

    Publisher<std::string> pub(DdstConf("ddst/evt/str1"));
    Subscriber<std::string> sub("ddst://ddst/evt/str1");

    sub.listen([&](const std::string& val) {
      captured = val;
      received.store(true, std::memory_order_release);
    });

    CHECK(pub.wait_for_subscribers(5s));
    CHECK(pub.publish(std::string("hello_ddst")));

    for (int i = 0; i < 100 && !received.load(std::memory_order_acquire); ++i) {
      std::this_thread::sleep_for(50ms);
    }

    CHECK(received.load(std::memory_order_acquire));
    CHECK(captured == "hello_ddst");
  }

  TEST_CASE("ddst-event-multi-pub") {
    std::atomic<int> count{0};

    Publisher<int> pub(DdstConf("ddst/evt/multi1"));
    Subscriber<int> sub("ddst://ddst/evt/multi1");

    sub.listen([&](const int& /*v*/) { count.fetch_add(1, std::memory_order_relaxed); });

    CHECK(pub.wait_for_subscribers(5s));

    for (int i = 0; i < 10; ++i) {
      pub.publish(i);
      std::this_thread::sleep_for(20ms);
    }

    std::this_thread::sleep_for(300ms);

    CHECK(count.load() >= 10);
  }

  TEST_CASE("ddst-event-multi-sub") {
    std::atomic<int> count1{0};
    std::atomic<int> count2{0};

    Publisher<Bytes> pub(DdstConf("ddst/evt/multisub1"));
    Subscriber<Bytes> sub1("ddst://ddst/evt/multisub1");
    Subscriber<Bytes> sub2("ddst://ddst/evt/multisub1");

    sub1.listen([&](const Bytes& /*d*/) { count1.fetch_add(1, std::memory_order_relaxed); });
    sub2.listen([&](const Bytes& /*d*/) { count2.fetch_add(1, std::memory_order_relaxed); });

    CHECK(pub.wait_for_subscribers(5s));

    for (int i = 0; i < 3; ++i) {
      pub.publish(Bytes{static_cast<uint8_t>(i)});
      std::this_thread::sleep_for(30ms);
    }

    std::this_thread::sleep_for(300ms);

    CHECK(count1.load() >= 3);
    CHECK(count2.load() >= 3);
  }

  TEST_CASE("ddst-event-force-publish") {
    Publisher<Bytes> pub(DdstConf("ddst/evt/force1"));

    CHECK(!pub.has_subscribers());

    for (int i = 0; i < 5; ++i) {
      CHECK(pub.publish(Bytes{static_cast<uint8_t>(i)}, true));
    }
  }

  TEST_CASE("ddst-event-detect-subscribers") {
    std::atomic<int> connected_count{0};

    Publisher<Bytes> pub(DdstConf("ddst/evt/detect1"));

    pub.detect_subscribers([&](bool connected) {
      if (connected) {
        connected_count.fetch_add(1, std::memory_order_relaxed);
      }
    });

    {
      Subscriber<Bytes> sub("ddst://ddst/evt/detect1");
      sub.listen([](const Bytes& /*d*/) {});

      std::this_thread::sleep_for(500ms);
      CHECK(pub.has_subscribers());
    }

    std::this_thread::sleep_for(500ms);
    CHECK(!pub.has_subscribers());
  }
}

// ---------------------------------------------------------------------------
// Ddst - method
// ---------------------------------------------------------------------------

TEST_SUITE("ddst-method") {
  TEST_CASE("ddst-method-send") {
    std::atomic<int> counter{0};

    Server<std::string> server(DdstConf("ddst/mth/send1"));
    server.listen([&](const std::string& /*req*/) { counter.fetch_add(1, std::memory_order_relaxed); });

    Client<std::string> client("ddst://ddst/mth/send1");
    CHECK(client.wait_for_connected(5s));
    CHECK(client.is_connected());

    CHECK(client.send("fire1"));
    std::this_thread::sleep_for(200ms);
    CHECK(counter.load() == 1);

    CHECK(client.send("fire2"));
    std::this_thread::sleep_for(200ms);
    CHECK(counter.load() == 2);
  }

  TEST_CASE("ddst-method-invoke") {
    Server<std::string, std::string> server(DdstConf("ddst/mth/invoke1"));
    server.listen([](const std::string& req, std::string& resp) { resp = "travo:" + req; });

    Client<std::string, std::string> client("ddst://ddst/mth/invoke1");
    CHECK(client.wait_for_connected(5s));

    SUBCASE("sync-optional") {
      auto resp = client.invoke("ping");
      CHECK(resp.has_value());
      CHECK(*resp == "travo:ping");
    }

    SUBCASE("sync-ref-overload") {
      std::string out;
      CHECK(client.invoke("pong", out, 5s));
      CHECK(out == "travo:pong");
    }

    SUBCASE("async-future") {
      auto fut = client.async_invoke("async");
      REQUIRE(fut.wait_for(5s) == std::future_status::ready);
      CHECK(fut.get() == "travo:async");
    }

    SUBCASE("multiple-sequential") {
      for (int i = 0; i < 5; ++i) {
        auto resp = client.invoke("r" + std::to_string(i));
        CHECK(resp.has_value());
        CHECK(*resp == "travo:r" + std::to_string(i));
      }
    }
  }

  TEST_CASE("ddst-method-async-reply") {
    std::atomic<uint64_t> saved_id{0};
    std::atomic<bool> req_received{false};

    Server<std::string, std::string> server(DdstConf("ddst/mth/async_reply1"));
    server.listen_for_reply([&](uint64_t req_id, const std::string& /*req*/) {
      saved_id.store(req_id, std::memory_order_release);
      req_received.store(true, std::memory_order_release);
    });

    Client<std::string, std::string> client("ddst://ddst/mth/async_reply1");
    CHECK(client.wait_for_connected(5s));

    auto fut = client.async_invoke("defer");

    for (int i = 0; i < 100 && !req_received.load(std::memory_order_acquire); ++i) {
      std::this_thread::sleep_for(50ms);
    }

    REQUIRE(req_received.load(std::memory_order_acquire));
    CHECK(server.reply(saved_id.load(), std::string("deferred_ddst")));

    REQUIRE(fut.wait_for(5s) == std::future_status::ready);
    CHECK(fut.get() == "deferred_ddst");
  }

  TEST_CASE("ddst-method-async-callback") {
    Server<std::string, std::string> server(DdstConf("ddst/mth/cb1"));
    server.listen([](const std::string& /*req*/, std::string& resp) { resp = "ddst_cb"; });

    Client<std::string, std::string> client("ddst://ddst/mth/cb1");
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
    CHECK(resp_val == "ddst_cb");
  }
}

// ---------------------------------------------------------------------------
// Ddst - field
// ---------------------------------------------------------------------------

TEST_SUITE("ddst-field") {
  TEST_CASE("ddst-field-setter-getter") {
    SUBCASE("polling-get") {
      Setter<Bytes> setter(DdstConf("ddst/fld/poll1"));
      Getter<Bytes> getter("ddst://ddst/fld/poll1");

      setter.set(Bytes{0x13, 0x37});
      std::this_thread::sleep_for(300ms);

      auto v = getter.get();
      REQUIRE(v.has_value());
      REQUIRE(v->size() == 2);
      CHECK((*v)[0] == 0x13);
      CHECK((*v)[1] == 0x37);
    }

    SUBCASE("wait-for-value") {
      Setter<Bytes> setter(DdstConf("ddst/fld/wait1"));
      Getter<Bytes> getter("ddst://ddst/fld/wait1");

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

      Setter<Bytes> setter(DdstConf("ddst/fld/cb1"));
      Getter<Bytes> getter("ddst://ddst/fld/cb1");

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

      Setter<Bytes> setter(DdstConf("ddst/fld/cr1"));
      Getter<Bytes> getter("ddst://ddst/fld/cr1");

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
      Setter<Bytes> setter(DdstConf("ddst/fld/late1"));
      setter.set(Bytes{0xC0, 0xDE});
      std::this_thread::sleep_for(200ms);

      Getter<Bytes> late_getter("ddst://ddst/fld/late1");
      CHECK(late_getter.wait_for_value(5s));
      auto v = late_getter.get();
      REQUIRE(v.has_value());
      CHECK((*v)[0] == 0xC0);
    }
  }
}

// ---------------------------------------------------------------------------
// Ddst - latency
// ---------------------------------------------------------------------------

TEST_SUITE("ddst-latency") {
  TEST_CASE("ddst-latency-stats") {
    Publisher<int> pub(DdstConf("ddst/lat/sub1"));
    Subscriber<int> sub("ddst://ddst/lat/sub1");

    sub.set_latency_and_lost_enabled(true);
    CHECK(sub.is_latency_and_lost_enabled());

    std::atomic<int> count{0};
    sub.listen([&](const int& /*v*/) { count.fetch_add(1, std::memory_order_relaxed); });

    CHECK(pub.wait_for_subscribers(5s));

    for (int i = 0; i < 10; ++i) {
      pub.publish(i);
      std::this_thread::sleep_for(10ms);
    }

    std::this_thread::sleep_for(300ms);

    CHECK(count.load() > 0);
    CHECK(sub.get_latency() >= 0);
    CHECK(sub.get_lost().total >= 0);

    sub.set_latency_and_lost_enabled(false);
    CHECK(!sub.is_latency_and_lost_enabled());
  }
}

// ---------------------------------------------------------------------------
// Ddst - identity
// ---------------------------------------------------------------------------

TEST_SUITE("ddst-identity") {
  TEST_CASE("ddst-node-identity") {
    Publisher<int> pub1(DdstConf("ddst/id/p1"));
    Publisher<int> pub2(DdstConf("ddst/id/p2"));
    Subscriber<int> sub("ddst://ddst/id/p1");

    CHECK(pub1.get_abstract_node() != nullptr);
    CHECK(pub2.get_abstract_node() != nullptr);
    CHECK(pub1.get_abstract_node() != pub2.get_abstract_node());
    CHECK(pub1.get_abstract_node() != sub.get_abstract_node());
  }
}

#endif  // VLINK_SUPPORT_DDST

// NOLINTEND
