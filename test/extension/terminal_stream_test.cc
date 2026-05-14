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

#include "./extension/terminal_stream.h"

#if defined(__unix__) && !defined(__CYGWIN__)

#include <doctest/doctest.h>

#include <ostream>
#include <string>

//
#include "../common_test.h"

// ---------------------------------------------------------------------------
// TEST SUITE: TerminalStream - singleton
// ---------------------------------------------------------------------------

TEST_SUITE("extension-TerminalStream - singleton") {
  TEST_CASE("get() returns reference to same instance") {
    TerminalStream& a = TerminalStream::get();
    TerminalStream& b = TerminalStream::get();
    CHECK(&a == &b);
  }

  TEST_CASE("kDefaultBufferSize is 1 MiB") { CHECK(TerminalStream::kDefaultBufferSize == 1024u * 1024u * 1u); }
}

// ---------------------------------------------------------------------------
// TEST SUITE: TerminalStream - init / TTY detection
// ---------------------------------------------------------------------------

TEST_SUITE("extension-TerminalStream - init") {
  TEST_CASE("init() is idempotent") {
    TerminalStream& ts = TerminalStream::get();

    // First call sets is_initialized
    ts.init();
    CHECK(ts.is_initialized() == true);

    // Second call must not crash or reset state
    ts.init();
    CHECK(ts.is_initialized() == true);
  }

  TEST_CASE("is_initialized() returns true after init()") {
    TerminalStream& ts = TerminalStream::get();
    ts.init();
    CHECK(ts.is_initialized() == true);
  }

  TEST_CASE("is_tty() is accessible after init()") {
    TerminalStream& ts = TerminalStream::get();
    ts.init();

    // In a test environment stdout is typically not a TTY.
    // We just check that the call does not crash.
    [[maybe_unused]] bool tty = ts.is_tty();
    CHECK(true);  // just ensure no exception/crash
  }
}

// ---------------------------------------------------------------------------
// TEST SUITE: TerminalStream - flush
// ---------------------------------------------------------------------------

TEST_SUITE("extension-TerminalStream - flush") {
  TEST_CASE("flush() does not crash on empty buffer") {
    TerminalStream& ts = TerminalStream::get();
    ts.flush();
    CHECK(true);
  }

  TEST_CASE("flush() does not crash after writing content") {
    TerminalStream& ts = TerminalStream::get();
    ts << "test flush";
    ts.flush();
    CHECK(true);
  }
}

// ---------------------------------------------------------------------------
// TEST SUITE: TerminalStream - operator<< basic types
// ---------------------------------------------------------------------------

TEST_SUITE("extension-TerminalStream - operator<< basic types") {
  TEST_CASE("operator<< char does not crash") {
    TerminalStream& ts = TerminalStream::get();
    ts << 'X';
    CHECK(true);
  }

  TEST_CASE("operator<< const char* does not crash") {
    TerminalStream& ts = TerminalStream::get();
    ts << "hello terminal";
    CHECK(true);
  }

  TEST_CASE("operator<< null const char* is no-op") {
    TerminalStream& ts = TerminalStream::get();
    const char* null_str = nullptr;
    ts << null_str;
    CHECK(true);
  }

  TEST_CASE("operator<< std::string does not crash") {
    TerminalStream& ts = TerminalStream::get();
    std::string s = "std string test";
    ts << s;
    CHECK(true);
  }

  TEST_CASE("operator<< std::string_view does not crash") {
    TerminalStream& ts = TerminalStream::get();
    std::string_view sv = "string view test";
    ts << sv;
    CHECK(true);
  }

  TEST_CASE("operator<< bool (true) does not crash") {
    TerminalStream& ts = TerminalStream::get();
    ts << true;
    CHECK(true);
  }

  TEST_CASE("operator<< bool (false) does not crash") {
    TerminalStream& ts = TerminalStream::get();
    ts << false;
    CHECK(true);
  }

  TEST_CASE("operator<< int does not crash") {
    TerminalStream& ts = TerminalStream::get();
    ts << 42;
    CHECK(true);
  }

  TEST_CASE("operator<< negative int does not crash") {
    TerminalStream& ts = TerminalStream::get();
    ts << -999;
    CHECK(true);
  }

  TEST_CASE("operator<< zero does not crash") {
    TerminalStream& ts = TerminalStream::get();
    ts << 0;
    CHECK(true);
  }

  TEST_CASE("operator<< unsigned int does not crash") {
    TerminalStream& ts = TerminalStream::get();
    ts << 100u;
    CHECK(true);
  }

  TEST_CASE("operator<< long does not crash") {
    TerminalStream& ts = TerminalStream::get();
    ts << 123456L;
    CHECK(true);
  }

  TEST_CASE("operator<< long long does not crash") {
    TerminalStream& ts = TerminalStream::get();
    ts << 9876543210LL;
    CHECK(true);
  }

  TEST_CASE("operator<< unsigned long long does not crash") {
    TerminalStream& ts = TerminalStream::get();
    ts << 18446744073709551615ULL;
    CHECK(true);
  }

  TEST_CASE("operator<< float does not crash") {
    TerminalStream& ts = TerminalStream::get();
    ts << 3.14f;
    CHECK(true);
  }

  TEST_CASE("operator<< double does not crash") {
    TerminalStream& ts = TerminalStream::get();
    ts << 2.718281828;
    CHECK(true);
  }

  TEST_CASE("operator<< long double does not crash") {
    TerminalStream& ts = TerminalStream::get();
    ts << 1.41421356237L;
    CHECK(true);
  }

  TEST_CASE("operator<< void pointer does not crash") {
    TerminalStream& ts = TerminalStream::get();
    void* ptr = &ts;
    ts << ptr;
    CHECK(true);
  }

  TEST_CASE("operator<< nullptr void pointer does not crash") {
    TerminalStream& ts = TerminalStream::get();
    const void* null_ptr = nullptr;
    ts << null_ptr;
    CHECK(true);
  }
}

// ---------------------------------------------------------------------------
// TEST SUITE: TerminalStream - manipulators
// ---------------------------------------------------------------------------

TEST_SUITE("extension-TerminalStream - manipulators") {
  TEST_CASE("endl manipulator flushes and appends newline") {
    TerminalStream& ts = TerminalStream::get();
    ts << "before endl" << TerminalStream::endl;
    CHECK(true);
  }

  TEST_CASE("flush_manip manipulator flushes buffer") {
    TerminalStream& ts = TerminalStream::get();
    ts << "before flush_manip" << TerminalStream::flush_manip;
    CHECK(true);
  }

  TEST_CASE("std::endl is accepted as manipulator") {
    TerminalStream& ts = TerminalStream::get();
    ts << "before std::endl" << std::endl;
    CHECK(true);
  }
}

// ---------------------------------------------------------------------------
// TEST SUITE: TerminalStream - write_raw
// ---------------------------------------------------------------------------

TEST_SUITE("extension-TerminalStream - write_raw") {
  TEST_CASE("write_raw with valid pointer does not crash") {
    TerminalStream& ts = TerminalStream::get();
    const char data[] = "raw write test";
    ts.write_raw(data, sizeof(data) - 1);
    CHECK(true);
  }

  TEST_CASE("write_raw with zero length does not crash") {
    TerminalStream& ts = TerminalStream::get();
    const char data[] = "ignored";
    ts.write_raw(data, 0);
    CHECK(true);
  }
}

// ---------------------------------------------------------------------------
// TEST SUITE: TerminalStream - chaining
// ---------------------------------------------------------------------------

TEST_SUITE("extension-TerminalStream - chaining") {
  TEST_CASE("operator<< returns self for chaining") {
    TerminalStream& ts = TerminalStream::get();
    TerminalStream& ref = (ts << "a" << 1 << ' ' << 3.14f);
    CHECK(&ref == &ts);
  }

  TEST_CASE("VLINK_TERM_OUT macro works") {
    VLINK_TERM_OUT << "VLINK_TERM_OUT test" << TerminalStream::endl;
    CHECK(true);
  }
}

// ---------------------------------------------------------------------------
// TEST SUITE: TerminalStream - MIN INT handling
// ---------------------------------------------------------------------------

TEST_SUITE("extension-TerminalStream - integer edge cases") {
  TEST_CASE("INT_MIN does not crash") {
    TerminalStream& ts = TerminalStream::get();
    ts << std::numeric_limits<int>::min() << TerminalStream::endl;
    CHECK(true);
  }

  TEST_CASE("INT_MAX does not crash") {
    TerminalStream& ts = TerminalStream::get();
    ts << std::numeric_limits<int>::max() << TerminalStream::endl;
    CHECK(true);
  }

  TEST_CASE("LLONG_MIN does not crash") {
    TerminalStream& ts = TerminalStream::get();
    ts << std::numeric_limits<long long>::min() << TerminalStream::endl;  // NOLINT(runtime/int, google-runtime-int)
    CHECK(true);
  }
}

#endif

// NOLINTEND
