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

#if defined(VLINK_SUPPORT_MQTT)

#include <atomic>
#include <future>
#include <string>
#include <thread>

#include "./modules/mqtt_conf.h"

static bool is_mqtt_broker_available() {
  static int result = -1;

  if (result >= 0) {
    return result != 0;
  }

  Publisher<int> probe(MqttConf("vlink/test/probe", "probe"));
  result = probe.has_subscribers() ? 1 : 0;

  return result != 0;
}

#define MQTT_REQUIRE_BROKER()                                                  \
  do {                                                                         \
    if (!is_mqtt_broker_available()) {                                         \
      MESSAGE("MQTT broker not available at tcp://localhost:1883 - skipping"); \
      return;                                                                  \
    }                                                                          \
  } while (0)

// ---------------------------------------------------------------------------
// Mqtt - init
// ---------------------------------------------------------------------------

TEST_SUITE("mqtt-init") {
  TEST_CASE("conf-defaults") {
    MqttConf conf("vehicle/speed");

    CHECK(conf.address == "vehicle/speed");
    CHECK(conf.event.empty());
    CHECK(conf.domain == 0);
    CHECK(conf.qos == 1);
    CHECK(conf.fragment.empty());
    CHECK(conf.get_transport_type() == TransportType::kMqtt);
  }

  TEST_CASE("conf-with-all-fields") {
    MqttConf conf("my/topic", "my_event", 2, 0, "tcp://broker:1883");

    CHECK(conf.address == "my/topic");
    CHECK(conf.event == "my_event");
    CHECK(conf.domain == 2);
    CHECK(conf.qos == 0);
    CHECK(conf.fragment == "tcp://broker:1883");
  }

  TEST_CASE("conf-equality") {
    MqttConf a("addr1", "ev1", 0, 1, "");
    MqttConf b("addr1", "ev1", 0, 1, "");
    MqttConf c("addr2", "ev1", 0, 1, "");

    CHECK(a == b);
    CHECK(a != c);
  }

  TEST_CASE("url-parse-all-impl-types") {
    Url url("mqtt://mqtt/init/parse1?event=ev1");

    CHECK(url.parse(kPublisher));
    CHECK(url.parse(kSubscriber));
    CHECK(url.parse(kServer));
    CHECK(url.parse(kClient));
    CHECK(url.parse(kSetter));
    CHECK(url.parse(kGetter));
  }

  TEST_CASE("unknown-impl-type-throws") {
    Url url("mqtt://mqtt/init/parse2");

    CHECK_THROWS_AS(url.parse(kUnknownImplType), std::runtime_error);
  }

  TEST_CASE("invalid-transport-throws") { CHECK_THROWS(Publisher<int>("mqtt1://bad/url")); }

  TEST_CASE("conf-invalid-domain") {
    MqttConf conf("addr");
    conf.domain = -1;

    CHECK(!conf.is_valid());
  }

  TEST_CASE("conf-invalid-qos") {
    MqttConf conf("addr");
    conf.qos = 3;

    CHECK(!conf.is_valid());
  }

  TEST_CASE("conf-empty-address") {
    MqttConf conf("");

    CHECK(!conf.is_valid());
  }

  TEST_CASE("conf-valid-range") {
    for (int q = 0; q <= 2; ++q) {
      MqttConf conf("addr", "", 0, q);

      CHECK(conf.is_valid());
    }
  }
}

// ---------------------------------------------------------------------------
// Mqtt - event
// ---------------------------------------------------------------------------

TEST_SUITE("mqtt-event") {
  TEST_CASE("mqtt-event-pub-sub") {
    MQTT_REQUIRE_BROKER();

    std::atomic<bool> received{false};
    Bytes captured;

    Publisher<Bytes> pub(MqttConf("mqtt/evt/pubsub1", "data"));
    Subscriber<Bytes> sub("mqtt://mqtt/evt/pubsub1?event=data");

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

  TEST_CASE("mqtt-event-string") {
    MQTT_REQUIRE_BROKER();

    std::atomic<bool> received{false};
    std::string captured;

    Publisher<std::string> pub(MqttConf("mqtt/evt/str1", "data"));
    Subscriber<std::string> sub("mqtt://mqtt/evt/str1?event=data");

    sub.listen([&](const std::string& val) {
      captured = val;
      received.store(true, std::memory_order_release);
    });

    CHECK(pub.wait_for_subscribers(5s));
    CHECK(pub.publish(std::string("hello_mqtt")));

    for (int i = 0; i < 100 && !received.load(std::memory_order_acquire); ++i) {
      std::this_thread::sleep_for(30ms);
    }

    CHECK(received.load(std::memory_order_acquire));
    CHECK(captured == "hello_mqtt");
  }

  TEST_CASE("mqtt-event-int") {
    MQTT_REQUIRE_BROKER();

    std::atomic<int> captured{0};
    std::atomic<bool> received{false};

    Publisher<int> pub(MqttConf("mqtt/evt/int1", "data"));
    Subscriber<int> sub("mqtt://mqtt/evt/int1?event=data");

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

  TEST_CASE("mqtt-event-multi-pub") {
    MQTT_REQUIRE_BROKER();

    std::atomic<int> count{0};

    Publisher<int> pub(MqttConf("mqtt/evt/multi1", "data"));
    Subscriber<int> sub("mqtt://mqtt/evt/multi1?event=data");

    sub.listen([&](const int& /*v*/) { count.fetch_add(1, std::memory_order_relaxed); });

    CHECK(pub.wait_for_subscribers(5s));

    for (int i = 0; i < 10; ++i) {
      pub.publish(i);
      std::this_thread::sleep_for(20ms);
    }

    std::this_thread::sleep_for(100ms);

    CHECK(count.load() >= 10);
  }

  TEST_CASE("mqtt-event-multi-sub") {
    MQTT_REQUIRE_BROKER();

    std::atomic<int> count1{0};
    std::atomic<int> count2{0};

    Publisher<Bytes> pub(MqttConf("mqtt/evt/multisub1", "data"));
    Subscriber<Bytes> sub1("mqtt://mqtt/evt/multisub1?event=data");
    Subscriber<Bytes> sub2("mqtt://mqtt/evt/multisub1?event=data");

    sub1.listen([&](const Bytes& /*d*/) { count1.fetch_add(1, std::memory_order_relaxed); });
    sub2.listen([&](const Bytes& /*d*/) { count2.fetch_add(1, std::memory_order_relaxed); });

    CHECK(pub.wait_for_subscribers(5s));

    for (int i = 0; i < 3; ++i) {
      pub.publish(Bytes{static_cast<uint8_t>(i)});
      std::this_thread::sleep_for(50ms);
    }

    std::this_thread::sleep_for(100ms);

    CHECK(count1.load() >= 3);
    CHECK(count2.load() >= 3);
  }

  TEST_CASE("mqtt-event-force-publish") {
    MQTT_REQUIRE_BROKER();

    Publisher<Bytes> pub(MqttConf("mqtt/evt/force1", "data"));

    for (int i = 0; i < 5; ++i) {
      CHECK(pub.publish(Bytes{static_cast<uint8_t>(i)}, true));
    }
  }
}

// ---------------------------------------------------------------------------
// Mqtt - method
// ---------------------------------------------------------------------------

TEST_SUITE("mqtt-method") {
  TEST_CASE("mqtt-method-send") {
    MQTT_REQUIRE_BROKER();

    std::atomic<int> counter{0};

    Server<std::string> server(MqttConf("mqtt/mth/send1", "req"));
    server.listen([&](const std::string& /*req*/) { counter.fetch_add(1, std::memory_order_relaxed); });

    Client<std::string> client("mqtt://mqtt/mth/send1?event=req");
    CHECK(client.wait_for_connected(5s));
    CHECK(client.is_connected());

    CHECK(client.send("fire1"));
    std::this_thread::sleep_for(100ms);
    CHECK(counter.load() == 1);

    CHECK(client.send("fire2"));
    std::this_thread::sleep_for(100ms);
    CHECK(counter.load() == 2);
  }

  TEST_CASE("mqtt-method-invoke") {
    MQTT_REQUIRE_BROKER();

    Server<std::string, std::string> server(MqttConf("mqtt/mth/invoke1", "req"));
    server.listen([](const std::string& req, std::string& resp) { resp = "mqtt:" + req; });

    Client<std::string, std::string> client("mqtt://mqtt/mth/invoke1?event=req");
    CHECK(client.wait_for_connected(5s));

    SUBCASE("sync-optional") {
      auto resp = client.invoke("ping");
      REQUIRE(resp.has_value());
      CHECK(*resp == "mqtt:ping");
    }

    SUBCASE("sync-ref-overload") {
      std::string out;
      CHECK(client.invoke("pong", out, 5s));
      CHECK(out == "mqtt:pong");
    }

    SUBCASE("async-future") {
      auto fut = client.async_invoke("async");
      REQUIRE(fut.wait_for(5s) == std::future_status::ready);
      CHECK(fut.get() == "mqtt:async");
    }

    SUBCASE("multiple-sequential") {
      for (int i = 0; i < 5; ++i) {
        auto resp = client.invoke("r" + std::to_string(i));
        REQUIRE(resp.has_value());
        CHECK(*resp == "mqtt:r" + std::to_string(i));
      }
    }
  }

  TEST_CASE("mqtt-method-async-reply") {
    MQTT_REQUIRE_BROKER();

    std::atomic<uint64_t> saved_id{0};
    std::atomic<bool> req_received{false};

    Server<std::string, std::string> server(MqttConf("mqtt/mth/async_reply1", "req"));
    server.listen_for_reply([&](uint64_t req_id, const std::string& /*req*/) {
      saved_id.store(req_id, std::memory_order_release);
      req_received.store(true, std::memory_order_release);
    });

    Client<std::string, std::string> client("mqtt://mqtt/mth/async_reply1?event=req");
    CHECK(client.wait_for_connected(5s));

    auto fut = client.async_invoke("defer");

    for (int i = 0; i < 100 && !req_received.load(std::memory_order_acquire); ++i) {
      std::this_thread::sleep_for(30ms);
    }

    REQUIRE(req_received.load(std::memory_order_acquire));
    CHECK(server.reply(saved_id.load(), std::string("deferred_mqtt")));

    REQUIRE(fut.wait_for(5s) == std::future_status::ready);
    CHECK(fut.get() == "deferred_mqtt");
  }

  TEST_CASE("mqtt-method-async-callback") {
    MQTT_REQUIRE_BROKER();

    Server<std::string, std::string> server(MqttConf("mqtt/mth/cb1", "req"));
    server.listen([](const std::string& /*req*/, std::string& resp) { resp = "mqtt_cb"; });

    Client<std::string, std::string> client("mqtt://mqtt/mth/cb1?event=req");
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
    CHECK(resp_val == "mqtt_cb");
  }
}

// ---------------------------------------------------------------------------
// Mqtt - field
// ---------------------------------------------------------------------------

TEST_SUITE("mqtt-field") {
  TEST_CASE("mqtt-field-setter-getter") {
    MQTT_REQUIRE_BROKER();

    SUBCASE("polling-get") {
      Setter<Bytes> setter(MqttConf("mqtt/fld/poll1", "val"));
      Getter<Bytes> getter("mqtt://mqtt/fld/poll1?event=val");

      setter.set(Bytes{0xAA, 0xBB, 0xCC});
      std::this_thread::sleep_for(100ms);

      auto v = getter.get();
      REQUIRE(v.has_value());
      REQUIRE(v->size() == 3);
      CHECK((*v)[0] == 0xAA);
      CHECK((*v)[2] == 0xCC);
    }

    SUBCASE("wait-for-value") {
      Setter<Bytes> setter(MqttConf("mqtt/fld/wait1", "val"));
      Getter<Bytes> getter("mqtt://mqtt/fld/wait1?event=val");

      std::thread writer([&] {
        std::this_thread::sleep_for(50ms);
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

      Setter<Bytes> setter(MqttConf("mqtt/fld/cb1", "val"));
      Getter<Bytes> getter("mqtt://mqtt/fld/cb1?event=val");

      getter.listen([&](const Bytes& /*val*/) { notified.store(true, std::memory_order_release); });

      std::this_thread::sleep_for(50ms);
      setter.set(Bytes{0x5A});

      for (int i = 0; i < 100 && !notified.load(std::memory_order_acquire); ++i) {
        std::this_thread::sleep_for(30ms);
      }

      CHECK(notified.load(std::memory_order_acquire));
    }

    SUBCASE("change-reporting") {
      std::atomic<int> cb_count{0};

      Setter<Bytes> setter(MqttConf("mqtt/fld/cr1", "val"));
      Getter<Bytes> getter("mqtt://mqtt/fld/cr1?event=val");

      getter.set_change_reporting(true);
      getter.listen([&](const Bytes& /*v*/) { cb_count.fetch_add(1, std::memory_order_relaxed); });

      std::this_thread::sleep_for(50ms);

      setter.set(Bytes{0x77});
      std::this_thread::sleep_for(50ms);
      setter.set(Bytes{0x77});
      std::this_thread::sleep_for(50ms);

      CHECK(cb_count.load() <= 1);
    }
  }
}

// ---------------------------------------------------------------------------
// Mqtt - latency
// ---------------------------------------------------------------------------

TEST_SUITE("mqtt-latency") {
  TEST_CASE("mqtt-latency-stats") {
    MQTT_REQUIRE_BROKER();

    Publisher<int> pub(MqttConf("mqtt/lat/sub1", "data"));
    Subscriber<int> sub("mqtt://mqtt/lat/sub1?event=data");

    sub.set_latency_and_lost_enabled(true);
    CHECK(sub.is_latency_and_lost_enabled());

    std::atomic<int> count{0};
    sub.listen([&](const int& /*v*/) { count.fetch_add(1, std::memory_order_relaxed); });

    CHECK(pub.wait_for_subscribers(5s));

    for (int i = 0; i < 10; ++i) {
      pub.publish(i);
      std::this_thread::sleep_for(10ms);
    }

    std::this_thread::sleep_for(100ms);

    CHECK(count.load() > 0);
    CHECK(sub.get_latency() >= 0);
    CHECK(sub.get_lost().total >= 0);

    sub.set_latency_and_lost_enabled(false);
    CHECK(!sub.is_latency_and_lost_enabled());
  }
}

// ---------------------------------------------------------------------------
// Mqtt - identity
// ---------------------------------------------------------------------------

TEST_SUITE("mqtt-identity") {
  TEST_CASE("mqtt-node-identity") {
    MQTT_REQUIRE_BROKER();

    Publisher<int> pub1(MqttConf("mqtt/id/p1", "data"));
    Publisher<int> pub2(MqttConf("mqtt/id/p2", "data"));
    Subscriber<int> sub("mqtt://mqtt/id/p1?event=data");

    CHECK(pub1.get_abstract_node() != nullptr);
    CHECK(pub2.get_abstract_node() != nullptr);
    CHECK(pub1.get_abstract_node() != pub2.get_abstract_node());
    CHECK(pub1.get_abstract_node() != sub.get_abstract_node());
  }
}

// ---------------------------------------------------------------------------
// Mqtt - dynamic
// ---------------------------------------------------------------------------

TEST_SUITE("mqtt-dynamic") {
  TEST_CASE("mqtt-dynamic") {
    MQTT_REQUIRE_BROKER();

    std::atomic<bool> received{false};
    DynamicData captured;

    Publisher<DynamicData> pub(MqttConf("mqtt/dyn/int1", "data"));
    Subscriber<DynamicData> sub("mqtt://mqtt/dyn/int1?event=data");

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

#endif  // VLINK_SUPPORT_MQTT

// NOLINTEND
