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

#include "./impl/server_impl.h"

#include <doctest/doctest.h>

#include <functional>

#include "./base/bytes.h"
#include "./impl/types.h"

//
#include "../common_test.h"

// ---------------------------------------------------------------------------
// Helpers: concrete subclass to test ServerImpl
// ---------------------------------------------------------------------------

namespace {

class TestServerImpl : public ServerImpl {
 public:
  TestServerImpl() = default;
  ~TestServerImpl() override = default;

  void init() override {}
  void deinit() override {}

  bool listen(ReqRespCallback&& callback) override {
    callback_ = std::move(callback);
    is_listened = true;
    return true;
  }

  void fire_request(uint64_t req_id, const Bytes& req_data, Bytes* resp_data) {
    if (callback_) {
      callback_(req_id, req_data, resp_data);
    }
  }

 private:
  ReqRespCallback callback_;
};

}  // namespace

// ---------------------------------------------------------------------------
// TEST SUITE: ServerImpl - construction
// ---------------------------------------------------------------------------

TEST_SUITE("impl-ServerImpl - construction") {
  TEST_CASE("constructor sets kServer impl_type") {
    TestServerImpl server;
    CHECK(server.impl_type == kServer);
  }

  TEST_CASE("is_listened defaults to false") {
    TestServerImpl server;
    CHECK(server.is_listened == false);
  }

  TEST_CASE("is_resp_type defaults to false") {
    TestServerImpl server;
    CHECK(server.is_resp_type == false);
  }

  TEST_CASE("is_sync_type defaults to false") {
    TestServerImpl server;
    CHECK(server.is_sync_type == false);
  }
}

// ---------------------------------------------------------------------------
// TEST SUITE: ServerImpl - has_clients
// ---------------------------------------------------------------------------

TEST_SUITE("impl-ServerImpl - has_clients") {
  TEST_CASE("has_clients returns false by default") {
    TestServerImpl server;
    CHECK(server.has_clients() == false);
  }
}

// ---------------------------------------------------------------------------
// TEST SUITE: ServerImpl - reply
// ---------------------------------------------------------------------------

TEST_SUITE("impl-ServerImpl - reply") {
  TEST_CASE("reply with is_sync=true returns false") {
    TestServerImpl server;
    Bytes resp;
    CHECK(server.reply(1, resp, true) == false);
  }

  TEST_CASE("reply with is_sync=false returns false and logs warning") {
    TestServerImpl server;
    Bytes resp;
    CHECK(server.reply(1, resp, false) == false);
  }

  TEST_CASE("reply with different req_id values") {
    TestServerImpl server;
    Bytes resp;
    CHECK(server.reply(0, resp, true) == false);
    CHECK(server.reply(UINT64_MAX, resp, true) == false);
  }
}

// ---------------------------------------------------------------------------
// TEST SUITE: ServerImpl - listen
// ---------------------------------------------------------------------------

TEST_SUITE("impl-ServerImpl - listen") {
  TEST_CASE("listen sets is_listened") {
    TestServerImpl server;

    server.listen([](uint64_t, const Bytes&, Bytes*) {});

    CHECK(server.is_listened == true);
  }

  TEST_CASE("listen callback fires on request") {
    TestServerImpl server;

    uint64_t received_id = 0;
    server.listen([&](uint64_t id, const Bytes&, Bytes*) { received_id = id; });

    Bytes req;
    server.fire_request(42, req, nullptr);

    CHECK(received_id == 42);
  }

  TEST_CASE("listen callback with response") {
    TestServerImpl server;

    server.listen([](uint64_t, const Bytes&, Bytes* resp) {
      if (resp) {
        *resp = Bytes({0xAA});
      }
    });

    Bytes req;
    Bytes resp;
    server.fire_request(1, req, &resp);

    CHECK(resp.size() == 1);
  }
}

// ---------------------------------------------------------------------------
// TEST SUITE: ServerImpl - flags
// ---------------------------------------------------------------------------

TEST_SUITE("impl-ServerImpl - flags") {
  TEST_CASE("is_resp_type is mutable") {
    TestServerImpl server;
    server.is_resp_type = true;
    CHECK(server.is_resp_type == true);
  }

  TEST_CASE("is_sync_type is mutable") {
    TestServerImpl server;
    server.is_sync_type = true;
    CHECK(server.is_sync_type == true);
  }
}

// NOLINTEND
