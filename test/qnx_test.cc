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

#if defined(VLINK_SUPPORT_QNX)

#include <atomic>
#include <future>
#include <string>
#include <thread>

#include "./modules/qnx_conf.h"

// ---------------------------------------------------------------------------
// Qnx - init
// ---------------------------------------------------------------------------

TEST_SUITE("qnx-init") {
  TEST_CASE("conf-defaults") {
    QnxConf conf("qnx_test_channel");

    CHECK(conf.address == "qnx_test_channel");
    CHECK(conf.event.empty());
    CHECK(conf.get_transport_type() == TransportType::kQnx);
  }

  TEST_CASE("conf-with-event") {
    QnxConf conf("qnx_test_channel", "speed");

    CHECK(conf.address == "qnx_test_channel");
    CHECK(conf.event == "speed");
  }

  TEST_CASE("conf-equality") {
    QnxConf a("channel_a", "evt");
    QnxConf b("channel_a", "evt");
    QnxConf c("channel_b", "evt");

    CHECK(a == b);
    CHECK(a != c);
  }

  TEST_CASE("conf-event-inequality") {
    QnxConf a("channel", "evt1");
    QnxConf b("channel", "evt2");

    CHECK(a != b);
  }

  TEST_CASE("url-parse-all-impl-types") {
    Url url("qnx://qnx_init_parse1");

    CHECK(url.parse(kPublisher));
    CHECK(url.parse(kSubscriber));
    CHECK(url.parse(kServer));
    CHECK(url.parse(kClient));
    CHECK(url.parse(kSetter));
    CHECK(url.parse(kGetter));
  }

  TEST_CASE("url-parse-with-event") {
    Url url("qnx://qnx_init_parse2?event=myevent");

    CHECK(url.parse(kPublisher));
    CHECK(url.parse(kSubscriber));
  }

  TEST_CASE("unknown-impl-type-throws") {
    Url url("qnx://qnx_init_parse3");

    CHECK_THROWS_AS(url.parse(kUnknownImplType), std::runtime_error);
  }

  TEST_CASE("invalid-transport-throws") { CHECK_THROWS(Publisher<int>("qnx1://bad_url")); }
}

// ---------------------------------------------------------------------------
// Qnx - event
// ---------------------------------------------------------------------------

TEST_SUITE("qnx-event") {
  TEST_CASE("qnx-event-pub-sub") {
    std::atomic<bool> received{false};
    Bytes captured;

    Publisher<Bytes> pub(QnxConf("qnx_evt_pubsub1", "data"));
    Subscriber<Bytes> sub("qnx://qnx_evt_pubsub1?event=data");

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

  TEST_CASE("qnx-event-string") {
    std::atomic<bool> received{false};
    std::string captured;

    Publisher<std::string> pub(QnxConf("qnx_evt_str1", "name"));
    Subscriber<std::string> sub("qnx://qnx_evt_str1?event=name");

    sub.listen([&](const std::string& val) {
      captured = val;
      received.store(true, std::memory_order_release);
    });

    CHECK(pub.wait_for_subscribers(5s));
    CHECK(pub.publish(std::string("hello_qnx")));

    for (int i = 0; i < 100 && !received.load(std::memory_order_acquire); ++i) {
      std::this_thread::sleep_for(50ms);
    }

    CHECK(received.load(std::memory_order_acquire));
    CHECK(captured == "hello_qnx");
  }

  TEST_CASE("qnx-event-multi-pub") {
    std::atomic<int> count{0};

    Publisher<int> pub(QnxConf("qnx_evt_multi1", "counter"));
    Subscriber<int> sub("qnx://qnx_evt_multi1?event=counter");

    sub.listen([&](const int& /*v*/) { count.fetch_add(1, std::memory_order_relaxed); });

    CHECK(pub.wait_for_subscribers(5s));

    for (int i = 0; i < 10; ++i) {
      pub.publish(i);
      std::this_thread::sleep_for(20ms);
    }

    std::this_thread::sleep_for(300ms);

    CHECK(count.load() >= 10);
  }

  TEST_CASE("qnx-event-multi-sub") {
    std::atomic<int> count1{0};
    std::atomic<int> count2{0};

    Publisher<Bytes> pub(QnxConf("qnx_evt_multisub1", "raw"));
    Subscriber<Bytes> sub1("qnx://qnx_evt_multisub1?event=raw");
    Subscriber<Bytes> sub2("qnx://qnx_evt_multisub1?event=raw");

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

  TEST_CASE("qnx-event-force-publish") {
    Publisher<Bytes> pub(QnxConf("qnx_evt_force1", "force"));

    CHECK(!pub.has_subscribers());

    for (int i = 0; i < 5; ++i) {
      CHECK(pub.publish(Bytes{static_cast<uint8_t>(i)}, true));
    }
  }

  TEST_CASE("qnx-event-detect-subscribers") {
    std::atomic<int> connected_count{0};

    Publisher<Bytes> pub(QnxConf("qnx_evt_detect1", "detect"));

    pub.detect_subscribers([&](bool connected) {
      if (connected) {
        connected_count.fetch_add(1, std::memory_order_relaxed);
      }
    });

    {
      Subscriber<Bytes> sub("qnx://qnx_evt_detect1?event=detect");
      sub.listen([](const Bytes& /*d*/) {});

      std::this_thread::sleep_for(500ms);
      CHECK(pub.has_subscribers());
    }

    std::this_thread::sleep_for(500ms);
    CHECK(!pub.has_subscribers());
  }
}

// ---------------------------------------------------------------------------
// Qnx - method
// ---------------------------------------------------------------------------

TEST_SUITE("qnx-method") {
  TEST_CASE("qnx-method-send") {
    std::atomic<int> counter{0};

    Server<std::string> server(QnxConf("qnx_mth_send1"));
    server.listen([&](const std::string& /*req*/) { counter.fetch_add(1, std::memory_order_relaxed); });

    Client<std::string> client("qnx://qnx_mth_send1");
    CHECK(client.wait_for_connected(5s));
    CHECK(client.is_connected());

    CHECK(client.send("fire1"));
    std::this_thread::sleep_for(200ms);
    CHECK(counter.load() == 1);

    CHECK(client.send("fire2"));
    std::this_thread::sleep_for(200ms);
    CHECK(counter.load() == 2);
  }

  TEST_CASE("qnx-method-invoke") {
    Server<std::string, std::string> server(QnxConf("qnx_mth_invoke1"));
    server.listen([](const std::string& req, std::string& resp) { resp = "qnx:" + req; });

    Client<std::string, std::string> client("qnx://qnx_mth_invoke1");
    CHECK(client.wait_for_connected(5s));

    SUBCASE("sync-optional") {
      auto resp = client.invoke("ping");
      CHECK(resp.has_value());
      CHECK(*resp == "qnx:ping");
    }

    SUBCASE("sync-ref-overload") {
      std::string out;
      CHECK(client.invoke("pong", out, 5s));
      CHECK(out == "qnx:pong");
    }

    SUBCASE("async-future") {
      auto fut = client.async_invoke("async");
      REQUIRE(fut.wait_for(5s) == std::future_status::ready);
      CHECK(fut.get() == "qnx:async");
    }

    SUBCASE("multiple-sequential") {
      for (int i = 0; i < 5; ++i) {
        auto resp = client.invoke("r" + std::to_string(i));
        CHECK(resp.has_value());
        CHECK(*resp == "qnx:r" + std::to_string(i));
      }
    }
  }

  // TEST_CASE("qnx-method-async-reply") {
  //   std::atomic<uint64_t> saved_id{0};
  //   std::atomic<bool> req_received{false};

  //   Server<std::string, std::string> server(QnxConf("qnx_mth_async_reply1"));
  //   server.listen_for_reply([&](uint64_t req_id, const std::string& /*req*/) {
  //     saved_id.store(req_id, std::memory_order_release);
  //     req_received.store(true, std::memory_order_release);
  //   });

  //   Client<std::string, std::string> client("qnx://qnx_mth_async_reply1");
  //   CHECK(client.wait_for_connected(5s));

  //   auto fut = client.async_invoke("defer");

  //   for (int i = 0; i < 100 && !req_received.load(std::memory_order_acquire); ++i) {
  //     std::this_thread::sleep_for(50ms);
  //   }

  //   REQUIRE(req_received.load(std::memory_order_acquire));
  //   CHECK(server.reply(saved_id.load(), std::string("deferred_qnx")));

  //   REQUIRE(fut.wait_for(5s) == std::future_status::ready);
  //   CHECK(fut.get() == "deferred_qnx");
  // }

  TEST_CASE("qnx-method-async-callback") {
    Server<std::string, std::string> server(QnxConf("qnx_mth_cb1"));
    server.listen([](const std::string& /*req*/, std::string& resp) { resp = "qnx_cb"; });

    Client<std::string, std::string> client("qnx://qnx_mth_cb1");
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
    CHECK(resp_val == "qnx_cb");
  }

  TEST_CASE("qnx-method-detect-connected") {
    std::atomic<bool> conn_flag{false};

    Server<std::string, std::string> server(QnxConf("qnx_mth_detect1"));
    server.listen([](const std::string& /*req*/, std::string& resp) { resp = "ok"; });

    Client<std::string, std::string> client("qnx://qnx_mth_detect1");
    client.detect_connected([&](bool connected) { conn_flag.store(connected, std::memory_order_release); });

    CHECK(client.wait_for_connected(5s));
    std::this_thread::sleep_for(200ms);
    CHECK(conn_flag.load(std::memory_order_acquire));
  }
}

// ---------------------------------------------------------------------------
// Qnx - field
// ---------------------------------------------------------------------------

TEST_SUITE("qnx-field") {
  TEST_CASE("qnx-field-setter-getter") {
    SUBCASE("polling-get") {
      Setter<Bytes> setter(QnxConf("qnx_fld_poll1", "value"));
      Getter<Bytes> getter("qnx://qnx_fld_poll1?event=value");

      setter.set(Bytes{0x13, 0x37});
      std::this_thread::sleep_for(300ms);

      auto v = getter.get();
      REQUIRE(v.has_value());
      REQUIRE(v->size() == 2);
      CHECK((*v)[0] == 0x13);
      CHECK((*v)[1] == 0x37);
    }

    SUBCASE("wait-for-value") {
      Setter<Bytes> setter(QnxConf("qnx_fld_wait1", "state"));
      Getter<Bytes> getter("qnx://qnx_fld_wait1?event=state");

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

      Setter<Bytes> setter(QnxConf("qnx_fld_cb1", "notify"));
      Getter<Bytes> getter("qnx://qnx_fld_cb1?event=notify");

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

      Setter<Bytes> setter(QnxConf("qnx_fld_cr1", "temp"));
      Getter<Bytes> getter("qnx://qnx_fld_cr1?event=temp");

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
      Setter<Bytes> setter(QnxConf("qnx_fld_late1", "cache"));
      setter.set(Bytes{0xC0, 0xDE});
      std::this_thread::sleep_for(200ms);

      Getter<Bytes> late_getter("qnx://qnx_fld_late1?event=cache");
      CHECK(late_getter.wait_for_value(5s));
      auto v = late_getter.get();
      REQUIRE(v.has_value());
      CHECK((*v)[0] == 0xC0);
    }

    SUBCASE("multi-set-sequence") {
      std::atomic<int> update_count{0};

      Setter<Bytes> setter(QnxConf("qnx_fld_seq1", "seq"));
      Getter<Bytes> getter("qnx://qnx_fld_seq1?event=seq");

      getter.listen([&](const Bytes& /*v*/) { update_count.fetch_add(1, std::memory_order_relaxed); });

      std::this_thread::sleep_for(100ms);

      for (int i = 0; i < 5; ++i) {
        setter.set(Bytes{static_cast<uint8_t>(i)});
        std::this_thread::sleep_for(50ms);
      }

      std::this_thread::sleep_for(200ms);
      CHECK(update_count.load() >= 5);

      auto v = getter.get();
      REQUIRE(v.has_value());
      CHECK((*v)[0] == 4);
    }
  }
}

// ---------------------------------------------------------------------------
// Qnx - latency
// ---------------------------------------------------------------------------

TEST_SUITE("qnx-latency") {
  TEST_CASE("qnx-latency-stats") {
    Publisher<int> pub(QnxConf("qnx_lat_sub1", "latency"));
    Subscriber<int> sub("qnx://qnx_lat_sub1?event=latency");

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
// Qnx - identity
// ---------------------------------------------------------------------------

TEST_SUITE("qnx-identity") {
  TEST_CASE("qnx-node-identity") {
    Publisher<int> pub1(QnxConf("qnx_id_p1", "evt"));
    Publisher<int> pub2(QnxConf("qnx_id_p2", "evt"));
    Subscriber<int> sub("qnx://qnx_id_p1?event=evt");

    CHECK(pub1.get_abstract_node() != nullptr);
    CHECK(pub2.get_abstract_node() != nullptr);
    CHECK(pub1.get_abstract_node() != pub2.get_abstract_node());
    CHECK(pub1.get_abstract_node() != sub.get_abstract_node());
  }
}

#endif  // VLINK_SUPPORT_QNX

// NOLINTEND
