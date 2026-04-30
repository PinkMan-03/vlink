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
 * @file bytes_zerocopy.cc
 * @brief vlink::Bytes ownership model: shallow_copy, deep_copy, deep_copy_self, loan_internal.
 *
 * The vlink::Bytes class has four ownership modes:
 *   1. Owner (create/deep_copy) -- owns the memory, frees on destruction.
 *   2. Shallow alias (shallow_copy) -- non-owning pointer, no free.
 *   3. Loaned (loan_internal) -- non-owning, managed by external system (iceoryx).
 *   4. Pointer-only (shallow_copy_ptr) -- wraps an opaque pointer, size==0.
 */

#include <vlink/base/logger.h>

#include "ownership_demo.h"

int main() {
  VLOG_I("=== Bytes Zero-Copy / Ownership Example ===");

  ownership_demo::demo_shallow_copy_mutable();
  ownership_demo::demo_shallow_copy_const();
  ownership_demo::demo_deep_copy_external();
  ownership_demo::demo_deep_copy_offset();
  ownership_demo::demo_instance_copy();
  ownership_demo::demo_deep_copy_self();
  ownership_demo::demo_ptr_wrapping();
  ownership_demo::demo_loan_internal();
  ownership_demo::demo_copy_semantics();
  ownership_demo::demo_move();

  VLOG_I("=== Bytes Zero-Copy Example Complete ===");
  return 0;
}
