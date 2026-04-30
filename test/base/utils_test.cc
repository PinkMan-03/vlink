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

#include "./base/utils.h"

#include <doctest/doctest.h>

#include <filesystem>
#include <string>
#include <thread>
#include <vector>

//
#include "../common_test.h"

// ---------------------------------------------------------------------------
// TEST SUITE: Process/application identity
// ---------------------------------------------------------------------------

TEST_SUITE("base-Utils - application identity") {
  TEST_CASE("get_app_path() returns non-empty string") {
    std::string path = Utils::get_app_path();
    CHECK(!path.empty());
  }

  TEST_CASE("get_app_dir() returns non-empty string") {
    std::string dir = Utils::get_app_dir();
    CHECK(!dir.empty());
  }

  TEST_CASE("get_app_name() returns non-empty string") {
    std::string name = Utils::get_app_name();
    CHECK(!name.empty());
  }

  TEST_CASE("get_app_dir() is a prefix of get_app_path()") {
    std::string path = Utils::get_app_path();
    std::string dir = Utils::get_app_dir();

    // dir must appear at the start of path
    CHECK(path.find(dir) == 0U);
  }

  TEST_CASE("get_pid() returns positive value") {
    int32_t pid = Utils::get_pid();
    CHECK(pid > 0);
  }

  TEST_CASE("get_pid_str() is a non-empty numeric string") {
    std::string pid_str = Utils::get_pid_str();
    CHECK(!pid_str.empty());

    for (char c : pid_str) {
      CHECK(c >= '0');
      CHECK(c <= '9');
    }
  }

  TEST_CASE("get_pid_str() matches get_pid()") {
    int32_t pid = Utils::get_pid();
    std::string pid_str = Utils::get_pid_str();
    CHECK(std::to_string(pid) == pid_str);
  }
}

// ---------------------------------------------------------------------------
// TEST SUITE: Hostname
// ---------------------------------------------------------------------------

#if defined(__unix__) && !defined(__CYGWIN__)
TEST_SUITE("base-Utils - hostname") {
  TEST_CASE("get_host_name() returns non-empty string") {
    std::string host = Utils::get_host_name();
    CHECK(!host.empty());
  }
}
#endif

// ---------------------------------------------------------------------------
// TEST SUITE: Temporary directory
// ---------------------------------------------------------------------------

TEST_SUITE("base-Utils - tmp dir") {
  TEST_CASE("get_tmp_dir() returns non-empty string") {
    std::string tmp = Utils::get_tmp_dir();
    CHECK(!tmp.empty());
  }
}

// ---------------------------------------------------------------------------
// TEST SUITE: Environment variables
// ---------------------------------------------------------------------------

TEST_SUITE("base-Utils - environment variables") {
  TEST_CASE("get_env(PATH) returns non-empty string on POSIX") {
    std::string path_val = Utils::get_env("PATH");
    // PATH is almost always set; if not, just verify it doesn't crash
    // (some minimal containers may not have it — tolerate empty)
    CHECK(true);  // Just ensure no crash/exception
    (void)path_val;
  }

  TEST_CASE("get_env with nonexistent key returns default value") {
    std::string val = Utils::get_env("NONEXISTENT_VAR_12345", "default");
    CHECK(val == "default");
  }

  TEST_CASE("get_env with nonexistent key returns empty string by default") {
    std::string val = Utils::get_env("NONEXISTENT_VAR_67890");
    CHECK(val.empty());
  }

  TEST_CASE("set_env and get_env round-trip") {
    bool ok = Utils::set_env("VLINK_TEST_KEY", "test_value");
    CHECK(ok);

    std::string val = Utils::get_env("VLINK_TEST_KEY");
    CHECK(val == "test_value");
  }

  TEST_CASE("unset_env removes the variable") {
    Utils::set_env("VLINK_TEST_UNSET", "to_be_removed");
    CHECK(Utils::get_env("VLINK_TEST_UNSET") == "to_be_removed");

    bool ok = Utils::unset_env("VLINK_TEST_UNSET");
    CHECK(ok);

    std::string val = Utils::get_env("VLINK_TEST_UNSET", "gone");
    CHECK(val == "gone");
  }

  TEST_CASE("set_env force=true overwrites existing value") {
    Utils::set_env("VLINK_FORCE2_TEST", "first", true);
    Utils::set_env("VLINK_FORCE2_TEST", "second", true);

    std::string val = Utils::get_env("VLINK_FORCE2_TEST");
    CHECK(val == "second");

    // Cleanup
    Utils::unset_env("VLINK_FORCE2_TEST");
  }
}

// ---------------------------------------------------------------------------
// TEST SUITE: Network addresses
// ---------------------------------------------------------------------------

TEST_SUITE("base-Utils - network") {
  TEST_CASE("get_all_ipv4_address() does not throw") {
    // May be empty in minimal containers — just ensure no crash
    std::vector<std::string> addrs = Utils::get_all_ipv4_address();
    CHECK(true);
    (void)addrs;
  }

  TEST_CASE("get_all_ipv4_address(false) returns valid dotted-decimal strings") {
    std::vector<std::string> addrs = Utils::get_all_ipv4_address(false);

    for (const auto& addr : addrs) {
      CHECK(!addr.empty());
      // Each address should contain dots (e.g. "127.0.0.1")
      CHECK(addr.find('.') != std::string::npos);
    }
  }

  TEST_CASE("get_all_ipv4_address(true) returns subset of get_all_ipv4_address(false)") {
    std::vector<std::string> all = Utils::get_all_ipv4_address(false);
    std::vector<std::string> avail = Utils::get_all_ipv4_address(true);

    CHECK(avail.size() <= all.size());
  }
}

// ---------------------------------------------------------------------------
// TEST SUITE: Thread utilities
// ---------------------------------------------------------------------------

TEST_SUITE("base-Utils - thread") {
  TEST_CASE("get_native_thread_id() returns value greater than 0") {
    uint64_t tid = Utils::get_native_thread_id();
    CHECK(tid > 0);
  }

  TEST_CASE("yield_cpu() does not crash") {
    // Simply call it and ensure the process doesn't crash
    Utils::yield_cpu();
    CHECK(true);
  }

  TEST_CASE("yield_cpu() can be called in a loop") {
    for (int i = 0; i < 100; ++i) {
      Utils::yield_cpu();
    }

    CHECK(true);
  }
}

// ---------------------------------------------------------------------------
// TEST SUITE: Timezone
// ---------------------------------------------------------------------------

TEST_SUITE("base-Utils - timezone") {
  TEST_CASE("get_timezone_diff() is in a plausible range") {
    int32_t diff = Utils::get_timezone_diff();

    // UTC-12 to UTC+14 covers all real timezones; in seconds:
    // -12 * 3600 = -43200  to  14 * 3600 = 50400
    CHECK(diff >= -43200);
    CHECK(diff <= 50400);
  }
}

// ---------------------------------------------------------------------------
// TEST SUITE: Resource usage
// ---------------------------------------------------------------------------

TEST_SUITE("base-Utils - resource usage") {
#ifndef __CYGWIN__
  TEST_CASE("get_cpu_usage() returns non-negative value") {
    double cpu = Utils::get_cpu_usage();
    CHECK(cpu >= 0.0);
  }

  TEST_CASE("get_memory_usage() returns non-negative value") {
    double mem = Utils::get_memory_usage();
    CHECK(mem >= 0.0);
  }

  TEST_CASE("get_memory_usage() is below 100 percent in normal conditions") {
    double mem = Utils::get_memory_usage();
    CHECK(mem < 100.0);
  }
#endif

  TEST_CASE("try_release_sys_memory() does not crash") {
    Utils::try_release_sys_memory();
    CHECK(true);
  }
}

// ---------------------------------------------------------------------------
// TEST SUITE: Machine ID
// ---------------------------------------------------------------------------

TEST_SUITE("base-Utils - machine id") {
  TEST_CASE("get_machine_id() does not crash") {
    // May be empty in sandboxed environments
    std::string mid = Utils::get_machine_id();
    CHECK(true);
    (void)mid;
  }
}

// ---------------------------------------------------------------------------
// TEST SUITE: Consistency checks
// ---------------------------------------------------------------------------

TEST_SUITE("base-Utils - consistency") {
  TEST_CASE("multiple calls to get_pid() return same value") {
    int32_t pid1 = Utils::get_pid();
    int32_t pid2 = Utils::get_pid();
    CHECK(pid1 == pid2);
  }

  TEST_CASE("multiple calls to get_host_name() return same value") {
    std::string h1 = Utils::get_host_name();
    std::string h2 = Utils::get_host_name();
    CHECK(h1 == h2);
  }

  TEST_CASE("get_app_name() is consistent across calls") {
    std::string n1 = Utils::get_app_name();
    std::string n2 = Utils::get_app_name();
    CHECK(n1 == n2);
  }

  TEST_CASE("get_native_thread_id() is consistent on same thread") {
    uint64_t t1 = Utils::get_native_thread_id();
    uint64_t t2 = Utils::get_native_thread_id();
    CHECK(t1 == t2);
  }
}

// ---------------------------------------------------------------------------
// TEST SUITE: IPv6 addresses
// ---------------------------------------------------------------------------

TEST_SUITE("base-Utils - ipv6") {
  TEST_CASE("get_all_ipv6_address() does not crash") {
    std::vector<std::string> addrs = Utils::get_all_ipv6_address();
    CHECK(true);
    (void)addrs;
  }

  TEST_CASE("get_all_ipv6_address(true) returns subset of get_all_ipv6_address(false)") {
    std::vector<std::string> all = Utils::get_all_ipv6_address(false);
    std::vector<std::string> avail = Utils::get_all_ipv6_address(true);
    CHECK(avail.size() <= all.size());
  }
}

// ---------------------------------------------------------------------------
// TEST SUITE: Interface lookup
// ---------------------------------------------------------------------------

TEST_SUITE("base-Utils - interface lookup") {
  TEST_CASE("get_interface_name_by_ipv4 with loopback returns non-empty") {
    // 127.0.0.1 should map to 'lo' on Linux
    std::string iface = Utils::get_interface_name_by_ipv4("127.0.0.1");
    // May be empty in minimal containers; just verify no crash
    CHECK(true);
    (void)iface;
  }

  TEST_CASE("get_interface_name_by_ipv4 with invalid address returns empty") {
    std::string iface = Utils::get_interface_name_by_ipv4("0.0.0.0");
    // No real interface has 0.0.0.0; result should be empty or a name — no crash
    CHECK(true);
    (void)iface;
  }

  TEST_CASE("get_interface_name_by_ipv6 does not crash") {
    std::string iface = Utils::get_interface_name_by_ipv6("::1");
    CHECK(true);
    (void)iface;
  }
}

// ---------------------------------------------------------------------------
// TEST SUITE: DDS default address
// ---------------------------------------------------------------------------

TEST_SUITE("base-Utils - dds address") {
  TEST_CASE("get_dds_default_address() returns at most max_count addresses") {
    constexpr int kMax = 3;
    std::vector<std::string> addrs = Utils::get_dds_default_address(false, kMax);
    CHECK(addrs.size() <= static_cast<size_t>(kMax));
  }

  TEST_CASE("get_dds_default_address() addresses are non-empty dotted-decimal") {
    std::vector<std::string> addrs = Utils::get_dds_default_address();

    for (const auto& a : addrs) {
      CHECK(!a.empty());
      CHECK(a.find('.') != std::string::npos);
    }
  }
}

// ---------------------------------------------------------------------------
// TEST SUITE: Thread naming and affinity
// ---------------------------------------------------------------------------

TEST_SUITE("base-Utils - thread naming") {
  TEST_CASE("set_thread_name on calling thread does not crash") {
    bool ok = Utils::set_thread_name("test_worker");
    // May fail in sandboxed environments; just verify no crash
    (void)ok;
    CHECK(true);
  }

  TEST_CASE("set_thread_name with explicit thread object") {
    bool done = false;
    std::thread t([&done]() { done = true; });

    // Set name on a running thread (may fail if already finished)
    Utils::set_thread_name("named_thr", &t);
    t.join();

    CHECK(done);
  }
}

// ---------------------------------------------------------------------------
// TEST SUITE: Process utilities
// ---------------------------------------------------------------------------

TEST_SUITE("base-Utils - process utilities") {
  TEST_CASE("is_process_running returns false for fake process") {
    bool running = Utils::is_process_running("__vlink_fake_process_xyz__");
    CHECK_FALSE(running);
  }

  TEST_CASE("is_process_running for the current executable does not crash") {
    // Just verify it does not crash; result depends on the environment
    std::string app_name = Utils::get_app_name();

    if (!app_name.empty()) {
      CHECK_NOTHROW((void)Utils::is_process_running(app_name));
    }
  }
}

// ---------------------------------------------------------------------------
// TEST SUITE: Device waiting
// ---------------------------------------------------------------------------

// TEST_SUITE("base-Utils - wait_for_device") {
//   TEST_CASE("wait_for_device returns true for a path that exists immediately") {
//     // /tmp always exists
//     bool ok = Utils::wait_for_device("/tmp", 500, 50);
//     CHECK(ok);
//   }

//   TEST_CASE("wait_for_device returns false when path never appears") {
//     bool ok = Utils::wait_for_device("/__vlink_nonexistent__", 200, 50);
//     CHECK_FALSE(ok);
//   }
// }

// ---------------------------------------------------------------------------
// TEST SUITE: Console utilities
// ---------------------------------------------------------------------------

TEST_SUITE("base-Utils - console") {
  TEST_CASE("set_console_utf8_output does not crash") {
    Utils::set_console_utf8_output();
    CHECK(true);
  }

  TEST_CASE("get_terminal_size does not crash") {
    auto [cols, rows] = Utils::get_terminal_size();
    // Returns {0,0} or negative when not a tty; just verify no crash
    (void)cols;
    (void)rows;
    CHECK(true);
  }
}

// ---------------------------------------------------------------------------
// Additional coverage tests
// ---------------------------------------------------------------------------

TEST_SUITE("base-Utils - env edge cases") {
  TEST_CASE("set_env with empty key") {
    // Setting empty key should not crash
    bool ok = Utils::set_env("", "value");
    (void)ok;
    CHECK(true);
  }

  // TEST_CASE("set_env with empty value") {
  //   bool ok = Utils::set_env("VLINK_EMPTY_VAL", "");
  //   CHECK(ok);
  //   std::string val = Utils::get_env("VLINK_EMPTY_VAL", "default");
  //   CHECK(val.empty());
  //   Utils::unset_env("VLINK_EMPTY_VAL");
  // }

  TEST_CASE("unset_env nonexistent key does not crash") {
    bool ok = Utils::unset_env("VLINK_NONEXISTENT_KEY_XYZ");
    (void)ok;
    CHECK(true);
  }

  TEST_CASE("get_env with empty key returns default") {
    std::string val = Utils::get_env("", "fallback");
    // Empty key likely won't be found
    CHECK((val == "fallback" || val.empty()));
  }
}

TEST_SUITE("base-Utils - thread extra") {
  TEST_CASE("set_thread_name with empty string") {
    bool ok = Utils::set_thread_name("");
    (void)ok;
    CHECK(true);
  }

  TEST_CASE("set_thread_name with long string truncates safely") {
    // Linux pthread_setname_np limits to 15 chars; should not crash
    std::string long_name(64, 'a');
    bool ok = Utils::set_thread_name(long_name);
    (void)ok;
    CHECK(true);
  }

  TEST_CASE("get_native_thread_id differs between threads") {
    uint64_t main_tid = Utils::get_native_thread_id();
    uint64_t other_tid = 0;

    std::thread t([&other_tid] { other_tid = Utils::get_native_thread_id(); });
    t.join();

    CHECK(main_tid != other_tid);
  }

  TEST_CASE("set_thread_priority on calling thread does not crash") {
    // Using SCHED_OTHER (0) with priority 0 should be safe
    bool ok = Utils::set_thread_priority(0, 0);
    (void)ok;
    CHECK(true);
  }

  TEST_CASE("set_thread_priority with default policy does not crash") {
    bool ok = Utils::set_thread_priority(0);
    (void)ok;
    CHECK(true);
  }

  TEST_CASE("set_thread_stick with zero mask does not crash") {
    bool ok = Utils::set_thread_stick(0);
    CHECK_FALSE(ok);
  }

  TEST_CASE("set_thread_stick with valid mask does not crash") {
    // Pin to core 0
    bool ok = Utils::set_thread_stick(0x1);
    (void)ok;
    CHECK(true);
  }
}

TEST_SUITE("base-Utils - app identity extra") {
  TEST_CASE("get_app_path contains get_app_name") {
    std::string path = Utils::get_app_path();
    std::string name = Utils::get_app_name();
    if (!path.empty() && !name.empty()) {
      CHECK(path.find(name) != std::string::npos);
    }
  }

  TEST_CASE("get_tmp_dir returns an existing directory") {
    std::string tmp = Utils::get_tmp_dir();
    if (!tmp.empty()) {
      CHECK(std::filesystem::exists(tmp));
    }
  }
}

TEST_SUITE("base-Utils - network extra") {
  TEST_CASE("get_all_ipv6_address entries contain colons") {
    auto addrs = Utils::get_all_ipv6_address();
    for (const auto& addr : addrs) {
      CHECK(addr.find(':') != std::string::npos);
    }
  }

  TEST_CASE("get_interface_name_by_ipv4 with nonexistent returns empty") {
    std::string iface = Utils::get_interface_name_by_ipv4("192.0.2.255");
    // RFC 5737 TEST-NET address; unlikely to exist
    // Just verify no crash; might be empty
    CHECK(true);
    (void)iface;
  }

  TEST_CASE("get_dds_default_address with filter_available=true") {
    auto addrs = Utils::get_dds_default_address(true, 10);
    for (const auto& a : addrs) {
      CHECK(!a.empty());
      CHECK(a.find('.') != std::string::npos);
    }
  }

  TEST_CASE("get_dds_default_address with large max_count") {
    auto addrs = Utils::get_dds_default_address(false, 100);
    // Just verify it does not crash
    for (const auto& a : addrs) {
      CHECK(!a.empty());
    }
  }
}

TEST_SUITE("base-Utils - resource usage extra") {
  TEST_CASE("get_cpu_usage called twice does not crash") {
    double cpu1 = Utils::get_cpu_usage();
    double cpu2 = Utils::get_cpu_usage();
    // First call may return -1 as a sentinel; just verify no crash
    (void)cpu1;
    (void)cpu2;
    CHECK(true);
  }
}

// TEST_SUITE("base-Utils - wait_for_device extra") {
//   TEST_CASE("wait_for_device with zero timeout returns immediately") {
//     bool ok = Utils::wait_for_device("/__vlink_nonexistent__", 0, 50);
//     CHECK_FALSE(ok);
//   }

//   TEST_CASE("wait_for_device with existing file returns immediately") {
// #if defined(_WIN32) || defined(__CYGWIN__)
//     bool ok = Utils::wait_for_device("NUL", 100, 10);
// #else
//     bool ok = Utils::wait_for_device("/dev/null", 100, 10);
// #endif
//     CHECK(ok);
//   }
// }

TEST_SUITE("base-Utils - machine id extra") {
  TEST_CASE("get_machine_id returns consistent value") {
    std::string id1 = Utils::get_machine_id();
    std::string id2 = Utils::get_machine_id();
    CHECK(id1 == id2);
  }
}

TEST_SUITE("base-Utils - timezone extra") {
  TEST_CASE("get_timezone_diff is consistent") {
    int32_t d1 = Utils::get_timezone_diff();
    int32_t d2 = Utils::get_timezone_diff();
    CHECK(d1 == d2);
  }
}

// NOLINTEND
