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

#if defined(VLINK_SUPPORT_SHM2)

#include <atomic>
#include <future>
#include <string>
#include <thread>
#include <vector>

#include "./modules/shm2_conf.h"

// ---------------------------------------------------------------------------
// Shm2 - init
// ---------------------------------------------------------------------------

TEST_SUITE("shm2-init") {
  TEST_CASE("conf-defaults") {
    Shm2Conf conf("vehicle/speed");

    CHECK(conf.address == "vehicle/speed");
    CHECK(conf.event.empty());
    CHECK(conf.domain == 0);
    CHECK(conf.depth == 0);
    CHECK(conf.history == 0);
    CHECK(conf.wait == 0);
    CHECK(conf.size == Shm2Conf::kDefaultMemSize);
    CHECK(conf.get_transport_type() == TransportType::kShm2);
  }

  TEST_CASE("default-mem-size-is-128") { CHECK(Shm2Conf::kDefaultMemSize == 128U); }

  TEST_CASE("max-mem-size-is-32mib") { CHECK(Shm2Conf::kMaxMemSize == 1024UL * 1024UL * 32); }

  TEST_CASE("conf-with-all-fields") {
    Shm2Conf conf("my/topic", "my_event", 1, 8, 2, 0, 4096);

    CHECK(conf.address == "my/topic");
    CHECK(conf.event == "my_event");
    CHECK(conf.domain == 1);
    CHECK(conf.depth == 8);
    CHECK(conf.history == 2);
    CHECK(conf.size == 4096);
  }

  TEST_CASE("conf-equality") {
    Shm2Conf a("addr1", "ev1", 0, 0, 0, 0, 128);
    Shm2Conf b("addr1", "ev1", 0, 0, 0, 0, 128);
    Shm2Conf c("addr2", "ev1", 0, 0, 0, 0, 128);
    Shm2Conf d("addr1", "ev1", 0, 0, 0, 0, 256);

    CHECK(a == b);
    CHECK(a != c);
    CHECK(a != d);
  }

  TEST_CASE("url-parse-all-impl-types") {
    Url url("shm2://shm2/init/parse1?event=ev1");

    CHECK(url.parse(kPublisher));
    CHECK(url.parse(kSubscriber));
    CHECK(url.parse(kServer));
    CHECK(url.parse(kClient));
    CHECK(url.parse(kSetter));
    CHECK(url.parse(kGetter));
  }

  TEST_CASE("unknown-impl-type-throws") {
    Url url("shm2://shm2/init/parse2");

    CHECK_THROWS_AS(url.parse(kUnknownImplType), std::runtime_error);
  }

  TEST_CASE("url-with-size-fragment") {
    Url url("shm2://shm2/init/size1#1M");

    CHECK(url.parse(kPublisher));
  }

  TEST_CASE("invalid-transport-throws") { CHECK_THROWS(Publisher<int>("shm21://bad/url")); }
}

// ---------------------------------------------------------------------------
// Shm2 - event
// ---------------------------------------------------------------------------

TEST_SUITE("shm2-event") {
  TEST_CASE("shm2-event-pub-sub") {
    std::atomic<bool> received{false};
    Bytes captured;

    Publisher<Bytes> pub(Shm2Conf("shm2/evt/pubsub1", "data", 0, 0, 0, 0, 1024));
    Subscriber<Bytes> sub("shm2://shm2/evt/pubsub1?event=data#1K");

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

  TEST_CASE("shm2-event-string") {
    std::atomic<bool> received{false};
    std::string captured;

    Publisher<std::string> pub(Shm2Conf("shm2/evt/str1", "data", 0, 0, 0, 0, 512));
    Subscriber<std::string> sub("shm2://shm2/evt/str1?event=data#512");

    sub.listen([&](const std::string& val) {
      captured = val;
      received.store(true, std::memory_order_release);
    });

    CHECK(pub.wait_for_subscribers(5s));
    CHECK(pub.publish(std::string("hello_shm2")));

    for (int i = 0; i < 100 && !received.load(std::memory_order_acquire); ++i) {
      std::this_thread::sleep_for(50ms);
    }

    CHECK(received.load(std::memory_order_acquire));
    CHECK(captured == "hello_shm2");
  }

  TEST_CASE("shm2-event-int") {
    std::atomic<int> captured{0};
    std::atomic<bool> received{false};

    Publisher<int> pub(Shm2Conf("shm2/evt/int1", "data", 0, 0, 0, 0, 128));
    Subscriber<int> sub("shm2://shm2/evt/int1?event=data");

    sub.listen([&](const int& v) {
      captured.store(v, std::memory_order_relaxed);
      received.store(true, std::memory_order_release);
    });

    CHECK(pub.wait_for_subscribers(5s));
    CHECK(pub.publish(9999));

    for (int i = 0; i < 100 && !received.load(std::memory_order_acquire); ++i) {
      std::this_thread::sleep_for(50ms);
    }

    CHECK(received.load(std::memory_order_acquire));
    CHECK(captured.load() == 9999);
  }

  TEST_CASE("shm2-event-multi-pub") {
    std::atomic<int> count{0};

    Publisher<int> pub(Shm2Conf("shm2/evt/multi1", "data", 0, 0, 0, 0, 128));
    Subscriber<int> sub("shm2://shm2/evt/multi1?event=data");

    sub.listen([&](const int& /*v*/) { count.fetch_add(1, std::memory_order_relaxed); });

    CHECK(pub.wait_for_subscribers(5s));

    for (int i = 0; i < 10; ++i) {
      pub.publish(i);
      std::this_thread::sleep_for(20ms);
    }

    std::this_thread::sleep_for(300ms);

    CHECK(count.load() >= 10);
  }

  TEST_CASE("shm2-event-multi-sub") {
    std::atomic<int> count1{0};
    std::atomic<int> count2{0};

    Publisher<Bytes> pub(Shm2Conf("shm2/evt/multisub1", "data", 0, 0, 0, 0, 1024));
    Subscriber<Bytes> sub1("shm2://shm2/evt/multisub1?event=data#1K");
    Subscriber<Bytes> sub2("shm2://shm2/evt/multisub1?event=data#1K");

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

  TEST_CASE("shm2-event-force-publish") {
    Publisher<Bytes> pub(Shm2Conf("shm2/evt/force1", "data", 0, 0, 0, 0, 256));

    CHECK(!pub.has_subscribers());

    for (int i = 0; i < 5; ++i) {
      CHECK(pub.publish(Bytes{static_cast<uint8_t>(i)}, true));
    }
  }

  TEST_CASE("shm2-event-detect-subscribers") {
    std::atomic<int> connected_count{0};

    Publisher<Bytes> pub(Shm2Conf("shm2/evt/detect1", "data", 0, 0, 0, 0, 256));

    pub.detect_subscribers([&](bool connected) {
      if (connected) {
        connected_count.fetch_add(1, std::memory_order_relaxed);
      }
    });

    {
      Subscriber<Bytes> sub("shm2://shm2/evt/detect1?event=data#256");
      sub.listen([](const Bytes& /*d*/) {});

      std::this_thread::sleep_for(300ms);
      CHECK(pub.has_subscribers());
    }

    std::this_thread::sleep_for(300ms);
    CHECK(!pub.has_subscribers());
  }
}

// ---------------------------------------------------------------------------
// Shm2 - method
// ---------------------------------------------------------------------------

TEST_SUITE("shm2-method") {
  TEST_CASE("shm2-method-send") {
    std::atomic<int> counter{0};

    Server<std::string> server(Shm2Conf("shm2/mth/send1", "req", 0, 0, 0, 0, 512));
    server.listen([&](const std::string& /*req*/) { counter.fetch_add(1, std::memory_order_relaxed); });

    Client<std::string> client("shm2://shm2/mth/send1?event=req#512");
    CHECK(client.wait_for_connected(5s));
    CHECK(client.is_connected());

    CHECK(client.send("fire1"));
    std::this_thread::sleep_for(200ms);
    CHECK(counter.load() == 1);

    CHECK(client.send("fire2"));
    std::this_thread::sleep_for(200ms);
    CHECK(counter.load() == 2);
  }

  TEST_CASE("shm2-method-invoke") {
    Server<std::string, std::string> server(Shm2Conf("shm2/mth/invoke1", "req", 0, 0, 0, 0, 512));
    server.listen([](const std::string& req, std::string& resp) { resp = "shm2:" + req; });

    Client<std::string, std::string> client("shm2://shm2/mth/invoke1?event=req#512");
    CHECK(client.wait_for_connected(5s));

    SUBCASE("sync-optional") {
      auto resp = client.invoke("ping");
      CHECK(resp.has_value());
      CHECK(*resp == "shm2:ping");
    }

    SUBCASE("sync-ref-overload") {
      std::string out;
      CHECK(client.invoke("pong", out, 5s));
      CHECK(out == "shm2:pong");
    }

    SUBCASE("async-future") {
      auto fut = client.async_invoke("async");
      REQUIRE(fut.wait_for(5s) == std::future_status::ready);
      CHECK(fut.get() == "shm2:async");
    }

    SUBCASE("multiple-sequential") {
      for (int i = 0; i < 5; ++i) {
        auto resp = client.invoke("r" + std::to_string(i));
        CHECK(resp.has_value());
        CHECK(*resp == "shm2:r" + std::to_string(i));
      }
    }
  }

  TEST_CASE("shm2-method-async-reply") {
    // Shm2ServerImpl does not support deferred async reply (is_sync=false).
    // The req_id is also ignored; reply() requires is_sync=true internally.
    // Verify that the synchronous RPC path still works, and that the
    // deferred-reply overload is explicitly unsupported.
    Server<std::string, std::string> server(Shm2Conf("shm2/mth/async_reply1", "req", 0, 0, 0, 0, 512));
    server.listen([](const std::string& /*req*/, std::string& resp) { resp = "sync_shm2"; });

    Client<std::string, std::string> client("shm2://shm2/mth/async_reply1?event=req#512");
    CHECK(client.wait_for_connected(5s));

    auto resp = client.invoke("request");
    CHECK(resp.has_value());
    CHECK(*resp == "sync_shm2");

    // Verify deferred-reply overload is not supported at runtime.
    Server<std::string, std::string> server2(Shm2Conf("shm2/mth/async_reply1b", "req", 0, 0, 0, 0, 512));
    server2.listen_for_reply([](uint64_t, const std::string&) {});
    CHECK_FALSE(server2.reply(1, std::string("x")));
  }

  TEST_CASE("shm2-method-async-callback") {
    Server<std::string, std::string> server(Shm2Conf("shm2/mth/cb1", "req", 0, 0, 0, 0, 512));
    server.listen([](const std::string& /*req*/, std::string& resp) { resp = "shm2_cb"; });

    Client<std::string, std::string> client("shm2://shm2/mth/cb1?event=req#512");
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
    CHECK(resp_val == "shm2_cb");
  }
}

// ---------------------------------------------------------------------------
// Shm2 - field
// ---------------------------------------------------------------------------

TEST_SUITE("shm2-field") {
  TEST_CASE("shm2-field-setter-getter") {
    SUBCASE("polling-get") {
      Setter<Bytes> setter(Shm2Conf("shm2/fld/poll1", "val", 0, 0, 1, 0, 256));
      Getter<Bytes> getter("shm2://shm2/fld/poll1?event=val#256");

      setter.set(Bytes{0x11, 0x22, 0x33});
      std::this_thread::sleep_for(300ms);

      auto v = getter.get();
      REQUIRE(v.has_value());
      REQUIRE(v->size() == 3);
      CHECK((*v)[0] == 0x11);
      CHECK((*v)[2] == 0x33);
    }

    SUBCASE("wait-for-value") {
      Setter<Bytes> setter(Shm2Conf("shm2/fld/wait1", "val", 0, 0, 1, 0, 256));
      Getter<Bytes> getter("shm2://shm2/fld/wait1?event=val#256");

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

      Setter<Bytes> setter(Shm2Conf("shm2/fld/cb1", "val", 0, 0, 1, 0, 256));
      Getter<Bytes> getter("shm2://shm2/fld/cb1?event=val#256");

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

      Setter<Bytes> setter(Shm2Conf("shm2/fld/cr1", "val", 0, 0, 1, 0, 256));
      Getter<Bytes> getter("shm2://shm2/fld/cr1?event=val#256");

      getter.set_change_reporting(true);
      getter.listen([&](const Bytes& /*v*/) { cb_count.fetch_add(1, std::memory_order_relaxed); });

      std::this_thread::sleep_for(100ms);

      setter.set(Bytes{0x55});
      std::this_thread::sleep_for(200ms);
      setter.set(Bytes{0x55});
      std::this_thread::sleep_for(200ms);

      CHECK(cb_count.load() <= 1);
    }

    SUBCASE("late-getter-receives-cached-value") {
      Setter<Bytes> setter(Shm2Conf("shm2/fld/late1", "val", 0, 0, 1, 0, 256));
      setter.set(Bytes{0xCA, 0xFE});
      std::this_thread::sleep_for(200ms);

      Getter<Bytes> late_getter("shm2://shm2/fld/late1?event=val#256");
      CHECK(late_getter.wait_for_value(5s));
      auto v = late_getter.get();
      REQUIRE(v.has_value());
      CHECK((*v)[0] == 0xCA);
    }

    SUBCASE("custom-mem-size") {
      // Test with a larger memory size
      uint64_t custom_size = 4096;
      Setter<Bytes> setter(Shm2Conf("shm2/fld/bigmem1", "val", 0, 0, 1, 0, custom_size));
      Getter<Bytes> getter("shm2://shm2/fld/bigmem1?event=val#4K");

      std::vector<uint8_t> raw_fill(1024, 0xAB);
      Bytes large_payload = Bytes::deep_copy(raw_fill.data(), raw_fill.size());
      setter.set(large_payload);
      std::this_thread::sleep_for(300ms);

      auto v = getter.get();
      REQUIRE(v.has_value());
      CHECK(v->size() == 1024);
      CHECK((*v)[0] == 0xAB);
      CHECK((*v)[1023] == 0xAB);
    }
  }
}

// ---------------------------------------------------------------------------
// Shm2 - latency
// ---------------------------------------------------------------------------

TEST_SUITE("shm2-latency") {
  TEST_CASE("shm2-latency-stats") {
    Publisher<int> pub(Shm2Conf("shm2/lat/sub1", "data", 0, 0, 0, 0, 128));
    Subscriber<int> sub("shm2://shm2/lat/sub1?event=data");

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
// Shm2 - identity
// ---------------------------------------------------------------------------

TEST_SUITE("shm2-identity") {
  TEST_CASE("shm2-node-identity") {
    Publisher<int> pub1(Shm2Conf("shm2/id/p1", "data"));
    Publisher<int> pub2(Shm2Conf("shm2/id/p2", "data"));
    Subscriber<int> sub("shm2://shm2/id/p1?event=data");

    CHECK(pub1.get_abstract_node() != nullptr);
    CHECK(pub2.get_abstract_node() != nullptr);
    CHECK(pub1.get_abstract_node() != pub2.get_abstract_node());
    CHECK(pub1.get_abstract_node() != sub.get_abstract_node());
  }
}

#endif  // VLINK_SUPPORT_SHM2

// NOLINTEND
