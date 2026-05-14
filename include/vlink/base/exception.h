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
 * All VLink exception classes are thin @c final wrappers around the
 * corresponding standard exception base.  They are grouped inside the
 * @c vlink::Exception namespace to avoid naming conflicts with application
 * code.
 *
 * To guarantee a single typeinfo / vtable per type across shared library
 * boundaries (notably on macOS where libc++abi compares typeinfo by pointer
 * equality), every class is exported via @c VLINK_EXPORT and the constructors
 * plus the virtual @c what() function are defined out-of-line in
 * @c exception.cc -- which acts as the key-function translation unit.
 *
 * The mapping between VLink exceptions and standard bases is:
 *
 * | VLink exception                | Standard base              | Typical usage                        |
 * | ------------------------------ | -------------------------- | ------------------------------------ |
 * | Exception::RuntimeError        | std::runtime_error         | General runtime failures (fatal log) |
 * | Exception::OutOfRange          | std::out_of_range          | Index or iterator out of valid range |
 * | Exception::InvalidArgument     | std::invalid_argument      | Bad function argument                |
 * | Exception::LogicError          | std::logic_error           | Violated precondition                |
 * | Exception::DomainError         | std::domain_error          | Value outside the function domain    |
 * | Exception::LengthError         | std::length_error          | Size exceeds implementation limit    |
 * | Exception::RangeError          | std::range_error           | Arithmetic range error               |
 * | Exception::OverflowError       | std::overflow_error        | Arithmetic overflow                  |
 * | Exception::UnderflowError      | std::underflow_error       | Arithmetic underflow                 |
 * | Exception::OperationCancelled  | std::exception             | Cooperative cancellation observed    |
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

#include <exception>
#include <stdexcept>
#include <string>

#include "./macros.h"

namespace vlink {

/**
 * @namespace vlink::Exception
 * @brief Container namespace for all VLink exception types.
 */
namespace Exception {  // NOLINT(readability-identifier-naming)

/**
 * @class RuntimeError
 * @brief Indicates a general runtime failure.
 */
class VLINK_EXPORT RuntimeError final : public std::runtime_error {
 public:
  /**
   * @brief Constructs the exception with a descriptive message.
   * @param what_arg Explanatory string returned by what().
   */
  explicit RuntimeError(const std::string& what_arg);

  /**
   * @brief Constructs the exception with a C-string descriptive message.
   * @param what_arg Null-terminated explanatory string returned by what().
   */
  explicit RuntimeError(const char* what_arg);

  /**
   * @brief Destroys the exception object.
   */
  ~RuntimeError() override;

  /**
   * @brief Returns the explanatory message associated with this exception.
   * @return Null-terminated explanatory string.
   */
  [[nodiscard]] const char* what() const noexcept override;
};

/**
 * @class OutOfRange
 * @brief Indicates an index or iterator that is outside the valid range.
 */
class VLINK_EXPORT OutOfRange final : public std::out_of_range {
 public:
  /**
   * @brief Constructs the exception with a descriptive message.
   * @param what_arg Explanatory string returned by what().
   */
  explicit OutOfRange(const std::string& what_arg);

  /**
   * @brief Constructs the exception with a C-string descriptive message.
   * @param what_arg Null-terminated explanatory string returned by what().
   */
  explicit OutOfRange(const char* what_arg);

  /**
   * @brief Destroys the exception object.
   */
  ~OutOfRange() override;

  /**
   * @brief Returns the explanatory message associated with this exception.
   * @return Null-terminated explanatory string.
   */
  [[nodiscard]] const char* what() const noexcept override;
};

/**
 * @class InvalidArgument
 * @brief Indicates that a function received an argument with an invalid value.
 */
class VLINK_EXPORT InvalidArgument final : public std::invalid_argument {
 public:
  /**
   * @brief Constructs the exception with a descriptive message.
   * @param what_arg Explanatory string returned by what().
   */
  explicit InvalidArgument(const std::string& what_arg);

  /**
   * @brief Constructs the exception with a C-string descriptive message.
   * @param what_arg Null-terminated explanatory string returned by what().
   */
  explicit InvalidArgument(const char* what_arg);

  /**
   * @brief Destroys the exception object.
   */
  ~InvalidArgument() override;

  /**
   * @brief Returns the explanatory message associated with this exception.
   * @return Null-terminated explanatory string.
   */
  [[nodiscard]] const char* what() const noexcept override;
};

/**
 * @class LogicError
 * @brief Indicates a violated program logic precondition.
 */
class VLINK_EXPORT LogicError final : public std::logic_error {
 public:
  /**
   * @brief Constructs the exception with a descriptive message.
   * @param what_arg Explanatory string returned by what().
   */
  explicit LogicError(const std::string& what_arg);

  /**
   * @brief Constructs the exception with a C-string descriptive message.
   * @param what_arg Null-terminated explanatory string returned by what().
   */
  explicit LogicError(const char* what_arg);

  /**
   * @brief Destroys the exception object.
   */
  ~LogicError() override;

  /**
   * @brief Returns the explanatory message associated with this exception.
   * @return Null-terminated explanatory string.
   */
  [[nodiscard]] const char* what() const noexcept override;
};

/**
 * @class DomainError
 * @brief Indicates that a value is outside the domain of a mathematical function.
 */
class VLINK_EXPORT DomainError final : public std::domain_error {
 public:
  /**
   * @brief Constructs the exception with a descriptive message.
   * @param what_arg Explanatory string returned by what().
   */
  explicit DomainError(const std::string& what_arg);

  /**
   * @brief Constructs the exception with a C-string descriptive message.
   * @param what_arg Null-terminated explanatory string returned by what().
   */
  explicit DomainError(const char* what_arg);

  /**
   * @brief Destroys the exception object.
   */
  ~DomainError() override;

  /**
   * @brief Returns the explanatory message associated with this exception.
   * @return Null-terminated explanatory string.
   */
  [[nodiscard]] const char* what() const noexcept override;
};

/**
 * @class LengthError
 * @brief Indicates an attempt to exceed the maximum allowable size or length.
 */
class VLINK_EXPORT LengthError final : public std::length_error {
 public:
  /**
   * @brief Constructs the exception with a descriptive message.
   * @param what_arg Explanatory string returned by what().
   */
  explicit LengthError(const std::string& what_arg);

  /**
   * @brief Constructs the exception with a C-string descriptive message.
   * @param what_arg Null-terminated explanatory string returned by what().
   */
  explicit LengthError(const char* what_arg);

  /**
   * @brief Destroys the exception object.
   */
  ~LengthError() override;

  /**
   * @brief Returns the explanatory message associated with this exception.
   * @return Null-terminated explanatory string.
   */
  [[nodiscard]] const char* what() const noexcept override;
};

/**
 * @class RangeError
 * @brief Indicates an arithmetic range error.
 */
class VLINK_EXPORT RangeError final : public std::range_error {
 public:
  /**
   * @brief Constructs the exception with a descriptive message.
   * @param what_arg Explanatory string returned by what().
   */
  explicit RangeError(const std::string& what_arg);

  /**
   * @brief Constructs the exception with a C-string descriptive message.
   * @param what_arg Null-terminated explanatory string returned by what().
   */
  explicit RangeError(const char* what_arg);

  /**
   * @brief Destroys the exception object.
   */
  ~RangeError() override;

  /**
   * @brief Returns the explanatory message associated with this exception.
   * @return Null-terminated explanatory string.
   */
  [[nodiscard]] const char* what() const noexcept override;
};

/**
 * @class OverflowError
 * @brief Indicates an arithmetic overflow.
 */
class VLINK_EXPORT OverflowError final : public std::overflow_error {
 public:
  /**
   * @brief Constructs the exception with a descriptive message.
   * @param what_arg Explanatory string returned by what().
   */
  explicit OverflowError(const std::string& what_arg);

  /**
   * @brief Constructs the exception with a C-string descriptive message.
   * @param what_arg Null-terminated explanatory string returned by what().
   */
  explicit OverflowError(const char* what_arg);

  /**
   * @brief Destroys the exception object.
   */
  ~OverflowError() override;

  /**
   * @brief Returns the explanatory message associated with this exception.
   * @return Null-terminated explanatory string.
   */
  [[nodiscard]] const char* what() const noexcept override;
};

/**
 * @class UnderflowError
 * @brief Indicates an arithmetic underflow.
 */
class VLINK_EXPORT UnderflowError final : public std::underflow_error {
 public:
  /**
   * @brief Constructs the exception with a descriptive message.
   * @param what_arg Explanatory string returned by what().
   */
  explicit UnderflowError(const std::string& what_arg);

  /**
   * @brief Constructs the exception with a C-string descriptive message.
   * @param what_arg Null-terminated explanatory string returned by what().
   */
  explicit UnderflowError(const char* what_arg);

  /**
   * @brief Destroys the exception object.
   */
  ~UnderflowError() override;

  /**
   * @brief Returns the explanatory message associated with this exception.
   * @return Null-terminated explanatory string.
   */
  [[nodiscard]] const char* what() const noexcept override;
};

/**
 * @class OperationCancelled
 * @brief Indicates that an operation observed a cooperative cancellation request.
 */
class VLINK_EXPORT OperationCancelled final : public std::exception {
 public:
  /**
   * @brief Constructs a cancellation exception with a default message.
   */
  OperationCancelled();

  /**
   * @brief Destroys the exception object.
   */
  ~OperationCancelled() override;

  /**
   * @brief Returns the explanatory cancellation message.
   * @return Null-terminated explanatory string.
   */
  [[nodiscard]] const char* what() const noexcept override;
};

}  // namespace Exception

}  // namespace vlink
