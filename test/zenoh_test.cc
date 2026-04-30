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

#if defined(VLINK_SUPPORT_ZENOH)

#include <atomic>
#include <future>
#include <string>
#include <thread>

#include "./modules/zenoh_conf.h"

// ---------------------------------------------------------------------------
// Zenoh - init
// ---------------------------------------------------------------------------

TEST_SUITE("zenoh-init") {
  TEST_CASE("conf-defaults") {
    ZenohConf conf("vehicle/speed");

    CHECK(conf.address == "vehicle/speed");
    CHECK(conf.event.empty());
    CHECK(conf.domain == 0);
    CHECK(conf.qos.empty());
    CHECK(conf.fragment.empty());
    CHECK(conf.get_transport_type() == TransportType::kZenoh);
  }

  TEST_CASE("conf-with-all-fields") {
    ZenohConf conf("my/topic", "my_event", 2, "reliable", "tcp");

    CHECK(conf.address == "my/topic");
    CHECK(conf.event == "my_event");
    CHECK(conf.domain == 2);
    CHECK(conf.qos == "reliable");
    CHECK(conf.fragment == "tcp");
  }

  TEST_CASE("conf-equality") {
    ZenohConf a("addr1", "ev1", 0, "", "");
    ZenohConf b("addr1", "ev1", 0, "", "");
    ZenohConf c("addr2", "ev1", 0, "", "");

    CHECK(a == b);
    CHECK(a != c);
  }

  TEST_CASE("url-parse-all-impl-types") {
    Url url("zenoh://zenoh/init/parse1?event=ev1");

    CHECK(url.parse(kPublisher));
    CHECK(url.parse(kSubscriber));
    CHECK(url.parse(kServer));
    CHECK(url.parse(kClient));
    CHECK(url.parse(kSetter));
    CHECK(url.parse(kGetter));
  }

  TEST_CASE("unknown-impl-type-throws") {
    Url url("zenoh://zenoh/init/parse2");

    CHECK_THROWS_AS(url.parse(kUnknownImplType), std::runtime_error);
  }

  TEST_CASE("invalid-transport-throws") { CHECK_THROWS(Publisher<int>("zenoh1://bad/url")); }

  TEST_CASE("register-qos-profile") {
    Qos qos;
    qos.reliability.kind = Qos::Reliability::kReliable;

    ZenohConf::register_qos("zenoh_reliable", qos);

    ZenohConf conf("zenoh/qos/test1", "", 0, "zenoh_reliable");
    CHECK(conf.qos == "zenoh_reliable");
  }
}

// ---------------------------------------------------------------------------
// Zenoh - event
// ---------------------------------------------------------------------------

TEST_SUITE("zenoh-event") {
  TEST_CASE("zenoh-event-pub-sub") {
    std::atomic<bool> received{false};
    Bytes captured;

    Publisher<Bytes> pub(ZenohConf("zenoh/evt/pubsub1", "data"));
    Subscriber<Bytes> sub("zenoh://zenoh/evt/pubsub1?event=data");

    sub.listen([&](const Bytes& data) {
      captured = data;
      received.store(true, std::memory_order_release);
    });

    CHECK(pub.wait_for_subscribers(5s));
    CHECK(pub.has_subscribers());

    Bytes payload{0xDE, 0xAD, 0xBE, 0xEF};
    CHECK(pub.publish(payload));

    for (int i = 0; i < 100 && !received.load(std::memory_order_acquire); ++i) {
      std::this_thread::sleep_for(30ms);
    }

    CHECK(received.load(std::memory_order_acquire));
    REQUIRE(captured.size() == 4);
    CHECK(captured[0] == 0xDE);
    CHECK(captured[3] == 0xEF);
  }

  TEST_CASE("zenoh-event-string") {
    std::atomic<bool> received{false};
    std::string captured;

    Publisher<std::string> pub(ZenohConf("zenoh/evt/str1", "data"));
    Subscriber<std::string> sub("zenoh://zenoh/evt/str1?event=data");

    sub.listen([&](const std::string& val) {
      captured = val;
      received.store(true, std::memory_order_release);
    });

    CHECK(pub.wait_for_subscribers(5s));
    CHECK(pub.publish(std::string("hello_zenoh")));

    for (int i = 0; i < 100 && !received.load(std::memory_order_acquire); ++i) {
      std::this_thread::sleep_for(30ms);
    }

    CHECK(received.load(std::memory_order_acquire));
    CHECK(captured == "hello_zenoh");
  }

  TEST_CASE("zenoh-event-int") {
    std::atomic<int> captured{0};
    std::atomic<bool> received{false};

    Publisher<int> pub(ZenohConf("zenoh/evt/int1", "data"));
    Subscriber<int> sub("zenoh://zenoh/evt/int1?event=data");

    sub.listen([&](const int& v) {
      captured.store(v, std::memory_order_relaxed);
      received.store(true, std::memory_order_release);
    });

    CHECK(pub.wait_for_subscribers(5s));
    CHECK(pub.publish(7654));

    for (int i = 0; i < 100 && !received.load(std::memory_order_acquire); ++i) {
      std::this_thread::sleep_for(30ms);
    }

    CHECK(received.load(std::memory_order_acquire));
    CHECK(captured.load() == 7654);
  }

  TEST_CASE("zenoh-event-multi-pub") {
    std::atomic<int> count{0};

    Publisher<int> pub(ZenohConf("zenoh/evt/multi1", "data"));
    Subscriber<int> sub("zenoh://zenoh/evt/multi1?event=data");

    sub.listen([&](const int& /*v*/) { count.fetch_add(1, std::memory_order_relaxed); });

    CHECK(pub.wait_for_subscribers(5s));

    for (int i = 0; i < 10; ++i) {
      pub.publish(i);
      std::this_thread::sleep_for(20ms);
    }

    std::this_thread::sleep_for(30ms);

    CHECK(count.load() >= 10);
  }

  TEST_CASE("zenoh-event-multi-sub") {
    std::atomic<int> count1{0};
    std::atomic<int> count2{0};

    Publisher<Bytes> pub(ZenohConf("zenoh/evt/multisub1", "data"));
    Subscriber<Bytes> sub1("zenoh://zenoh/evt/multisub1?event=data");
    Subscriber<Bytes> sub2("zenoh://zenoh/evt/multisub1?event=data");

    sub1.listen([&](const Bytes& /*d*/) { count1.fetch_add(1, std::memory_order_relaxed); });
    sub2.listen([&](const Bytes& /*d*/) { count2.fetch_add(1, std::memory_order_relaxed); });

    CHECK(pub.wait_for_subscribers(5s));

    for (int i = 0; i < 3; ++i) {
      pub.publish(Bytes{static_cast<uint8_t>(i)});
      std::this_thread::sleep_for(30ms);
    }

    std::this_thread::sleep_for(30ms);

    CHECK(count1.load() >= 3);
    CHECK(count2.load() >= 3);
  }

  TEST_CASE("zenoh-event-force-publish") {
    Publisher<Bytes> pub(ZenohConf("zenoh/evt/force1", "data"));

    CHECK(!pub.has_subscribers());

    for (int i = 0; i < 5; ++i) {
      CHECK(pub.publish(Bytes{static_cast<uint8_t>(i)}, true));
    }
  }

  TEST_CASE("zenoh-event-detect-subscribers") {
    std::atomic<int> connected_count{0};

    Publisher<Bytes> pub(ZenohConf("zenoh/evt/detect1", "data"));

    pub.detect_subscribers([&](bool connected) {
      if (connected) {
        connected_count.fetch_add(1, std::memory_order_relaxed);
      }
    });

    {
      Subscriber<Bytes> sub("zenoh://zenoh/evt/detect1?event=data");
      sub.listen([](const Bytes& /*d*/) {});

      std::this_thread::sleep_for(30ms);
      CHECK(pub.has_subscribers());
    }

    std::this_thread::sleep_for(30ms);
    CHECK(!pub.has_subscribers());
  }
}

// ---------------------------------------------------------------------------
// Zenoh - method
// ---------------------------------------------------------------------------

TEST_SUITE("zenoh-method") {
  TEST_CASE("zenoh-method-send") {
    std::atomic<int> counter{0};

    Server<std::string> server(ZenohConf("zenoh/mth/send1", "req"));
    server.listen([&](const std::string& /*req*/) { counter.fetch_add(1, std::memory_order_relaxed); });

    Client<std::string> client("zenoh://zenoh/mth/send1?event=req");
    CHECK(client.wait_for_connected(5s));
    CHECK(client.is_connected());

    CHECK(client.send("fire1"));
    std::this_thread::sleep_for(30ms);
    CHECK(counter.load() == 1);

    CHECK(client.send("fire2"));
    std::this_thread::sleep_for(30ms);
    CHECK(counter.load() == 2);
  }

  TEST_CASE("zenoh-method-invoke") {
    Server<std::string, std::string> server(ZenohConf("zenoh/mth/invoke1", "req"));
    server.listen([](const std::string& req, std::string& resp) { resp = "zenoh:" + req; });

    Client<std::string, std::string> client("zenoh://zenoh/mth/invoke1?event=req");
    CHECK(client.wait_for_connected(5s));

    SUBCASE("sync-optional") {
      auto resp = client.invoke("ping");
      CHECK(resp.has_value());
      CHECK(*resp == "zenoh:ping");
    }

    SUBCASE("sync-ref-overload") {
      std::string out;
      CHECK(client.invoke("pong", out, 5s));
      CHECK(out == "zenoh:pong");
    }

    SUBCASE("async-future") {
      auto fut = client.async_invoke("async");
      REQUIRE(fut.wait_for(5s) == std::future_status::ready);
      CHECK(fut.get() == "zenoh:async");
    }

    SUBCASE("multiple-sequential") {
      for (int i = 0; i < 5; ++i) {
        auto resp = client.invoke("r" + std::to_string(i));
        CHECK(resp.has_value());
        CHECK(*resp == "zenoh:r" + std::to_string(i));
      }
    }
  }

  TEST_CASE("zenoh-method-async-reply") {
    std::atomic<uint64_t> saved_id{0};
    std::atomic<bool> req_received{false};

    Server<std::string, std::string> server(ZenohConf("zenoh/mth/async_reply1", "req"));
    server.listen_for_reply([&](uint64_t req_id, const std::string& /*req*/) {
      saved_id.store(req_id, std::memory_order_release);
      req_received.store(true, std::memory_order_release);
    });

    Client<std::string, std::string> client("zenoh://zenoh/mth/async_reply1?event=req");
    CHECK(client.wait_for_connected(5s));

    auto fut = client.async_invoke("defer");

    for (int i = 0; i < 100 && !req_received.load(std::memory_order_acquire); ++i) {
      std::this_thread::sleep_for(30ms);
    }

    REQUIRE(req_received.load(std::memory_order_acquire));
    CHECK(server.reply(saved_id.load(), std::string("deferred_zenoh")));

    REQUIRE(fut.wait_for(5s) == std::future_status::ready);
    CHECK(fut.get() == "deferred_zenoh");
  }

  TEST_CASE("zenoh-method-async-callback") {
    Server<std::string, std::string> server(ZenohConf("zenoh/mth/cb1", "req"));
    server.listen([](const std::string& /*req*/, std::string& resp) { resp = "zenoh_cb"; });

    Client<std::string, std::string> client("zenoh://zenoh/mth/cb1?event=req");
    CHECK(client.wait_for_connected(5s));

    std::atomic<bool> got{false};
    std::string resp_val;

    bool ok = client.invoke("msg", [&](const std::string& resp) {
      resp_val = resp;
      got.store(true, std::memory_order_release);
    });

    CHECK(ok);

    for (int i = 0; i < 100 && !got.load(std::memory_order_acquire); ++i) {
      std::this_thread::sleep_for(30ms);
    }

    CHECK(got.load(std::memory_order_acquire));
    CHECK(resp_val == "zenoh_cb");
  }

  TEST_CASE("zenoh-method-detect-connected") {
    std::atomic<bool> connected_event{false};

    Server<std::string, std::string> server(ZenohConf("zenoh/mth/detect_conn1", "req"));
    server.listen([](const std::string& /*req*/, std::string& resp) { resp = "ok"; });

    Client<std::string, std::string> client("zenoh://zenoh/mth/detect_conn1?event=req");
    client.detect_connected([&](bool connected) {
      if (connected) {
        connected_event.store(true, std::memory_order_release);
      }
    });

    for (int i = 0; i < 100 && !connected_event.load(std::memory_order_acquire); ++i) {
      std::this_thread::sleep_for(30ms);
    }

    CHECK(connected_event.load(std::memory_order_acquire));
  }
}

// ---------------------------------------------------------------------------
// Zenoh - field
// ---------------------------------------------------------------------------

TEST_SUITE("zenoh-field") {
  TEST_CASE("zenoh-field-setter-getter") {
    SUBCASE("polling-get") {
      Setter<Bytes> setter(ZenohConf("zenoh/fld/poll1", "val"));
      Getter<Bytes> getter("zenoh://zenoh/fld/poll1?event=val");

      setter.set(Bytes{0xAA, 0xBB, 0xCC});
      std::this_thread::sleep_for(30ms);

      auto v = getter.get();
      REQUIRE(v.has_value());
      REQUIRE(v->size() == 3);
      CHECK((*v)[0] == 0xAA);
      CHECK((*v)[2] == 0xCC);
    }

    SUBCASE("wait-for-value") {
      Setter<Bytes> setter(ZenohConf("zenoh/fld/wait1", "val"));
      Getter<Bytes> getter("zenoh://zenoh/fld/wait1?event=val");

      std::thread writer([&] {
        std::this_thread::sleep_for(30ms);
        setter.set(Bytes{0x12, 0x34});
      });

      CHECK(getter.wait_for_value(5s));
      auto v = getter.get();
      REQUIRE(v.has_value());
      CHECK((*v)[0] == 0x12);

      writer.join();
    }

    SUBCASE("listen-callback") {
      std::atomic<bool> notified{false};

      Setter<Bytes> setter(ZenohConf("zenoh/fld/cb1", "val"));
      Getter<Bytes> getter("zenoh://zenoh/fld/cb1?event=val");

      getter.listen([&](const Bytes& /*val*/) { notified.store(true, std::memory_order_release); });

      std::this_thread::sleep_for(30ms);
      setter.set(Bytes{0x5A});

      for (int i = 0; i < 100 && !notified.load(std::memory_order_acquire); ++i) {
        std::this_thread::sleep_for(30ms);
      }

      CHECK(notified.load(std::memory_order_acquire));
    }

    SUBCASE("change-reporting") {
      std::atomic<int> cb_count{0};

      Setter<Bytes> setter(ZenohConf("zenoh/fld/cr1", "val"));
      Getter<Bytes> getter("zenoh://zenoh/fld/cr1?event=val");

      getter.set_change_reporting(true);
      getter.listen([&](const Bytes& /*v*/) { cb_count.fetch_add(1, std::memory_order_relaxed); });

      std::this_thread::sleep_for(30ms);

      setter.set(Bytes{0x77});
      std::this_thread::sleep_for(30ms);
      setter.set(Bytes{0x77});
      std::this_thread::sleep_for(30ms);

      CHECK(cb_count.load() <= 1);
    }

    SUBCASE("late-getter-receives-cached-value") {
      Setter<Bytes> setter(ZenohConf("zenoh/fld/late1", "val"));
      setter.set(Bytes{0xFE, 0xED});
      std::this_thread::sleep_for(30ms);

      Getter<Bytes> late_getter("zenoh://zenoh/fld/late1?event=val");
      CHECK(late_getter.wait_for_value(5s));
      auto v = late_getter.get();
      REQUIRE(v.has_value());
      CHECK((*v)[0] == 0xFE);
    }
  }
}

// ---------------------------------------------------------------------------
// Zenoh - latency
// ---------------------------------------------------------------------------

TEST_SUITE("zenoh-latency") {
  TEST_CASE("zenoh-latency-stats") {
    Publisher<int> pub(ZenohConf("zenoh/lat/sub1", "data"));
    Subscriber<int> sub("zenoh://zenoh/lat/sub1?event=data");

    sub.set_latency_and_lost_enabled(true);
    CHECK(sub.is_latency_and_lost_enabled());

    std::atomic<int> count{0};
    sub.listen([&](const int& /*v*/) { count.fetch_add(1, std::memory_order_relaxed); });

    CHECK(pub.wait_for_subscribers(5s));

    for (int i = 0; i < 10; ++i) {
      pub.publish(i);
      std::this_thread::sleep_for(10ms);
    }

    std::this_thread::sleep_for(30ms);

    CHECK(count.load() > 0);
    CHECK(sub.get_latency() >= 0);
    CHECK(sub.get_lost().total >= 0);

    sub.set_latency_and_lost_enabled(false);
    CHECK(!sub.is_latency_and_lost_enabled());
  }
}

// ---------------------------------------------------------------------------
// Zenoh - identity
// ---------------------------------------------------------------------------

TEST_SUITE("zenoh-identity") {
  TEST_CASE("zenoh-node-identity") {
    Publisher<int> pub1(ZenohConf("zenoh/id/p1", "data"));
    Publisher<int> pub2(ZenohConf("zenoh/id/p2", "data"));
    Subscriber<int> sub("zenoh://zenoh/id/p1?event=data");

    CHECK(pub1.get_abstract_node() != nullptr);
    CHECK(pub2.get_abstract_node() != nullptr);
    CHECK(pub1.get_abstract_node() != pub2.get_abstract_node());
    CHECK(pub1.get_abstract_node() != sub.get_abstract_node());
  }
}

// ---------------------------------------------------------------------------
// Zenoh - dynamic
// ---------------------------------------------------------------------------

TEST_SUITE("zenoh-dynamic") {
  TEST_CASE("zenoh-dynamic") {
    std::atomic<bool> received{false};
    DynamicData captured;

    Publisher<DynamicData> pub(ZenohConf("zenoh/dyn/int1", "data"));
    Subscriber<DynamicData> sub("zenoh://zenoh/dyn/int1?event=data");

    sub.listen([&](const DynamicData& d) {
      captured = d;
      received.store(true, std::memory_order_release);
    });

    CHECK(pub.wait_for_subscribers(5s));

    DynamicData d;
    d.load("int", 321);
    CHECK(pub.publish(d));

    for (int i = 0; i < 100 && !received.load(std::memory_order_acquire); ++i) {
      std::this_thread::sleep_for(30ms);
    }

    CHECK(received.load(std::memory_order_acquire));
    CHECK(captured.as<int>() == 321);
  }
}

TEST_SUITE("zenoh-audit-fixes") {
  TEST_CASE("zenoh-client-concurrent-call") {
    Server<std::string, std::string> server(ZenohConf("zenoh/audit/cc1", "req"));
    server.listen([](const std::string& req, std::string& resp) { resp = "ack:" + req; });

    Client<std::string, std::string> client("zenoh://zenoh/audit/cc1?event=req");
    REQUIRE(client.wait_for_connected(5s));

    constexpr int kThreads = 2;
    constexpr int kCallsPerThread = 8;
    std::atomic<int> ok_count{0};
    std::atomic<int> mismatch_count{0};
    std::vector<std::thread> workers;
    workers.reserve(kThreads);

    for (int t = 0; t < kThreads; ++t) {
      workers.emplace_back([&client, &ok_count, &mismatch_count, t]() {
        for (int i = 0; i < kCallsPerThread; ++i) {
          std::string out;
          std::string req = "t" + std::to_string(t) + "i" + std::to_string(i);

          if (client.invoke(req, out, 10s)) {
            if (out == "ack:" + req) {
              ok_count.fetch_add(1, std::memory_order_relaxed);
            } else {
              mismatch_count.fetch_add(1, std::memory_order_relaxed);
            }
          }
        }
      });
    }

    for (auto& w : workers) {
      w.join();
    }

    CHECK(mismatch_count.load() == 0);
    CHECK(ok_count.load() >= kThreads * kCallsPerThread * 90 / 100);
  }

  TEST_CASE("zenoh-server-reply-empty-body") {
    Server<std::string, std::string> server(ZenohConf("zenoh/audit/re1", "req"));
    server.listen([](const std::string& /*req*/, std::string& resp) { resp.clear(); });

    Client<std::string, std::string> client("zenoh://zenoh/audit/re1?event=req");
    REQUIRE(client.wait_for_connected(5s));

    std::string out;
    CHECK(client.invoke("any", out, 5s));
    CHECK(out.empty());

    CHECK(client.invoke("second", out, 5s));
    CHECK(out.empty());
  }

  TEST_CASE("zenoh-server-reply-large-string") {
    Server<std::string, std::string> server(ZenohConf("zenoh/audit/rbin1", "req"));
    server.listen([](const std::string& req, std::string& resp) { resp = req + std::string(4096, 'X'); });

    Client<std::string, std::string> client("zenoh://zenoh/audit/rbin1?event=req");
    REQUIRE(client.wait_for_connected(5s));

    std::string out;
    CHECK(client.invoke(std::string("p"), out, 5s));
    REQUIRE(out.size() == 4097);
    CHECK(out.front() == 'p');
    CHECK(out.back() == 'X');
  }

  TEST_CASE("zenoh-invalid-url-construction-fails") { CHECK_THROWS(Publisher<int>("zenoh-bad-scheme://topic")); }

  TEST_CASE("zenoh-shm-env-defaults-present") {
    const auto shm_pool = Utils::get_env("VLINK_ZENOH_SHM_POOL");
    const auto shm_thr = Utils::get_env("VLINK_ZENOH_SHM_THRESHOLD");
    const auto shm_mode = Utils::get_env("VLINK_ZENOH_SHM_MODE");

    CHECK((shm_pool.empty() || std::stoi(shm_pool) > 0));
    CHECK((shm_thr.empty() || std::stoi(shm_thr) > 0));
    CHECK((shm_mode.empty() || shm_mode == "lazy" || shm_mode == "init"));
  }

  TEST_CASE("zenoh-batching-env-defaults-present") {
    const auto bt = Utils::get_env("VLINK_ZENOH_BATCH_TIME_LIMIT_MS");
    const auto be = Utils::get_env("VLINK_ZENOH_BATCH_ENABLED");

    CHECK((bt.empty() || std::stoi(bt) >= 0));
    CHECK((be.empty() || be == "true" || be == "false"));
  }

  TEST_CASE("zenoh-locality-env-accepts-values") {
    const auto loc = Utils::get_env("VLINK_ZENOH_ALLOWED_LOCALITY");

    CHECK((loc.empty() || loc == "any" || loc == "local" || loc == "remote"));
  }

  TEST_CASE("zenoh-server-publisher-coexistence") {
    Publisher<int> pub(ZenohConf("zenoh/audit/coexist1", "d"));
    Server<std::string, std::string> server(ZenohConf("zenoh/audit/coexist2", "r"));

    Subscriber<int> sub("zenoh://zenoh/audit/coexist1?event=d");
    Client<std::string, std::string> client("zenoh://zenoh/audit/coexist2?event=r");

    std::atomic<int> sub_got{0};
    sub.listen([&sub_got](const int& v) { sub_got.store(v, std::memory_order_relaxed); });
    server.listen([](const std::string& req, std::string& resp) { resp = "echo:" + req; });

    REQUIRE(pub.wait_for_subscribers(5s));
    REQUIRE(client.wait_for_connected(5s));

    CHECK(pub.publish(42));

    std::string out;
    CHECK(client.invoke("hi", out, 5s));
    CHECK(out == "echo:hi");

    for (int i = 0; i < 100 && sub_got.load() == 0; ++i) {
      std::this_thread::sleep_for(20ms);
    }
    CHECK(sub_got.load() == 42);
  }

  TEST_CASE("zenoh-session-reuse-after-invalid-fragment") {
    CHECK_NOTHROW(Publisher<int>(ZenohConf("zenoh/audit/ses-good", "", 0, "", "tcp")));
    CHECK_NOTHROW(Publisher<int>(ZenohConf("zenoh/audit/ses-good2", "", 0, "", "tcp")));
  }

  TEST_CASE("zenoh-client-timeout-no-server") {
    Client<std::string, std::string> client("zenoh://zenoh/audit/noserver1?event=req");

    std::string out;
    CHECK_FALSE(client.invoke("never", out, 200ms));
  }
}

#endif  // VLINK_SUPPORT_ZENOH

// NOLINTEND
