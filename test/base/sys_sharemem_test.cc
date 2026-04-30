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

#include "./base/sys_sharemem.h"

#ifdef __linux__

#include <doctest/doctest.h>
#include <unistd.h>

#include <cstring>
#include <string>
#define VLINK_GETPID() ::getpid()

//
#include "../common_test.h"

// Build a unique shared-memory name using the current PID to avoid cross-run
// collisions in the POSIX namespace (names must start with '/').
static std::string make_shm_name(const char* tag) {
  return std::string("/vlink_test_") + tag + "_" + std::to_string(VLINK_GETPID());
}

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

struct ShmGuard {
  SysSharemem& shm;
  bool force;

  explicit ShmGuard(SysSharemem& s, bool f = true) : shm(s), force(f) {}

  ~ShmGuard() {
    if (shm.is_attached()) {
      shm.detach(force);
    }
  }
};

// ---------------------------------------------------------------------------
// TEST SUITE
// ---------------------------------------------------------------------------

TEST_SUITE("base-SysSharemem") {
  // ---------------------------------------------------------------------------
  // TEST: initial state before create / attach
  // ---------------------------------------------------------------------------

  TEST_CASE("initial-state") {
    SysSharemem shm;

    CHECK_FALSE(shm.is_attached());
    CHECK(shm.data() == nullptr);
    CHECK(shm.size() == 0);
  }

  // ---------------------------------------------------------------------------
  // TEST: create allocates a region with the requested size
  // ---------------------------------------------------------------------------

  TEST_CASE("create-basic") {
    const std::string name = make_shm_name("create");
    SysSharemem shm;
    ShmGuard guard{shm};

    constexpr size_t kSize = 4096;
    bool ok = shm.create(name, kSize);

    CHECK(ok);
    CHECK(shm.is_attached());
    CHECK(shm.size() == kSize);
    CHECK(shm.data() != nullptr);
  }

  // ---------------------------------------------------------------------------
  // TEST: write data to shared memory and read it back via the same object
  // ---------------------------------------------------------------------------

  TEST_CASE("write-read-same-object") {
    const std::string name = make_shm_name("write_read");
    SysSharemem shm;
    ShmGuard guard{shm};

    constexpr size_t kSize = 256;
    REQUIRE(shm.create(name, kSize));

    // Write a pattern
    auto* ptr = static_cast<uint8_t*>(shm.data());
    REQUIRE(ptr != nullptr);

    for (size_t i = 0; i < kSize; ++i) {
      ptr[i] = static_cast<uint8_t>(i & 0xFF);
    }

    // Read back through the const accessor
    const auto* cptr = static_cast<const uint8_t*>(static_cast<const SysSharemem&>(shm).data());
    REQUIRE(cptr != nullptr);

    for (size_t i = 0; i < kSize; ++i) {
      CHECK(cptr[i] == static_cast<uint8_t>(i & 0xFF));
    }
  }

  // ---------------------------------------------------------------------------
  // TEST: write via creator, attach from a second object and read back
  // ---------------------------------------------------------------------------

  TEST_CASE("create-then-attach") {
    const std::string name = make_shm_name("cre_att");

    SysSharemem creator;
    constexpr size_t kSize = 1024;
    REQUIRE(creator.create(name, kSize));

    // Write a sentinel value
    auto* wptr = static_cast<uint32_t*>(creator.data());
    REQUIRE(wptr != nullptr);
    *wptr = 0xDEADBEEF;

    // Attach from a separate object (simulates a second process)
    SysSharemem reader;
    bool att = reader.attach(name);
    CHECK(att);

    if (att) {
      CHECK(reader.is_attached());
      CHECK(reader.size() == kSize);

      const auto* rptr = static_cast<const uint32_t*>(reader.data());
      REQUIRE(rptr != nullptr);
      CHECK(*rptr == 0xDEADBEEF);

      // Detach reader without removing the OS object
      reader.detach(false);
      CHECK_FALSE(reader.is_attached());
    }

    // Creator detaches and removes
    creator.detach(true);
    CHECK_FALSE(creator.is_attached());
  }

  // ---------------------------------------------------------------------------
  // TEST: read-only attach cannot write (data() returns non-null but const view
  //       is available; the writable data() returns nullptr for kReadOnly mode)
  // ---------------------------------------------------------------------------

  TEST_CASE("readonly-attach") {
    const std::string name = make_shm_name("ro_att");

    SysSharemem creator;
    constexpr size_t kSize = 512;
    REQUIRE(creator.create(name, kSize));

    auto* wptr = static_cast<uint8_t*>(creator.data());
    REQUIRE(wptr != nullptr);
    std::memset(wptr, 0xAB, kSize);

    // Attach read-only
    SysSharemem reader;
    bool att = reader.attach(name, SysSharemem::kReadOnly);
    CHECK(att);

    if (att) {
      CHECK(reader.is_attached());
      CHECK(reader.size() == kSize);

      // Const data pointer must be valid
      const auto* rptr = static_cast<const uint8_t*>(static_cast<const SysSharemem&>(reader).data());
      REQUIRE(rptr != nullptr);
      CHECK(rptr[0] == 0xAB);
      CHECK(rptr[kSize - 1] == 0xAB);

      reader.detach(false);
    }

    creator.detach(true);
  }

  // ---------------------------------------------------------------------------
  // TEST: detach without prior create/attach returns false
  // ---------------------------------------------------------------------------

  TEST_CASE("detach-not-attached") {
    SysSharemem shm;

    bool d = shm.detach(true);
    CHECK_FALSE(d);
  }

  // ---------------------------------------------------------------------------
  // TEST: attach to a non-existent name fails gracefully
  // ---------------------------------------------------------------------------

  TEST_CASE("attach-nonexistent") {
    SysSharemem shm;
    const std::string name = make_shm_name("noexist_zzz");

    bool ok = shm.attach(name);
    CHECK_FALSE(ok);
    CHECK_FALSE(shm.is_attached());
    CHECK(shm.data() == nullptr);
    CHECK(shm.size() == 0);
  }

  // ---------------------------------------------------------------------------
  // TEST: create fails if name already exists (O_EXCL behavior)
  // ---------------------------------------------------------------------------

  TEST_CASE("create-fails-if-name-exists") {
    const std::string name = make_shm_name("replace");

    SysSharemem first;
    REQUIRE(first.create(name, 256));
    auto* p = static_cast<uint8_t*>(first.data());
    REQUIRE(p != nullptr);
    p[0] = 0x11;
    first.detach(false);  // leave in namespace, only unmap

    // Re-create with the same name should FAIL because create() uses O_EXCL
    SysSharemem second;
    bool ok = second.create(name, 512);
    CHECK_FALSE(ok);

    // Cleanup: remove the name left in the namespace by first
    SysSharemem cleanup;
    if (cleanup.attach(name)) {
      cleanup.detach(true);
    }
  }

  // ---------------------------------------------------------------------------
  // TEST: destructor auto-detaches (no crash)
  // ---------------------------------------------------------------------------

  TEST_CASE("destructor-detaches") {
    const std::string name = make_shm_name("dtor");

    {
      SysSharemem shm;
      REQUIRE(shm.create(name, 128));
      CHECK(shm.is_attached());
      // shm goes out of scope — destructor calls detach(false): only unmaps,
      // does NOT unlink the POSIX shared memory name
    }

    // Name is still in the namespace after destruction; attach should succeed
    SysSharemem probe;
    bool ok = probe.attach(name);
    CHECK(ok);

    // Cleanup: remove the name from the namespace
    if (ok) {
      probe.detach(true);
    }
  }

  // ---------------------------------------------------------------------------
  // TEST: large region (1 MB) can be created and written
  // ---------------------------------------------------------------------------

  TEST_CASE("large-region") {
    const std::string name = make_shm_name("large");
    SysSharemem shm;
    ShmGuard guard{shm};

    constexpr size_t kOneMiB = 1024UL * 1024UL;
    bool ok = shm.create(name, kOneMiB);
    CHECK(ok);

    if (ok) {
      CHECK(shm.size() == kOneMiB);

      auto* ptr = static_cast<uint8_t*>(shm.data());
      REQUIRE(ptr != nullptr);

      // Touch first and last byte
      ptr[0] = 0xAA;
      ptr[kOneMiB - 1] = 0xBB;

      const auto& cshm = shm;
      const auto* cptr = static_cast<const uint8_t*>(cshm.data());
      CHECK(cptr[0] == 0xAA);
      CHECK(cptr[kOneMiB - 1] == 0xBB);
    }
  }

  // ---------------------------------------------------------------------------
  // TEST: Mode enum values are as documented
  // ---------------------------------------------------------------------------

  TEST_CASE("mode-enum-values") {
    CHECK(static_cast<int>(SysSharemem::kReadOnly) == 0);
    CHECK(static_cast<int>(SysSharemem::kReadWrite) == 1);
  }

  // ---------------------------------------------------------------------------
  // TEST: detach(force=false) does not unlink the OS object
  // ---------------------------------------------------------------------------

  TEST_CASE("detach-no-force") {
    const std::string name = make_shm_name("det_nof");

    SysSharemem creator;
    REQUIRE(creator.create(name, 256));

    // Detach without removing from namespace
    bool d = creator.detach(false);
    CHECK(d);
    CHECK_FALSE(creator.is_attached());

    // The object must still exist in the namespace
    SysSharemem probe;
    bool ok = probe.attach(name);
    CHECK(ok);
    CHECK(probe.size() == 256);

    // Final cleanup
    probe.detach(true);
  }

  // ---------------------------------------------------------------------------
  // TEST: data() returns nullptr after detach
  // ---------------------------------------------------------------------------

  TEST_CASE("data-null-after-detach") {
    const std::string name = make_shm_name("data_det");

    SysSharemem shm;
    REQUIRE(shm.create(name, 128));
    CHECK(shm.data() != nullptr);

    shm.detach(true);

    CHECK(shm.data() == nullptr);
    CHECK(shm.size() == 0);
  }

  // ---------------------------------------------------------------------------
  // TEST: size() returns 0 after detach
  // ---------------------------------------------------------------------------

  TEST_CASE("size-zero-after-detach") {
    const std::string name = make_shm_name("size_det");

    SysSharemem shm;
    REQUIRE(shm.create(name, 512));
    CHECK(shm.size() == 512);

    shm.detach(true);
    CHECK(shm.size() == 0);
  }

  // ---------------------------------------------------------------------------
  // TEST: read-write mode (default) allows writing to data()
  // ---------------------------------------------------------------------------

  TEST_CASE("readwrite-default-mode") {
    const std::string name = make_shm_name("rw_mode");

    SysSharemem shm;
    ShmGuard guard{shm};

    REQUIRE(shm.create(name, 64, SysSharemem::kReadWrite));

    auto* ptr = static_cast<uint8_t*>(shm.data());
    REQUIRE(ptr != nullptr);

    ptr[0] = 0xFF;
    CHECK(ptr[0] == 0xFF);
  }

  // ---------------------------------------------------------------------------
  // TEST: multiple sequential create/detach cycles on different names
  // ---------------------------------------------------------------------------

  TEST_CASE("multiple-sequential-regions") {
    for (int i = 0; i < 3; ++i) {
      std::string name = make_shm_name(("seq_" + std::to_string(i)).c_str());
      SysSharemem shm;

      bool ok = shm.create(name, 128);
      CHECK(ok);

      if (ok) {
        auto* ptr = static_cast<uint8_t*>(shm.data());
        REQUIRE(ptr != nullptr);
        ptr[0] = static_cast<uint8_t>(i);

        const auto& cshm = shm;
        const auto* cptr = static_cast<const uint8_t*>(cshm.data());
        CHECK(cptr[0] == static_cast<uint8_t>(i));

        shm.detach(true);
      }
    }
  }
}

#endif

// NOLINTEND
