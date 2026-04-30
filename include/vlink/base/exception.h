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

/**
 * @file exception.h
 * @brief VLink-specific exception types wrapping the C++ standard exception hierarchy.
 *
 * @details
 * All VLink exception classes are thin @c final wrappers that inherit their
 * constructors from the corresponding standard exception base.  They are grouped
 * inside the @c vlink::Exception namespace to avoid naming conflicts with
 * application code.
 *
 * The mapping between VLink exceptions and standard bases is:
 *
 * | VLink exception          | Standard base              | Typical usage                        |
 * | ------------------------ | -------------------------- | ------------------------------------ |
 * | Exception::RuntimeError  | std::runtime_error         | General runtime failures (fatal log) |
 * | Exception::OutOfRange    | std::out_of_range          | Index or iterator out of valid range |
 * | Exception::InvalidArgument | std::invalid_argument    | Bad function argument                |
 * | Exception::LogicError    | std::logic_error           | Violated precondition                |
 * | Exception::DomainError   | std::domain_error          | Value outside the function domain    |
 * | Exception::LengthError   | std::length_error          | Size exceeds implementation limit    |
 * | Exception::RangeError    | std::range_error           | Arithmetic range error               |
 * | Exception::OverflowError | std::overflow_error        | Arithmetic overflow                  |
 * | Exception::UnderflowError| std::underflow_error       | Arithmetic underflow                 |
 *
 * @note
 * @c Exception::RuntimeError is the exception thrown by @c Logger when a
 * @c kFatal-level log message is emitted.  If a @c kFatal log occurs the
 * Logger flushes all pending output and then throws this exception, allowing
 * the application to catch it and perform a controlled shutdown.
 *
 * @par Example
 * @code
 * try {
 *   VLOG_F("Critical failure: ", reason);
 * } catch (const vlink::Exception::RuntimeError& e) {
 *   // perform cleanup before process exit
 *   std::cerr << e.what() << "\n";
 * }
 * @endcode
 */

#pragma once

#include <stdexcept>

namespace vlink {

/**
 * @namespace vlink::Exception
 * @brief Container namespace for all VLink exception types.
 */
namespace Exception {  // NOLINT(readability-identifier-naming)

/**
 * @class RuntimeError
 * @brief Indicates a general runtime failure.
 *
 * @details
 * Thrown by the Logger when a @c kFatal log level is used.  Inherits all
 * constructors from @c std::runtime_error so it can be constructed with
 * an @c std::string or C-string message.
 */
class RuntimeError final : public std::runtime_error {
  using std::runtime_error::runtime_error;
};

/**
 * @class OutOfRange
 * @brief Indicates an index or iterator that is outside the valid range.
 *
 * @details
 * Inherits all constructors from @c std::out_of_range.
 */
class OutOfRange final : public std::out_of_range {
  using std::out_of_range::out_of_range;
};

/**
 * @class InvalidArgument
 * @brief Indicates that a function received an argument with an invalid value.
 *
 * @details
 * Inherits all constructors from @c std::invalid_argument.
 */
class InvalidArgument final : public std::invalid_argument {
  using std::invalid_argument::invalid_argument;
};

/**
 * @class LogicError
 * @brief Indicates a violated program logic precondition.
 *
 * @details
 * Inherits all constructors from @c std::logic_error.
 */
class LogicError final : public std::logic_error {
  using std::logic_error::logic_error;
};

/**
 * @class DomainError
 * @brief Indicates that a value is outside the domain of a mathematical function.
 *
 * @details
 * Inherits all constructors from @c std::domain_error.
 */
class DomainError final : public std::domain_error {
  using std::domain_error::domain_error;
};

/**
 * @class LengthError
 * @brief Indicates an attempt to exceed the maximum allowable size or length.
 *
 * @details
 * Inherits all constructors from @c std::length_error.
 */
class LengthError final : public std::length_error {
  using std::length_error::length_error;
};

/**
 * @class RangeError
 * @brief Indicates an arithmetic range error.
 *
 * @details
 * Inherits all constructors from @c std::range_error.
 */
class RangeError final : public std::range_error {
  using std::range_error::range_error;
};

/**
 * @class OverflowError
 * @brief Indicates an arithmetic overflow.
 *
 * @details
 * Inherits all constructors from @c std::overflow_error.
 */
class OverflowError final : public std::overflow_error {
  using std::overflow_error::overflow_error;
};

/**
 * @class UnderflowError
 * @brief Indicates an arithmetic underflow.
 *
 * @details
 * Inherits all constructors from @c std::underflow_error.
 */
class UnderflowError final : public std::underflow_error {
  using std::underflow_error::underflow_error;
};

}  // namespace Exception

}  // namespace vlink
