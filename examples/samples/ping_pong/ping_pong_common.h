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

#define DDS_PING_URL "dds://ping"
#define DDS_PONG_URL "dds://pong"

#define DDSC_PING_URL "ddsc://ping"
#define DDSC_PONG_URL "ddsc://pong"

#define SOMEIP_PING_URL "someip://0x05/0x06?groups=0x7&event=0x8"
#define SOMEIP_PONG_URL "someip://0x05/0x06?groups=0x7&event=0x9"

#define SHM_PING_URL "shm://ping_pong/ping"
#define SHM_PONG_URL "shm://ping_pong/pong"

#define FDBUS_PING_URL "fdbus://ping_pong/ping"
#define FDBUS_PONG_URL "fdbus://ping_pong/pong"

#define QNX_PING_URL "qnx://ping_pong/ping"
#define QNX_PONG_URL "qnx://ping_pong/pong"

namespace Common {  // NOLINT(readability-identifier-naming)

std::string get_ping_url() {
  auto url = vlink::Utils::get_env("PING_URL");

  if (!url.empty()) {
    VLOG_I("PING_URL=", url);
    return url;
  }

  auto transport = vlink::Utils::get_env("PING_TRANSPORT", "dds");

  if (transport == "dds") {
    url = DDS_PING_URL;
  } else if (transport == "ddsc") {
    url = DDSC_PING_URL;
  } else if (transport == "someip") {
    url = SOMEIP_PING_URL;
  } else if (transport == "shm") {
    url = SHM_PING_URL;
  } else if (transport == "fdbus") {
    url = FDBUS_PING_URL;
  } else if (transport == "qnx") {
    url = QNX_PING_URL;
  } else {
    url = DDS_PING_URL;
  }

  VLOG_I("PING_URL=", url);

  return url;
}

std::string get_pong_url() {
  auto url = vlink::Utils::get_env("PONG_URL");

  if (!url.empty()) {
    VLOG_I("PONG_URL=", url);
    return url;
  }

  auto transport = vlink::Utils::get_env("PONG_TRANSPORT", "dds");

  if (transport == "dds") {
    url = DDS_PONG_URL;
  } else if (transport == "ddsc") {
    url = DDSC_PONG_URL;
  } else if (transport == "someip") {
    url = SOMEIP_PONG_URL;
  } else if (transport == "shm") {
    url = SHM_PONG_URL;
  } else if (transport == "fdbus") {
    url = FDBUS_PONG_URL;
  } else if (transport == "qnx") {
    url = QNX_PONG_URL;
  } else {
    url = DDS_PONG_URL;
  }

  VLOG_I("PONG_URL=", url);

  return url;
}

}  // namespace Common
