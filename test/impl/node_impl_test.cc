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

#include "./impl/node_impl.h"

#include <doctest/doctest.h>

#include <any>
#include <atomic>
#include <memory>
#include <string>
#include <thread>

#include "./base/message_loop.h"
#include "./extension/status_detail.h"
#include "./impl/types.h"

//
#include "../common_test.h"

// ---------------------------------------------------------------------------
// Helpers: concrete subclass to test the abstract NodeImpl base
// ---------------------------------------------------------------------------

namespace {

class TestNodeImpl : public NodeImpl {
 public:
  explicit TestNodeImpl(ImplType type = kPublisher) : NodeImpl(type) {}

  ~TestNodeImpl() override = default;

  void init() override { init_called = true; }

  void deinit() override { deinit_called = true; }

  bool init_called{false};
  bool deinit_called{false};
};

}  // namespace

// ---------------------------------------------------------------------------
// TEST SUITE: AbstractNode
// ---------------------------------------------------------------------------

TEST_SUITE("impl-AbstractNode") {
  TEST_CASE("get_native_handle returns any with nullptr by default") {
    class ConcreteAbstractNode : public AbstractNode {
     public:
      ConcreteAbstractNode() = default;
      ~ConcreteAbstractNode() override = default;
    };

    ConcreteAbstractNode node;
    auto handle = node.get_native_handle();
    // Base impl returns std::any(nullptr) which has_value()==true but holds nullptr_t
    CHECK(handle.has_value());
    CHECK(std::any_cast<std::nullptr_t>(handle) == nullptr);
  }
}

// ---------------------------------------------------------------------------
// TEST SUITE: NodeImpl - construction
// ---------------------------------------------------------------------------

TEST_SUITE("impl-NodeImpl - construction") {
  TEST_CASE("constructor sets impl_type") {
    TestNodeImpl node(kPublisher);
    CHECK(node.impl_type == kPublisher);
  }

  TEST_CASE("constructor sets different impl_types") {
    TestNodeImpl pub(kPublisher);
    TestNodeImpl sub(kSubscriber);
    TestNodeImpl cli(kClient);
    TestNodeImpl srv(kServer);
    TestNodeImpl get(kGetter);
    TestNodeImpl set(kSetter);

    CHECK(pub.impl_type == kPublisher);
    CHECK(sub.impl_type == kSubscriber);
    CHECK(cli.impl_type == kClient);
    CHECK(srv.impl_type == kServer);
    CHECK(get.impl_type == kGetter);
    CHECK(set.impl_type == kSetter);
  }

  TEST_CASE("default member values") {
    TestNodeImpl node;
    CHECK(node.url.empty());
    CHECK(node.ser_type.empty());
    CHECK(node.transport_type == TransportType::kUnknown);
    CHECK(node.is_cdr_type == false);
    CHECK(node.is_security_type == false);
    CHECK(node.is_discovery_enabled == true);
    CHECK(node.profiler == nullptr);
  }
}

// ---------------------------------------------------------------------------
// TEST SUITE: NodeImpl - loan stubs
// ---------------------------------------------------------------------------

TEST_SUITE("impl-NodeImpl - loan stubs") {
  TEST_CASE("is_support_loan returns false by default") {
    TestNodeImpl node;
    CHECK(node.is_support_loan() == false);
  }

  TEST_CASE("loan returns empty Bytes") {
    TestNodeImpl node;
    auto bytes = node.loan(1024);
    CHECK(bytes.empty());
  }

  TEST_CASE("return_loan returns false") {
    TestNodeImpl node;
    Bytes b;
    CHECK(node.return_loan(b) == false);
  }

  TEST_CASE("set_manual_unloan does not crash") {
    TestNodeImpl node;
    node.set_manual_unloan(true);
    node.set_manual_unloan(false);
    CHECK(true);
  }
}

// ---------------------------------------------------------------------------
// TEST SUITE: NodeImpl - suspend / resume stubs
// ---------------------------------------------------------------------------

TEST_SUITE("impl-NodeImpl - suspend / resume") {
  TEST_CASE("suspend returns false") {
    TestNodeImpl node;
    CHECK(node.suspend() == false);
  }

  TEST_CASE("resume returns false") {
    TestNodeImpl node;
    CHECK(node.resume() == false);
  }

  TEST_CASE("is_suspend returns false") {
    TestNodeImpl node;
    CHECK(node.is_suspend() == false);
  }
}

// ---------------------------------------------------------------------------
// TEST SUITE: NodeImpl - interrupt
// ---------------------------------------------------------------------------

TEST_SUITE("impl-NodeImpl - interrupt") {
  TEST_CASE("is_interrupted returns false initially") {
    TestNodeImpl node;
    CHECK(node.is_interrupted() == false);
  }

  TEST_CASE("interrupt sets interrupted flag") {
    TestNodeImpl node;
    node.interrupt();
    CHECK(node.is_interrupted() == true);
  }

  TEST_CASE("reset_interrupted clears flag") {
    TestNodeImpl node;
    node.interrupt();
    CHECK(node.is_interrupted() == true);

    node.reset_interrupted();
    CHECK(node.is_interrupted() == false);
  }

  TEST_CASE("multiple interrupt calls are safe") {
    TestNodeImpl node;
    node.interrupt();
    node.interrupt();
    CHECK(node.is_interrupted() == true);

    node.reset_interrupted();
    CHECK(node.is_interrupted() == false);
  }
}

// ---------------------------------------------------------------------------
// TEST SUITE: NodeImpl - property management
// ---------------------------------------------------------------------------

TEST_SUITE("impl-NodeImpl - properties") {
  TEST_CASE("set and get property") {
    TestNodeImpl node;
    node.set_property("key1", "value1");
    CHECK(node.get_property("key1") == "value1");
  }

  TEST_CASE("get non-existent property returns empty string") {
    TestNodeImpl node;
    CHECK(node.get_property("nonexistent").empty());
  }

  TEST_CASE("overwrite property") {
    TestNodeImpl node;
    node.set_property("key", "old");
    node.set_property("key", "new");
    CHECK(node.get_property("key") == "new");
  }

  TEST_CASE("multiple properties") {
    TestNodeImpl node;
    node.set_property("a", "1");
    node.set_property("b", "2");
    node.set_property("c", "3");

    CHECK(node.get_property("a") == "1");
    CHECK(node.get_property("b") == "2");
    CHECK(node.get_property("c") == "3");
  }

  TEST_CASE("get_all_properties returns snapshot") {
    TestNodeImpl node;
    node.set_property("x", "10");
    node.set_property("y", "20");

    auto props = node.get_all_properties();
    CHECK(props.size() == 2);
    CHECK(props["x"] == "10");
    CHECK(props["y"] == "20");
  }

  TEST_CASE("get_all_properties returns empty map initially") {
    TestNodeImpl node;
    auto props = node.get_all_properties();
    CHECK(props.empty());
  }
}

// ---------------------------------------------------------------------------
// TEST SUITE: NodeImpl - message loop attach / detach
// ---------------------------------------------------------------------------

TEST_SUITE("impl-NodeImpl - message loop") {
  TEST_CASE("get_message_loop returns nullptr initially") {
    TestNodeImpl node;
    CHECK(node.get_message_loop() == nullptr);
  }

  TEST_CASE("attach succeeds on first call") {
    TestNodeImpl node;
    MessageLoop loop;
    CHECK(node.attach(&loop) == true);
    CHECK(node.get_message_loop() == &loop);
  }

  TEST_CASE("attach fails when already attached") {
    TestNodeImpl node;
    MessageLoop loop1;
    MessageLoop loop2;
    CHECK(node.attach(&loop1) == true);
    CHECK(node.attach(&loop2) == false);
    CHECK(node.get_message_loop() == &loop1);
  }

  TEST_CASE("detach returns false when not attached") {
    TestNodeImpl node;
    CHECK(node.detach() == false);
  }

  TEST_CASE("attach then detach") {
    TestNodeImpl node;
    MessageLoop loop;
    loop.async_run();
    CHECK(node.attach(&loop) == true);
    CHECK(node.detach() == true);
    CHECK(node.get_message_loop() == nullptr);
    loop.quit();
  }

  TEST_CASE("re-attach after detach") {
    TestNodeImpl node;
    MessageLoop loop;
    loop.async_run();
    node.attach(&loop);
    node.detach();
    CHECK(node.attach(&loop) == true);
    CHECK(node.get_message_loop() == &loop);
    node.detach();
    loop.quit();
  }
}

// ---------------------------------------------------------------------------
// TEST SUITE: NodeImpl - status handler
// ---------------------------------------------------------------------------

TEST_SUITE("impl-NodeImpl - status handler") {
  TEST_CASE("register_status_handler on non-DDS transport is no-op") {
    TestNodeImpl node;
    node.transport_type = TransportType::kIntra;

    bool called = false;
    node.register_status_handler([&](const Status::BasePtr&) { called = true; });

    CHECK(node.has_register_status() == false);
  }

  TEST_CASE("register_status_handler on DDS transport works") {
    TestNodeImpl node;
    node.transport_type = TransportType::kDds;

    bool called = false;
    node.register_status_handler([&](const Status::BasePtr&) { called = true; });

    CHECK(node.has_register_status() == true);
  }

  TEST_CASE("has_register_status on non-DDS returns false") {
    TestNodeImpl node;
    node.transport_type = TransportType::kShm;
    CHECK(node.has_register_status() == false);
  }

  TEST_CASE("call_status on DDS transport fires callback directly") {
    TestNodeImpl node;
    node.transport_type = TransportType::kDds;

    Status::Type received_type = Status::kUnknown;
    node.register_status_handler([&](const Status::BasePtr& ptr) { received_type = ptr->get_type(); });

    auto status = std::make_shared<Status::PublicationMatched>();
    node.call_status(status);

    CHECK(received_type == Status::kPublicationMatched);
  }

  TEST_CASE("call_status on non-DDS transport is no-op") {
    TestNodeImpl node;
    node.transport_type = TransportType::kShm;

    bool called = false;
    // Cannot register on non-DDS, so callback won't be set
    node.call_status(std::make_shared<Status::Unknown>());
    CHECK(called == false);
  }

  TEST_CASE("call_status via message loop") {
    TestNodeImpl node;
    node.transport_type = TransportType::kDds;
    MessageLoop loop;
    loop.async_run();
    node.attach(&loop);

    std::atomic_bool called{false};
    node.register_status_handler([&](const Status::BasePtr&) { called = true; });

    node.call_status(std::make_shared<Status::PublicationMatched>());

    std::this_thread::sleep_for(50ms);
    CHECK(called == true);

    node.detach();
    loop.quit();
  }

  TEST_CASE("call_status with DDS transport types - ddsc") {
    TestNodeImpl node;
    node.transport_type = TransportType::kDdsc;

    bool called = false;
    node.register_status_handler([&](const Status::BasePtr&) { called = true; });
    CHECK(node.has_register_status() == true);

    node.call_status(std::make_shared<Status::SubscriptionMatched>());
    CHECK(called == true);
  }

  TEST_CASE("call_status with DDS transport types - ddsr") {
    TestNodeImpl node;
    node.transport_type = TransportType::kDdsr;

    bool called = false;
    node.register_status_handler([&](const Status::BasePtr&) { called = true; });
    CHECK(node.has_register_status() == true);
  }

  TEST_CASE("call_status with DDS transport types - ddst") {
    TestNodeImpl node;
    node.transport_type = TransportType::kDdst;

    bool called = false;
    node.register_status_handler([&](const Status::BasePtr&) { called = true; });
    CHECK(node.has_register_status() == true);
  }
}

// ---------------------------------------------------------------------------
// TEST SUITE: NodeImpl - get_conf / get_abstract_node / get_status
// ---------------------------------------------------------------------------

TEST_SUITE("impl-NodeImpl - virtual stubs") {
  TEST_CASE("get_conf returns nullptr by default") {
    TestNodeImpl node;
    CHECK(node.get_conf() == nullptr);
  }

  TEST_CASE("get_abstract_node returns nullptr by default") {
    TestNodeImpl node;
    CHECK(node.get_abstract_node() == nullptr);
  }

  TEST_CASE("get_status returns Unknown by default") {
    TestNodeImpl node;
    auto status = node.get_status(Status::kPublicationMatched);
    REQUIRE(status != nullptr);
    CHECK(status->get_type() == Status::kUnknown);
  }

  TEST_CASE("get_target_conf with nullptr returns nullptr") {
    TestNodeImpl node;
    const auto* conf = node.get_target_conf<Conf>();
    CHECK(conf == nullptr);
  }
}

// ---------------------------------------------------------------------------
// TEST SUITE: NodeImpl - discovery enable/disable
// ---------------------------------------------------------------------------

TEST_SUITE("impl-NodeImpl - discovery") {
  TEST_CASE("discovery enabled by default") {
    TestNodeImpl node;
    CHECK(node.get_discovery_enabled() == true);
  }

  TEST_CASE("set_discovery_enabled false") {
    TestNodeImpl node;
    node.set_discovery_enabled(false);
    CHECK(node.get_discovery_enabled() == false);
  }

  TEST_CASE("set_discovery_enabled toggle") {
    TestNodeImpl node;
    node.set_discovery_enabled(false);
    CHECK(node.get_discovery_enabled() == false);

    node.set_discovery_enabled(true);
    CHECK(node.get_discovery_enabled() == true);
  }
}

// ---------------------------------------------------------------------------
// TEST SUITE: NodeImpl - record path
// ---------------------------------------------------------------------------

TEST_SUITE("impl-NodeImpl - record path") {
  TEST_CASE("set_record_path with empty string does not crash") {
    TestNodeImpl node;
    node.set_record_path("");
    CHECK(true);
  }

  TEST_CASE("try_record without global recorder does not crash") {
    TestNodeImpl node;
    node.url = "intra://test";
    node.transport_type = TransportType::kIntra;
    Bytes data;
    node.try_record(ActionType::kUnknownAction, data);
    CHECK(true);
  }
}

// ---------------------------------------------------------------------------
// TEST SUITE: NodeImpl - init_ext / deinit_ext
// ---------------------------------------------------------------------------

TEST_SUITE("impl-NodeImpl - init_ext / deinit_ext") {
  TEST_CASE("init_ext and deinit_ext do not crash") {
    TestNodeImpl node;
    node.url = "intra://test_topic";
    node.transport_type = TransportType::kIntra;
    node.init_ext();
    node.deinit_ext();
    CHECK(true);
  }

  TEST_CASE("init_ext with discovery disabled") {
    TestNodeImpl node;
    node.url = "dds://test_topic";
    node.transport_type = TransportType::kDds;
    node.set_discovery_enabled(false);
    node.init_ext();
    node.deinit_ext();
    CHECK(true);
  }

  TEST_CASE("init_ext with security type") {
    TestNodeImpl node;
    node.url = "dds://test_topic";
    node.transport_type = TransportType::kDds;
    node.is_security_type = true;
    node.init_ext();
    node.deinit_ext();
    CHECK(true);
  }

  TEST_CASE("init_ext with empty url") {
    TestNodeImpl node;
    node.transport_type = TransportType::kDds;
    node.init_ext();
    node.deinit_ext();
    CHECK(true);
  }
}

// ---------------------------------------------------------------------------
// TEST SUITE: NodeImpl - global_init
// ---------------------------------------------------------------------------

TEST_SUITE("impl-NodeImpl - global_init") {
  TEST_CASE("global_init can be called multiple times") {
    NodeImpl::global_init();
    NodeImpl::global_init();
    CHECK(true);
  }
}

// ---------------------------------------------------------------------------
// TEST SUITE: NodeImpl - has_suspend atomic
// ---------------------------------------------------------------------------

TEST_SUITE("impl-NodeImpl - has_suspend") {
  TEST_CASE("has_suspend defaults to false") {
    TestNodeImpl node;
    CHECK(node.has_suspend == false);
  }

  TEST_CASE("has_suspend is writable") {
    TestNodeImpl node;
    node.has_suspend = true;
    CHECK(node.has_suspend == true);
  }
}

// NOLINTEND
