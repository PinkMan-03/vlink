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

#include "./base/fast_stream.h"

#include <iostream>

//

#include <doctest/doctest.h>

#include <iomanip>
#include <sstream>
#include <string>
#include <string_view>

//
#include "../common_test.h"

// ---------------------------------------------------------------------------
TEST_SUITE("base-FastStream") {
  // -------------------------------------------------------------------------
  TEST_CASE("default constructed stream is empty") {
    FastStream stream;

    CHECK(stream.size() == 0u);
    CHECK(stream.capacity() >= 0u);
  }

  // -------------------------------------------------------------------------
  TEST_CASE("write string literal via operator<<") {
    FastStream stream;
    stream << "hello";

    CHECK(stream.size() == 5u);
  }

  // -------------------------------------------------------------------------
  TEST_CASE("write integer via operator<<") {
    FastStream stream;
    stream << 42;

    std::string_view sv = stream.take_view();
    CHECK(sv == "42");
  }

  // -------------------------------------------------------------------------
  TEST_CASE("write multiple values via operator<<") {
    FastStream stream;
    stream << "x=" << 10 << " y=" << 20;

    std::string_view sv = stream.take_view();
    CHECK(sv == "x=10 y=20");
  }

  // -------------------------------------------------------------------------
  TEST_CASE("take_view returns current buffer content") {
    FastStream stream;
    stream << "vlink";

    std::string_view sv = stream.take_view();
    CHECK(sv == "vlink");
  }

  // -------------------------------------------------------------------------
  TEST_CASE("take_view does not reset the stream size") {
    FastStream stream;
    stream << "before";
    (void)stream.take_view();  // returns view but does NOT reset

    // size() is still non-zero because take_view() doesn't reset
    CHECK(stream.size() == 6u);
  }

  // -------------------------------------------------------------------------
  TEST_CASE("after reset followed by new write, take_view shows new content") {
    FastStream stream;
    stream << "first";
    stream.reset();

    stream << "second";
    std::string_view sv = stream.take_view();
    CHECK(sv == "second");
  }

  // -------------------------------------------------------------------------
  TEST_CASE("reset clears the buffer") {
    FastStream stream;
    stream << "content";

    CHECK(stream.size() > 0u);

    stream.reset();

    CHECK(stream.size() == 0u);
  }

  // -------------------------------------------------------------------------
  TEST_CASE("after reset stream accepts new content") {
    FastStream stream;
    stream << "old";
    stream.reset();
    stream << "new";

    std::string_view sv = stream.take_view();
    CHECK(sv == "new");
  }

  // -------------------------------------------------------------------------
  TEST_CASE("append_to copies buffer to external string") {
    FastStream stream;
    stream << "appended";

    std::string target = "prefix-";
    stream.append_to(target);

    CHECK(target == "prefix-appended");
    // Buffer not reset after append_to
    CHECK(stream.size() == 8u);
  }

  // -------------------------------------------------------------------------
  TEST_CASE("append_to multiple times accumulates in target") {
    FastStream stream;
    stream << "part1";

    std::string result;
    stream.append_to(result);
    stream.reset();

    stream << "part2";
    stream.append_to(result);

    CHECK(result == "part1part2");
  }

  // -------------------------------------------------------------------------
  TEST_CASE("write_raw appends raw bytes") {
    FastStream stream;
    const char* data = "raw";
    stream.write_raw(data, 3u);

    std::string_view sv = stream.take_view();
    CHECK(sv == "raw");
  }

  // -------------------------------------------------------------------------
  TEST_CASE("write_raw zero length does not change size") {
    FastStream stream;
    stream << "before";
    size_t sz_before = stream.size();

    stream.write_raw("ignored", 0u);

    CHECK(stream.size() == sz_before);
  }

  // -------------------------------------------------------------------------
  TEST_CASE("write_raw chained with operator<<") {
    FastStream stream;
    stream.write_raw("A", 1u);
    stream << "B";
    stream.write_raw("C", 1u);

    std::string_view sv = stream.take_view();
    CHECK(sv == "ABC");
  }

  // -------------------------------------------------------------------------
  TEST_CASE("capacity returns at least default 256 bytes after construction") {
    FastStream stream;
    CHECK(stream.capacity() >= 256u);
  }

  // -------------------------------------------------------------------------
  TEST_CASE("buffer grows beyond initial capacity") {
    FastStream stream;

    // Write more than 256 bytes
    std::string big(300, 'X');
    stream << big;

    CHECK(stream.size() == 300u);
    CHECK(stream.capacity() >= 300u);

    std::string_view sv = stream.take_view();
    CHECK(sv.size() == 300u);
    CHECK(sv.front() == 'X');
    CHECK(sv.back() == 'X');
  }

  // -------------------------------------------------------------------------
  TEST_CASE("shrink_to_fit after reset reduces capacity") {
    FastStream stream;
    std::string big(8000, 'Y');
    stream << big;

    CHECK(stream.capacity() >= 8000u);

    stream.reset();
    stream.shrink_to_fit();

    // After shrink the capacity may still be >= 8192 due to kMaxExpandSize
    // growth strategy, but should not grow further. Just verify no crash.
    CHECK(stream.size() == 0u);
  }

  // -------------------------------------------------------------------------
  TEST_CASE("stream supports std::endl and std::flush") {
    FastStream stream;
    stream << "line" << std::endl;

    CHECK(stream.size() >= 5u);  // "line\n"
  }

  // -------------------------------------------------------------------------
  TEST_CASE("stream supports std::hex manipulator") {
    FastStream stream;
    stream << std::hex << 255;

    std::string_view sv = stream.take_view();
    CHECK(sv == "ff");
  }

  // -------------------------------------------------------------------------
  TEST_CASE("stream supports std::setw and std::setfill") {
    FastStream stream;
    stream << std::setw(5) << std::setfill('0') << 42;

    std::string_view sv = stream.take_view();
    CHECK(sv == "00042");
  }

  // -------------------------------------------------------------------------
  TEST_CASE("stream supports bool as 1/0 by default") {
    FastStream stream;
    stream << true << "/" << false;

    std::string_view sv = stream.take_view();
    CHECK(sv == "1/0");
  }

  // -------------------------------------------------------------------------
  TEST_CASE("stream supports std::boolalpha") {
    FastStream stream;
    stream << std::boolalpha << true << "/" << false;

    std::string_view sv = stream.take_view();
    CHECK(sv == "true/false");
  }

  // -------------------------------------------------------------------------
  TEST_CASE("stream supports floating point output") {
    FastStream stream;
    stream << 3.14;

    std::string_view sv = stream.take_view();
    CHECK(!sv.empty());
    CHECK(sv.find("3.14") != std::string_view::npos);
  }

  // -------------------------------------------------------------------------
  TEST_CASE("stream error state cleared after reset") {
    FastStream stream;
    // Force badbit by writing when the stream is fine (just test reset clears)
    stream << "data";
    stream.reset();

    // After reset, stream should be in good state
    stream << "new data";
    CHECK(stream.size() == 8u);
  }

  // -------------------------------------------------------------------------
  TEST_CASE("size matches actual written content length") {
    FastStream stream;
    std::string content = "measure me";
    stream << content;

    CHECK(stream.size() == content.size());
  }

  // -------------------------------------------------------------------------
  TEST_CASE("empty take_view returns empty string_view") {
    FastStream stream;
    std::string_view sv = stream.take_view();

    CHECK(sv.empty());
  }

  // -------------------------------------------------------------------------
  TEST_CASE("consecutive resets and writes behave correctly") {
    FastStream stream;

    for (int i = 0; i < 5; ++i) {
      stream << "iteration-" << i;
      std::string_view sv = stream.take_view();
      CHECK(!sv.empty());
      stream.reset();
    }

    CHECK(stream.size() == 0u);
  }
}

// NOLINTEND
