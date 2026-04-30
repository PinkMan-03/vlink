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

#include "./base/format.h"

#include <doctest/doctest.h>

#include <cstring>
#include <iterator>
#include <string>
#include <string_view>
#include <vector>

//
#include "../common_test.h"

// ---------------------------------------------------------------------------
// Helper: format_to_n into a std::string for easy comparison
// ---------------------------------------------------------------------------
template <typename... ArgsT>
static std::string fmt(const char* fmt_str, const ArgsT&... args) {
  char buf[256];
  auto result = format::format_to_n(buf, sizeof(buf) - 1, fmt_str, args...);
  buf[result.size < sizeof(buf) ? result.size : sizeof(buf) - 1] = '\0';  // NOLINT(clang-analyzer-security.ArrayBound)
  return std::string(buf, result.size < sizeof(buf) - 1 ? result.size : sizeof(buf) - 1);
}

// ---------------------------------------------------------------------------
TEST_SUITE("base-Format") {
  // -------------------------------------------------------------------------
  TEST_CASE("plain text with no placeholders") {
    char buf[64];
    auto r = format::format_to_n(buf, sizeof(buf) - 1, "hello world");
    buf[r.size] = '\0';  // NOLINT(clang-analyzer-security.ArrayBound)

    CHECK(std::string(buf) == "hello world");
    CHECK(r.size == 11u);
    CHECK(!r.truncated);
  }

  // -------------------------------------------------------------------------
  TEST_CASE("format int") {
    CHECK(fmt("{}", 42) == "42");
    CHECK(fmt("{}", -1) == "-1");
    CHECK(fmt("{}", 0) == "0");
    CHECK(fmt("{}", 2147483647) == "2147483647");
    CHECK(fmt("{}", -2147483647) == "-2147483647");
  }

  // -------------------------------------------------------------------------
  TEST_CASE("format unsigned int") {
    CHECK(fmt("{}", 0u) == "0");
    CHECK(fmt("{}", 42u) == "42");
    CHECK(fmt("{}", 4294967295u) == "4294967295");
  }

  // -------------------------------------------------------------------------
  TEST_CASE("format long long") {
    long long big = 9000000000LL;  // NOLINT(runtime/int, google-runtime-int)
    CHECK(fmt("{}", big) == "9000000000");

    long long neg_big = -9000000000LL;  // NOLINT(runtime/int, google-runtime-int)
    CHECK(fmt("{}", neg_big) == "-9000000000");
  }

  // -------------------------------------------------------------------------
  TEST_CASE("format unsigned long long") {
    unsigned long long v = 18000000000ULL;  // NOLINT(runtime/int, google-runtime-int)
    CHECK(fmt("{}", v) == "18000000000");
  }

  // -------------------------------------------------------------------------
  TEST_CASE("format bool") {
    CHECK(fmt("{}", true) == "true");
    CHECK(fmt("{}", false) == "false");
  }

  // -------------------------------------------------------------------------
  TEST_CASE("format char") {
    CHECK(fmt("{}", 'A') == "A");
    CHECK(fmt("{}", '0') == "0");
  }

  // -------------------------------------------------------------------------
  TEST_CASE("format float") {
    char buf[64];
    auto r = format::format_to_n(buf, sizeof(buf) - 1, "{}", 1.5f);
    buf[r.size] = '\0';  // NOLINT(clang-analyzer-security.ArrayBound)

    std::string s(buf);
    CHECK(!s.empty());
    // "1.5" is the expected %g output
    CHECK(s == "1.5");
  }

  // -------------------------------------------------------------------------
  TEST_CASE("format double") {
    char buf[64];
    auto r = format::format_to_n(buf, sizeof(buf) - 1, "{}", 3.14);
    buf[r.size] = '\0';  // NOLINT(clang-analyzer-security.ArrayBound)

    std::string s(buf);
    CHECK(!s.empty());
    CHECK(s == "3.14");
  }

  // -------------------------------------------------------------------------
  TEST_CASE("format const char*") {
    CHECK(fmt("{}", "hello") == "hello");
    CHECK(fmt("{}", "world") == "world");
  }

  // -------------------------------------------------------------------------
  TEST_CASE("format null const char* writes (null)") {
    const char* p = nullptr;
    CHECK(fmt("{}", p) == "(null)");
  }

  // -------------------------------------------------------------------------
  TEST_CASE("format std::string") {
    std::string s = "vlink";
    CHECK(fmt("{}", s) == "vlink");
  }

  // -------------------------------------------------------------------------
  TEST_CASE("format std::string_view") {
    std::string_view sv = "view";
    CHECK(fmt("{}", sv) == "view");
  }

  // -------------------------------------------------------------------------
  TEST_CASE("format pointer") {
    int x = 0;
    int* p = &x;
    char buf[64];
    auto r = format::format_to_n(buf, sizeof(buf) - 1, "{}", p);
    buf[r.size] = '\0';  // NOLINT(clang-analyzer-security.ArrayBound)

    std::string s(buf);
    // Pointer output starts with "0x"
    CHECK(s.substr(0, 2) == "0x");
    CHECK(s.size() >= 3u);
  }

  // -------------------------------------------------------------------------
  TEST_CASE("format nullptr pointer") {
    void* p = nullptr;
    char buf[64];
    auto r = format::format_to_n(buf, sizeof(buf) - 1, "{}", p);
    buf[r.size] = '\0';  // NOLINT(clang-analyzer-security.ArrayBound)

    std::string s(buf);
    CHECK(s == "0x0");
  }

  // -------------------------------------------------------------------------
  TEST_CASE("format enum uses underlying integral value") {
    enum class Color : int { Red = 1, Green = 2, Blue = 3 };  // NOLINT(performance-enum-size)
    CHECK(fmt("{}", Color::Red) == "1");
    CHECK(fmt("{}", Color::Green) == "2");
    CHECK(fmt("{}", Color::Blue) == "3");
  }

  // -------------------------------------------------------------------------
  TEST_CASE("multiple positional {} placeholders consumed in order") {
    CHECK(fmt("{} {} {}", 1, 2, 3) == "1 2 3");
    CHECK(fmt("a={} b={}", 10, 20) == "a=10 b=20");
  }

  // -------------------------------------------------------------------------
  TEST_CASE("explicit index placeholders {0} {1}") {
    char buf[64];
    auto r = format::format_to_n(buf, sizeof(buf) - 1, "{0} {1}", 10, 20);
    buf[r.size] = '\0';  // NOLINT(clang-analyzer-security.ArrayBound)

    CHECK(std::string(buf) == "10 20");
  }

  // -------------------------------------------------------------------------
  TEST_CASE("explicit index can repeat argument") {
    char buf[64];
    auto r = format::format_to_n(buf, sizeof(buf) - 1, "{0} {0}", 42);
    buf[r.size] = '\0';  // NOLINT(clang-analyzer-security.ArrayBound)

    CHECK(std::string(buf) == "42 42");
  }

  // -------------------------------------------------------------------------
  TEST_CASE("escaped braces {{ and }}") {
    char buf[64];
    auto r = format::format_to_n(buf, sizeof(buf) - 1, "{{}}");
    buf[r.size] = '\0';  // NOLINT(clang-analyzer-security.ArrayBound)

    // "{{" -> '{', "}}" -> '}'
    CHECK(std::string(buf) == "{}");
  }

  // -------------------------------------------------------------------------
  TEST_CASE("truncated output sets truncated flag") {
    char buf[5];
    auto r = format::format_to_n(buf, 4, "hello world");

    CHECK(r.truncated);
    CHECK(r.size == 11u);
  }

  // -------------------------------------------------------------------------
  TEST_CASE("non-truncated output clears truncated flag") {
    char buf[64];
    auto r = format::format_to_n(buf, sizeof(buf) - 1, "hi");

    CHECK(!r.truncated);
    CHECK(r.size == 2u);
  }

  // -------------------------------------------------------------------------
  TEST_CASE("format_to fixed array overload") {
    char buf[64];
    int v = 7;
    auto r = format::format_to(buf, "x={}", v);

    buf[r.size < sizeof(buf) ? r.size : sizeof(buf) - 1] = '\0';
    CHECK(std::string(buf, r.size) == "x=7");
    CHECK(!r.truncated);
  }

  // -------------------------------------------------------------------------
  TEST_CASE("format_to iterator overload writes via char* pointer") {
    char buf[64];
    char* ptr = buf;
    int a = 3;
    int b = 4;
    // Use explicit char* (not array) to invoke the iterator overload
    char* it = format::format_to(ptr, "v={} w={}", a, b);
    auto written = static_cast<size_t>(it - buf);

    CHECK(std::string(buf, written) == "v=3 w=4");
  }

  // -------------------------------------------------------------------------
  TEST_CASE("mixed types in single format call") {
    char buf[128];
    auto r = format::format_to_n(buf, sizeof(buf) - 1, "{} {} {} {} {}", 1, true, 'X', "str", 3.14);
    buf[r.size] = '\0';  // NOLINT(clang-analyzer-security.ArrayBound)

    std::string s(buf);
    CHECK(s.find("1") != std::string::npos);
    CHECK(s.find("true") != std::string::npos);
    CHECK(s.find("X") != std::string::npos);
    CHECK(s.find("str") != std::string::npos);
    CHECK(s.find("3.14") != std::string::npos);
  }

  // -------------------------------------------------------------------------
  TEST_CASE("format short integer") {
    short v = 100;  // NOLINT(runtime/int, google-runtime-int)
    CHECK(fmt("{}", v) == "100");
  }

  // -------------------------------------------------------------------------
  TEST_CASE("format unsigned short integer") {
    unsigned short v = 200;  // NOLINT(runtime/int, google-runtime-int)
    CHECK(fmt("{}", v) == "200");
  }

  // -------------------------------------------------------------------------
  TEST_CASE("format signed char") {
    signed char v = -5;  // NOLINT(runtime/int, google-runtime-int)
    CHECK(fmt("{}", v) == "-5");
  }

  // -------------------------------------------------------------------------
  TEST_CASE("format unsigned char") {
    unsigned char v = 255;  // NOLINT(runtime/int, google-runtime-int)
    CHECK(fmt("{}", v) == "255");
  }

  // -------------------------------------------------------------------------
  TEST_CASE("format long") {
    long v = 100000L;  // NOLINT(runtime/int, google-runtime-int)
    CHECK(fmt("{}", v) == "100000");
  }

  // -------------------------------------------------------------------------
  TEST_CASE("format unsigned long") {
    unsigned long v = 200000UL;  // NOLINT(runtime/int, google-runtime-int)
    CHECK(fmt("{}", v) == "200000");
  }

  // -------------------------------------------------------------------------
  TEST_CASE("format_to_n with no args and literal braces") {
    char buf[64];
    auto r = format::format_to_n(buf, sizeof(buf) - 1, "{{open}} {{close}}");
    buf[r.size] = '\0';  // NOLINT(clang-analyzer-security.ArrayBound)

    CHECK(std::string(buf, r.size) == "{open} {close}");
    CHECK(!r.truncated);
  }

  // -------------------------------------------------------------------------
  TEST_CASE("empty format string produces empty output") {
    char buf[64];
    auto r = format::format_to_n(buf, sizeof(buf) - 1, "");
    buf[r.size] = '\0';  // NOLINT(clang-analyzer-security.ArrayBound)

    CHECK(r.size == 0u);
    CHECK(!r.truncated);
  }

  // -------------------------------------------------------------------------
  TEST_CASE("zero capacity buffer: size reported correctly even when truncated") {
    char buf[1];
    auto r = format::format_to_n(buf, 0, "hello");

    CHECK(r.size == 5u);
    CHECK(r.truncated);
  }

  // -------------------------------------------------------------------------
  TEST_CASE("FString implicit construction from string literal") {
    vlink::format::FString<> fs("test literal");
    std::string_view sv = fs;
    CHECK(sv == "test literal");
  }

  // -------------------------------------------------------------------------
  TEST_CASE("make_format_args captures arguments") {
    auto store = vlink::format::make_format_args(1, 2, 3);
    CHECK(store.kNumArgs == 3u);
  }

  // -------------------------------------------------------------------------
  TEST_CASE("format negative int edge case INT_MIN") { CHECK(fmt("{}", -2147483647 - 1) == "-2147483648"); }

  // -------------------------------------------------------------------------
  TEST_CASE("format single char brace at end of format string") {
    char buf[64];
    auto r = format::format_to_n(buf, sizeof(buf) - 1, "trailing{");
    buf[r.size] = '\0';
    CHECK(std::string(buf) == "trailing{");
  }

  // -------------------------------------------------------------------------
  TEST_CASE("format single closing brace not doubled") {
    char buf[64];
    auto r = format::format_to_n(buf, sizeof(buf) - 1, "a}b");
    buf[r.size] = '\0';
    CHECK(std::string(buf) == "a}b");
  }

  // -------------------------------------------------------------------------
  TEST_CASE("format more placeholders than arguments") {
    // Extra {} beyond arg count should just be skipped
    char buf[64];
    auto r = format::format_to_n(buf, sizeof(buf) - 1, "{} {} {}", 1);
    buf[r.size] = '\0';
    std::string s(buf, r.size);
    // Only first placeholder should produce output
    CHECK(s.find("1") != std::string::npos);
  }

  // -------------------------------------------------------------------------
  TEST_CASE("format explicit index out of range skipped") {
    char buf[64];
    auto r = format::format_to_n(buf, sizeof(buf) - 1, "{99}", 42);
    buf[r.size] = '\0';
    // Index 99 is out of range, should produce no output for that placeholder
    CHECK(std::string(buf, r.size).empty());
  }

  // -------------------------------------------------------------------------
  TEST_CASE("format double zero") { CHECK(fmt("{}", 0.0) == "0"); }

  // -------------------------------------------------------------------------
  TEST_CASE("format float negative") {
    char buf[64];
    auto r = format::format_to_n(buf, sizeof(buf) - 1, "{}", -2.5F);
    buf[r.size] = '\0';
    CHECK(std::string(buf) == "-2.5");
  }

  // -------------------------------------------------------------------------
  TEST_CASE("format double large value") {
    char buf[64];
    auto r = format::format_to_n(buf, sizeof(buf) - 1, "{}", 1e15);
    buf[r.size] = '\0';
    std::string s(buf, r.size);
    CHECK(!s.empty());
  }

  // -------------------------------------------------------------------------
  TEST_CASE("format unsigned long long zero") {
    unsigned long long v = 0ULL;  // NOLINT(runtime/int, google-runtime-int)
    CHECK(fmt("{}", v) == "0");
  }

  // -------------------------------------------------------------------------
  TEST_CASE("format negative long long edge") {
    long long v = -1LL;  // NOLINT(runtime/int, google-runtime-int)
    CHECK(fmt("{}", v) == "-1");
  }

  // -------------------------------------------------------------------------
  TEST_CASE("format empty std::string") {
    std::string s;
    CHECK(fmt("{}", s).empty());
  }

  // -------------------------------------------------------------------------
  TEST_CASE("format empty string_view") {
    std::string_view sv;
    CHECK(fmt("{}", sv).empty());
  }

  // -------------------------------------------------------------------------
  TEST_CASE("format mixed escaped braces and placeholders") { CHECK(fmt("{{{}}} {}", 1, 2) == "{1} 2"); }

  // -------------------------------------------------------------------------
  TEST_CASE("truncation with placeholder argument") {
    char buf[4];
    auto r = format::format_to_n(buf, 3, "{}", 12345);
    CHECK(r.truncated);
    CHECK(r.size == 5u);
    // First 3 chars should be "123"
    CHECK(buf[0] == '1');
    CHECK(buf[1] == '2');
    CHECK(buf[2] == '3');
  }

  // -------------------------------------------------------------------------
  TEST_CASE("format_to iterator with back_inserter") {
    std::string result;
    int val = 99;
    format::format_to(std::back_inserter(result), "num={}", val);
    CHECK(result == "num=99");
  }

  // -------------------------------------------------------------------------
  TEST_CASE("FString construction from std::string") {
    std::string s = "dynamic";
    vlink::format::FString<> fs(s);
    CHECK(fs.get() == "dynamic");
  }

  // -------------------------------------------------------------------------
  TEST_CASE("FString construction from std::string_view") {
    std::string_view sv = "view-based";
    vlink::format::FString<> fs(sv);
    CHECK(fs.get() == "view-based");
  }

  // -------------------------------------------------------------------------
  TEST_CASE("format unsigned char boundary 0") {
    unsigned char v = 0;  // NOLINT(runtime/int, google-runtime-int)
    CHECK(fmt("{}", v) == "0");
  }

  // -------------------------------------------------------------------------
  TEST_CASE("format signed char positive") {
    signed char v = 127;  // NOLINT(runtime/int, google-runtime-int)
    CHECK(fmt("{}", v) == "127");
  }

  // -------------------------------------------------------------------------
  TEST_CASE("format short negative boundary") {
    short v = -32768;  // NOLINT(runtime/int, google-runtime-int)
    CHECK(fmt("{}", v) == "-32768");
  }

  // -------------------------------------------------------------------------
  TEST_CASE("format unsigned short max") {
    unsigned short v = 65535;  // NOLINT(runtime/int, google-runtime-int)
    CHECK(fmt("{}", v) == "65535");
  }
}

// NOLINTEND
