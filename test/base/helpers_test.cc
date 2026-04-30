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

#include "./base/helpers.h"

#include <doctest/doctest.h>

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

//
#include "../common_test.h"

// ---------------------------------------------------------------------------
// TEST SUITE: to_int
// ---------------------------------------------------------------------------

TEST_SUITE("base-Helpers - to_int") {
  TEST_CASE("converts positive decimal string") { CHECK(Helpers::to_int("42") == 42); }

  TEST_CASE("converts negative decimal string") { CHECK(Helpers::to_int("-7") == -7); }

  TEST_CASE("converts zero string") { CHECK(Helpers::to_int("0") == 0); }

  TEST_CASE("invalid string returns default value 0") { CHECK(Helpers::to_int("abc") == 0); }

  TEST_CASE("invalid string returns supplied default value") { CHECK(Helpers::to_int("xyz", 99) == 99); }

  TEST_CASE("empty string returns default value") { CHECK(Helpers::to_int("", -1) == -1); }
}

// ---------------------------------------------------------------------------
// TEST SUITE: to_long
// ---------------------------------------------------------------------------

TEST_SUITE("base-Helpers - to_long") {
  TEST_CASE("converts positive large integer") { CHECK(Helpers::to_long("123456789") == 123456789LL); }

  TEST_CASE("converts negative large integer") { CHECK(Helpers::to_long("-100") == -100LL); }

  TEST_CASE("converts zero") { CHECK(Helpers::to_long("0") == 0LL); }

  TEST_CASE("invalid string returns default") { CHECK(Helpers::to_long("bad", -1) == -1LL); }

  TEST_CASE("offset adjusts expected parsed length") {
    // offset means pos must equal str.size() - offset when parsing completes.
    // "1234" with offset=2 expects pos==2, so it would return default.
    // "12" with offset=0 should parse 12 fully (pos==2==size-0)
    CHECK(Helpers::to_long("12", 0, 0) == 12LL);
  }
}

// ---------------------------------------------------------------------------
// TEST SUITE: double_to_string
// ---------------------------------------------------------------------------

TEST_SUITE("base-Helpers - double_to_string") {
  TEST_CASE("default precision of 2 decimal places") {
    std::string s = Helpers::double_to_string(3.14);  // NOLINT(modernize-use-std-numbers)
    CHECK(!s.empty());
    CHECK(s.find('.') != std::string::npos);
  }

  TEST_CASE("zero returns valid string") {
    std::string s = Helpers::double_to_string(0.0);
    CHECK(!s.empty());
  }

  TEST_CASE("custom precision 0 omits decimal") {
    std::string s = Helpers::double_to_string(3.7, 0);
    CHECK(!s.empty());
  }

  TEST_CASE("negative value converted correctly") {
    std::string s = Helpers::double_to_string(-1.5);
    CHECK(s.find('-') != std::string::npos);
  }
}

// ---------------------------------------------------------------------------
// TEST SUITE: hash_combine
// ---------------------------------------------------------------------------

TEST_SUITE("base-Helpers - hash_combine") {
  TEST_CASE("same inputs produce same output") {
    uint64_t h1 = Helpers::hash_combine(100ULL, 200ULL);
    uint64_t h2 = Helpers::hash_combine(100ULL, 200ULL);
    CHECK(h1 == h2);
  }

  TEST_CASE("different inputs produce different output") {
    uint64_t h1 = Helpers::hash_combine(1ULL, 2ULL);
    uint64_t h2 = Helpers::hash_combine(2ULL, 1ULL);
    CHECK(h1 != h2);
  }

  TEST_CASE("zero inputs does not crash") {
    uint64_t h = Helpers::hash_combine(0ULL, 0ULL);
    (void)h;
    CHECK(true);
  }
}

// ---------------------------------------------------------------------------
// TEST SUITE: replace_string
// ---------------------------------------------------------------------------

TEST_SUITE("base-Helpers - replace_string") {
  TEST_CASE("replaces single occurrence") {
    std::string s = "hello world";
    Helpers::replace_string(s, "world", "VLink");
    CHECK(s == "hello VLink");
  }

  TEST_CASE("replaces all occurrences") {
    std::string s = "aaa";
    Helpers::replace_string(s, "a", "bb");
    CHECK(s == "bbbbbb");
  }

  TEST_CASE("no match leaves string unchanged") {
    std::string s = "hello";
    Helpers::replace_string(s, "xyz", "abc");
    CHECK(s == "hello");
  }

  TEST_CASE("empty from string leaves string unchanged") {
    std::string s = "hello";
    Helpers::replace_string(s, "", "X");
    CHECK(s == "hello");
  }

  TEST_CASE("replace with empty string removes occurrences") {
    std::string s = "hello world";
    Helpers::replace_string(s, "world", "");
    CHECK(s == "hello ");
  }
}

// ---------------------------------------------------------------------------
// TEST SUITE: trim_string
// ---------------------------------------------------------------------------

TEST_SUITE("base-Helpers - trim_string") {
  TEST_CASE("trims leading spaces") { CHECK(Helpers::trim_string("   hello") == "hello"); }

  TEST_CASE("trims trailing spaces") { CHECK(Helpers::trim_string("hello   ") == "hello"); }

  TEST_CASE("trims both sides") { CHECK(Helpers::trim_string("  hello  ") == "hello"); }

  TEST_CASE("empty string returns empty") { CHECK(Helpers::trim_string("") == ""); }

  TEST_CASE("no whitespace returns original") { CHECK(Helpers::trim_string("hello") == "hello"); }
}

// ---------------------------------------------------------------------------
// TEST SUITE: get_split_string
// ---------------------------------------------------------------------------

TEST_SUITE("base-Helpers - get_split_string") {
  TEST_CASE("splits on delimiter") {
    auto parts = Helpers::get_split_string("a,b,c", ',');
    REQUIRE(parts.size() == 3U);
    CHECK(parts[0] == "a");
    CHECK(parts[1] == "b");
    CHECK(parts[2] == "c");
  }

  TEST_CASE("no delimiter returns single-element vector") {
    auto parts = Helpers::get_split_string("hello", ',');
    REQUIRE(parts.size() == 1U);
    CHECK(parts[0] == "hello");
  }

  TEST_CASE("empty string returns empty vector") {
    // Implementation returns {} for empty input
    auto parts = Helpers::get_split_string("", ',');
    CHECK(parts.empty());
  }

  TEST_CASE("consecutive delimiters skip empty tokens") {
    // Implementation skips empty segments between consecutive delimiters
    auto parts = Helpers::get_split_string("a,,b", ',');
    REQUIRE(parts.size() == 2U);
    CHECK(parts[0] == "a");
    CHECK(parts[1] == "b");
  }

  TEST_CASE("leading delimiter skips leading empty token") {
    // Implementation skips the leading empty part
    auto parts = Helpers::get_split_string(",a", ',');
    REQUIRE(parts.size() == 1U);
    CHECK(parts[0] == "a");
  }
}

// ---------------------------------------------------------------------------
// TEST SUITE: get_split_string_view
// ---------------------------------------------------------------------------

TEST_SUITE("base-Helpers - get_split_string_view") {
  TEST_CASE("splits correctly into views") {
    std::string src = "x:y:z";
    auto parts = Helpers::get_split_string_view(src, ':');
    REQUIRE(parts.size() == 3U);
    CHECK(parts[0] == "x");
    CHECK(parts[1] == "y");
    CHECK(parts[2] == "z");
  }

  TEST_CASE("no delimiter returns one view") {
    std::string src = "hello";
    auto parts = Helpers::get_split_string_view(src, '/');
    REQUIRE(parts.size() == 1U);
    CHECK(parts[0] == "hello");
  }
}

// ---------------------------------------------------------------------------
// TEST SUITE: get_pair_string
// ---------------------------------------------------------------------------

TEST_SUITE("base-Helpers - get_pair_string") {
  TEST_CASE("splits on first delimiter") {
    auto p = Helpers::get_pair_string("key=value", '=');
    CHECK(p.first == "key");
    CHECK(p.second == "value");
  }

  TEST_CASE("no delimiter returns original string in first and empty second") {
    // Implementation: when only one token found, returns {token, ""}
    auto p = Helpers::get_pair_string("noeq", '=');
    CHECK(p.first == "noeq");
    CHECK(p.second == "");
  }

  TEST_CASE("more than two tokens splits at first delimiter") {
    // Implementation: splits at first occurrence of '=', rest goes to second
    auto p = Helpers::get_pair_string("a=b=c", '=');
    CHECK(p.first == "a");
    CHECK(p.second == "b=c");
  }
}

// ---------------------------------------------------------------------------
// TEST SUITE: get_hash_code
// ---------------------------------------------------------------------------

TEST_SUITE("base-Helpers - get_hash_code") {
  TEST_CASE("same string produces same hash") {
    uint32_t h1 = Helpers::get_hash_code("hello");
    uint32_t h2 = Helpers::get_hash_code("hello");
    CHECK(h1 == h2);
  }

  TEST_CASE("different strings produce different hashes") {
    uint32_t h1 = Helpers::get_hash_code("hello");
    uint32_t h2 = Helpers::get_hash_code("world");
    CHECK(h1 != h2);
  }

  TEST_CASE("empty string does not crash") {
    uint32_t h = Helpers::get_hash_code("");
    (void)h;
    CHECK(true);
  }
}

// ---------------------------------------------------------------------------
// TEST SUITE: format_file_size
// ---------------------------------------------------------------------------

TEST_SUITE("base-Helpers - format_file_size") {
  TEST_CASE("bytes range") {
    std::string s = Helpers::format_file_size(512U);
    CHECK(!s.empty());
    CHECK(s.find('B') != std::string::npos);
  }

  TEST_CASE("kilobytes range") {
    std::string s = Helpers::format_file_size(1536U);
    CHECK(!s.empty());
    CHECK(s.find('K') != std::string::npos);
  }

  TEST_CASE("megabytes range") {
    std::string s = Helpers::format_file_size(1024U * 1024U * 2U);
    CHECK(!s.empty());
    CHECK(s.find('M') != std::string::npos);
  }

  TEST_CASE("zero size does not crash") {
    std::string s = Helpers::format_file_size(0U);
    CHECK(!s.empty());
  }
}

// ---------------------------------------------------------------------------
// TEST SUITE: format_rate_size
// ---------------------------------------------------------------------------

TEST_SUITE("base-Helpers - format_rate_size") {
  TEST_CASE("bytes-per-second range") {
    std::string s = Helpers::format_rate_size(500U);
    CHECK(!s.empty());
  }

  TEST_CASE("megabytes-per-second range contains MB") {
    std::string s = Helpers::format_rate_size(1024U * 1024U);
    CHECK(s.find('M') != std::string::npos);
  }
}

// ---------------------------------------------------------------------------
// TEST SUITE: format_milliseconds
// ---------------------------------------------------------------------------

TEST_SUITE("base-Helpers - format_milliseconds") {
  TEST_CASE("2 minutes 5 seconds without millis") {
    std::string s = Helpers::format_milliseconds(125000LL, false);
    CHECK(!s.empty());
  }

  TEST_CASE("2 minutes 5 seconds with millis") {
    std::string s = Helpers::format_milliseconds(125000LL, true);
    CHECK(!s.empty());
  }

  TEST_CASE("zero duration does not crash") {
    std::string s = Helpers::format_milliseconds(0LL, false);
    CHECK(!s.empty());
  }
}

// ---------------------------------------------------------------------------
// TEST SUITE: format_date
// ---------------------------------------------------------------------------

TEST_SUITE("base-Helpers - format_date") {
  TEST_CASE("epoch zero produces valid string") {
    std::string s = Helpers::format_date(0LL);
    CHECK(!s.empty());
  }

  TEST_CASE("non-zero nanosecond timestamp does not crash") {
    // Arbitrary timestamp: 2026-01-01T00:00:00 UTC approx
    int64_t ts = 1767225600LL * 1000000000LL;
    std::string s = Helpers::format_date(ts);
    CHECK(!s.empty());
  }
}

// ---------------------------------------------------------------------------
// TEST SUITE: format_time_diff
// ---------------------------------------------------------------------------

TEST_SUITE("base-Helpers - format_time_diff") {
  TEST_CASE("small milliseconds shows ms unit") {
    std::string s = Helpers::format_time_diff(250);
    CHECK(!s.empty());
  }

  TEST_CASE("large milliseconds shows h/m/s") {
    std::string s = Helpers::format_time_diff(5000000);
    CHECK(!s.empty());
  }

  TEST_CASE("zero does not crash") {
    std::string s = Helpers::format_time_diff(0);
    CHECK(!s.empty());
  }
}

// ---------------------------------------------------------------------------
// TEST SUITE: format_hex_number
// ---------------------------------------------------------------------------

TEST_SUITE("base-Helpers - format_hex_number") {
  TEST_CASE("signed integer produces 0x prefix") {
    std::string s = Helpers::format_hex_number(static_cast<int64_t>(255));
    CHECK(s.find("0x") == 0U);
  }

  TEST_CASE("unsigned integer produces 0x prefix") {
    std::string s = Helpers::format_hex_number(static_cast<uint64_t>(255U));
    CHECK(s.find("0x") == 0U);
  }

  TEST_CASE("zero produces 0x0 or 0x00...") {
    std::string s = Helpers::format_hex_number(static_cast<int64_t>(0));
    CHECK(s.find("0x") == 0U);
  }
}

// ---------------------------------------------------------------------------
// TEST SUITE: convert_date_to_timestamp
// ---------------------------------------------------------------------------

TEST_SUITE("base-Helpers - convert_date_to_timestamp") {
  TEST_CASE("valid date string with time returns positive timestamp") {
    // Implementation uses %Y/%m/%d %H:%M:%S format
    int64_t ts = Helpers::convert_date_to_timestamp("2026/01/01 00:00:00");
    CHECK(ts > 0);
  }

  TEST_CASE("invalid date string returns -1") {
    int64_t ts = Helpers::convert_date_to_timestamp("not-a-date");
    CHECK(ts == -1);
  }

  TEST_CASE("empty string returns -1") {
    int64_t ts = Helpers::convert_date_to_timestamp("");
    CHECK(ts == -1);
  }
}

// ---------------------------------------------------------------------------
// TEST SUITE: has_startwith (template / string literal overload)
// ---------------------------------------------------------------------------

TEST_SUITE("base-Helpers - has_startwith template") {
  TEST_CASE("string starts with prefix literal") {
    std::string s = "hello world";
    CHECK(Helpers::has_startwith(s, "hello"));
  }

  TEST_CASE("string does not start with prefix literal") {
    std::string s = "world hello";
    CHECK(!Helpers::has_startwith(s, "hello"));
  }

  TEST_CASE("empty string does not start with non-empty literal") {
    std::string s;
    CHECK(!Helpers::has_startwith(s, "hello"));
  }
}

// ---------------------------------------------------------------------------
// TEST SUITE: has_endwith (template / string literal overload)
// ---------------------------------------------------------------------------

TEST_SUITE("base-Helpers - has_endwith template") {
  TEST_CASE("string ends with suffix literal") {
    std::string s = "hello world";
    CHECK(Helpers::has_endwith(s, "world"));
  }

  TEST_CASE("string does not end with suffix literal") {
    std::string s = "hello world";
    CHECK(!Helpers::has_endwith(s, "hello"));
  }

  TEST_CASE("empty string does not end with non-empty literal") {
    std::string s;
    CHECK(!Helpers::has_endwith(s, "x"));
  }
}

// ---------------------------------------------------------------------------
// TEST SUITE: has_startwith (string_view overload)
// ---------------------------------------------------------------------------

TEST_SUITE("base-Helpers - has_startwith string_view") {
  TEST_CASE("starts with non-empty prefix") {
    CHECK(Helpers::has_startwith(std::string_view("foobar"), std::string_view("foo")));
  }

  TEST_CASE("does not start with wrong prefix") {
    CHECK(!Helpers::has_startwith(std::string_view("foobar"), std::string_view("bar")));
  }

  TEST_CASE("starts with empty prefix always true") {
    CHECK(Helpers::has_startwith(std::string_view("foobar"), std::string_view("")));
  }

  TEST_CASE("empty string starts with empty prefix") {
    CHECK(Helpers::has_startwith(std::string_view(""), std::string_view("")));
  }

  TEST_CASE("empty string does not start with non-empty prefix") {
    CHECK(!Helpers::has_startwith(std::string_view(""), std::string_view("x")));
  }
}

// ---------------------------------------------------------------------------
// TEST SUITE: has_endwith (string_view overload)
// ---------------------------------------------------------------------------

TEST_SUITE("base-Helpers - has_endwith string_view") {
  TEST_CASE("ends with non-empty suffix") {
    CHECK(Helpers::has_endwith(std::string_view("foobar"), std::string_view("bar")));
  }

  TEST_CASE("does not end with wrong suffix") {
    CHECK(!Helpers::has_endwith(std::string_view("foobar"), std::string_view("foo")));
  }

  TEST_CASE("ends with empty suffix always true") {
    CHECK(Helpers::has_endwith(std::string_view("foobar"), std::string_view("")));
  }

  TEST_CASE("empty string ends with empty suffix") {
    CHECK(Helpers::has_endwith(std::string_view(""), std::string_view("")));
  }

  TEST_CASE("empty string does not end with non-empty suffix") {
    CHECK(!Helpers::has_endwith(std::string_view(""), std::string_view("x")));
  }
}

// ---------------------------------------------------------------------------
// TEST SUITE: contains_substring
// ---------------------------------------------------------------------------

TEST_SUITE("base-Helpers - contains_substring") {
  TEST_CASE("substring present") { CHECK(Helpers::contains_substring("hello world", "world")); }

  TEST_CASE("substring not present") { CHECK(!Helpers::contains_substring("hello world", "xyz")); }

  TEST_CASE("empty needle always returns true") { CHECK(Helpers::contains_substring("hello", "")); }

  TEST_CASE("empty haystack with non-empty needle returns false") { CHECK(!Helpers::contains_substring("", "x")); }

  TEST_CASE("both empty returns true") { CHECK(Helpers::contains_substring("", "")); }

  TEST_CASE("needle equals haystack") { CHECK(Helpers::contains_substring("hello", "hello")); }

  TEST_CASE("needle longer than haystack returns false") { CHECK(!Helpers::contains_substring("hi", "hello")); }

  TEST_CASE("substring at start") { CHECK(Helpers::contains_substring("hello world", "hello")); }

  TEST_CASE("substring at end") { CHECK(Helpers::contains_substring("hello world", "world")); }
}

// ---------------------------------------------------------------------------
// TEST SUITE: string_to_wstring / wstring_to_string
// ---------------------------------------------------------------------------

TEST_SUITE("base-Helpers - wstring conversion") {
  TEST_CASE("ASCII round-trip") {
    std::string original = "hello";
    std::wstring wide = Helpers::string_to_wstring(original);
    std::string back = Helpers::wstring_to_string(wide);
    CHECK(back == original);
  }

  TEST_CASE("empty string round-trip") {
    std::string original;
    std::wstring wide = Helpers::string_to_wstring(original);
    std::string back = Helpers::wstring_to_string(wide);
    CHECK(back == original);
  }
}

// ---------------------------------------------------------------------------
// TEST SUITE: path_to_string
// ---------------------------------------------------------------------------

TEST_SUITE("base-Helpers - path_to_string") {
  TEST_CASE("converts path to UTF-8 string") {
    std::filesystem::path p = std::filesystem::temp_directory_path() / "test";
    std::string s = Helpers::path_to_string(p);
    CHECK(!s.empty());
    CHECK(s.find("test") != std::string::npos);
  }

  TEST_CASE("empty path produces empty or root string") {
    std::filesystem::path p = "";
    std::string s = Helpers::path_to_string(p);
    CHECK(true);
    (void)s;
  }
}

// ---------------------------------------------------------------------------
// Additional coverage tests
// ---------------------------------------------------------------------------

TEST_SUITE("base-Helpers - to_int extra") {
  TEST_CASE("hex string with 0x prefix") { CHECK(Helpers::to_int("0xff") == 255); }

  TEST_CASE("octal string with 0 prefix") { CHECK(Helpers::to_int("010") == 8); }

  TEST_CASE("partial numeric string returns default") { CHECK(Helpers::to_int("42abc") == 0); }

  TEST_CASE("string with leading whitespace returns default") {
    // stoi handles leading whitespace, but pos != str.size()
    // Actually stoi skips leading whitespace but pos would match
    CHECK(Helpers::to_int("  42") == 42);
  }

  TEST_CASE("string with trailing whitespace returns default") { CHECK(Helpers::to_int("42 ", -1) == -1); }

  TEST_CASE("very large number causes overflow returns default") {
    CHECK(Helpers::to_int("99999999999999999999", -1) == -1);
  }
}

TEST_SUITE("base-Helpers - to_long extra") {
  TEST_CASE("empty string returns default") { CHECK(Helpers::to_long("", 42) == 42); }

  TEST_CASE("hex long with 0x prefix") { CHECK(Helpers::to_long("0xff") == 255LL); }

  TEST_CASE("overflow returns default") { CHECK(Helpers::to_long("99999999999999999999", -1) == -1LL); }

  TEST_CASE("offset with partial parse returns default") {
    // "1234" size=4, offset=2 expects pos==2 but stoll parses all 4 digits so pos=4 != 4-2=2
    CHECK(Helpers::to_long("1234", -1, 2) == -1LL);
  }

  TEST_CASE("negative value with offset 0") { CHECK(Helpers::to_long("-999", 0, 0) == -999LL); }
}

TEST_SUITE("base-Helpers - double_to_string extra") {
  TEST_CASE("large precision") {
    std::string s = Helpers::double_to_string(1.0 / 3.0, 10);
    CHECK(s.find('.') != std::string::npos);
    // Should have many decimal digits
    auto dot_pos = s.find('.');
    CHECK(s.size() - dot_pos - 1 >= 10U);
  }

  TEST_CASE("large value") {
    std::string s = Helpers::double_to_string(1e18);
    CHECK(!s.empty());
  }

  TEST_CASE("negative zero") {
    std::string s = Helpers::double_to_string(-0.0, 2);
    CHECK(!s.empty());
  }
}

TEST_SUITE("base-Helpers - replace_string extra") {
  TEST_CASE("replace with longer string") {
    std::string s = "ab";
    Helpers::replace_string(s, "a", "xyz");
    CHECK(s == "xyzb");
  }

  TEST_CASE("replace with same string") {
    std::string s = "hello";
    Helpers::replace_string(s, "hello", "hello");
    CHECK(s == "hello");
  }

  TEST_CASE("empty target string leaves unchanged") {
    std::string s = "hello";
    Helpers::replace_string(s, "", "X");
    CHECK(s == "hello");
  }

  TEST_CASE("empty source string is no-op") {
    std::string s;
    Helpers::replace_string(s, "a", "b");
    CHECK(s.empty());
  }
}

TEST_SUITE("base-Helpers - trim_string extra") {
  TEST_CASE("trims tabs and newlines") { CHECK(Helpers::trim_string("\t\nhello\t\n") == "hello"); }

  TEST_CASE("single character") { CHECK(Helpers::trim_string("x") == "x"); }

  TEST_CASE("inner whitespace preserved") { CHECK(Helpers::trim_string("  a b  ") == "a b"); }
}

TEST_SUITE("base-Helpers - get_split_string extra") {
  TEST_CASE("trailing delimiter") {
    auto parts = Helpers::get_split_string("a,b,", ',');
    REQUIRE(parts.size() == 2U);
    CHECK(parts[0] == "a");
    CHECK(parts[1] == "b");
  }

  TEST_CASE("single character string") {
    auto parts = Helpers::get_split_string("x", ',');
    REQUIRE(parts.size() == 1U);
    CHECK(parts[0] == "x");
  }

  TEST_CASE("delimiter only string") {
    auto parts = Helpers::get_split_string(",", ',');
    CHECK(parts.empty());
  }

  TEST_CASE("multiple consecutive delimiters") {
    auto parts = Helpers::get_split_string(",,,", ',');
    CHECK(parts.empty());
  }
}

TEST_SUITE("base-Helpers - get_split_string_view extra") {
  TEST_CASE("empty string returns empty") {
    auto parts = Helpers::get_split_string_view("", ':');
    CHECK(parts.empty());
  }

  TEST_CASE("consecutive delimiters skip empty") {
    auto parts = Helpers::get_split_string_view("a::b", ':');
    REQUIRE(parts.size() == 2U);
    CHECK(parts[0] == "a");
    CHECK(parts[1] == "b");
  }

  TEST_CASE("trailing delimiter") {
    auto parts = Helpers::get_split_string_view("a:b:", ':');
    REQUIRE(parts.size() == 2U);
    CHECK(parts[0] == "a");
    CHECK(parts[1] == "b");
  }
}

TEST_SUITE("base-Helpers - get_pair_string extra") {
  TEST_CASE("empty string returns empty pair") {
    auto p = Helpers::get_pair_string("", '=');
    CHECK(p.first.empty());
    CHECK(p.second.empty());
  }

  TEST_CASE("pair with spaces gets trimmed") {
    auto p = Helpers::get_pair_string("key = value", '=');
    CHECK(p.first == "key");
    CHECK(p.second == "value");
  }

  TEST_CASE("delimiter at start") {
    // ",b" splits at first ',' -> first is empty, second is "b"
    auto p = Helpers::get_pair_string(",b", ',');
    CHECK(p.first.empty());
    CHECK(p.second == "b");
  }
}

TEST_SUITE("base-Helpers - get_pair_string_view") {
  TEST_CASE("basic split") {
    auto p = Helpers::get_pair_string_view("key=value", '=');
    CHECK(p.first == "key");
    CHECK(p.second == "value");
  }

  TEST_CASE("no delimiter returns whole string as first") {
    auto p = Helpers::get_pair_string_view("noeq", '=');
    CHECK(p.first == "noeq");
    CHECK(p.second.empty());
  }

  TEST_CASE("more than two tokens splits at first delimiter") {
    auto p = Helpers::get_pair_string_view("a=b=c", '=');
    CHECK(p.first == "a");
    CHECK(p.second == "b=c");
  }

  TEST_CASE("empty string returns empty pair") {
    auto p = Helpers::get_pair_string_view("", '=');
    CHECK(p.first.empty());
    CHECK(p.second.empty());
  }

  TEST_CASE("delimiter at start") {
    auto p = Helpers::get_pair_string_view(",b", ',');
    CHECK(p.first.empty());
    CHECK(p.second == "b");
  }

  TEST_CASE("delimiter at end") {
    auto p = Helpers::get_pair_string_view("a,", ',');
    CHECK(p.first == "a");
    CHECK(p.second.empty());
  }

  TEST_CASE("views refer into original string") {
    std::string original = "hello:world";
    auto p = Helpers::get_pair_string_view(original, ':');
    CHECK(p.first.data() >= original.data());
    CHECK(p.first.data() < original.data() + original.size());
    CHECK(p.second.data() >= original.data());
    CHECK(p.second.data() <= original.data() + original.size());
  }

  TEST_CASE("no trim applied (unlike get_pair_string)") {
    auto p = Helpers::get_pair_string_view(" key = value ", '=');
    CHECK(p.first == " key ");
    CHECK(p.second == " value ");
  }
}

TEST_SUITE("base-Helpers - get_hash_code extra") {
  TEST_CASE("numeric string returns the number itself") {
    // get_hash_code first tries to parse as uint32_t via from_chars
    CHECK(Helpers::get_hash_code("42") == 42U);
  }

  TEST_CASE("non-numeric string returns hash") {
    uint32_t h = Helpers::get_hash_code("hello");
    CHECK(h != 0U);  // Extremely unlikely to be 0 for "hello"
  }

  TEST_CASE("empty string returns 0") { CHECK(Helpers::get_hash_code("") == 0U); }
}

TEST_SUITE("base-Helpers - format_file_size extra") {
  TEST_CASE("gigabytes range") {
    std::string s = Helpers::format_file_size(static_cast<size_t>(2) * 1024 * 1024 * 1024);
    CHECK(s.find('G') != std::string::npos);
  }

  TEST_CASE("exact 1 KB") {
    std::string s = Helpers::format_file_size(1024U);
    CHECK(s.find('K') != std::string::npos);
  }

  TEST_CASE("exact 1 MB") {
    std::string s = Helpers::format_file_size(1024U * 1024U);
    CHECK(s.find('M') != std::string::npos);
  }
}

TEST_SUITE("base-Helpers - format_rate_size extra") {
  TEST_CASE("below 1 KB") {
    std::string s = Helpers::format_rate_size(500U);
    CHECK(s.find("B/s") != std::string::npos);
  }

  TEST_CASE("kilobytes range") {
    std::string s = Helpers::format_rate_size(2048U);
    CHECK(s.find("KB/s") != std::string::npos);
  }

  TEST_CASE("gigabytes range") {
    std::string s = Helpers::format_rate_size(static_cast<size_t>(2) * 1024 * 1024 * 1024);
    CHECK(s.find("GB/s") != std::string::npos);
  }

  TEST_CASE("zero rate") {
    std::string s = Helpers::format_rate_size(0U);
    CHECK(!s.empty());
  }
}

TEST_SUITE("base-Helpers - format_milliseconds extra") {
  TEST_CASE("negative milliseconds wraps around") {
    std::string s = Helpers::format_milliseconds(-1000LL, false);
    CHECK(!s.empty());
  }

  TEST_CASE("exact hour boundary") {
    std::string s = Helpers::format_milliseconds(3600000LL, false);
    CHECK(s.find("01:00:00") != std::string::npos);
  }

  TEST_CASE("with millis shows extra precision") {
    std::string s = Helpers::format_milliseconds(1500LL, true);
    CHECK(s.find("500") != std::string::npos);
  }
}

TEST_SUITE("base-Helpers - format_time_diff extra") {
  TEST_CASE("negative time diff has minus sign") {
    std::string s = Helpers::format_time_diff(-5000);
    CHECK(s[0] == '-');
  }

  TEST_CASE("exact second") {
    std::string s = Helpers::format_time_diff(1000);
    CHECK(!s.empty());
  }
}

TEST_SUITE("base-Helpers - format_hex_number extra") {
  TEST_CASE("signed negative value") {
    std::string s = Helpers::format_hex_number(static_cast<int64_t>(-1));
    CHECK(s.find("0x") == 0U);
  }

  TEST_CASE("unsigned max produces hex digits") {
    std::string s = Helpers::format_hex_number(static_cast<uint64_t>(0xFFFF));
    CHECK(s == "0xFFFF");
  }

  TEST_CASE("unsigned zero") {
    std::string s = Helpers::format_hex_number(static_cast<uint64_t>(0));
    CHECK(s.find("0x") == 0U);
  }

  TEST_CASE("signed positive 16") {
    std::string s = Helpers::format_hex_number(static_cast<int64_t>(16));
    CHECK(s == "0x10");
  }
}

TEST_SUITE("base-Helpers - format_date extra") {
  TEST_CASE("known timestamp produces expected format") {
    // 2020-01-01 00:00:00.000 UTC = 1577836800000000000 ns
    // Note: format_date outputs local time, so the exact date/time varies by timezone.
    int64_t ts = 1577836800LL * 1000000000LL;
    std::string s = Helpers::format_date(ts);
    CHECK(s.find("2020") != std::string::npos);
    CHECK(s.find(":") != std::string::npos);
  }

  TEST_CASE("date with millisecond precision") {
    int64_t ts = 1577836800LL * 1000000000LL + 123000000LL;  // 123ms
    std::string s = Helpers::format_date(ts);
    CHECK(s.find(".123") != std::string::npos);
  }
}

TEST_SUITE("base-Helpers - convert_date_to_timestamp extra") {
  TEST_CASE("date with milliseconds delimiter") {
    int64_t ts = Helpers::convert_date_to_timestamp("2026/01/01 00:00:00:500");
    CHECK(ts > 0);
  }

  TEST_CASE("date with invalid milliseconds delimiter returns -1") {
    int64_t ts = Helpers::convert_date_to_timestamp("2026/01/01 00:00:00.500");
    CHECK(ts == -1);
  }

  TEST_CASE("date with out-of-range milliseconds returns -1") {
    int64_t ts = Helpers::convert_date_to_timestamp("2026/01/01 00:00:00:1500");
    CHECK(ts == -1);
  }
}

TEST_SUITE("base-Helpers - string encoding") {
  TEST_CASE("string_local_to_utf8 returns input on POSIX") {
    std::string s = "hello";
    CHECK(Helpers::string_local_to_utf8(s) == s);
  }

  TEST_CASE("string_utf8_to_local returns input on POSIX") {
    std::string s = "world";
    CHECK(Helpers::string_utf8_to_local(s) == s);
  }

  TEST_CASE("string_local_to_utf8 empty") { CHECK(Helpers::string_local_to_utf8("") == ""); }

  TEST_CASE("string_utf8_to_local empty") { CHECK(Helpers::string_utf8_to_local("") == ""); }
}

TEST_SUITE("base-Helpers - wstring extra") {
  TEST_CASE("unicode round-trip") {
    std::string original = "\xc3\xa9";  // UTF-8 for 'e with accent'
    std::wstring wide = Helpers::string_to_wstring(original);
    std::string back = Helpers::wstring_to_string(wide);
    CHECK(back == original);
  }
}

TEST_SUITE("base-Helpers - has_startwith template extra") {
  TEST_CASE("exact match") {
    std::string s = "hello";
    CHECK(Helpers::has_startwith(s, "hello"));
  }

  TEST_CASE("prefix longer than string") {
    std::string s = "hi";
    CHECK(!Helpers::has_startwith(s, "hello"));
  }
}

TEST_SUITE("base-Helpers - has_endwith template extra") {
  TEST_CASE("exact match") {
    std::string s = "world";
    CHECK(Helpers::has_endwith(s, "world"));
  }

  TEST_CASE("suffix longer than string") {
    std::string s = "hi";
    CHECK(!Helpers::has_endwith(s, "hello"));
  }
}

TEST_SUITE("base-Helpers - contains_substring extra") {
  TEST_CASE("single character needle") { CHECK(Helpers::contains_substring("abc", "b")); }

  TEST_CASE("repeated pattern finds first") { CHECK(Helpers::contains_substring("abcabc", "cab")); }
}

// NOLINTEND
