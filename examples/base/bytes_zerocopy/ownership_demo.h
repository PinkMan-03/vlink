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

#pragma once

/**
 * @file ownership_demo.h
 * @brief Helper functions demonstrating Bytes ownership semantics.
 *
 * The Bytes class has four ownership modes:
 *   1. Owner (create/deep_copy) -- owns the memory, frees on destruction.
 *   2. Shallow alias (shallow_copy) -- non-owning pointer, no free.
 *   3. Loaned (loan_internal) -- non-owning, managed by external system (iceoryx).
 *   4. Pointer-only (shallow_copy_ptr) -- wraps an opaque pointer, size==0.
 */

#include <vlink/base/bytes.h>
#include <vlink/base/logger.h>

#include <cstring>

namespace ownership_demo {

// Print ownership metadata for a Bytes object.
inline void print_ownership(const char* label, const vlink::Bytes& b) {
  VLOG_I(label, ": size=", b.size(), " is_owner=", b.is_owner(), " is_loaned=", b.is_loaned(), " is_ptr=", b.is_ptr(),
         " empty=", b.empty());
}

// Demonstrate shallow_copy (mutable) -- non-owning alias.
inline void demo_shallow_copy_mutable() {
  VLOG_I("--- 1. shallow_copy (mutable) ---");
  uint8_t external_buf[32];
  std::memset(external_buf, 0x42, sizeof(external_buf));

  auto alias = vlink::Bytes::shallow_copy(external_buf, sizeof(external_buf));
  print_ownership("shallow_copy mutable", alias);
  VLOG_I("Alias data == external_buf: ", (alias.data() == external_buf));

  alias[0] = 0xFF;
  VLOG_I("external_buf[0] after alias[0]=0xFF: 0x", std::hex, static_cast<int>(external_buf[0]));
}

// Demonstrate shallow_copy (const) -- non-owning read-only alias.
inline void demo_shallow_copy_const() {
  VLOG_I("--- 2. shallow_copy (const) ---");
  const uint8_t read_only_buf[] = {0x10, 0x20, 0x30};
  auto alias = vlink::Bytes::shallow_copy(read_only_buf, sizeof(read_only_buf));
  print_ownership("shallow_copy const", alias);

  const auto* p = static_cast<const vlink::Bytes&>(alias).data();
  VLOG_I("const data()[0]: 0x", std::hex, static_cast<int>(p[0]));
}

// Demonstrate deep_copy (from external buffer) -- owning copy.
inline void demo_deep_copy_external() {
  VLOG_I("--- 3. deep_copy (external) ---");
  uint8_t external_buf[] = {0xAA, 0xBB, 0xCC, 0xDD};
  auto owned = vlink::Bytes::deep_copy(external_buf, sizeof(external_buf));
  print_ownership("deep_copy external", owned);
  VLOG_I("owned data != external_buf: ", (owned.data() != external_buf));
  VLOG_I("Content matches: ", (owned[0] == 0xAA && owned[3] == 0xDD));
}

// Demonstrate deep_copy with offset -- prefix reservation.
inline void demo_deep_copy_offset() {
  VLOG_I("--- 4. deep_copy with offset ---");
  uint8_t payload[] = {0x01, 0x02, 0x03};
  auto buf = vlink::Bytes::deep_copy(payload, sizeof(payload), 4);
  print_ownership("deep_copy offset=4", buf);
  VLOG_I("offset=", static_cast<int>(buf.offset()), " size=", buf.size(), " real_size=", buf.real_size());
}

// Demonstrate instance-method deep_copy and shallow_copy.
inline void demo_instance_copy() {
  VLOG_I("--- 5. Instance deep_copy / shallow_copy ---");
  auto original = vlink::Bytes::from_string("original data");
  vlink::Bytes copy_target;

  copy_target.deep_copy(original);
  print_ownership("after deep_copy", copy_target);
  VLOG_I("Content: \"", copy_target.to_string(), "\"");
  VLOG_I("Different memory: ", (copy_target.data() != original.data()));

  vlink::Bytes alias_target;
  alias_target.shallow_copy(original);
  print_ownership("after shallow_copy", alias_target);
  VLOG_I("Same memory: ", (alias_target.data() == original.data()));
}

// Demonstrate deep_copy_self -- convert non-owning to owning.
inline void demo_deep_copy_self() {
  VLOG_I("--- 6. deep_copy_self ---");
  uint8_t ext[] = {0x11, 0x22, 0x33};
  auto alias = vlink::Bytes::shallow_copy(ext, sizeof(ext));
  VLOG_I("Before deep_copy_self: is_owner=", alias.is_owner());

  alias.deep_copy_self();
  VLOG_I("After deep_copy_self: is_owner=", alias.is_owner());
  VLOG_I("Data still valid: 0x", std::hex, static_cast<int>(alias[0]));

  ext[0] = 0xFF;
  VLOG_I("After ext[0]=0xFF, alias[0]=0x", std::hex, static_cast<int>(alias[0]));
}

// Demonstrate shallow_copy_ptr / is_ptr / to_ptr -- opaque pointer wrapping.
inline void demo_ptr_wrapping() {
  VLOG_I("--- 7. shallow_copy_ptr / is_ptr / to_ptr ---");
  int my_object = 42;
  auto ptr_bytes = vlink::Bytes::shallow_copy_ptr(&my_object);
  print_ownership("shallow_copy_ptr", ptr_bytes);

  VLOG_I("is_ptr: ", ptr_bytes.is_ptr());
  int* recovered = ptr_bytes.to_ptr<int>();
  VLOG_I("to_ptr<int>() value: ", *recovered);
  VLOG_I("Same address: ", (recovered == &my_object));
}

// Demonstrate loan_internal -- iceoryx zero-copy loan.
inline void demo_loan_internal() {
  VLOG_I("--- 8. loan_internal ---");
  uint8_t simulated_shm_chunk[64];
  std::memset(simulated_shm_chunk, 0x99, sizeof(simulated_shm_chunk));

  auto loaned = vlink::Bytes::loan_internal(simulated_shm_chunk, sizeof(simulated_shm_chunk));
  print_ownership("loan_internal", loaned);
  VLOG_I("loaned[0]: 0x", std::hex, static_cast<int>(loaned[0]));
}

// Demonstrate copy constructor behavior depending on ownership.
inline void demo_copy_semantics() {
  VLOG_I("--- 9. Copy constructor semantics ---");

  auto owner = vlink::Bytes::from_string("owned");
  vlink::Bytes copy_of_owner(owner);
  VLOG_I("Copy of owner: is_owner=", copy_of_owner.is_owner(), " diff memory=", (copy_of_owner.data() != owner.data()));

  uint8_t ext[] = {1, 2, 3};
  auto alias = vlink::Bytes::shallow_copy(ext, 3);
  vlink::Bytes copy_of_alias(alias);
  VLOG_I("Copy of alias: is_owner=", copy_of_alias.is_owner(), " same memory=", (copy_of_alias.data() == alias.data()));
}

// Demonstrate move constructor -- transfers all state.
inline void demo_move() {
  VLOG_I("--- 10. Move constructor ---");
  auto source = vlink::Bytes::from_string("moveable");
  VLOG_I("Before move: size=", source.size());

  vlink::Bytes dest(std::move(source));
  VLOG_I("After move: dest.size=", dest.size(), " source.empty=", source.empty());
  VLOG_I("Content: \"", dest.to_string(), "\"");
}

}  // namespace ownership_demo
