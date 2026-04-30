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

#include "./base/name_detector.h"

#include <doctest/doctest.h>

#include <string_view>

//
#include "../common_test.h"

// ---------------------------------------------------------------------------
// Helper types used in tests
// ---------------------------------------------------------------------------
struct MyStruct {};

class MyClass {};

enum class Color { Red, Green, Blue };  // NOLINT(performance-enum-size, readability-identifier-naming)

enum Direction { North, South, East, West };  // NOLINT(performance-enum-size, readability-identifier-naming)

// ---------------------------------------------------------------------------
TEST_SUITE("base-NameDetector") {
  // -------------------------------------------------------------------------
  TEST_CASE("is_support returns true for fundamental types") {
    CHECK(NameDetector::is_support<int>());
    CHECK(NameDetector::is_support<double>());
    CHECK(NameDetector::is_support<char>());
    CHECK(NameDetector::is_support<bool>());
  }

  // -------------------------------------------------------------------------
  TEST_CASE("is_support returns true for user-defined struct") { CHECK(NameDetector::is_support<MyStruct>()); }

  // -------------------------------------------------------------------------
  TEST_CASE("is_support returns true for user-defined class") { CHECK(NameDetector::is_support<MyClass>()); }

  // -------------------------------------------------------------------------
  TEST_CASE("is_support returns true for enum class") { CHECK(NameDetector::is_support<Color>()); }

  // -------------------------------------------------------------------------
  TEST_CASE("get<T>() returns non-empty string for int") {
    std::string_view name = NameDetector::get<int>();
    CHECK(!name.empty());
  }

  // -------------------------------------------------------------------------
  TEST_CASE("get<T>() contains expected name for int") {
    std::string_view name = NameDetector::get<int>();
    CHECK(name.find("int") != std::string_view::npos);
  }

  // -------------------------------------------------------------------------
  TEST_CASE("get<T>() returns non-empty string for double") {
    std::string_view name = NameDetector::get<double>();
    CHECK(!name.empty());
    CHECK(name.find("double") != std::string_view::npos);
  }

  // -------------------------------------------------------------------------
  TEST_CASE("get<T>() for struct contains struct name") {
    std::string_view name = NameDetector::get<MyStruct>();
    CHECK(!name.empty());
    CHECK(name.find("MyStruct") != std::string_view::npos);
  }

  // -------------------------------------------------------------------------
  TEST_CASE("get<T>() for class contains class name") {
    std::string_view name = NameDetector::get<MyClass>();
    CHECK(!name.empty());
    CHECK(name.find("MyClass") != std::string_view::npos);
  }

  // -------------------------------------------------------------------------
  TEST_CASE("get<T>() for enum class contains enum name") {
    std::string_view name = NameDetector::get<Color>();
    CHECK(!name.empty());
    CHECK(name.find("Color") != std::string_view::npos);
  }

  // -------------------------------------------------------------------------
  TEST_CASE("get<T>() result is stable across multiple calls") {
    std::string_view a = NameDetector::get<int>();
    std::string_view b = NameDetector::get<int>();

    CHECK(a == b);
    // Both point to static storage so data pointers should match
    CHECK(a.data() == b.data());
  }

  // -------------------------------------------------------------------------
  TEST_CASE("get<T>() different types produce different names") {
    std::string_view name_int = NameDetector::get<int>();
    std::string_view name_double = NameDetector::get<double>();

    CHECK(name_int != name_double);
  }

  // -------------------------------------------------------------------------
  TEST_CASE("get_enum returns name for scoped enum value") {
    std::string_view name = NameDetector::get_enum(Color::Red);
    CHECK(!name.empty());
    CHECK(name.find("Red") != std::string_view::npos);
  }

  // -------------------------------------------------------------------------
  TEST_CASE("get_enum returns name for Green") {
    std::string_view name = NameDetector::get_enum(Color::Green);
    CHECK(!name.empty());
    CHECK(name.find("Green") != std::string_view::npos);
  }

  // -------------------------------------------------------------------------
  TEST_CASE("get_enum returns name for Blue") {
    std::string_view name = NameDetector::get_enum(Color::Blue);
    CHECK(!name.empty());
    CHECK(name.find("Blue") != std::string_view::npos);
  }

  // -------------------------------------------------------------------------
  TEST_CASE("get_enum returns name for unscoped enum value") {
    std::string_view name = NameDetector::get_enum(North);
    CHECK(!name.empty());
    CHECK(name.find("North") != std::string_view::npos);
  }

  // -------------------------------------------------------------------------
  TEST_CASE("get_enum returns different names for different values") {
    std::string_view red = NameDetector::get_enum(Color::Red);
    std::string_view blue = NameDetector::get_enum(Color::Blue);

    CHECK(red != blue);
  }

  // -------------------------------------------------------------------------
  TEST_CASE("get<T>() for void* contains pointer info") {
    std::string_view name = NameDetector::get<void*>();
    CHECK(!name.empty());
  }

  // -------------------------------------------------------------------------
  TEST_CASE("get<T>() for std::string is non-empty") {
    std::string_view name = NameDetector::get<std::string>();
    CHECK(!name.empty());
  }
}

// NOLINTEND
