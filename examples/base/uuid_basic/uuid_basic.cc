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
 * @file uuid_basic.cc
 * @brief Demonstrates @c vlink::Uuid operations: construction, serialisation, parsing,
 *        v4 random generation, byte-level access, comparison, hashing, and the
 *        @c random_bytes / @c random_hex utility helpers.
 *
 * @c vlink::Uuid is a value-typed RFC 4122 128-bit identifier.  The class is
 * trivially copyable, the default constructor produces a nil UUID, and several
 * accessors (@c is_nil / @c variant / @c version / @c bytes) are @c constexpr so
 * UUIDs can be used as compile-time constants.
 *
 * This example walks through the major operations the production code base
 * relies on -- including the @c Uuid::random_hex pipeline that backs the proxy
 * auth-token handshake.
 */

#include <vlink/base/logger.h>

#include "uuid_examples.h"

int main() {
  VLOG_I("=== Uuid Basic Example ===");

  uuid_examples::demo_default_nil();
  uuid_examples::demo_construct_from_array();
  uuid_examples::demo_construct_from_raw_array();
  uuid_examples::demo_iterator_range();
  uuid_examples::demo_generate_random();
  uuid_examples::demo_generate_random_with_engine();
  uuid_examples::demo_from_string_round_trip();
  uuid_examples::demo_is_valid();
  uuid_examples::demo_variant_version_detect();
  uuid_examples::demo_byte_access();
  uuid_examples::demo_comparisons_and_hash();
  uuid_examples::demo_random_bytes_and_hex();
  uuid_examples::demo_swap();
  uuid_examples::demo_constexpr_use();

  VLOG_I("=== Uuid Basic Example Complete ===");
  return 0;
}
