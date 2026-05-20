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

#include <vlink/base/logger.h>
#include <vlink/vlink.h>

#include <atomic>
#include <thread>

#include "dds_idlPubSubTypes.h"

using namespace vlink;                 // NOLINT(build/namespaces, google-build-using-namespace)
using namespace std::chrono_literals;  // NOLINT(build/namespaces, google-build-using-namespace)

int main() {
  std::atomic_bool exit_flag{false};

  DdsConf::register_url<dds::MessagePubSubType>("dds://hello/event");
  DdsConf::register_url<dds::RequestPubSubType, dds::ResponsePubSubType>("dds://hello/method");

  // Method
  Server<dds::Request, dds::Response> server("dds://hello/method");
  server.listen([](const dds::Request& req, dds::Response& res) {
    if (req.type() == 100) {
      res.value("AA");
    } else if (req.type() == 200) {
      res.value("BB");
    }
  });

  Client<dds::Request, dds::Response> client("dds://hello/method");

  client.detect_connected([](bool connected) { VLOG_I("server status:", connected); });

  dds::Request req;
  req.type(100);

  std::optional<dds::Response> resp1 = client.invoke(req);

  if (resp1.has_value()) {
    VLOG_I("resp1:", resp1.value().value());
  }

  req.type(200);

  std::optional<dds::Response> resp2 = client.invoke(req);

  if (resp2.has_value()) {
    VLOG_I("resp2:", resp2.value().value());
  }

  // Event
  Subscriber<dds::Message> sub("dds://hello/event");
  sub.listen([&exit_flag](const dds::Message& msg) {
    VLOG_I("msg:", msg.value());

    if (msg.value() == "hello") {
      exit_flag = true;
    }
  });

  Publisher<dds::Message> pub("dds://hello/event");

  dds::Message msg;
  msg.value("hello");

  pub.wait_for_subscribers();

  pub.publish(msg);

  while (!exit_flag) {
    std::this_thread::sleep_for(1s);
  }

  return 0;
}
