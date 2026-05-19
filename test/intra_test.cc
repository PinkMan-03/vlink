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

#if defined(VLINK_SUPPORT_INTRA)

#include <atomic>
#include <future>
#include <string>
#include <thread>

#include "./modules/intra_conf.h"

// ---------------------------------------------------------------------------
// Intra - init: URL parsing and conf construction
// ---------------------------------------------------------------------------

TEST_SUITE("intra-init") {
  TEST_CASE("url-parse-success") {
    Url url("intra://test1?event=hi");

    CHECK(url.parse(kPublisher));
    CHECK(url.parse(kSubscriber));
    CHECK(url.parse(kServer));
    CHECK(url.parse(kClient));
  }

  TEST_CASE("url-parse-unknown-impl-type-throws") {
    Url url("intra://test1?event=hi");

    CHECK_THROWS_AS(url.parse(kUnknownImplType), std::runtime_error);
  }

  TEST_CASE("conf-parse-success") {
    IntraConf conf("test1", "hi");

    CHECK(conf.parse(kPublisher));
    CHECK(conf.parse(kSubscriber));
    CHECK(conf.parse(kServer));
    CHECK(conf.parse(kClient));
  }

  TEST_CASE("conf-parse-unsupported-type-compile-time-only") {
    IntraConf conf("test1", "hi");

    // kSetter and kGetter restrictions are compile-time only (static_assert in Node<>).
    // At runtime, Conf::parse() accepts any non-unknown ImplType without throwing.
    CHECK(conf.parse(kSetter));
    CHECK(conf.parse(kGetter));
  }

  TEST_CASE("conf-equality") {
    IntraConf a("addr1", "ev1", 0, "queue");
    IntraConf b("addr1", "ev1", 0, "queue");
    IntraConf c("addr2", "ev1", 0, "queue");

    CHECK(a == b);
    CHECK(a != c);
  }

  TEST_CASE("conf-transport-type") {
    IntraConf conf("addr1");

    CHECK(conf.get_transport_type() == TransportType::kIntra);
  }

  TEST_CASE("invalid-url-transport-publisher-throws") {
    // Transport "intra1" is unknown — construction must throw
    CHECK_THROWS(Publisher<int>("intra1://test1/event=hi"));
  }

  TEST_CASE("invalid-url-transport-subscriber-throws") {
    // Malformed URL — must throw
    CHECK_THROWS(Subscriber<int>("intra2:/event"));
  }

  TEST_CASE("direct-fragment-url-parse") {
    Url url_direct("intra://topic#direct");

    CHECK(url_direct.parse(kPublisher));
    CHECK(url_direct.parse(kSubscriber));
  }

  TEST_CASE("conf-with-pipeline-and-direct") {
    IntraConf conf("pipeline_addr", "ev", 8, "direct");

    CHECK(conf.address == "pipeline_addr");
    CHECK(conf.event == "ev");
    CHECK(conf.pipeline == 8);
    CHECK(conf.type == "direct");
  }

  TEST_CASE("fire-and-forget method metadata keeps request family") {
    Server<std::string> server(IntraConf("meta_send", "null"), InitType::kWithoutInit);
    Client<std::string> client("intra://meta_send?event=null", InitType::kWithoutInit);

    CHECK(server.get_ser_type() == "string");
    CHECK(client.get_ser_type() == "string");
    CHECK(server.get_schema_type() == SchemaType::kRaw);
    CHECK(client.get_schema_type() == SchemaType::kRaw);
  }

  TEST_CASE("request-response method metadata keeps paired ser and family") {
    Server<std::string, std::string> server(IntraConf("meta_rpc", "null"), InitType::kWithoutInit);
    Client<std::string, std::string> client("intra://meta_rpc?event=null", InitType::kWithoutInit);

    CHECK(server.get_ser_type() == "string;string");
    CHECK(client.get_ser_type() == "string;string");
    CHECK(server.get_schema_type() == SchemaType::kRaw);
    CHECK(client.get_schema_type() == SchemaType::kRaw);
  }

  TEST_CASE("bytes request-response method metadata keeps empty ser") {
    Server<Bytes, Bytes> server(IntraConf("meta_bytes_rpc", "null"), InitType::kWithoutInit);
    Client<Bytes, Bytes> client("intra://meta_bytes_rpc?event=null", InitType::kWithoutInit);

    CHECK(server.get_ser_type().empty());
    CHECK(client.get_ser_type().empty());
    CHECK(server.get_schema_type() == SchemaType::kRaw);
    CHECK(client.get_schema_type() == SchemaType::kRaw);
  }

  TEST_CASE("set_ser_type keeps schema family in sync") {
    Publisher<Bytes> pub(IntraConf("meta_override", "null"), InitType::kWithoutInit);

    pub.set_ser_type("demo.proto.Old", SchemaType::kProtobuf);
    CHECK(pub.get_schema_type() == SchemaType::kProtobuf);

    pub.set_ser_type("demo.proto.New");
    CHECK(pub.get_schema_type() == SchemaType::kProtobuf);

    pub.set_ser_type("vlink::zerocopy::RawData");
    CHECK(pub.get_schema_type() == SchemaType::kZeroCopy);

    pub.set_ser_type("demo.custom.Payload");
    CHECK(pub.get_schema_type() == SchemaType::kUnknown);

    pub.set_ser_type("string");
    CHECK(pub.get_schema_type() == SchemaType::kRaw);
  }
}

// ---------------------------------------------------------------------------
// Intra - method: fire-and-forget, RPC, async
// ---------------------------------------------------------------------------

TEST_SUITE("intra-method") {
  TEST_CASE("intra-method-send") {
    std::atomic<int> counter{0};

    Server<std::string> server(IntraConf("test_send1", "null"));

    server.listen([&](const std::string& req) {
      (void)req;
      counter.fetch_add(1, std::memory_order_relaxed);
    });

    Client<std::string> client("intra://test_send1?event=null");

    CHECK(client.wait_for_connected(5s));
    CHECK(client.is_connected());

    CHECK(client.send("send"));

    std::this_thread::sleep_for(20ms);

    CHECK(counter.load() == 1);

    // Second send
    CHECK(client.send("send2"));

    std::this_thread::sleep_for(20ms);

    CHECK(counter.load() == 2);
  }

  TEST_CASE("intra-method-invoke") {
    Server<std::string, std::string> server(IntraConf("test_invoke2", "null"));

    server.listen([](const std::string& req, std::string& resp) {
      (void)req;
      resp = "response";
    });

    Client<std::string, std::string> client("intra://test_invoke2?event=null");

    CHECK(client.wait_for_connected(5s));

    SUBCASE("future-based-async") {
      auto fut = client.async_invoke("invoke");

      REQUIRE(fut.wait_for(5s) == std::future_status::ready);
      CHECK(fut.get() == "response");
    }

    SUBCASE("optional-synchronous") {
      auto resp = client.invoke("invoke");

      CHECK(resp.has_value());
      CHECK(*resp == "response");
    }

    SUBCASE("synchronous-with-ref") {
      std::string out;
      bool ok = client.invoke("invoke", out, 5s);

      CHECK(ok);
      CHECK(out == "response");
    }

    SUBCASE("multiple-sequential-invocations") {
      for (int i = 0; i < 5; ++i) {
        auto resp = client.invoke("invoke");

        CHECK(resp.has_value());
        CHECK(*resp == "response");
      }
    }
  }

  TEST_CASE("intra-method-invoke-invalid-response-fails") {
    Server<Bytes, Bytes> server(IntraConf("test_bad_resp", "null"));

    server.listen([](const Bytes& /*req*/, Bytes& resp) { resp = Bytes{0x01}; });

    Client<int, int> client("intra://test_bad_resp?event=null");

    CHECK(client.wait_for_connected(5s));

    int out = 1234;
    CHECK_FALSE(client.invoke(7, out, 5s));
    CHECK(out == 1234);

    auto resp = client.invoke(7, 5s);
    CHECK_FALSE(resp.has_value());
  }

  TEST_CASE("intra-method-async-reply") {
    // IntraServerImpl does not implement deferred async reply via req_id.
    // The base ServerImpl::reply() returns false for non-sync mode.
    Server<std::string, std::string> server(IntraConf("test_async_reply3", "null"));

    server.listen([](const std::string& /*req*/, std::string& resp) { resp = "sync_response"; });

    Client<std::string, std::string> client("intra://test_async_reply3?event=null");

    CHECK(client.wait_for_connected(5s));

    // Verify that synchronous invoke still works (intra supports sync RPC)
    auto resp = client.invoke("request");
    CHECK(resp.has_value());
    CHECK(*resp == "sync_response");

    // Verify that the deferred-reply overload is not supported at runtime
    Server<std::string, std::string> server2(IntraConf("test_async_reply3b", "null"));
    server2.listen_for_reply([](uint64_t, const std::string&) {});
    CHECK_FALSE(server2.reply(1, std::string("x")));
  }

  TEST_CASE("intra-method-async-callback") {
    Server<std::string, std::string> server(IntraConf("test_async_cb4", "null"));

    server.listen([](const std::string& /*req*/, std::string& resp) { resp = "cb_response"; });

    Client<std::string, std::string> client("intra://test_async_cb4?event=null");

    CHECK(client.wait_for_connected(5s));

    std::atomic<bool> got_response{false};
    std::string received_resp;

    bool ok = client.invoke("msg", [&](const std::string& resp) {
      received_resp = resp;
      got_response.store(true, std::memory_order_release);
    });

    CHECK(ok);

    for (int i = 0; i < 50 && !got_response.load(std::memory_order_acquire); ++i) {
      std::this_thread::sleep_for(20ms);
    }

    CHECK(got_response.load(std::memory_order_acquire));
    CHECK(received_resp == "cb_response");
  }

  TEST_CASE("intra-detect-connected") {
    std::atomic<bool> connected_event{false};

    Server<std::string, std::string> server(IntraConf("test_detect_conn", "null"));

    server.listen([](const std::string& /*req*/, std::string& resp) { resp = "ok"; });

    Client<std::string, std::string> client("intra://test_detect_conn?event=null");

    client.detect_connected([&](bool connected) {
      if (connected) {
        connected_event.store(true, std::memory_order_release);
      }
    });

    for (int i = 0; i < 50 && !connected_event.load(std::memory_order_acquire); ++i) {
      std::this_thread::sleep_for(20ms);
    }

    CHECK(connected_event.load(std::memory_order_acquire));
  }
}

// ---------------------------------------------------------------------------
// Intra - event: publish/subscribe
// ---------------------------------------------------------------------------

TEST_SUITE("intra-event") {
  TEST_CASE("intra-event-queue") {
    std::atomic<int> count{0};

    Publisher<std::string> pub(IntraConf("event_q", "", 0, "queue"));
    Subscriber<std::string> sub("intra://event_q");

    sub.listen([&](const std::string& msg) {
      (void)msg;
      count.fetch_add(1, std::memory_order_relaxed);
    });

    CHECK(pub.wait_for_subscribers(5s));
    CHECK(pub.has_subscribers());

    CHECK(pub.publish("msg1"));

    std::this_thread::sleep_for(20ms);

    CHECK(count.load() == 1);

    // Publish more messages
    pub.publish("msg2");
    pub.publish("msg3");

    std::this_thread::sleep_for(20ms);

    CHECK(count.load() == 3);
  }

  TEST_CASE("intra-event-direct") {
    std::atomic<int> count{0};

    Publisher<std::string> pub(IntraConf("event_d", "", 0, "direct"));
    Subscriber<std::string> sub("intra://event_d#direct");

    sub.listen([&](const std::string& msg) {
      (void)msg;
      count.fetch_add(1, std::memory_order_relaxed);
    });

    CHECK(pub.wait_for_subscribers(5s));

    CHECK(pub.publish("direct_msg"));

    std::this_thread::sleep_for(20ms);

    CHECK(count.load() == 1);
  }

  TEST_CASE("intra-event-multi-sub") {
    std::atomic<int> count1{0};
    std::atomic<int> count2{0};

    Publisher<std::string> pub(IntraConf("event_multi", "", 0, "queue"));
    Subscriber<std::string> sub1("intra://event_multi");
    Subscriber<std::string> sub2("intra://event_multi");

    sub1.listen([&](const std::string& /*msg*/) { count1.fetch_add(1, std::memory_order_relaxed); });
    sub2.listen([&](const std::string& /*msg*/) { count2.fetch_add(1, std::memory_order_relaxed); });

    // Wait until both subscribers are registered
    for (int i = 0; i < 50; ++i) {
      if (pub.has_subscribers()) {
        break;
      }

      std::this_thread::sleep_for(20ms);
    }

    CHECK(pub.publish("broadcast"));

    std::this_thread::sleep_for(20ms);

    // Both subscribers must receive the message
    CHECK(count1.load() == 1);
    CHECK(count2.load() == 1);
  }

  TEST_CASE("intra-event-force-publish") {
    Publisher<std::string> pub(IntraConf("event_force", "", 0, "queue"));

    // No subscriber is connected; force=true should return true without crash
    bool result = pub.publish("ignored", true);
    CHECK(result);

    // Publish multiple times with force
    for (int i = 0; i < 5; ++i) {
      CHECK(pub.publish("force_" + std::to_string(i), true));
    }
  }

  TEST_CASE("intra-event-pipeline") {
    std::atomic<int> count{0};

    Publisher<int> pub(IntraConf("event_pipeline1", "", 4, "queue"));
    Subscriber<int> sub("intra://event_pipeline1?pipeline=4");

    sub.listen([&](const int& /*val*/) { count.fetch_add(1, std::memory_order_relaxed); });

    CHECK(pub.wait_for_subscribers(5s));

    for (int i = 0; i < 8; ++i) {
      pub.publish(i);
    }

    std::this_thread::sleep_for(20ms);

    CHECK(count.load() > 0);
  }

  TEST_CASE("intra-event-pub-sub") {
    std::atomic<bool> received{false};
    Bytes captured;

    Publisher<Bytes> pub(IntraConf("event_bytes1", "", 0, "queue"));
    Subscriber<Bytes> sub("intra://event_bytes1");

    sub.listen([&](const Bytes& data) {
      captured = data;
      received.store(true, std::memory_order_release);
    });

    CHECK(pub.wait_for_subscribers(5s));

    Bytes payload{1, 2, 3};
    CHECK(pub.publish(payload));

    for (int i = 0; i < 50 && !received.load(std::memory_order_acquire); ++i) {
      std::this_thread::sleep_for(20ms);
    }

    CHECK(received.load(std::memory_order_acquire));
    REQUIRE(captured.size() == 3);
    CHECK(captured[0] == 1);
    CHECK(captured[1] == 2);
    CHECK(captured[2] == 3);
  }

  TEST_CASE("intra-event-int") {
    std::atomic<bool> received{false};
    std::atomic<int> captured{0};

    Publisher<int> pub(IntraConf("event_int1", "", 0, "queue"));
    Subscriber<int> sub("intra://event_int1");

    sub.listen([&](const int& val) {
      captured.store(val, std::memory_order_relaxed);
      received.store(true, std::memory_order_release);
    });

    CHECK(pub.wait_for_subscribers(5s));
    CHECK(pub.publish(42));

    for (int i = 0; i < 50 && !received.load(std::memory_order_acquire); ++i) {
      std::this_thread::sleep_for(20ms);
    }

    CHECK(received.load(std::memory_order_acquire));
    CHECK(captured.load() == 42);

    SUBCASE("multiple-values") {
      for (int v : {100, 200, 300}) {
        received.store(false, std::memory_order_release);
        pub.publish(v);

        for (int i = 0; i < 50 && !received.load(std::memory_order_acquire); ++i) {
          std::this_thread::sleep_for(20ms);
        }

        CHECK(captured.load() == v);
      }
    }
  }

  TEST_CASE("intra-event-string") {
    std::atomic<bool> received{false};
    std::string captured;

    Publisher<std::string> pub(IntraConf("event_str1", "", 0, "queue"));
    Subscriber<std::string> sub("intra://event_str1");

    sub.listen([&](const std::string& val) {
      captured = val;
      received.store(true, std::memory_order_release);
    });

    CHECK(pub.wait_for_subscribers(5s));
    CHECK(pub.publish(std::string("hello_world")));

    for (int i = 0; i < 50 && !received.load(std::memory_order_acquire); ++i) {
      std::this_thread::sleep_for(20ms);
    }

    CHECK(received.load(std::memory_order_acquire));
    CHECK(captured == "hello_world");
  }

  TEST_CASE("intra-detect-subscribers") {
    std::atomic<int> detected_count{0};

    Publisher<int> pub(IntraConf("event_detect1", "", 0, "queue"));

    pub.detect_subscribers([&](bool connected) {
      if (connected) {
        detected_count.fetch_add(1, std::memory_order_relaxed);
      }
    });

    {
      Subscriber<int> sub("intra://event_detect1");

      sub.listen([](const int& /*v*/) {});

      std::this_thread::sleep_for(20ms);

      // At least one connect event
      CHECK(pub.has_subscribers());
    }

    // After sub is destroyed: no subscriber
    std::this_thread::sleep_for(20ms);
    CHECK(!pub.has_subscribers());
  }

  TEST_CASE("intra-serialize") {
#if defined(VLINK_TEST_SUPPORT_PROTOBUF)
    SUBCASE("protobuf-message") {
      std::atomic<bool> received{false};
      pb::Message captured;

      Publisher<pb::Message> pub(IntraConf("event_pb_msg", "", 0, "queue"));
      Subscriber<pb::Message> sub("intra://event_pb_msg");

      sub.listen([&](const pb::Message& msg) {
        captured = msg;
        received.store(true, std::memory_order_release);
      });

      CHECK(pub.wait_for_subscribers(5s));

      pb::Message msg;
      msg.set_value("proto_test");
      CHECK(pub.publish(msg));

      for (int i = 0; i < 50 && !received.load(std::memory_order_acquire); ++i) {
        std::this_thread::sleep_for(20ms);
      }

      CHECK(received.load(std::memory_order_acquire));
      CHECK(captured.value() == "proto_test");
    }

    SUBCASE("protobuf-request-response") {
      Server<pb::Request, pb::Response> server(IntraConf("test_pb_rpc", "null"));

      server.listen([](const pb::Request& req, pb::Response& resp) { resp.set_value(std::to_string(req.type() * 2)); });

      Client<pb::Request, pb::Response> client("intra://test_pb_rpc?event=null");

      CHECK(client.wait_for_connected(5s));

      pb::Request req;
      req.set_type(21);
      auto resp = client.invoke(req);

      CHECK(resp.has_value());
      CHECK(resp->value() == "42");
    }
#endif

#if defined(VLINK_TEST_SUPPORT_FLATBUFFERS)
    SUBCASE("flatbuffers-message") {
      std::atomic<bool> received{false};

      Publisher<fbs::MessageT> pub(IntraConf("event_fbs_msg", "", 0, "queue"));
      Subscriber<fbs::MessageT> sub("intra://event_fbs_msg");

      sub.listen([&](const fbs::MessageT& /*msg*/) { received.store(true, std::memory_order_release); });

      CHECK(pub.wait_for_subscribers(5s));

      fbs::MessageT msg;
      msg.value = "flatbuffers_test";
      CHECK(pub.publish(msg));

      for (int i = 0; i < 50 && !received.load(std::memory_order_acquire); ++i) {
        std::this_thread::sleep_for(20ms);
      }

      CHECK(received.load(std::memory_order_acquire));
    }
#endif

    // Always-on subcase: no serialization dependency
    SUBCASE("plain-int-always") {
      std::atomic<bool> received{false};
      std::atomic<int> val{0};

      Publisher<int> pub(IntraConf("event_ser_int1", "", 0, "queue"));
      Subscriber<int> sub("intra://event_ser_int1");

      sub.listen([&](const int& v) {
        val.store(v, std::memory_order_relaxed);
        received.store(true, std::memory_order_release);
      });

      CHECK(pub.wait_for_subscribers(5s));
      pub.publish(99);

      for (int i = 0; i < 50 && !received.load(std::memory_order_acquire); ++i) {
        std::this_thread::sleep_for(20ms);
      }

      CHECK(received.load(std::memory_order_acquire));
      CHECK(val.load() == 99);
    }
  }
}

// ---------------------------------------------------------------------------
// Intra - dynamic: DynamicData through intra transport
// ---------------------------------------------------------------------------

TEST_SUITE("intra-dynamic") {
  TEST_CASE("intra-dynamic") {
    SUBCASE("string-payload") {
      std::atomic<bool> received{false};
      DynamicData captured;

      Publisher<DynamicData> pub(IntraConf("event_dyn_str", "", 0, "queue"));
      Subscriber<DynamicData> sub("intra://event_dyn_str");

      sub.listen([&](const DynamicData& data) {
        captured = data;
        received.store(true, std::memory_order_release);
      });

      CHECK(pub.wait_for_subscribers(5s));

      DynamicData data;
      data.load("type_str", std::string("hello"));

      CHECK(pub.publish(data));

      for (int i = 0; i < 50 && !received.load(std::memory_order_acquire); ++i) {
        std::this_thread::sleep_for(20ms);
      }

      CHECK(received.load(std::memory_order_acquire));
      CHECK(captured.get_type() == "type_str");
      CHECK(!captured.is_empty());
      CHECK(captured.as<std::string>() == "hello");
    }

    SUBCASE("int-payload") {
      std::atomic<bool> received{false};
      DynamicData captured;

      Publisher<DynamicData> pub(IntraConf("event_dyn_int", "", 0, "queue"));
      Subscriber<DynamicData> sub("intra://event_dyn_int");

      sub.listen([&](const DynamicData& data) {
        captured = data;
        received.store(true, std::memory_order_release);
      });

      CHECK(pub.wait_for_subscribers(5s));

      DynamicData data;
      data.load("type_int", 42);

      CHECK(pub.publish(data));

      for (int i = 0; i < 50 && !received.load(std::memory_order_acquire); ++i) {
        std::this_thread::sleep_for(20ms);
      }

      CHECK(received.load(std::memory_order_acquire));
      CHECK(captured.get_type() == "type_int");
      CHECK(captured.as<int>() == 42);
    }

    SUBCASE("bytes-payload") {
      std::atomic<bool> received{false};
      DynamicData captured;

      Publisher<DynamicData> pub(IntraConf("event_dyn_bytes", "", 0, "queue"));
      Subscriber<DynamicData> sub("intra://event_dyn_bytes");

      sub.listen([&](const DynamicData& data) {
        captured = data;
        received.store(true, std::memory_order_release);
      });

      CHECK(pub.wait_for_subscribers(5s));

      DynamicData data;
      Bytes payload{0xAA, 0xBB, 0xCC};
      data.load("type_bytes", payload);

      CHECK(pub.publish(data));

      for (int i = 0; i < 50 && !received.load(std::memory_order_acquire); ++i) {
        std::this_thread::sleep_for(20ms);
      }

      CHECK(received.load(std::memory_order_acquire));
      CHECK(captured.get_type() == "type_bytes");

      Bytes out = captured.as<Bytes>();
      REQUIRE(out.size() == 3);
      CHECK(out[0] == 0xAA);
      CHECK(out[1] == 0xBB);
      CHECK(out[2] == 0xCC);
    }

    SUBCASE("empty-dynamic-data") {
      DynamicData dd;
      CHECK(dd.is_empty());
      CHECK(dd.get_type().empty());
    }
  }
}

// ---------------------------------------------------------------------------
// Intra - latency: latency and lost sample statistics
// ---------------------------------------------------------------------------

TEST_SUITE("intra-latency") {
  TEST_CASE("intra-latency-stats") {
    Publisher<int> pub(IntraConf("event_latency1", "", 0, "queue"));
    Subscriber<int> sub("intra://event_latency1");

    // IntraSubscriberImpl does not implement latency/lost tracking:
    // set_latency_and_lost_enabled() is a no-op and is_latency_and_lost_enabled()
    // always returns false in the base SubscriberImpl.
    sub.set_latency_and_lost_enabled(true);
    CHECK(sub.is_latency_and_lost_enabled() == false);

    std::atomic<int> count{0};

    sub.listen([&](const int& /*val*/) { count.fetch_add(1, std::memory_order_relaxed); });

    CHECK(pub.wait_for_subscribers(5s));

    // Publish several messages
    for (int i = 0; i < 10; ++i) {
      pub.publish(i);
      std::this_thread::sleep_for(5ms);
    }

    std::this_thread::sleep_for(20ms);

    CHECK(count.load() > 0);

    // Base implementation returns 0 / empty for latency and lost stats
    CHECK(sub.get_latency() == 0);
    CHECK(sub.get_lost().total == 0);

    sub.set_latency_and_lost_enabled(false);
    CHECK(sub.is_latency_and_lost_enabled() == false);
  }
}

// ---------------------------------------------------------------------------
// Intra - identity: abstract node handles are distinct per instance
// ---------------------------------------------------------------------------

TEST_SUITE("intra-identity") {
  TEST_CASE("intra-node-identity") {
    Publisher<int> pub1(IntraConf("event_node_id1", "", 0, "queue"));
    Publisher<int> pub2(IntraConf("event_node_id2", "", 0, "queue"));
    Subscriber<int> sub1("intra://event_node_id1");

    auto node1 = pub1.get_abstract_node();
    auto node2 = pub2.get_abstract_node();
    auto node3 = sub1.get_abstract_node();

    // Each node must yield a non-null pointer
    CHECK(node1 != nullptr);
    CHECK(node2 != nullptr);
    CHECK(node3 != nullptr);

    // Different topics yield distinct nodes
    CHECK(node1 != node2);
    // Same topic: publisher and subscriber share the same underlying IntraNode
    CHECK(node1 == node3);
  }
}

#endif  // VLINK_SUPPORT_INTRA

// NOLINTEND
