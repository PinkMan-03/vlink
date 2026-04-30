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

#if defined(VLINK_SUPPORT_DDS)

#include <atomic>
#include <future>
#include <string>
#include <thread>

#include "./modules/dds_conf.h"

// ---------------------------------------------------------------------------
// Dds - init
// ---------------------------------------------------------------------------

TEST_SUITE("dds-init") {
  TEST_CASE("conf-defaults") {
    DdsConf conf("vehicle/speed");

    CHECK(conf.topic == "vehicle/speed");
    CHECK(conf.domain == 0);
    CHECK(conf.depth == 0);
    CHECK(conf.qos.empty());
    CHECK(conf.qos_ext.empty());
    CHECK(conf.get_transport_type() == TransportType::kDds);
  }

  TEST_CASE("conf-with-domain-depth") {
    DdsConf conf("my/topic", 2, 16);

    CHECK(conf.domain == 2);
    CHECK(conf.depth == 16);
  }

  TEST_CASE("conf-with-named-qos") {
    DdsConf conf("my/topic", 0, 0, "reliable");

    CHECK(conf.qos == "reliable");
    CHECK(conf.qos_ext.empty());
  }

  TEST_CASE("conf-with-qos-ext") {
    DdsConf::PropertiesMap ext{{"writer", "RELIABLE"}, {"reader", "RELIABLE"}};
    DdsConf conf("my/topic", 0, ext);

    CHECK(conf.qos_ext == ext);
    CHECK(conf.qos.empty());
  }

  TEST_CASE("conf-equality") {
    DdsConf a("topic/x", 1, 8, "q1");
    DdsConf b("topic/x", 1, 8, "q1");
    DdsConf c("topic/y", 1, 8, "q1");

    CHECK(a == b);
    CHECK(!(a != b));
    CHECK(a != c);
  }

  TEST_CASE("url-parse-all-impl-types") {
    Url url("dds://dds/init/parse1");

    CHECK(url.parse(kPublisher));
    CHECK(url.parse(kSubscriber));
    CHECK(url.parse(kServer));
    CHECK(url.parse(kClient));
    CHECK(url.parse(kSetter));
    CHECK(url.parse(kGetter));
  }

  TEST_CASE("unknown-impl-type-throws") {
    Url url("dds://dds/init/parse2");

    CHECK_THROWS_AS(url.parse(kUnknownImplType), std::runtime_error);
  }

  TEST_CASE("invalid-transport-throws") { CHECK_THROWS(Publisher<int>("dds1://bad/url")); }

  TEST_CASE("register-qos-profile") {
    Qos qos;
    qos.reliability.kind = Qos::Reliability::kReliable;
    qos.durability.kind = Qos::Durability::kTransientLocal;

    DdsConf::register_qos("dds_reliable_tl", qos);

    DdsConf conf("dds/qos/test1", 0, 0, "dds_reliable_tl");
    CHECK(conf.qos == "dds_reliable_tl");
  }

  TEST_CASE("get-discovered-topics") { CHECK_NOTHROW((void)DdsConf::get_discovered_topics(0)); }
}

// ---------------------------------------------------------------------------
// Dds - event
// ---------------------------------------------------------------------------

TEST_SUITE("dds-event") {
  TEST_CASE("dds-event-pub-sub") {
    std::atomic<bool> received{false};
    Bytes captured;

    Publisher<Bytes> pub(DdsConf("dds/evt/pubsub1"));
    Subscriber<Bytes> sub("dds://dds/evt/pubsub1");

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

  TEST_CASE("dds-event-string") {
    std::atomic<bool> received{false};
    std::string captured;

    Publisher<std::string> pub(DdsConf("dds/evt/str1"));
    Subscriber<std::string> sub("dds://dds/evt/str1");

    sub.listen([&](const std::string& val) {
      captured = val;
      received.store(true, std::memory_order_release);
    });

    CHECK(pub.wait_for_subscribers(5s));
    CHECK(pub.publish(std::string("hello_dds")));

    for (int i = 0; i < 100 && !received.load(std::memory_order_acquire); ++i) {
      std::this_thread::sleep_for(30ms);
    }

    CHECK(received.load(std::memory_order_acquire));
    CHECK(captured == "hello_dds");
  }

  TEST_CASE("dds-event-int") {
    std::atomic<int> captured{0};
    std::atomic<bool> received{false};

    Publisher<int> pub(DdsConf("dds/evt/int1"));
    Subscriber<int> sub("dds://dds/evt/int1");

    sub.listen([&](const int& v) {
      captured.store(v, std::memory_order_relaxed);
      received.store(true, std::memory_order_release);
    });

    CHECK(pub.wait_for_subscribers(5s));
    CHECK(pub.publish(12345));

    for (int i = 0; i < 100 && !received.load(std::memory_order_acquire); ++i) {
      std::this_thread::sleep_for(30ms);
    }

    CHECK(received.load(std::memory_order_acquire));
    CHECK(captured.load() == 12345);
  }

  TEST_CASE("dds-event-multi-pub") {
    std::atomic<int> count{0};

    Publisher<int> pub(DdsConf("dds/evt/multi1"));
    Subscriber<int> sub("dds://dds/evt/multi1");

    sub.listen([&](const int& /*v*/) { count.fetch_add(1, std::memory_order_relaxed); });

    CHECK(pub.wait_for_subscribers(5s));

    for (int i = 0; i < 10; ++i) {
      pub.publish(i);
      std::this_thread::sleep_for(20ms);
    }

    std::this_thread::sleep_for(30ms);

    CHECK(count.load() >= 10);
  }

  TEST_CASE("dds-event-multi-sub") {
    std::atomic<int> count1{0};
    std::atomic<int> count2{0};

    Publisher<Bytes> pub(DdsConf("dds/evt/multisub1"));
    Subscriber<Bytes> sub1("dds://dds/evt/multisub1");
    Subscriber<Bytes> sub2("dds://dds/evt/multisub1");

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

  TEST_CASE("dds-event-force-publish") {
    Publisher<Bytes> pub(DdsConf("dds/evt/force1"));

    CHECK(!pub.has_subscribers());

    for (int i = 0; i < 5; ++i) {
      CHECK(pub.publish(Bytes{static_cast<uint8_t>(i)}, true));
    }
  }

  TEST_CASE("dds-event-detect-subscribers") {
    std::atomic<int> connected_count{0};
    std::atomic<int> disconnected_count{0};

    Publisher<Bytes> pub(DdsConf("dds/evt/detect1"));

    pub.detect_subscribers([&](bool connected) {
      if (connected) {
        connected_count.fetch_add(1, std::memory_order_relaxed);
      } else {
        disconnected_count.fetch_add(1, std::memory_order_relaxed);
      }
    });

    {
      Subscriber<Bytes> sub("dds://dds/evt/detect1");
      sub.listen([](const Bytes& /*d*/) {});

      std::this_thread::sleep_for(30ms);
      CHECK(pub.has_subscribers());
    }

    std::this_thread::sleep_for(30ms);
    CHECK(!pub.has_subscribers());
    CHECK(disconnected_count.load() >= 1);
  }

  TEST_CASE("dds-serialize") {
#if defined(VLINK_TEST_SUPPORT_PROTOBUF)
    SUBCASE("protobuf-pub-sub") {
      std::atomic<bool> received{false};
      pb::Message captured;

      Publisher<pb::Message> pub(DdsConf("dds/ser/pb1"));
      Subscriber<pb::Message> sub("dds://dds/ser/pb1");

      sub.listen([&](const pb::Message& msg) {
        captured = msg;
        received.store(true, std::memory_order_release);
      });

      CHECK(pub.wait_for_subscribers(5s));

      pb::Message msg;
      msg.set_value("dds_proto");
      CHECK(pub.publish(msg));

      for (int i = 0; i < 100 && !received.load(std::memory_order_acquire); ++i) {
        std::this_thread::sleep_for(30ms);
      }

      CHECK(received.load(std::memory_order_acquire));
      CHECK(captured.value() == "dds_proto");
    }

    SUBCASE("protobuf-rpc") {
      Server<pb::Request, pb::Response> server(DdsConf("dds/ser/pb_rpc1"));
      server.listen([](const pb::Request& req, pb::Response& resp) { resp.set_value(std::to_string(req.type() * 2)); });

      Client<pb::Request, pb::Response> client("dds://dds/ser/pb_rpc1");
      CHECK(client.wait_for_connected(5s));

      pb::Request req;
      req.set_type(10);
      auto resp = client.invoke(req);
      CHECK(resp.has_value());
      CHECK(resp->value() == "20");
    }
#endif

    SUBCASE("plain-bytes-always") {
      std::atomic<bool> received{false};

      Publisher<Bytes> pub(DdsConf("dds/ser/plain1"));
      Subscriber<Bytes> sub("dds://dds/ser/plain1");

      sub.listen([&](const Bytes& /*d*/) { received.store(true, std::memory_order_release); });

      CHECK(pub.wait_for_subscribers(5s));
      pub.publish(Bytes{0x01, 0x02, 0x03});

      for (int i = 0; i < 100 && !received.load(std::memory_order_acquire); ++i) {
        std::this_thread::sleep_for(30ms);
      }

      CHECK(received.load(std::memory_order_acquire));
    }
  }
}

// ---------------------------------------------------------------------------
// Dds - method
// ---------------------------------------------------------------------------

TEST_SUITE("dds-method") {
  TEST_CASE("dds-method-send") {
    std::atomic<int> counter{0};

    Server<std::string> server(DdsConf("dds/mth/send1"));
    server.listen([&](const std::string& /*req*/) { counter.fetch_add(1, std::memory_order_relaxed); });

    Client<std::string> client("dds://dds/mth/send1");
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

  TEST_CASE("dds-method-invoke") {
    Server<std::string, std::string> server(DdsConf("dds/mth/invoke1"));
    server.listen([](const std::string& req, std::string& resp) { resp = "echo:" + req; });

    Client<std::string, std::string> client("dds://dds/mth/invoke1");
    CHECK(client.wait_for_connected(5s));

    SUBCASE("sync-optional") {
      auto resp = client.invoke("ping");
      CHECK(resp.has_value());
      CHECK(*resp == "echo:ping");
    }

    SUBCASE("sync-ref-overload") {
      std::string out;
      CHECK(client.invoke("world", out, 5s));
      CHECK(out == "echo:world");
    }

    SUBCASE("async-future") {
      auto fut = client.async_invoke("async");
      REQUIRE(fut.wait_for(5s) == std::future_status::ready);
      CHECK(fut.get() == "echo:async");
    }

    SUBCASE("multiple-sequential") {
      for (int i = 0; i < 5; ++i) {
        auto resp = client.invoke("r" + std::to_string(i));
        CHECK(resp.has_value());
        CHECK(*resp == "echo:r" + std::to_string(i));
      }
    }
  }

  TEST_CASE("dds-method-async-reply") {
    std::atomic<uint64_t> saved_id{0};
    std::atomic<bool> req_received{false};

    Server<std::string, std::string> server(DdsConf("dds/mth/async_reply1"));
    server.listen_for_reply([&](uint64_t req_id, const std::string& /*req*/) {
      saved_id.store(req_id, std::memory_order_release);
      req_received.store(true, std::memory_order_release);
    });

    Client<std::string, std::string> client("dds://dds/mth/async_reply1");
    CHECK(client.wait_for_connected(5s));

    auto fut = client.async_invoke("deferred");

    for (int i = 0; i < 100 && !req_received.load(std::memory_order_acquire); ++i) {
      std::this_thread::sleep_for(30ms);
    }

    REQUIRE(req_received.load(std::memory_order_acquire));
    CHECK(server.reply(saved_id.load(), std::string("deferred_resp")));

    REQUIRE(fut.wait_for(5s) == std::future_status::ready);
    CHECK(fut.get() == "deferred_resp");
  }

  TEST_CASE("dds-method-async-callback") {
    Server<std::string, std::string> server(DdsConf("dds/mth/cb1"));
    server.listen([](const std::string& /*req*/, std::string& resp) { resp = "cb_ok"; });

    Client<std::string, std::string> client("dds://dds/mth/cb1");
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
    CHECK(resp_val == "cb_ok");
  }

  TEST_CASE("dds-method-detect-connected") {
    std::atomic<bool> connected_event{false};

    Server<std::string, std::string> server(DdsConf("dds/mth/detect_conn1"));
    server.listen([](const std::string& /*req*/, std::string& resp) { resp = "ok"; });

    Client<std::string, std::string> client("dds://dds/mth/detect_conn1");
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
// Dds - field
// ---------------------------------------------------------------------------

TEST_SUITE("dds-field") {
  TEST_CASE("dds-field-setter-getter") {
    SUBCASE("polling-get") {
      Setter<Bytes> setter(DdsConf("dds/fld/poll1"));
      Getter<Bytes> getter("dds://dds/fld/poll1");

      setter.set(Bytes{0x11, 0x22, 0x33});
      std::this_thread::sleep_for(30ms);

      auto v = getter.get();
      REQUIRE(v.has_value());
      REQUIRE(v->size() == 3);
      CHECK((*v)[0] == 0x11);
      CHECK((*v)[2] == 0x33);
    }

    SUBCASE("wait-for-value") {
      Setter<Bytes> setter(DdsConf("dds/fld/wait1"));
      Getter<Bytes> getter("dds://dds/fld/wait1");

      std::thread writer([&] {
        std::this_thread::sleep_for(30ms);
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

      Setter<Bytes> setter(DdsConf("dds/fld/cb1"));
      Getter<Bytes> getter("dds://dds/fld/cb1");

      getter.listen([&](const Bytes& val) {
        cb_val = val;
        notified.store(true, std::memory_order_release);
      });

      std::this_thread::sleep_for(30ms);
      setter.set(Bytes{0xFF, 0x00});

      for (int i = 0; i < 100 && !notified.load(std::memory_order_acquire); ++i) {
        std::this_thread::sleep_for(30ms);
      }

      CHECK(notified.load(std::memory_order_acquire));
      REQUIRE(cb_val.size() == 2);
      CHECK(cb_val[0] == 0xFF);
    }

    SUBCASE("change-reporting-suppresses-duplicate") {
      std::atomic<int> cb_count{0};

      Setter<Bytes> setter(DdsConf("dds/fld/cr1"));
      Getter<Bytes> getter("dds://dds/fld/cr1");

      getter.set_change_reporting(true);
      CHECK(getter.get_change_reporting());

      getter.listen([&](const Bytes& /*v*/) { cb_count.fetch_add(1, std::memory_order_relaxed); });

      std::this_thread::sleep_for(30ms);

      setter.set(Bytes{0x55});
      std::this_thread::sleep_for(30ms);
      setter.set(Bytes{0x55});
      std::this_thread::sleep_for(30ms);

      CHECK(cb_count.load() <= 1);
    }

    SUBCASE("late-getter-receives-cached-value") {
      Setter<Bytes> setter(DdsConf("dds/fld/late1"));
      setter.set(Bytes{0xCA, 0xFE});
      std::this_thread::sleep_for(30ms);

      Getter<Bytes> late_getter("dds://dds/fld/late1");
      CHECK(late_getter.wait_for_value(5s));
      auto v = late_getter.get();
      REQUIRE(v.has_value());
      CHECK((*v)[0] == 0xCA);
    }

    SUBCASE("multiple-sequential-sets") {
      Setter<int> setter(DdsConf("dds/fld/seq1"));
      Getter<int> getter("dds://dds/fld/seq1");

      for (int i = 1; i <= 5; ++i) {
        setter.set(i * 10);
        std::this_thread::sleep_for(30ms);
      }

      auto v = getter.get();
      REQUIRE(v.has_value());
      CHECK(*v == 50);
    }
  }
}

// ---------------------------------------------------------------------------
// Dds - latency
// ---------------------------------------------------------------------------

TEST_SUITE("dds-latency") {
  TEST_CASE("dds-latency-stats") {
    Publisher<int> pub(DdsConf("dds/lat/sub1"));
    Subscriber<int> sub("dds://dds/lat/sub1");

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

  TEST_CASE("dds-latency-stats-getter") {
    Setter<int> setter(DdsConf("dds/lat/get1"));
    Getter<int> getter("dds://dds/lat/get1");

    getter.set_latency_and_lost_enabled(true);
    CHECK(getter.is_latency_and_lost_enabled());

    getter.listen([](const int& /*v*/) {});
    std::this_thread::sleep_for(30ms);

    for (int i = 0; i < 5; ++i) {
      setter.set(i);
      std::this_thread::sleep_for(30ms);
    }

    std::this_thread::sleep_for(30ms);

    CHECK(getter.get_latency() >= 0);
    getter.set_latency_and_lost_enabled(false);
    CHECK(!getter.is_latency_and_lost_enabled());
  }
}

// ---------------------------------------------------------------------------
// Dds - identity
// ---------------------------------------------------------------------------

TEST_SUITE("dds-identity") {
  TEST_CASE("dds-node-identity") {
    Publisher<int> pub1(DdsConf("dds/id/p1"));
    Publisher<int> pub2(DdsConf("dds/id/p2"));
    Subscriber<int> sub("dds://dds/id/p1");

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
// Dds - dynamic
// ---------------------------------------------------------------------------

TEST_SUITE("dds-dynamic") {
  TEST_CASE("dds-dynamic") {
    SUBCASE("int-payload") {
      std::atomic<bool> received{false};
      DynamicData captured;

      Publisher<DynamicData> pub(DdsConf("dds/dyn/int1"));
      Subscriber<DynamicData> sub("dds://dds/dyn/int1");

      sub.listen([&](const DynamicData& d) {
        captured = d;
        received.store(true, std::memory_order_release);
      });

      CHECK(pub.wait_for_subscribers(5s));

      DynamicData d;
      d.load("int", 999);
      CHECK(pub.publish(d));

      for (int i = 0; i < 100 && !received.load(std::memory_order_acquire); ++i) {
        std::this_thread::sleep_for(30ms);
      }

      CHECK(received.load(std::memory_order_acquire));
      CHECK(captured.as<int>() == 999);
    }

    SUBCASE("string-payload") {
      std::atomic<bool> received{false};
      DynamicData captured;

      Publisher<DynamicData> pub(DdsConf("dds/dyn/str1"));
      Subscriber<DynamicData> sub("dds://dds/dyn/str1");

      sub.listen([&](const DynamicData& d) {
        captured = d;
        received.store(true, std::memory_order_release);
      });

      CHECK(pub.wait_for_subscribers(5s));

      DynamicData d;
      d.load("str", std::string("dds_dynamic"));
      CHECK(pub.publish(d));

      for (int i = 0; i < 100 && !received.load(std::memory_order_acquire); ++i) {
        std::this_thread::sleep_for(30ms);
      }

      CHECK(received.load(std::memory_order_acquire));
      CHECK(captured.as<std::string>() == "dds_dynamic");
    }
  }
}

#endif  // VLINK_SUPPORT_DDS

// NOLINTEND
