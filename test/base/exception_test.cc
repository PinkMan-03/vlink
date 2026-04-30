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

#include "./base/exception.h"

#include <doctest/doctest.h>

#include <stdexcept>
#include <string>

//
#include "../common_test.h"

// ---------------------------------------------------------------------------
TEST_SUITE("base-Exception") {
  // -------------------------------------------------------------------------
  TEST_CASE("RuntimeError can be thrown and caught by its own type") {
    bool caught = false;

    try {
      throw Exception::RuntimeError("runtime failure");
    } catch (const Exception::RuntimeError& e) {
      caught = true;
      CHECK(std::string(e.what()) == "runtime failure");
    }

    CHECK(caught);
  }

  // -------------------------------------------------------------------------
  TEST_CASE("RuntimeError is catchable as std::runtime_error") {
    bool caught = false;

    try {
      throw Exception::RuntimeError("base catch");
    } catch (const std::runtime_error& e) {
      caught = true;
      CHECK(std::string(e.what()) == "base catch");
    }

    CHECK(caught);
  }

  // -------------------------------------------------------------------------
  TEST_CASE("RuntimeError is catchable as std::exception") {
    bool caught = false;

    try {
      throw Exception::RuntimeError("exception base");
    } catch (const std::exception& e) {
      caught = true;
      CHECK(std::string(e.what()) == "exception base");
    }

    CHECK(caught);
  }

  // -------------------------------------------------------------------------
  TEST_CASE("OutOfRange can be thrown and caught by its own type") {
    bool caught = false;

    try {
      throw Exception::OutOfRange("index out of range");
    } catch (const Exception::OutOfRange& e) {
      caught = true;
      CHECK(std::string(e.what()) == "index out of range");
    }

    CHECK(caught);
  }

  // -------------------------------------------------------------------------
  TEST_CASE("OutOfRange is catchable as std::out_of_range") {
    bool caught = false;

    try {
      throw Exception::OutOfRange("out of range base");
    } catch (const std::out_of_range& e) {
      caught = true;
      CHECK(!std::string(e.what()).empty());
    }

    CHECK(caught);
  }

  // -------------------------------------------------------------------------
  TEST_CASE("InvalidArgument can be thrown and caught by its own type") {
    bool caught = false;

    try {
      throw Exception::InvalidArgument("bad argument");
    } catch (const Exception::InvalidArgument& e) {
      caught = true;
      CHECK(std::string(e.what()) == "bad argument");
    }

    CHECK(caught);
  }

  // -------------------------------------------------------------------------
  TEST_CASE("InvalidArgument is catchable as std::invalid_argument") {
    bool caught = false;

    try {
      throw Exception::InvalidArgument("invalid arg base");
    } catch (const std::invalid_argument& e) {
      caught = true;
      CHECK(!std::string(e.what()).empty());
    }

    CHECK(caught);
  }

  // -------------------------------------------------------------------------
  TEST_CASE("LogicError can be thrown and caught by its own type") {
    bool caught = false;

    try {
      throw Exception::LogicError("logic violated");
    } catch (const Exception::LogicError& e) {
      caught = true;
      CHECK(std::string(e.what()) == "logic violated");
    }

    CHECK(caught);
  }

  // -------------------------------------------------------------------------
  TEST_CASE("LogicError is catchable as std::logic_error") {
    bool caught = false;

    try {
      throw Exception::LogicError("logic base");
    } catch (const std::logic_error& e) {
      caught = true;
      CHECK(!std::string(e.what()).empty());
    }

    CHECK(caught);
  }

  // -------------------------------------------------------------------------
  TEST_CASE("DomainError can be thrown and caught by its own type") {
    bool caught = false;

    try {
      throw Exception::DomainError("domain error");
    } catch (const Exception::DomainError& e) {
      caught = true;
      CHECK(std::string(e.what()) == "domain error");
    }

    CHECK(caught);
  }

  // -------------------------------------------------------------------------
  TEST_CASE("DomainError is catchable as std::domain_error") {
    bool caught = false;

    try {
      throw Exception::DomainError("domain base");
    } catch (const std::domain_error& e) {
      caught = true;
      CHECK(!std::string(e.what()).empty());
    }

    CHECK(caught);
  }

  // -------------------------------------------------------------------------
  TEST_CASE("LengthError can be thrown and caught by its own type") {
    bool caught = false;

    try {
      throw Exception::LengthError("length exceeded");
    } catch (const Exception::LengthError& e) {
      caught = true;
      CHECK(std::string(e.what()) == "length exceeded");
    }

    CHECK(caught);
  }

  // -------------------------------------------------------------------------
  TEST_CASE("LengthError is catchable as std::length_error") {
    bool caught = false;

    try {
      throw Exception::LengthError("length base");
    } catch (const std::length_error& e) {
      caught = true;
      CHECK(!std::string(e.what()).empty());
    }

    CHECK(caught);
  }

  // -------------------------------------------------------------------------
  TEST_CASE("RangeError can be thrown and caught by its own type") {
    bool caught = false;

    try {
      throw Exception::RangeError("range error");
    } catch (const Exception::RangeError& e) {
      caught = true;
      CHECK(std::string(e.what()) == "range error");
    }

    CHECK(caught);
  }

  // -------------------------------------------------------------------------
  TEST_CASE("RangeError is catchable as std::range_error") {
    bool caught = false;

    try {
      throw Exception::RangeError("range base");
    } catch (const std::range_error& e) {
      caught = true;
      CHECK(!std::string(e.what()).empty());
    }

    CHECK(caught);
  }

  // -------------------------------------------------------------------------
  TEST_CASE("OverflowError can be thrown and caught by its own type") {
    bool caught = false;

    try {
      throw Exception::OverflowError("overflow occurred");
    } catch (const Exception::OverflowError& e) {
      caught = true;
      CHECK(std::string(e.what()) == "overflow occurred");
    }

    CHECK(caught);
  }

  // -------------------------------------------------------------------------
  TEST_CASE("OverflowError is catchable as std::overflow_error") {
    bool caught = false;

    try {
      throw Exception::OverflowError("overflow base");
    } catch (const std::overflow_error& e) {
      caught = true;
      CHECK(!std::string(e.what()).empty());
    }

    CHECK(caught);
  }

  // -------------------------------------------------------------------------
  TEST_CASE("UnderflowError can be thrown and caught by its own type") {
    bool caught = false;

    try {
      throw Exception::UnderflowError("underflow occurred");
    } catch (const Exception::UnderflowError& e) {
      caught = true;
      CHECK(std::string(e.what()) == "underflow occurred");
    }

    CHECK(caught);
  }

  // -------------------------------------------------------------------------
  TEST_CASE("UnderflowError is catchable as std::underflow_error") {
    bool caught = false;

    try {
      throw Exception::UnderflowError("underflow base");
    } catch (const std::underflow_error& e) {
      caught = true;
      CHECK(!std::string(e.what()).empty());
    }

    CHECK(caught);
  }

  // -------------------------------------------------------------------------
  TEST_CASE("all vlink exceptions are catchable as std::exception") {
    auto throw_and_catch = [](auto ex) {
      bool caught = false;
      try {
        throw ex;
      } catch (const std::exception& e) {
        caught = true;
        CHECK(!std::string(e.what()).empty());
      }
      CHECK(caught);
    };

    throw_and_catch(Exception::RuntimeError("re"));
    throw_and_catch(Exception::OutOfRange("oor"));
    throw_and_catch(Exception::InvalidArgument("ia"));
    throw_and_catch(Exception::LogicError("le"));
    throw_and_catch(Exception::DomainError("de"));
    throw_and_catch(Exception::LengthError("lne"));
    throw_and_catch(Exception::RangeError("re2"));
    throw_and_catch(Exception::OverflowError("of"));
    throw_and_catch(Exception::UnderflowError("uf"));
  }

  // -------------------------------------------------------------------------
  TEST_CASE("what() returns the message passed to constructor") {
    const std::string msg = "detailed error message";

    Exception::RuntimeError err(msg);
    CHECK(std::string(err.what()) == msg);
  }

  // -------------------------------------------------------------------------
  TEST_CASE("exceptions can be constructed from std::string") {
    std::string s = "from std::string";

    Exception::InvalidArgument err(s);
    CHECK(std::string(err.what()) == s);
  }

  // -------------------------------------------------------------------------
  TEST_CASE("exceptions not mistakenly caught by sibling type") {
    bool caught_runtime = false;
    bool caught_logic = false;

    try {
      throw Exception::RuntimeError("runtime");
    } catch (const Exception::LogicError&) {
      caught_logic = true;
    } catch (const Exception::RuntimeError&) {
      caught_runtime = true;
    }

    CHECK(caught_runtime);
    CHECK(!caught_logic);
  }

  // -------------------------------------------------------------------------
  TEST_CASE("exception hierarchy: LogicError is not caught as std::runtime_error") {
    bool caught_runtime = false;
    bool caught_logic = false;

    try {
      throw Exception::LogicError("logic");
    } catch (const std::runtime_error&) {
      caught_runtime = true;
    } catch (const std::logic_error&) {
      caught_logic = true;
    }

    CHECK(!caught_runtime);
    CHECK(caught_logic);
  }
}

// NOLINTEND
