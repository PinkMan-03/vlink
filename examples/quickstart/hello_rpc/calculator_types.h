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
 * @file calculator_types.h
 * @brief POD request/response types shared between calculator_server and calculator_client.
 *
 * These types use VLink's kStandardType serialization (memcpy-based).
 * IMPORTANT: POD structs must NOT have default member initializers
 * (e.g. use `int x;` not `int x{0};`) to guarantee trivially-copyable
 * and standard-layout properties across all compilers.
 */

#pragma once

namespace example {

/// Calculator request -- a binary arithmetic operation.
struct CalcRequest {
  int a;    ///< Left operand
  int b;    ///< Right operand
  char op;  ///< Operator: '+', '-', '*', '/'
};

/// Calculator response -- the result of the operation.
struct CalcResponse {
  int result;  ///< Computation result
};

/// RPC service URL shared by server and client.
/// The example programs allow overriding this with VLINK_CALCULATOR_URL.
static const char* const kCalculatorUrl = "intra://hello/calculator";

/// Fire-and-forget notification URL (no response expected).
/// The example programs allow overriding this with VLINK_NOTIFY_URL.
static const char* const kNotifyUrl = "intra://hello/notify";

}  // namespace example
