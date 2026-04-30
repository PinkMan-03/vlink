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

#if defined(VLINK_SUPPORT_FDBUS)

#include <atomic>
#include <future>
#include <string>
#include <thread>

#include "./modules/fdbus_conf.h"

static bool ensure_fdbus_ready() {
  if (!FdbusConf::has_name_server()) {
    VLOG_W("FDBus name_server is not running, skipping.");
    return false;
  }

  return true;
}

// ---------------------------------------------------------------------------
// Fdbus - init
// ---------------------------------------------------------------------------

TEST_SUITE("fdbus-init") {
  TEST_CASE("conf-defaults") {
    if (!ensure_fdbus_ready()) {
      return;
    }

    FdbusConf conf("fdbus_test_svc");

    CHECK(conf.address == "fdbus_test_svc");
    CHECK(conf.event.empty());
    CHECK(conf.transport == "svc");
    CHECK(conf.get_transport_type() == TransportType::kFdbus);
  }

  TEST_CASE("conf-with-event") {
    if (!ensure_fdbus_ready()) {
      return;
    }

    FdbusConf conf("fdbus_test_svc", "speed");

    CHECK(conf.event == "speed");
  }

  TEST_CASE("conf-with-ipc-transport") {
    if (!ensure_fdbus_ready()) {
      return;
    }

    FdbusConf conf("fdbus_test_ipc", "", "ipc");

    CHECK(conf.transport == "ipc");
  }

  TEST_CASE("conf-equality-ignores-transport") {
    if (!ensure_fdbus_ready()) {
      return;
    }

    // transport is intentionally excluded from equality comparison
    FdbusConf a("my_svc", "evt");
    FdbusConf b("my_svc", "evt");
    FdbusConf c("my_svc", "evt2");

    CHECK(a == b);
    CHECK(a != c);
  }

  TEST_CASE("conf-equality-svc-ipc-equal") {
    if (!ensure_fdbus_ready()) {
      return;
    }

    FdbusConf svc("my_svc", "evt", "svc");
    FdbusConf ipc("my_svc", "evt", "ipc");

    // transport excluded from equality
    CHECK(svc == ipc);
  }

  TEST_CASE("url-parse-all-impl-types") {
    if (!ensure_fdbus_ready()) {
      return;
    }

    Url url("fdbus://fdbus_init_parse1");

    CHECK(url.parse(kPublisher));
    CHECK(url.parse(kSubscriber));
    CHECK(url.parse(kServer));
    CHECK(url.parse(kClient));
    CHECK(url.parse(kSetter));
    CHECK(url.parse(kGetter));
  }

  TEST_CASE("url-parse-with-event") {
    if (!ensure_fdbus_ready()) {
      return;
    }

    Url url("fdbus://fdbus_init_parse2?event=myevent");

    CHECK(url.parse(kPublisher));
    CHECK(url.parse(kSubscriber));
  }

  TEST_CASE("url-parse-ipc-fragment") {
    if (!ensure_fdbus_ready()) {
      return;
    }

    Url url("fdbus://fdbus_init_parse3#ipc");

    CHECK(url.parse(kPublisher));
    CHECK(url.parse(kSubscriber));
  }

  TEST_CASE("unknown-impl-type-throws") {
    if (!ensure_fdbus_ready()) {
      return;
    }

    Url url("fdbus://fdbus_init_parse4");

    CHECK_THROWS_AS(url.parse(kUnknownImplType), std::runtime_error);
  }

  TEST_CASE("invalid-transport-throws") {
    if (!ensure_fdbus_ready()) {
      return;
    }

    CHECK_THROWS(Publisher<int>("fdbus1://bad_url"));
  }
}

// ---------------------------------------------------------------------------
// Fdbus - event
// ---------------------------------------------------------------------------

TEST_SUITE("fdbus-event") {
  TEST_CASE("fdbus-event-pub-sub") {
    if (!ensure_fdbus_ready()) {
      return;
    }

    std::atomic<bool> received{false};
    Bytes captured;

    Publisher<Bytes> pub(FdbusConf("fdbus_evt_pubsub1", "data"));
    Subscriber<Bytes> sub("fdbus://fdbus_evt_pubsub1?event=data");

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

  TEST_CASE("fdbus-event-string") {
    if (!ensure_fdbus_ready()) {
      return;
    }

    std::atomic<bool> received{false};
    std::string captured;

    Publisher<std::string> pub(FdbusConf("fdbus_evt_str1", "name"));
    Subscriber<std::string> sub("fdbus://fdbus_evt_str1?event=name");

    sub.listen([&](const std::string& val) {
      captured = val;
      received.store(true, std::memory_order_release);
    });

    CHECK(pub.wait_for_subscribers(5s));
    CHECK(pub.publish(std::string("hello_fdbus")));

    for (int i = 0; i < 100 && !received.load(std::memory_order_acquire); ++i) {
      std::this_thread::sleep_for(50ms);
    }

    CHECK(received.load(std::memory_order_acquire));
    CHECK(captured == "hello_fdbus");
  }

  TEST_CASE("fdbus-event-multi-pub") {
    if (!ensure_fdbus_ready()) {
      return;
    }

    std::atomic<int> count{0};

    Publisher<int> pub(FdbusConf("fdbus_evt_multi1", "counter"));
    Subscriber<int> sub("fdbus://fdbus_evt_multi1?event=counter");

    sub.listen([&](const int& /*v*/) { count.fetch_add(1, std::memory_order_relaxed); });

    CHECK(pub.wait_for_subscribers(5s));

    for (int i = 0; i < 10; ++i) {
      pub.publish(i);
      std::this_thread::sleep_for(20ms);
    }

    std::this_thread::sleep_for(300ms);

    CHECK(count.load() >= 10);
  }

  TEST_CASE("fdbus-event-multi-sub") {
    if (!ensure_fdbus_ready()) {
      return;
    }

    std::atomic<int> count1{0};
    std::atomic<int> count2{0};

    Publisher<Bytes> pub(FdbusConf("fdbus_evt_multisub1", "raw"));
    Subscriber<Bytes> sub1("fdbus://fdbus_evt_multisub1?event=raw");
    Subscriber<Bytes> sub2("fdbus://fdbus_evt_multisub1?event=raw");

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

  TEST_CASE("fdbus-event-force-publish") {
    if (!ensure_fdbus_ready()) {
      return;
    }

    Publisher<Bytes> pub(FdbusConf("fdbus_evt_force1", "force"));

    CHECK(!pub.has_subscribers());

    for (int i = 0; i < 5; ++i) {
      CHECK(pub.publish(Bytes{static_cast<uint8_t>(i)}, true));
    }
  }

  TEST_CASE("fdbus-event-detect-subscribers") {
    if (!ensure_fdbus_ready()) {
      return;
    }

    std::atomic<int> connected_count{0};

    Publisher<Bytes> pub(FdbusConf("fdbus_evt_detect1", "detect"));

    pub.detect_subscribers([&](bool connected) {
      if (connected) {
        connected_count.fetch_add(1, std::memory_order_relaxed);
      }
    });

    {
      Subscriber<Bytes> sub("fdbus://fdbus_evt_detect1?event=detect");
      sub.listen([](const Bytes& /*d*/) {});

      std::this_thread::sleep_for(500ms);
      CHECK(pub.has_subscribers());
    }

    std::this_thread::sleep_for(500ms);
    CHECK(!pub.has_subscribers());
  }

  // TEST_CASE("fdbus-event-ipc-mode") {
  //   std::atomic<bool> received{false};

  //   Publisher<int> pub(FdbusConf("fdbus_ipc_evt1", "data", "ipc"));
  //   Subscriber<int> sub("fdbus://fdbus_ipc_evt1?event=data#ipc");

  //   sub.listen([&](const int& /*v*/) { received.store(true, std::memory_order_release); });

  //   CHECK(pub.wait_for_subscribers(5s));
  //   CHECK(pub.publish(42));

  //   for (int i = 0; i < 100 && !received.load(std::memory_order_acquire); ++i) {
  //     std::this_thread::sleep_for(50ms);
  //   }

  //   CHECK(received.load(std::memory_order_acquire));
  // }
}

// ---------------------------------------------------------------------------
// Fdbus - method
// ---------------------------------------------------------------------------

TEST_SUITE("fdbus-method") {
  TEST_CASE("fdbus-method-send") {
    if (!ensure_fdbus_ready()) {
      return;
    }

    std::atomic<int> counter{0};

    Server<std::string> server(FdbusConf("fdbus_mth_send1"));
    server.listen([&](const std::string& /*req*/) { counter.fetch_add(1, std::memory_order_relaxed); });

    Client<std::string> client("fdbus://fdbus_mth_send1");
    CHECK(client.wait_for_connected(5s));
    CHECK(client.is_connected());

    CHECK(client.send("fire1"));
    std::this_thread::sleep_for(200ms);
    CHECK(counter.load() == 1);

    CHECK(client.send("fire2"));
    std::this_thread::sleep_for(200ms);
    CHECK(counter.load() == 2);
  }

  TEST_CASE("fdbus-method-invoke") {
    if (!ensure_fdbus_ready()) {
      return;
    }

    Server<std::string, std::string> server(FdbusConf("fdbus_mth_invoke1"));
    server.listen([](const std::string& req, std::string& resp) { resp = "fdbus:" + req; });

    Client<std::string, std::string> client("fdbus://fdbus_mth_invoke1");
    CHECK(client.wait_for_connected(5s));

    SUBCASE("sync-optional") {
      auto resp = client.invoke("ping");
      CHECK(resp.has_value());
      CHECK(*resp == "fdbus:ping");
    }

    SUBCASE("sync-ref-overload") {
      std::string out;
      CHECK(client.invoke("pong", out, 5s));
      CHECK(out == "fdbus:pong");
    }

    SUBCASE("async-future") {
      auto fut = client.async_invoke("async");
      REQUIRE(fut.wait_for(5s) == std::future_status::ready);
      CHECK(fut.get() == "fdbus:async");
    }

    SUBCASE("multiple-sequential") {
      for (int i = 0; i < 5; ++i) {
        auto resp = client.invoke("r" + std::to_string(i));
        CHECK(resp.has_value());
        CHECK(*resp == "fdbus:r" + std::to_string(i));
      }
    }
  }

  // TEST_CASE("fdbus-method-async-reply") {
  //   std::atomic<uint64_t> saved_id{0};
  //   std::atomic<bool> req_received{false};

  //   Server<std::string, std::string> server(FdbusConf("fdbus_mth_async_reply1"));
  //   server.listen_for_reply([&](uint64_t req_id, const std::string& /*req*/) {
  //     saved_id.store(req_id, std::memory_order_release);
  //     req_received.store(true, std::memory_order_release);
  //   });

  //   Client<std::string, std::string> client("fdbus://fdbus_mth_async_reply1");
  //   CHECK(client.wait_for_connected(5s));

  //   auto fut = client.async_invoke("defer");

  //   for (int i = 0; i < 100 && !req_received.load(std::memory_order_acquire); ++i) {
  //     std::this_thread::sleep_for(50ms);
  //   }

  //   REQUIRE(req_received.load(std::memory_order_acquire));
  //   CHECK(server.reply(saved_id.load(), std::string("deferred_fdbus")));

  //   REQUIRE(fut.wait_for(5s) == std::future_status::ready);
  //   CHECK(fut.get() == "deferred_fdbus");
  // }

  TEST_CASE("fdbus-method-async-callback") {
    if (!ensure_fdbus_ready()) {
      return;
    }

    Server<std::string, std::string> server(FdbusConf("fdbus_mth_cb1"));
    server.listen([](const std::string& /*req*/, std::string& resp) { resp = "fdbus_cb"; });

    Client<std::string, std::string> client("fdbus://fdbus_mth_cb1");
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
    CHECK(resp_val == "fdbus_cb");
  }

  TEST_CASE("fdbus-method-detect-connected") {
    if (!ensure_fdbus_ready()) {
      return;
    }

    std::atomic<bool> conn_flag{false};

    Server<std::string, std::string> server(FdbusConf("fdbus_mth_detect1"));
    server.listen([](const std::string& /*req*/, std::string& resp) { resp = "ok"; });

    Client<std::string, std::string> client("fdbus://fdbus_mth_detect1");
    client.detect_connected([&](bool connected) { conn_flag.store(connected, std::memory_order_release); });

    CHECK(client.wait_for_connected(5s));
    std::this_thread::sleep_for(200ms);
    CHECK(conn_flag.load(std::memory_order_acquire));
  }

  // TEST_CASE("fdbus-method-ipc-mode") {
  //   Server<std::string, std::string> server(FdbusConf("fdbus_mth_ipc1", "", "ipc"));
  //   server.listen([](const std::string& req, std::string& resp) { resp = "ipc:" + req; });

  //   Client<std::string, std::string> client("fdbus://fdbus_mth_ipc1#ipc");
  //   CHECK(client.wait_for_connected(5s));

  //   auto resp = client.invoke("hello");
  //   CHECK(resp.has_value());
  //   CHECK(*resp == "ipc:hello");
  // }
}

// ---------------------------------------------------------------------------
// Fdbus - field
// ---------------------------------------------------------------------------

TEST_SUITE("fdbus-field") {
  TEST_CASE("fdbus-field-setter-getter") {
    if (!ensure_fdbus_ready()) {
      return;
    }

    SUBCASE("polling-get") {
      Setter<Bytes> setter(FdbusConf("fdbus_fld_poll1", "value"));
      Getter<Bytes> getter("fdbus://fdbus_fld_poll1?event=value");

      setter.set(Bytes{0x13, 0x37});
      std::this_thread::sleep_for(300ms);

      auto v = getter.get();
      REQUIRE(v.has_value());
      REQUIRE(v->size() == 2);
      CHECK((*v)[0] == 0x13);
      CHECK((*v)[1] == 0x37);
    }

    SUBCASE("wait-for-value") {
      Setter<Bytes> setter(FdbusConf("fdbus_fld_wait1", "state"));
      Getter<Bytes> getter("fdbus://fdbus_fld_wait1?event=state");

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

      Setter<Bytes> setter(FdbusConf("fdbus_fld_cb1", "notify"));
      Getter<Bytes> getter("fdbus://fdbus_fld_cb1?event=notify");

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

      Setter<Bytes> setter(FdbusConf("fdbus_fld_cr1", "temp"));
      Getter<Bytes> getter("fdbus://fdbus_fld_cr1?event=temp");

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
      Setter<Bytes> setter(FdbusConf("fdbus_fld_late1", "cache"));
      setter.set(Bytes{0xC0, 0xDE});
      std::this_thread::sleep_for(200ms);

      Getter<Bytes> late_getter("fdbus://fdbus_fld_late1?event=cache");
      CHECK(late_getter.wait_for_value(5s));
      auto v = late_getter.get();
      REQUIRE(v.has_value());
      CHECK((*v)[0] == 0xC0);
    }

    SUBCASE("string-field") {
      Setter<std::string> setter(FdbusConf("fdbus_fld_str1", "label"));
      Getter<std::string> getter("fdbus://fdbus_fld_str1?event=label");

      setter.set(std::string("fdbus_field_val"));
      CHECK(getter.wait_for_value(5s));
      auto v = getter.get();
      REQUIRE(v.has_value());
      CHECK(*v == "fdbus_field_val");
    }
  }
}

// ---------------------------------------------------------------------------
// Fdbus - latency
// ---------------------------------------------------------------------------

// TEST_SUITE("fdbus-latency") {
//   TEST_CASE("fdbus-latency-stats") {
//     Publisher<int> pub(FdbusConf("fdbus_lat_sub1", "latency"));
//     Subscriber<int> sub("fdbus://fdbus_lat_sub1?event=latency");

//     sub.set_latency_and_lost_enabled(true);
//     CHECK(sub.is_latency_and_lost_enabled());

//     std::atomic<int> count{0};
//     sub.listen([&](const int& /*v*/) { count.fetch_add(1, std::memory_order_relaxed); });

//     CHECK(pub.wait_for_subscribers(5s));

//     for (int i = 0; i < 10; ++i) {
//       pub.publish(i);
//       std::this_thread::sleep_for(10ms);
//     }

//     std::this_thread::sleep_for(300ms);

//     CHECK(count.load() > 0);
//     CHECK(sub.get_latency() >= 0);
//     CHECK(sub.get_lost().total > 0);

//     sub.set_latency_and_lost_enabled(false);
//     CHECK(!sub.is_latency_and_lost_enabled());
//   }
// }

// ---------------------------------------------------------------------------
// Fdbus - identity
// ---------------------------------------------------------------------------

TEST_SUITE("fdbus-identity") {
  TEST_CASE("fdbus-node-identity") {
    if (!ensure_fdbus_ready()) {
      return;
    }

    Publisher<int> pub1(FdbusConf("fdbus_id_p1", "evt"));
    Publisher<int> pub2(FdbusConf("fdbus_id_p2", "evt"));
    Subscriber<int> sub("fdbus://fdbus_id_p1?event=evt");

    CHECK(pub1.get_abstract_node() != nullptr);
    CHECK(pub2.get_abstract_node() != nullptr);
    CHECK(pub1.get_abstract_node() != pub2.get_abstract_node());
    CHECK(pub1.get_abstract_node() != sub.get_abstract_node());
  }
}

#endif  // VLINK_SUPPORT_FDBUS

// NOLINTEND
