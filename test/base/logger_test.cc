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

#include "./base/logger.h"

#include <doctest/doctest.h>

#include <atomic>
#include <string>
#include <string_view>

//
#include "../common_test.h"

// ---------------------------------------------------------------------------
// TEST SUITE: Singleton access
// ---------------------------------------------------------------------------

TEST_SUITE("base-Logger - singleton") {
  TEST_CASE("get_instance returns same object on repeated calls") {
    Logger& a = Logger::get();
    Logger& b = Logger::get();
    CHECK(&a == &b);
  }

  TEST_CASE("init() can be called without crashing") {
    Logger::init("test_app");
    CHECK(true);
  }

  TEST_CASE("init() with both app name and empty log path") {
    Logger::init("vlink_test", "");
    CHECK(true);
  }
}

// ---------------------------------------------------------------------------
// TEST SUITE: Level constants
// ---------------------------------------------------------------------------

TEST_SUITE("base-Logger - level constants") {
  TEST_CASE("level enum values are ordered correctly") {
    CHECK(Logger::kTrace < Logger::kDebug);
    CHECK(Logger::kDebug < Logger::kInfo);
    CHECK(Logger::kInfo < Logger::kWarn);
    CHECK(Logger::kWarn < Logger::kError);
    CHECK(Logger::kError < Logger::kFatal);
    CHECK(Logger::kFatal < Logger::kOff);
  }

  TEST_CASE("kTrace == 0") { CHECK(static_cast<int>(Logger::kTrace) == 0); }

  TEST_CASE("kOff == 6") { CHECK(static_cast<int>(Logger::kOff) == 6); }

  TEST_CASE("kMinimumLevel is kTrace by default") { CHECK(Logger::kMinimumLevel == Logger::kTrace); }

  TEST_CASE("kDetailLevel is kWarn by default") { CHECK(Logger::kDetailLevel == Logger::kWarn); }

  TEST_CASE("kLocalBufferSize is 4096") { CHECK(Logger::kLocalBufferSize == 4096); }
}

// ---------------------------------------------------------------------------
// TEST SUITE: Console and file level get/set
// ---------------------------------------------------------------------------

TEST_SUITE("base-Logger - level get/set") {
  TEST_CASE("set and get_console_level round-trip") {
    Logger::init("test");

    Logger::set_console_level(Logger::kInfo);
    CHECK(Logger::get_console_level() == Logger::kInfo);

    Logger::set_console_level(Logger::kDebug);
    CHECK(Logger::get_console_level() == Logger::kDebug);

    Logger::set_console_level(Logger::kWarn);
    CHECK(Logger::get_console_level() == Logger::kWarn);
  }

  TEST_CASE("set and get_file_level round-trip") {
    Logger::init("test");

    Logger::set_file_level(Logger::kError);
    CHECK(Logger::get_file_level() == Logger::kError);

    Logger::set_file_level(Logger::kOff);
    CHECK(Logger::get_file_level() == Logger::kOff);
  }

  TEST_CASE("set_console_level to kOff disables console") {
    Logger::init("test");
    Logger::set_console_level(Logger::kOff);
    CHECK(Logger::get_console_level() == Logger::kOff);

    // Restore
    Logger::set_console_level(Logger::kTrace);
  }
}

// ---------------------------------------------------------------------------
// TEST SUITE: is_writable
// ---------------------------------------------------------------------------

TEST_SUITE("base-Logger - is_writable") {
  TEST_CASE("is_writable returns false when both sinks are kOff") {
    Logger::init("test");
    Logger::set_console_level(Logger::kOff);
    Logger::set_file_level(Logger::kOff);

    CHECK(!Logger::is_writable(Logger::kTrace));
    CHECK(!Logger::is_writable(Logger::kDebug));
    CHECK(!Logger::is_writable(Logger::kInfo));
    CHECK(!Logger::is_writable(Logger::kWarn));
    CHECK(!Logger::is_writable(Logger::kError));

    // Restore
    Logger::set_console_level(Logger::kTrace);
  }

  TEST_CASE("is_writable returns true for Info when console is set to Info") {
    Logger::init("test");
    Logger::set_console_level(Logger::kInfo);
    Logger::set_file_level(Logger::kOff);

    CHECK(Logger::is_writable(Logger::kInfo));
    CHECK(Logger::is_writable(Logger::kWarn));
    CHECK(Logger::is_writable(Logger::kError));

    // Restore
    Logger::set_console_level(Logger::kTrace);
  }
}

// ---------------------------------------------------------------------------
// TEST SUITE: Console format enable
// ---------------------------------------------------------------------------

TEST_SUITE("base-Logger - console format") {
  TEST_CASE("set_console_fmt_enable and get_console_fmt_enable round-trip") {
    Logger::init("test");

    Logger::set_console_fmt_enable(false);
    CHECK(Logger::get_console_fmt_enable() == false);

    Logger::set_console_fmt_enable(true);
    CHECK(Logger::get_console_fmt_enable() == true);
  }
}

// ---------------------------------------------------------------------------
// TEST SUITE: Stream settings
// ---------------------------------------------------------------------------

TEST_SUITE("base-Logger - stream settings") {
  TEST_CASE("set_stream_precision and get_stream_precision round-trip") {
    Logger::init("test");

    Logger::set_stream_precision(6);
    CHECK(Logger::get_stream_precision() == 6);

    Logger::set_stream_precision(2);
    CHECK(Logger::get_stream_precision() == 2);
  }

  TEST_CASE("set_stream_width and get_stream_width round-trip") {
    Logger::init("test");

    Logger::set_stream_width(10);
    CHECK(Logger::get_stream_width() == 10);

    Logger::set_stream_width(0);
    CHECK(Logger::get_stream_width() == 0);
  }
}

// ---------------------------------------------------------------------------
// TEST SUITE: extract_filename
// ---------------------------------------------------------------------------

TEST_SUITE("base-Logger - extract_filename") {
  TEST_CASE("extracts basename from POSIX path") {
    auto sv = Logger::extract_filename("/home/user/project/main.cc");
    CHECK(sv == "main.cc");
  }

  TEST_CASE("extracts basename from Windows path") {
    auto sv = Logger::extract_filename("C:\\src\\vlink\\main.cc");  // NOLINT(modernize-raw-string-literal)
    CHECK(sv == "main.cc");
  }

  TEST_CASE("returns whole string when no separator present") {
    auto sv = Logger::extract_filename("main.cc");
    CHECK(sv == "main.cc");
  }

  TEST_CASE("handles empty string") {
    auto sv = Logger::extract_filename("");
    CHECK(sv.empty());
  }
}

// ---------------------------------------------------------------------------
// TEST SUITE: Flush and backtrace
// ---------------------------------------------------------------------------

TEST_SUITE("base-Logger - flush and backtrace") {
  TEST_CASE("flush() does not crash") {
    Logger::init("test");
    Logger::flush();
    CHECK(true);
  }

  TEST_CASE("enable_backtrace and disable_backtrace do not crash") {
    Logger::init("test");
    Logger::enable_backtrace(16);
    Logger::disable_backtrace();
    CHECK(true);
  }

  TEST_CASE("dump_backtrace() does not crash") {
    Logger::init("test");
    Logger::enable_backtrace(8);
    Logger::dump_backtrace();
    Logger::disable_backtrace();
    CHECK(true);
  }
}

// ---------------------------------------------------------------------------
// TEST SUITE: Macro smoke tests - VLOG_* / MLOG_* / CLOG_*
// ---------------------------------------------------------------------------

TEST_SUITE("base-Logger - macro smoke tests") {
  TEST_CASE("VLOG_I macro does not crash") {
    Logger::init("test");
    Logger::set_console_level(Logger::kInfo);

    VLOG_I("unit test info message value=", 42);
    CHECK(true);
  }

  TEST_CASE("VLOG_D macro does not crash") {
    Logger::init("test");
    Logger::set_console_level(Logger::kDebug);

    VLOG_D("debug value=", 3.14);
    CHECK(true);
  }

  TEST_CASE("VLOG_W macro does not crash") {
    Logger::init("test");
    Logger::set_console_level(Logger::kWarn);

    VLOG_W("warning message");
    CHECK(true);
  }

  TEST_CASE("VLOG_E macro does not crash") {
    Logger::init("test");
    Logger::set_console_level(Logger::kError);

    VLOG_E("error message code=", 1);
    CHECK(true);
  }

  TEST_CASE("MLOG_I format-style macro does not crash") {
    Logger::init("test");
    Logger::set_console_level(Logger::kInfo);

    MLOG_I("format test value={}", 99);
    CHECK(true);
  }

  TEST_CASE("CLOG_I c-style macro does not crash") {
    Logger::init("test");
    Logger::set_console_level(Logger::kInfo);

    CLOG_I("c-style value=%d", 7);
    CHECK(true);
  }

  TEST_CASE("VLOG_F throws RuntimeError") {
    Logger::init("test");
    Logger::set_console_level(Logger::kFatal);

    CHECK_THROWS(VLOG_F("fatal test message"));
  }
}

// ---------------------------------------------------------------------------
// TEST SUITE: Custom handler callback
// ---------------------------------------------------------------------------

TEST_SUITE("base-Logger - custom handler") {
  TEST_CASE("register_console_handler is called for logged messages") {
    Logger::init("test");
    Logger::set_console_level(Logger::kInfo);

    bool called = false;
    Logger::register_console_handler([&called](Logger::Level, std::string_view) { called = true; });

    VLOG_I("handler callback test");
    CHECK(called);

    // Unregister by setting nullptr handler (reset to default)
    Logger::register_console_handler(nullptr);
  }

  TEST_CASE("register_console_handler receives correct level") {
    Logger::init("test");
    Logger::set_console_level(Logger::kWarn);

    Logger::Level received_level = Logger::kTrace;
    Logger::register_console_handler([&received_level](Logger::Level lv, std::string_view) { received_level = lv; });

    VLOG_W("level check");
    CHECK(received_level == Logger::kWarn);

    Logger::register_console_handler(nullptr);
  }
}

TEST_SUITE("base-Logger - SLOG macros") {
  TEST_CASE("SLOG_I does not crash") {
    Logger::init("test");
    Logger::set_console_level(Logger::kInfo);
    SLOG_I << "slog_info_test";
    CHECK(true);
  }

  TEST_CASE("SLOG_D does not crash") {
    Logger::init("test");
    Logger::set_console_level(Logger::kDebug);
    SLOG_D << "slog_debug_test";
    CHECK(true);
  }

  TEST_CASE("SLOG_W does not crash") {
    Logger::init("test");
    Logger::set_console_level(Logger::kWarn);
    SLOG_W << "slog_warn_test";
    CHECK(true);
  }

  TEST_CASE("SLOG_E does not crash") {
    Logger::init("test");
    Logger::set_console_level(Logger::kError);
    SLOG_E << "slog_error_test";
    CHECK(true);
  }
}

TEST_SUITE("base-Logger - MLOG macros") {
  TEST_CASE("MLOG_D does not crash") {
    Logger::init("test");
    Logger::set_console_level(Logger::kDebug);
    MLOG_D("mlog_debug");
    CHECK(true);
  }

  TEST_CASE("MLOG_W does not crash") {
    Logger::init("test");
    Logger::set_console_level(Logger::kWarn);
    MLOG_W("mlog_warn");
    CHECK(true);
  }

  TEST_CASE("MLOG_E does not crash") {
    Logger::init("test");
    Logger::set_console_level(Logger::kError);
    MLOG_E("mlog_error");
    CHECK(true);
  }
}

TEST_SUITE("base-Logger - CLOG macros") {
  TEST_CASE("CLOG_D does not crash") {
    Logger::init("test");
    Logger::set_console_level(Logger::kDebug);
    CLOG_D("clog_debug %d", 1);
    CHECK(true);
  }

  TEST_CASE("CLOG_W does not crash") {
    Logger::init("test");
    Logger::set_console_level(Logger::kWarn);
    CLOG_W("clog_warn %d", 2);
    CHECK(true);
  }

  TEST_CASE("CLOG_E does not crash") {
    Logger::init("test");
    Logger::set_console_level(Logger::kError);
    CLOG_E("clog_error %d", 3);
    CHECK(true);
  }
}

TEST_SUITE("base-Logger - is_busy") {
  TEST_CASE("is_busy returns bool without crashing") {
    Logger::init("test");
    bool b = Logger::is_busy();
    (void)b;
    CHECK(true);
  }
}

TEST_SUITE("base-Logger - file handler") {
  TEST_CASE("register_file_handler callback receives log messages") {
    Logger::init("test");
    Logger::set_file_level(Logger::kInfo);

    std::atomic<int> file_calls{0};
    Logger::register_file_handler(
        [&file_calls](Logger::Level, std::string_view) { file_calls.fetch_add(1, std::memory_order_relaxed); });

    VLOG_I("file handler test message");
    Logger::flush();

    CHECK(file_calls.load() >= 1);

    Logger::register_file_handler(nullptr);  // Reset
  }

  TEST_CASE("set_file_level and get_file_level round-trip") {
    Logger::init("test");
    Logger::set_file_level(Logger::kError);
    CHECK(Logger::get_file_level() == Logger::kError);

    Logger::set_file_level(Logger::kInfo);
    CHECK(Logger::get_file_level() == Logger::kInfo);
  }
}

TEST_SUITE("base-Logger - stream settings") {
  TEST_CASE("set_stream_flag and get_stream_flag round-trip") {
    Logger::init("test");
    auto orig = Logger::get_stream_flag();

    Logger::set_stream_flag(std::ios_base::hex);
    CHECK(Logger::get_stream_flag() == std::ios_base::hex);

    Logger::set_stream_flag(orig);  // Restore
  }

  TEST_CASE("set_stream_width and get_stream_width round-trip") {
    Logger::init("test");
    Logger::set_stream_width(10);
    CHECK(Logger::get_stream_width() == 10);
    Logger::set_stream_width(0);
  }

  TEST_CASE("set_stream_precision and get_stream_precision round-trip") {
    Logger::init("test");
    Logger::set_stream_precision(6);
    CHECK(Logger::get_stream_precision() == 6);
  }
}

// NOLINTEND
