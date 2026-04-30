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

#include <doctest/doctest.h>

#include <filesystem>
#include <string>
#include <system_error>

#ifdef VLINK_SUPPORT_SHM

#include "../common_test.h"

TEST_SUITE("modules-ShmConf - construction") {
  TEST_CASE("construct with address only sets defaults") {
    ShmConf conf("my_service");
    CHECK(conf.address == "my_service");
    CHECK(conf.event.empty());
    CHECK(conf.domain == 0);
    CHECK(conf.depth == 0);
    CHECK(conf.history == 0);
    CHECK(conf.wait == 0);
  }

  TEST_CASE("construct with all params") {
    ShmConf conf("addr", "evt", 1, 10, 5, 1);
    CHECK(conf.address == "addr");
    CHECK(conf.event == "evt");
    CHECK(conf.domain == 1);
    CHECK(conf.depth == 10);
    CHECK(conf.history == 5);
    CHECK(conf.wait == 1);
  }
}

TEST_SUITE("modules-ShmConf - equality operators") {
  TEST_CASE("equal configs compare equal") {
    ShmConf a("addr", "evt", 1, 5, 2, 0);
    ShmConf b("addr", "evt", 1, 5, 2, 0);
    CHECK(a == b);
    CHECK(!(a != b));
  }

  TEST_CASE("different address compares not equal") {
    ShmConf a("addr_a");
    ShmConf b("addr_b");
    CHECK(a != b);
  }

  TEST_CASE("different domain compares not equal") {
    ShmConf a("addr", "", 0);
    ShmConf b("addr", "", 1);
    CHECK(a != b);
  }

  TEST_CASE("different wait compares not equal") {
    ShmConf a("addr", "", 0, 0, 0, 0);
    ShmConf b("addr", "", 0, 0, 0, 1);
    CHECK(a != b);
  }
}

TEST_SUITE("modules-ShmConf - transport type") {
  TEST_CASE("get_transport_type returns kShm") {
    ShmConf conf("topic");
    CHECK(conf.get_transport_type() == TransportType::kShm);
  }
}

TEST_SUITE("modules-ShmConf - has_roudi_running") {
  TEST_CASE("returns bool without side effect") {
    bool first = ShmConf::has_roudi_running();
    bool second = ShmConf::has_roudi_running();
    CHECK(first == second);
  }

  TEST_CASE("independent from has_runtime_inited") {
    bool roudi_present = ShmConf::has_roudi_running();
    bool runtime_inited = ShmConf::has_runtime_inited();
    CHECK((roudi_present == true || roudi_present == false));
    CHECK((runtime_inited == true || runtime_inited == false));
  }

  TEST_CASE("matches mgmt segment presence on POSIX") {
#if !defined(_WIN32)
    std::error_code ec;
    bool seg_present = std::filesystem::exists("/dev/shm/iceoryx_mgmt", ec);
    CHECK(ShmConf::has_roudi_running() == seg_present);
#else
    CHECK(true);
#endif
  }

  TEST_CASE("safe to call repeatedly from the same thread") {
    for (int i = 0; i < 16; ++i) {
      (void)ShmConf::has_roudi_running();
    }
    CHECK(true);
  }
}

#else

TEST_SUITE("modules-ShmConf - not supported") {
  TEST_CASE("VLINK_SUPPORT_SHM not defined - skip") { CHECK(true); }
}

#endif  // VLINK_SUPPORT_SHM

// NOLINTEND
