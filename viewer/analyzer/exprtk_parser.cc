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

#include "./exprtk_parser.h"

#include <exprtk/exprtk.hpp>
#include <string>

namespace vlink::Exprtk {

using symbol_table_t = exprtk::symbol_table<double>;
using expression_t = exprtk::expression<double>;
using parser_t = exprtk::parser<double>;

std::optional<double> parse(const std::string& expression, VariableList& variable_list) {
  if (expression.empty()) {
    return std::nullopt;
  }

  symbol_table_t symbol_table;
  symbol_table.add_constants();

  for (auto& [name, value] : variable_list) {
    symbol_table.add_variable(name, value);
  }

  expression_t pxpr;
  pxpr.register_symbol_table(symbol_table);

  parser_t parser;

  if (!parser.compile(expression, pxpr)) {
    return std::nullopt;
  }

  return pxpr.value();
}

}  // namespace vlink::Exprtk
