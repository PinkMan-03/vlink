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

#if defined(VLINK_SUPPORT_SHM)

#include <atomic>
#include <future>
#include <string>
#include <thread>

#include "./modules/shm_conf.h"

static bool ensure_shm_ready() {
  if (!ShmConf::auto_init_roudi(true)) {
    VLOG_W("RouDi is not running, skipping.");
    return false;
  }

  return true;
}

// ---------------------------------------------------------------------------
// Shm - init
// ---------------------------------------------------------------------------

TEST_SUITE("shm-init") {
  TEST_CASE("conf-defaults") {
    if (!ensure_shm_ready()) {
      return;
    }

    ShmConf conf("vehicle/speed");

    CHECK(conf.address == "vehicle/speed");
    CHECK(conf.event.empty());
    CHECK(conf.domain == 0);
    CHECK(conf.depth == 0);
    CHECK(conf.history == 0);
    CHECK(conf.wait == 0);
    CHECK(conf.get_transport_type() == TransportType::kShm);
  }

  TEST_CASE("conf-with-all-fields") {
    if (!ensure_shm_ready()) {
      return;
    }

    ShmConf conf("my/topic", "my_event", 1, 10, 5, 0);

    CHECK(conf.address == "my/topic");
    CHECK(conf.event == "my_event");
    CHECK(conf.domain == 1);
    CHECK(conf.depth == 10);
    CHECK(conf.history == 5);
    CHECK(conf.wait == 0);
  }

  TEST_CASE("conf-equality") {
    if (!ensure_shm_ready()) {
      return;
    }

    ShmConf a("addr1", "ev1", 0, 0, 0, 0);
    ShmConf b("addr1", "ev1", 0, 0, 0, 0);
    ShmConf c("addr2", "ev1", 0, 0, 0, 0);

    CHECK(a == b);
    CHECK(a != c);
  }

  TEST_CASE("url-parse-pub-sub-server-client") {
    if (!ensure_shm_ready()) {
      return;
    }

    Url url("shm://shm/init/parse1?event=ev1");

    CHECK(url.parse(kPublisher));
    CHECK(url.parse(kSubscriber));
    CHECK(url.parse(kServer));
    CHECK(url.parse(kClient));
    CHECK(url.parse(kSetter));
    CHECK(url.parse(kGetter));
  }

  TEST_CASE("unknown-impl-type-throws") {
    if (!ensure_shm_ready()) {
      return;
    }

    Url url("shm://shm/init/parse2");

    CHECK_THROWS_AS(url.parse(kUnknownImplType), std::runtime_error);
  }

  TEST_CASE("has-runtime-inited") {
    if (!ensure_shm_ready()) {
      return;
    }

    CHECK(ShmConf::has_runtime_inited());
  }
}

// ---------------------------------------------------------------------------
// Shm - event
// ---------------------------------------------------------------------------

TEST_SUITE("shm-event") {
  TEST_CASE("shm-event-pub-sub") {
    if (!ensure_shm_ready()) {
      return;
    }

    std::atomic<bool> received{false};
    Bytes captured;

    Publisher<Bytes> pub(ShmConf("shm/evt/pubsub1", "data"));
    Subscriber<Bytes> sub("shm://shm/evt/pubsub1?event=data");

    sub.listen([&](const Bytes& data) {
      captured = data;
      received.store(true, std::memory_order_release);
    });

    CHECK(pub.wait_for_subscribers(5s));
    CHECK(pub.has_subscribers());

    Bytes payload{0xDE, 0xAD, 0xBE, 0xEF};
    CHECK(pub.publish(payload));

    for (int i = 0; i < 100 && !received.load(std::memory_order_acquire); ++i) {
      std::this_thread::sleep_for(50ms);
    }

    CHECK(received.load(std::memory_order_acquire));
    REQUIRE(captured.size() == 4);
    CHECK(captured[0] == 0xDE);
    CHECK(captured[3] == 0xEF);
  }

  TEST_CASE("shm-event-string") {
    if (!ensure_shm_ready()) {
      return;
    }

    std::atomic<bool> received{false};
    std::string captured;

    Publisher<std::string> pub(ShmConf("shm/evt/str1", "data"));
    Subscriber<std::string> sub("shm://shm/evt/str1?event=data");

    sub.listen([&](const std::string& val) {
      captured = val;
      received.store(true, std::memory_order_release);
    });

    CHECK(pub.wait_for_subscribers(5s));
    CHECK(pub.publish(std::string("hello_shm")));

    for (int i = 0; i < 100 && !received.load(std::memory_order_acquire); ++i) {
      std::this_thread::sleep_for(50ms);
    }

    CHECK(received.load(std::memory_order_acquire));
    CHECK(captured == "hello_shm");
  }

  TEST_CASE("shm-event-int") {
    if (!ensure_shm_ready()) {
      return;
    }

    std::atomic<int> captured{0};
    std::atomic<bool> received{false};

    Publisher<int> pub(ShmConf("shm/evt/int1", "data"));
    Subscriber<int> sub("shm://shm/evt/int1?event=data");

    sub.listen([&](const int& v) {
      captured.store(v, std::memory_order_relaxed);
      received.store(true, std::memory_order_release);
    });

    CHECK(pub.wait_for_subscribers(5s));
    CHECK(pub.publish(42));

    for (int i = 0; i < 100 && !received.load(std::memory_order_acquire); ++i) {
      std::this_thread::sleep_for(50ms);
    }

    CHECK(received.load(std::memory_order_acquire));
    CHECK(captured.load() == 42);
  }

  TEST_CASE("shm-event-multi-pub") {
    if (!ensure_shm_ready()) {
      return;
    }

    std::atomic<int> count{0};

    Publisher<int> pub(ShmConf("shm/evt/multi1", "data"));
    Subscriber<int> sub("shm://shm/evt/multi1?event=data");

    sub.listen([&](const int& /*v*/) { count.fetch_add(1, std::memory_order_relaxed); });

    CHECK(pub.wait_for_subscribers(5s));

    for (int i = 0; i < 10; ++i) {
      pub.publish(i);
      std::this_thread::sleep_for(20ms);
    }

    std::this_thread::sleep_for(300ms);

    CHECK(count.load() >= 10);
  }

  TEST_CASE("shm-event-multi-sub") {
    if (!ensure_shm_ready()) {
      return;
    }

    std::atomic<int> count1{0};
    std::atomic<int> count2{0};

    Publisher<Bytes> pub(ShmConf("shm/evt/multisub1", "data"));
    Subscriber<Bytes> sub1("shm://shm/evt/multisub1?event=data");
    Subscriber<Bytes> sub2("shm://shm/evt/multisub1?event=data");

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

  TEST_CASE("shm-event-force-publish") {
    if (!ensure_shm_ready()) {
      return;
    }

    Publisher<Bytes> pub(ShmConf("shm/evt/force1", "data"));

    CHECK(!pub.has_subscribers());

    for (int i = 0; i < 5; ++i) {
      CHECK(pub.publish(Bytes{static_cast<uint8_t>(i)}, true));
    }
  }

  TEST_CASE("shm-event-detect-subscribers") {
    if (!ensure_shm_ready()) {
      return;
    }

    std::atomic<int> connected_count{0};

    Publisher<Bytes> pub(ShmConf("shm/evt/detect1", "data"));

    pub.detect_subscribers([&](bool connected) {
      if (connected) {
        connected_count.fetch_add(1, std::memory_order_relaxed);
      }
    });

    {
      Subscriber<Bytes> sub("shm://shm/evt/detect1?event=data");
      sub.listen([](const Bytes& /*d*/) {});

      std::this_thread::sleep_for(300ms);
      CHECK(pub.has_subscribers());
    }

    std::this_thread::sleep_for(300ms);
    CHECK(!pub.has_subscribers());
  }
}

// ---------------------------------------------------------------------------
// Shm - method
// ---------------------------------------------------------------------------

TEST_SUITE("shm-method") {
  TEST_CASE("shm-method-send") {
    if (!ensure_shm_ready()) {
      return;
    }

    std::atomic<int> counter{0};

    Server<std::string> server(ShmConf("shm/mth/send1", "req"));
    server.listen([&](const std::string& /*req*/) { counter.fetch_add(1, std::memory_order_relaxed); });

    Client<std::string> client("shm://shm/mth/send1?event=req");
    CHECK(client.wait_for_connected(5s));
    CHECK(client.is_connected());

    CHECK(client.send("fire1"));
    std::this_thread::sleep_for(200ms);
    CHECK(counter.load() == 1);

    CHECK(client.send("fire2"));
    std::this_thread::sleep_for(200ms);
    CHECK(counter.load() == 2);
  }

  TEST_CASE("shm-method-invoke") {
    if (!ensure_shm_ready()) {
      return;
    }

    Server<std::string, std::string> server(ShmConf("shm/mth/invoke1", "req"));
    server.listen([](const std::string& req, std::string& resp) { resp = "shm:" + req; });

    Client<std::string, std::string> client("shm://shm/mth/invoke1?event=req");
    CHECK(client.wait_for_connected(5s));

    SUBCASE("sync-optional") {
      auto resp = client.invoke("ping");
      CHECK(resp.has_value());
      CHECK(*resp == "shm:ping");
    }

    SUBCASE("sync-ref-overload") {
      std::string out;
      CHECK(client.invoke("pong", out, 5s));
      CHECK(out == "shm:pong");
    }

    SUBCASE("async-future") {
      auto fut = client.async_invoke("async");
      REQUIRE(fut.wait_for(5s) == std::future_status::ready);
      CHECK(fut.get() == "shm:async");
    }

    SUBCASE("multiple-sequential") {
      for (int i = 0; i < 5; ++i) {
        auto resp = client.invoke("r" + std::to_string(i));
        CHECK(resp.has_value());
        CHECK(*resp == "shm:r" + std::to_string(i));
      }
    }
  }

  // TEST_CASE("shm-method-async-reply") {
  //   if (!ensure_shm_ready()) {
  //     return;
  //   }

  //   Server<std::string, std::string> server(ShmConf("shm/mth/async_reply1", "req"));
  //   server.listen([](const std::string& /*req*/, std::string& resp) { resp = "sync_shm"; });

  //   Client<std::string, std::string> client("shm://shm/mth/async_reply1?event=req");
  //   CHECK(client.wait_for_connected(5s));

  //   auto resp = client.invoke("request");
  //   CHECK(resp.has_value());
  //   CHECK(*resp == "sync_shm");

  //   Server<std::string, std::string> server2(ShmConf("shm/mth/async_reply1b", "req"));
  //   server2.listen_for_reply([](uint64_t, const std::string&) {});
  //   CHECK_FALSE(server2.reply(1, std::string("x")));
  // }

  TEST_CASE("shm-method-async-callback") {
    if (!ensure_shm_ready()) {
      return;
    }

    Server<std::string, std::string> server(ShmConf("shm/mth/cb1", "req"));
    server.listen([](const std::string& /*req*/, std::string& resp) { resp = "shm_cb"; });

    Client<std::string, std::string> client("shm://shm/mth/cb1?event=req");
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
    CHECK(resp_val == "shm_cb");
  }

  TEST_CASE("shm-method-detect-connected") {
    if (!ensure_shm_ready()) {
      return;
    }

    std::atomic<bool> connected_event{false};

    Server<std::string, std::string> server(ShmConf("shm/mth/detect_conn1", "req"));
    server.listen([](const std::string& /*req*/, std::string& resp) { resp = "ok"; });

    Client<std::string, std::string> client("shm://shm/mth/detect_conn1?event=req");
    client.detect_connected([&](bool connected) {
      if (connected) {
        connected_event.store(true, std::memory_order_release);
      }
    });

    for (int i = 0; i < 100 && !connected_event.load(std::memory_order_acquire); ++i) {
      std::this_thread::sleep_for(50ms);
    }

    CHECK(connected_event.load(std::memory_order_acquire));
  }
}

// ---------------------------------------------------------------------------
// Shm - field
// ---------------------------------------------------------------------------

TEST_SUITE("shm-field") {
  TEST_CASE("shm-field-setter-getter") {
    if (!ensure_shm_ready()) {
      return;
    }

    SUBCASE("polling-get") {
      Setter<Bytes> setter(ShmConf("shm/fld/poll1", "val", 0, 0, 1));
      Getter<Bytes> getter("shm://shm/fld/poll1?event=val");

      setter.set(Bytes{0x11, 0x22, 0x33});
      std::this_thread::sleep_for(300ms);

      auto v = getter.get();
      REQUIRE(v.has_value());
      REQUIRE(v->size() == 3);
      CHECK((*v)[0] == 0x11);
      CHECK((*v)[2] == 0x33);
    }

    SUBCASE("wait-for-value") {
      Setter<Bytes> setter(ShmConf("shm/fld/wait1", "val", 0, 0, 1));
      Getter<Bytes> getter("shm://shm/fld/wait1?event=val");

      std::thread writer([&] {
        std::this_thread::sleep_for(100ms);
        setter.set(Bytes{0xAB, 0xCD});
      });

      CHECK(getter.wait_for_value(5s));
      auto v = getter.get();
      REQUIRE(v.has_value());
      CHECK((*v)[0] == 0xAB);

      writer.join();
    }

    SUBCASE("listen-callback") {
      std::atomic<bool> notified{false};
      Bytes cb_val;

      Setter<Bytes> setter(ShmConf("shm/fld/cb1", "val", 0, 0, 1));
      Getter<Bytes> getter("shm://shm/fld/cb1?event=val");

      getter.listen([&](const Bytes& val) {
        cb_val = val;
        notified.store(true, std::memory_order_release);
      });

      std::this_thread::sleep_for(100ms);
      setter.set(Bytes{0xFF, 0x00});

      for (int i = 0; i < 100 && !notified.load(std::memory_order_acquire); ++i) {
        std::this_thread::sleep_for(50ms);
      }

      CHECK(notified.load(std::memory_order_acquire));
      REQUIRE(cb_val.size() == 2);
      CHECK(cb_val[0] == 0xFF);
    }

    SUBCASE("change-reporting") {
      std::atomic<int> cb_count{0};

      Setter<Bytes> setter(ShmConf("shm/fld/cr1", "val", 0, 0, 1));
      Getter<Bytes> getter("shm://shm/fld/cr1?event=val");

      getter.set_change_reporting(true);
      CHECK(getter.get_change_reporting());

      getter.listen([&](const Bytes& /*v*/) { cb_count.fetch_add(1, std::memory_order_relaxed); });

      std::this_thread::sleep_for(100ms);

      setter.set(Bytes{0x55});
      std::this_thread::sleep_for(200ms);
      setter.set(Bytes{0x55});
      std::this_thread::sleep_for(200ms);

      CHECK(cb_count.load() <= 1);
    }

    SUBCASE("late-getter-receives-cached-value") {
      Setter<Bytes> setter(ShmConf("shm/fld/late1", "val", 0, 0, 1));
      setter.set(Bytes{0xCA, 0xFE});
      std::this_thread::sleep_for(200ms);

      Getter<Bytes> late_getter("shm://shm/fld/late1?event=val");
      CHECK(late_getter.wait_for_value(5s));
      auto v = late_getter.get();
      REQUIRE(v.has_value());
      CHECK((*v)[0] == 0xCA);
    }
  }
}

// ---------------------------------------------------------------------------
// Shm - latency
// ---------------------------------------------------------------------------

TEST_SUITE("shm-latency") {
  TEST_CASE("shm-latency-stats") {
    if (!ensure_shm_ready()) {
      return;
    }

    Publisher<int> pub(ShmConf("shm/lat/sub1", "data"));
    Subscriber<int> sub("shm://shm/lat/sub1?event=data");

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
// Shm - identity
// ---------------------------------------------------------------------------

TEST_SUITE("shm-identity") {
  TEST_CASE("shm-node-identity") {
    if (!ensure_shm_ready()) {
      return;
    }

    Publisher<int> pub1(ShmConf("shm/id/p1", "data"));
    Publisher<int> pub2(ShmConf("shm/id/p2", "data"));
    Subscriber<int> sub("shm://shm/id/p1?event=data");

    CHECK(pub1.get_abstract_node() != nullptr);
    CHECK(pub2.get_abstract_node() != nullptr);
    CHECK(pub1.get_abstract_node() != pub2.get_abstract_node());
    CHECK(pub1.get_abstract_node() != sub.get_abstract_node());
  }
}

// ---------------------------------------------------------------------------
// Shm - dynamic
// ---------------------------------------------------------------------------

TEST_SUITE("shm-dynamic") {
  TEST_CASE("shm-dynamic") {
    if (!ensure_shm_ready()) {
      return;
    }

    std::atomic<bool> received{false};
    DynamicData captured;

    Publisher<DynamicData> pub(ShmConf("shm/dyn/int1", "data"));
    Subscriber<DynamicData> sub("shm://shm/dyn/int1?event=data");

    sub.listen([&](const DynamicData& d) {
      captured = d;
      received.store(true, std::memory_order_release);
    });

    CHECK(pub.wait_for_subscribers(5s));

    DynamicData d;
    d.load("int", 888);
    CHECK(pub.publish(d));

    for (int i = 0; i < 100 && !received.load(std::memory_order_acquire); ++i) {
      std::this_thread::sleep_for(50ms);
    }

    CHECK(received.load(std::memory_order_acquire));
    CHECK(captured.as<int>() == 888);
  }
}

#endif  // VLINK_SUPPORT_SHM

// NOLINTEND
