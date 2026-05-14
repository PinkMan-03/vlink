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

#include "./base/exception.h"

namespace vlink {
namespace Exception {

RuntimeError::RuntimeError(const std::string& what_arg) : std::runtime_error(what_arg) {}
RuntimeError::RuntimeError(const char* what_arg) : std::runtime_error(what_arg) {}
RuntimeError::~RuntimeError() = default;
const char* RuntimeError::what() const noexcept { return std::runtime_error::what(); }

OutOfRange::OutOfRange(const std::string& what_arg) : std::out_of_range(what_arg) {}
OutOfRange::OutOfRange(const char* what_arg) : std::out_of_range(what_arg) {}
OutOfRange::~OutOfRange() = default;
const char* OutOfRange::what() const noexcept { return std::out_of_range::what(); }

InvalidArgument::InvalidArgument(const std::string& what_arg) : std::invalid_argument(what_arg) {}
InvalidArgument::InvalidArgument(const char* what_arg) : std::invalid_argument(what_arg) {}
InvalidArgument::~InvalidArgument() = default;
const char* InvalidArgument::what() const noexcept { return std::invalid_argument::what(); }

LogicError::LogicError(const std::string& what_arg) : std::logic_error(what_arg) {}
LogicError::LogicError(const char* what_arg) : std::logic_error(what_arg) {}
LogicError::~LogicError() = default;
const char* LogicError::what() const noexcept { return std::logic_error::what(); }

DomainError::DomainError(const std::string& what_arg) : std::domain_error(what_arg) {}
DomainError::DomainError(const char* what_arg) : std::domain_error(what_arg) {}
DomainError::~DomainError() = default;
const char* DomainError::what() const noexcept { return std::domain_error::what(); }

LengthError::LengthError(const std::string& what_arg) : std::length_error(what_arg) {}
LengthError::LengthError(const char* what_arg) : std::length_error(what_arg) {}
LengthError::~LengthError() = default;
const char* LengthError::what() const noexcept { return std::length_error::what(); }

RangeError::RangeError(const std::string& what_arg) : std::range_error(what_arg) {}
RangeError::RangeError(const char* what_arg) : std::range_error(what_arg) {}
RangeError::~RangeError() = default;
const char* RangeError::what() const noexcept { return std::range_error::what(); }

OverflowError::OverflowError(const std::string& what_arg) : std::overflow_error(what_arg) {}
OverflowError::OverflowError(const char* what_arg) : std::overflow_error(what_arg) {}
OverflowError::~OverflowError() = default;
const char* OverflowError::what() const noexcept { return std::overflow_error::what(); }

UnderflowError::UnderflowError(const std::string& what_arg) : std::underflow_error(what_arg) {}
UnderflowError::UnderflowError(const char* what_arg) : std::underflow_error(what_arg) {}
UnderflowError::~UnderflowError() = default;
const char* UnderflowError::what() const noexcept { return std::underflow_error::what(); }

OperationCancelled::OperationCancelled() noexcept = default;
OperationCancelled::~OperationCancelled() = default;
const char* OperationCancelled::what() const noexcept { return "vlink operation cancelled"; }

}  // namespace Exception
}  // namespace vlink
