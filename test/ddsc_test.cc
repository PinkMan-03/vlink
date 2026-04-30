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

#if defined(VLINK_SUPPORT_DDSC)

#include <atomic>
#include <future>
#include <string>
#include <thread>

#include "./modules/ddsc_conf.h"

// ---------------------------------------------------------------------------
// Ddsc - init
// ---------------------------------------------------------------------------

TEST_SUITE("ddsc-init") {
  TEST_CASE("conf-defaults") {
    DdscConf conf("vehicle/speed");

    CHECK(conf.topic == "vehicle/speed");
    CHECK(conf.domain == 0);
    CHECK(conf.depth == 0);
    CHECK(conf.qos.empty());
    CHECK(conf.get_transport_type() == TransportType::kDdsc);
  }

  TEST_CASE("conf-with-domain-depth") {
    DdscConf conf("my/topic", 3, 20);

    CHECK(conf.domain == 3);
    CHECK(conf.depth == 20);
  }

  TEST_CASE("conf-with-named-qos") {
    DdscConf conf("my/topic", 0, 0, "best_effort");

    CHECK(conf.qos == "best_effort");
  }

  TEST_CASE("conf-equality") {
    DdscConf a("topic/a", 1, 5, "q1");
    DdscConf b("topic/a", 1, 5, "q1");
    DdscConf c("topic/b", 1, 5, "q1");

    CHECK(a == b);
    CHECK(a != c);
  }

  TEST_CASE("url-parse-all-impl-types") {
    Url url("ddsc://ddsc/init/parse1");

    CHECK(url.parse(kPublisher));
    CHECK(url.parse(kSubscriber));
    CHECK(url.parse(kServer));
    CHECK(url.parse(kClient));
    CHECK(url.parse(kSetter));
    CHECK(url.parse(kGetter));
  }

  TEST_CASE("unknown-impl-type-throws") {
    Url url("ddsc://ddsc/init/parse2");

    CHECK_THROWS_AS(url.parse(kUnknownImplType), std::runtime_error);
  }

  TEST_CASE("invalid-transport-throws") { CHECK_THROWS(Publisher<int>("ddsc1://bad/url")); }

  TEST_CASE("register-qos-profile") {
    Qos qos;
    qos.reliability.kind = Qos::Reliability::kReliable;

    DdscConf::register_qos("ddsc_reliable", qos);

    DdscConf conf("ddsc/qos/test1", 0, 0, "ddsc_reliable");
    CHECK(conf.qos == "ddsc_reliable");
  }
}

// ---------------------------------------------------------------------------
// Ddsc - event
// ---------------------------------------------------------------------------

TEST_SUITE("ddsc-event") {
  TEST_CASE("ddsc-event-pub-sub") {
    std::atomic<bool> received{false};
    Bytes captured;

    Publisher<Bytes> pub(DdscConf("ddsc/evt/pubsub1"));
    Subscriber<Bytes> sub("ddsc://ddsc/evt/pubsub1");

    sub.listen([&](const Bytes& data) {
      captured = data;
      received.store(true, std::memory_order_release);
    });

    CHECK(pub.wait_for_subscribers(5s));
    CHECK(pub.has_subscribers());

    Bytes payload{0xCA, 0xFE, 0xBA, 0xBE};
    CHECK(pub.publish(payload));

    for (int i = 0; i < 100 && !received.load(std::memory_order_acquire); ++i) {
      std::this_thread::sleep_for(30ms);
    }

    CHECK(received.load(std::memory_order_acquire));
    REQUIRE(captured.size() == 4);
    CHECK(captured[0] == 0xCA);
    CHECK(captured[3] == 0xBE);
  }

  TEST_CASE("ddsc-event-string") {
    std::atomic<bool> received{false};
    std::string captured;

    Publisher<std::string> pub(DdscConf("ddsc/evt/str1"));
    Subscriber<std::string> sub("ddsc://ddsc/evt/str1");

    sub.listen([&](const std::string& val) {
      captured = val;
      received.store(true, std::memory_order_release);
    });

    CHECK(pub.wait_for_subscribers(5s));
    CHECK(pub.publish(std::string("hello_ddsc")));

    for (int i = 0; i < 100 && !received.load(std::memory_order_acquire); ++i) {
      std::this_thread::sleep_for(30ms);
    }

    CHECK(received.load(std::memory_order_acquire));
    CHECK(captured == "hello_ddsc");
  }

  TEST_CASE("ddsc-event-int") {
    std::atomic<int> captured{0};
    std::atomic<bool> received{false};

    Publisher<int> pub(DdscConf("ddsc/evt/int1"));
    Subscriber<int> sub("ddsc://ddsc/evt/int1");

    sub.listen([&](const int& v) {
      captured.store(v, std::memory_order_relaxed);
      received.store(true, std::memory_order_release);
    });

    CHECK(pub.wait_for_subscribers(5s));
    CHECK(pub.publish(54321));

    for (int i = 0; i < 100 && !received.load(std::memory_order_acquire); ++i) {
      std::this_thread::sleep_for(30ms);
    }

    CHECK(received.load(std::memory_order_acquire));
    CHECK(captured.load() == 54321);
  }

  TEST_CASE("ddsc-event-multi-pub") {
    std::atomic<int> count{0};

    Publisher<int> pub(DdscConf("ddsc/evt/multi1"));
    Subscriber<int> sub("ddsc://ddsc/evt/multi1");

    sub.listen([&](const int& /*v*/) { count.fetch_add(1, std::memory_order_relaxed); });

    CHECK(pub.wait_for_subscribers(5s));

    for (int i = 0; i < 10; ++i) {
      pub.publish(i);
      std::this_thread::sleep_for(20ms);
    }

    std::this_thread::sleep_for(30ms);

    CHECK(count.load() >= 10);
  }

  TEST_CASE("ddsc-event-multi-sub") {
    std::atomic<int> count1{0};
    std::atomic<int> count2{0};

    Publisher<Bytes> pub(DdscConf("ddsc/evt/multisub1"));
    Subscriber<Bytes> sub1("ddsc://ddsc/evt/multisub1");
    Subscriber<Bytes> sub2("ddsc://ddsc/evt/multisub1");

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

  TEST_CASE("ddsc-event-force-publish") {
    Publisher<Bytes> pub(DdscConf("ddsc/evt/force1"));

    CHECK(!pub.has_subscribers());

    for (int i = 0; i < 5; ++i) {
      CHECK(pub.publish(Bytes{static_cast<uint8_t>(i)}, true));
    }
  }

  TEST_CASE("ddsc-event-detect-subscribers") {
    std::atomic<int> connected_count{0};

    Publisher<Bytes> pub(DdscConf("ddsc/evt/detect1"));

    pub.detect_subscribers([&](bool connected) {
      if (connected) {
        connected_count.fetch_add(1, std::memory_order_relaxed);
      }
    });

    {
      Subscriber<Bytes> sub("ddsc://ddsc/evt/detect1");
      sub.listen([](const Bytes& /*d*/) {});

      std::this_thread::sleep_for(30ms);
      CHECK(pub.has_subscribers());
    }

    std::this_thread::sleep_for(30ms);
    CHECK(!pub.has_subscribers());
  }

  TEST_CASE("ddsc-serialize") {
#if defined(VLINK_TEST_SUPPORT_PROTOBUF)
    SUBCASE("protobuf-pub-sub") {
      std::atomic<bool> received{false};
      pb::Message captured;

      Publisher<pb::Message> pub(DdscConf("ddsc/ser/pb1"));
      Subscriber<pb::Message> sub("ddsc://ddsc/ser/pb1");

      sub.listen([&](const pb::Message& msg) {
        captured = msg;
        received.store(true, std::memory_order_release);
      });

      CHECK(pub.wait_for_subscribers(5s));

      pb::Message msg;
      msg.set_value("ddsc_proto");
      CHECK(pub.publish(msg));

      for (int i = 0; i < 100 && !received.load(std::memory_order_acquire); ++i) {
        std::this_thread::sleep_for(30ms);
      }

      CHECK(received.load(std::memory_order_acquire));
      CHECK(captured.value() == "ddsc_proto");
    }
#endif

    SUBCASE("plain-bytes-always") {
      std::atomic<bool> received{false};

      Publisher<Bytes> pub(DdscConf("ddsc/ser/plain1"));
      Subscriber<Bytes> sub("ddsc://ddsc/ser/plain1");

      sub.listen([&](const Bytes& /*d*/) { received.store(true, std::memory_order_release); });

      CHECK(pub.wait_for_subscribers(5s));
      pub.publish(Bytes{0xAB, 0xCD});

      for (int i = 0; i < 100 && !received.load(std::memory_order_acquire); ++i) {
        std::this_thread::sleep_for(30ms);
      }

      CHECK(received.load(std::memory_order_acquire));
    }
  }
}

// ---------------------------------------------------------------------------
// Ddsc - method
// ---------------------------------------------------------------------------

TEST_SUITE("ddsc-method") {
  TEST_CASE("ddsc-method-send") {
    std::atomic<int> counter{0};

    Server<std::string> server(DdscConf("ddsc/mth/send1"));
    server.listen([&](const std::string& /*req*/) { counter.fetch_add(1, std::memory_order_relaxed); });

    Client<std::string> client("ddsc://ddsc/mth/send1");
    CHECK(client.wait_for_connected(5s));
    CHECK(client.is_connected());

    auto wait_for_counter = [&](int expected) {
      auto deadline = std::chrono::steady_clock::now() + 5s;
      while (counter.load(std::memory_order_acquire) < expected && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(10ms);
      }
    };

    CHECK(client.send("fire1"));
    wait_for_counter(1);
    CHECK(counter.load() == 1);

    CHECK(client.send("fire2"));
    wait_for_counter(2);
    CHECK(counter.load() == 2);
  }

  TEST_CASE("ddsc-method-invoke") {
    Server<std::string, std::string> server(DdscConf("ddsc/mth/invoke1"));
    server.listen([](const std::string& req, std::string& resp) { resp = "cyclone:" + req; });

    Client<std::string, std::string> client("ddsc://ddsc/mth/invoke1");
    CHECK(client.wait_for_connected(5s));

    SUBCASE("sync-optional") {
      auto resp = client.invoke("ping");
      CHECK(resp.has_value());
      CHECK(*resp == "cyclone:ping");
    }

    SUBCASE("sync-ref-overload") {
      std::string out;
      CHECK(client.invoke("pong", out, 5s));
      CHECK(out == "cyclone:pong");
    }

    SUBCASE("async-future") {
      auto fut = client.async_invoke("async");
      REQUIRE(fut.wait_for(5s) == std::future_status::ready);
      CHECK(fut.get() == "cyclone:async");
    }

    SUBCASE("multiple-sequential") {
      for (int i = 0; i < 5; ++i) {
        auto resp = client.invoke("r" + std::to_string(i));
        CHECK(resp.has_value());
        CHECK(*resp == "cyclone:r" + std::to_string(i));
      }
    }
  }

  TEST_CASE("ddsc-method-async-reply") {
    std::atomic<uint64_t> saved_id{0};
    std::atomic<bool> req_received{false};

    Server<std::string, std::string> server(DdscConf("ddsc/mth/async_reply1"));
    server.listen_for_reply([&](uint64_t req_id, const std::string& /*req*/) {
      saved_id.store(req_id, std::memory_order_release);
      req_received.store(true, std::memory_order_release);
    });

    Client<std::string, std::string> client("ddsc://ddsc/mth/async_reply1");
    CHECK(client.wait_for_connected(5s));

    auto fut = client.async_invoke("defer");

    for (int i = 0; i < 100 && !req_received.load(std::memory_order_acquire); ++i) {
      std::this_thread::sleep_for(30ms);
    }

    REQUIRE(req_received.load(std::memory_order_acquire));
    CHECK(server.reply(saved_id.load(), std::string("deferred_ddsc")));

    REQUIRE(fut.wait_for(5s) == std::future_status::ready);
    CHECK(fut.get() == "deferred_ddsc");
  }

  TEST_CASE("ddsc-method-async-callback") {
    Server<std::string, std::string> server(DdscConf("ddsc/mth/cb1"));
    server.listen([](const std::string& /*req*/, std::string& resp) { resp = "ddsc_cb"; });

    Client<std::string, std::string> client("ddsc://ddsc/mth/cb1");
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
    CHECK(resp_val == "ddsc_cb");
  }

  TEST_CASE("ddsc-method-detect-connected") {
    std::atomic<bool> connected_event{false};

    Server<std::string, std::string> server(DdscConf("ddsc/mth/detect_conn1"));
    server.listen([](const std::string& /*req*/, std::string& resp) { resp = "ok"; });

    Client<std::string, std::string> client("ddsc://ddsc/mth/detect_conn1");
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
// Ddsc - field
// ---------------------------------------------------------------------------

TEST_SUITE("ddsc-field") {
  TEST_CASE("ddsc-field-setter-getter") {
    SUBCASE("polling-get") {
      Setter<Bytes> setter(DdscConf("ddsc/fld/poll1"));
      Getter<Bytes> getter("ddsc://ddsc/fld/poll1");

      setter.set(Bytes{0xAA, 0xBB});
      std::this_thread::sleep_for(30ms);

      auto v = getter.get();
      REQUIRE(v.has_value());
      REQUIRE(v->size() == 2);
      CHECK((*v)[0] == 0xAA);
      CHECK((*v)[1] == 0xBB);
    }

    SUBCASE("wait-for-value") {
      Setter<Bytes> setter(DdscConf("ddsc/fld/wait1"));
      Getter<Bytes> getter("ddsc://ddsc/fld/wait1");

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

      Setter<Bytes> setter(DdscConf("ddsc/fld/cb1"));
      Getter<Bytes> getter("ddsc://ddsc/fld/cb1");

      getter.listen([&](const Bytes& /*val*/) { notified.store(true, std::memory_order_release); });

      std::this_thread::sleep_for(30ms);
      setter.set(Bytes{0x42});

      for (int i = 0; i < 100 && !notified.load(std::memory_order_acquire); ++i) {
        std::this_thread::sleep_for(30ms);
      }

      CHECK(notified.load(std::memory_order_acquire));
    }

    SUBCASE("change-reporting") {
      std::atomic<int> cb_count{0};

      Setter<Bytes> setter(DdscConf("ddsc/fld/cr1"));
      Getter<Bytes> getter("ddsc://ddsc/fld/cr1");

      getter.set_change_reporting(true);
      CHECK(getter.get_change_reporting());

      getter.listen([&](const Bytes& /*v*/) { cb_count.fetch_add(1, std::memory_order_relaxed); });

      std::this_thread::sleep_for(30ms);

      setter.set(Bytes{0x77});
      std::this_thread::sleep_for(30ms);
      setter.set(Bytes{0x77});
      std::this_thread::sleep_for(30ms);

      CHECK(cb_count.load() <= 1);
    }

    SUBCASE("late-getter-receives-cached-value") {
      Setter<Bytes> setter(DdscConf("ddsc/fld/late1"));
      setter.set(Bytes{0xBE, 0xEF});
      std::this_thread::sleep_for(30ms);

      Getter<Bytes> late_getter("ddsc://ddsc/fld/late1");
      CHECK(late_getter.wait_for_value(5s));
      auto v = late_getter.get();
      REQUIRE(v.has_value());
      CHECK((*v)[0] == 0xBE);
    }
  }
}

// ---------------------------------------------------------------------------
// Ddsc - latency
// ---------------------------------------------------------------------------

TEST_SUITE("ddsc-latency") {
  TEST_CASE("ddsc-latency-stats") {
    Publisher<int> pub(DdscConf("ddsc/lat/sub1"));
    Subscriber<int> sub("ddsc://ddsc/lat/sub1");

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
// Ddsc - identity
// ---------------------------------------------------------------------------

TEST_SUITE("ddsc-identity") {
  TEST_CASE("ddsc-node-identity") {
    Publisher<int> pub1(DdscConf("ddsc/id/p1"));
    Publisher<int> pub2(DdscConf("ddsc/id/p2"));
    Subscriber<int> sub("ddsc://ddsc/id/p1");

    const auto* n1 = pub1.get_abstract_node();
    const auto* n2 = pub2.get_abstract_node();
    const auto* n3 = sub.get_abstract_node();

    CHECK(n1 != nullptr);
    CHECK(n2 != nullptr);
    CHECK(n3 != nullptr);
    CHECK(n1 != n2);
    CHECK(n1 != n3);
  }
}

// ---------------------------------------------------------------------------
// Ddsc - dynamic
// ---------------------------------------------------------------------------

TEST_SUITE("ddsc-dynamic") {
  TEST_CASE("ddsc-dynamic") {
    std::atomic<bool> received{false};
    DynamicData captured;

    Publisher<DynamicData> pub(DdscConf("ddsc/dyn/int1"));
    Subscriber<DynamicData> sub("ddsc://ddsc/dyn/int1");

    sub.listen([&](const DynamicData& d) {
      captured = d;
      received.store(true, std::memory_order_release);
    });

    CHECK(pub.wait_for_subscribers(5s));

    DynamicData d;
    d.load("int", 42);
    CHECK(pub.publish(d));

    for (int i = 0; i < 100 && !received.load(std::memory_order_acquire); ++i) {
      std::this_thread::sleep_for(30ms);
    }

    CHECK(received.load(std::memory_order_acquire));
    CHECK(captured.as<int>() == 42);
  }
}

#endif  // VLINK_SUPPORT_DDSC

// NOLINTEND
