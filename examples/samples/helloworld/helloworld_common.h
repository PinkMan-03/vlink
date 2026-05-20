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

#pragma once

#include <vlink/base/logger.h>
#include <vlink/base/utils.h>

#include <string>

#define DDS_METHOD_URL "dds://helloworld/method"
#define DDS_EVENT_URL "dds://helloworld/event"

#define DDSC_METHOD_URL "ddsc://helloworld/method"
#define DDSC_EVENT_URL "ddsc://helloworld/event"

#define SOMEIP_METHOD_URL "someip://0x01/0x02?method=0x1"
#define SOMEIP_EVENT_URL "someip://0x01/0x02?groups=0x1&event=0x2"

#define SHM_METHOD_URL "shm://helloworld/method"
#define SHM_EVENT_URL "shm://helloworld/event"

#define FDBUS_METHOD_URL "fdbus://helloworld/method"
#define FDBUS_EVENT_URL "fdbus://helloworld/event"

#define QNX_METHOD_URL "qnx://helloworld/method"
#define QNX_EVENT_URL "qnx://helloworld/event"

namespace Common {  // NOLINT(readability-identifier-naming)

std::string get_method_url() {
  auto url = vlink::Utils::get_env("METHOD_URL");

  if (!url.empty()) {
    VLOG_I("METHOD_URL=", url);
    return url;
  }

  auto transport = vlink::Utils::get_env("METHOD_TRANSPORT", "dds");

  if (transport == "dds") {
    url = DDS_METHOD_URL;
  } else if (transport == "ddsc") {
    url = DDSC_METHOD_URL;
  } else if (transport == "someip") {
    url = SOMEIP_METHOD_URL;
  } else if (transport == "shm") {
    url = SHM_METHOD_URL;
  } else if (transport == "fdbus") {
    url = FDBUS_METHOD_URL;
  } else if (transport == "qnx") {
    url = QNX_METHOD_URL;
  } else {
    url = DDS_METHOD_URL;
  }

  VLOG_I("METHOD_URL=", url);

  return url;
}

std::string get_event_url() {
  auto url = vlink::Utils::get_env("EVENT_URL");

  if (!url.empty()) {
    VLOG_I("EVENT_URL=", url);
    return url;
  }

  auto transport = vlink::Utils::get_env("EVENT_TRANSPORT", "dds");

  if (transport == "dds") {
    url = DDS_EVENT_URL;
  } else if (transport == "ddsc") {
    url = DDSC_EVENT_URL;
  } else if (transport == "someip") {
    url = SOMEIP_EVENT_URL;
  } else if (transport == "shm") {
    url = SHM_EVENT_URL;
  } else if (transport == "fdbus") {
    url = FDBUS_EVENT_URL;
  } else if (transport == "qnx") {
    url = QNX_EVENT_URL;
  } else {
    url = DDS_EVENT_URL;
  }

  VLOG_I("EVENT_URL=", url);

  return url;
}

}  // namespace Common
