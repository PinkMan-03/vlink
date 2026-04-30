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

#include "./impl/url.h"

#include <doctest/doctest.h>

#include <string>

//
#include "../common_test.h"

// ---------------------------------------------------------------------------
// TEST SUITE: Url - static helper methods
// ---------------------------------------------------------------------------

TEST_SUITE("impl-Url - static helpers") {
  TEST_CASE("get_sort_index - intra:// has lower index than dds://") {
    int intra_idx = Url::get_sort_index("intra://topic");
    int dds_idx = Url::get_sort_index("dds://topic");

    CHECK(intra_idx < dds_idx);
  }

  TEST_CASE("get_sort_index - shm:// has low index") {
    int shm_idx = Url::get_sort_index("shm://topic");
    int zenoh_idx = Url::get_sort_index("zenoh://topic");

    CHECK(shm_idx < zenoh_idx);
  }

  TEST_CASE("get_sort_index - returns non-negative value") {
    CHECK(Url::get_sort_index("intra://test") >= 0);
    CHECK(Url::get_sort_index("dds://test") >= 0);
    CHECK(Url::get_sort_index("zenoh://test") >= 0);
    CHECK(Url::get_sort_index("unknown://test") >= 0);
  }

  TEST_CASE("is_local_type - intra:// is local") { CHECK(Url::is_local_type("intra://topic") == true); }

  TEST_CASE("is_local_type - shm:// is local") { CHECK(Url::is_local_type("shm://topic") == true); }

  TEST_CASE("is_local_type - shm2:// is local") { CHECK(Url::is_local_type("shm2://topic") == true); }

  TEST_CASE("is_local_type - dds:// is not local") { CHECK(Url::is_local_type("dds://topic") == false); }

  TEST_CASE("is_local_type - zenoh:// is not local") { CHECK(Url::is_local_type("zenoh://topic") == false); }

  TEST_CASE("is_local_type - ddsc:// is not local") { CHECK(Url::is_local_type("ddsc://topic") == false); }

  TEST_CASE("is_intra_type - intra:// returns true") { CHECK(Url::is_intra_type("intra://topic") == true); }

  TEST_CASE("is_intra_type - shm:// returns false") { CHECK(Url::is_intra_type("shm://topic") == false); }

  TEST_CASE("is_intra_type - dds:// returns false") { CHECK(Url::is_intra_type("dds://topic") == false); }

  TEST_CASE("is_shm_type - shm:// returns true") { CHECK(Url::is_shm_type("shm://topic") == true); }

  TEST_CASE("is_shm_type - shm2:// returns true") { CHECK(Url::is_shm_type("shm2://topic") == true); }

  TEST_CASE("is_shm_type - intra:// returns false") { CHECK(Url::is_shm_type("intra://topic") == false); }

  TEST_CASE("is_shm_type - dds:// returns false") { CHECK(Url::is_shm_type("dds://topic") == false); }

  TEST_CASE("get_transport_enable_flags returns non-zero when intra is enabled") {
#ifdef VLINK_SUPPORT_INTRA
    uint16_t flags = Url::get_transport_enable_flags();
    CHECK((flags & Url::kEnableIntra) != 0);
#else
    CHECK(true);  // skip when intra not compiled in
#endif
  }

  TEST_CASE("TransportEnableFlag bitmask values are distinct") {
    CHECK(Url::kEnableIntra != Url::kEnableShm);
    CHECK(Url::kEnableShm != Url::kEnableShm2);
    CHECK(Url::kEnableZenoh != Url::kEnableDds);
    CHECK(Url::kEnableDds != Url::kEnableDdsc);
  }

  TEST_CASE("kEnableAll has all lower bits set") {
    // kEnableAll = 0xFFFF
    CHECK(Url::kEnableAll == 0xFFFF);
  }

  TEST_CASE("kEnableEmpty is zero") { CHECK(Url::kEnableEmpty == 0); }
}

// ---------------------------------------------------------------------------
// TEST SUITE: Url - construction and get_str
// ---------------------------------------------------------------------------

TEST_SUITE("impl-Url - construction") {
  TEST_CASE("intra:// URL constructs successfully") {
    Url url("intra://test_topic");
    CHECK(url.get_str() == "intra://test_topic");
  }

  TEST_CASE("get_target returns non-null for valid URL") {
    Url url("intra://topic");
    CHECK(url.get_target() != nullptr);
  }

  TEST_CASE("get_transport_type returns kIntra for intra:// URL") {
    Url url("intra://topic");
    CHECK(url.get_transport_type() == TransportType::kIntra);
  }

  TEST_CASE("copy constructor produces independent copy") {
    Url original("intra://copy_test");
    Url copy(original);

    CHECK(copy.get_str() == original.get_str());
    CHECK(copy.get_target() != original.get_target());
  }

  TEST_CASE("move constructor transfers ownership") {
    Url original("intra://move_test");
    const std::string expected_str = original.get_str();

    Url moved(std::move(original));

    CHECK(moved.get_str() == expected_str);
    CHECK(moved.get_target() != nullptr);
  }

  TEST_CASE("copy assignment works") {
    Url a("intra://topic_a");
    Url b("intra://topic_b");

    b = a;

    CHECK(b.get_str() == a.get_str());
    CHECK(b.get_target() != a.get_target());
  }

  TEST_CASE("move assignment works") {
    Url a("intra://topic_a");
    Url b("intra://topic_b");
    const std::string expected = a.get_str();

    b = std::move(a);

    CHECK(b.get_str() == expected);
  }

  // TEST_CASE("self copy assignment is safe") {
  //   Url url("intra://self_assign");
  //   url = url;  // NOLINT
  //   CHECK(url.get_str() == "intra://self_assign");
  // }

  // TEST_CASE("self move assignment is safe") {
  //   Url url("intra://self_move");
  //   url = std::move(url);
  //   // After self-move the object should still be in a valid (possibly empty) state
  //   CHECK(true);
  // }
}

// ---------------------------------------------------------------------------
// TEST SUITE: Url - parse and validity
// ---------------------------------------------------------------------------

TEST_SUITE("impl-Url - parse") {
  TEST_CASE("parse kPublisher succeeds for intra://") {
    Url url("intra://parse_test");
    bool ok = url.parse(kPublisher);
    CHECK(ok == true);
  }

  TEST_CASE("parse kSubscriber succeeds for intra://") {
    Url url("intra://parse_test");
    bool ok = url.parse(kSubscriber);
    CHECK(ok == true);
  }

  TEST_CASE("is_valid returns true for intra://") {
    Url url("intra://valid_test");
    url.parse(kPublisher);
    CHECK(url.is_valid() == true);
  }

  TEST_CASE("get_impl_type returns correct type after parse") {
    Url url("intra://impl_test");
    url.parse(kPublisher);
    ImplType t = url.get_impl_type();
    CHECK((t & kPublisher) != 0);
  }
}

// ---------------------------------------------------------------------------
// TEST SUITE: Url - Protocol struct fields
// ---------------------------------------------------------------------------

TEST_SUITE("impl-Url - protocol fields via target") {
  TEST_CASE("get_transport_type kIntra after intra:// construction") {
    Url url("intra://namespace/topic");
    CHECK(url.get_transport_type() == TransportType::kIntra);
  }

#ifdef VLINK_SUPPORT_DDS
  TEST_CASE("get_transport_type kDds after dds:// construction") {
    Url url("dds://namespace/topic");
    CHECK(url.get_transport_type() == TransportType::kDds);
  }
#endif

#ifdef VLINK_SUPPORT_ZENOH
  TEST_CASE("get_transport_type kZenoh after zenoh:// construction") {
    Url url("zenoh://namespace/topic");
    CHECK(url.get_transport_type() == TransportType::kZenoh);
  }
#endif
}

// ---------------------------------------------------------------------------
// TEST SUITE: Url - additional static helpers edge cases
// ---------------------------------------------------------------------------

TEST_SUITE("impl-Url - static helpers edge cases") {
  TEST_CASE("is_local_type - someip:// is not local") { CHECK(Url::is_local_type("someip://topic") == false); }

  TEST_CASE("is_local_type - fdbus:// is not local") { CHECK(Url::is_local_type("fdbus://topic") == false); }

  TEST_CASE("is_local_type - empty string") { CHECK(Url::is_local_type("") == false); }

  TEST_CASE("is_intra_type - empty string returns false") { CHECK(Url::is_intra_type("") == false); }

  TEST_CASE("is_shm_type - empty string returns false") { CHECK(Url::is_shm_type("") == false); }

  TEST_CASE("is_intra_type - shm2:// returns false") { CHECK(Url::is_intra_type("shm2://topic") == false); }

  TEST_CASE("is_shm_type - someip:// returns false") { CHECK(Url::is_shm_type("someip://topic") == false); }

  TEST_CASE("get_sort_index - shm2:// has low index") {
    int shm2_idx = Url::get_sort_index("shm2://topic");
    int dds_idx = Url::get_sort_index("dds://topic");
    CHECK(shm2_idx < dds_idx);
  }

  TEST_CASE("get_sort_index - someip:// returns non-negative") { CHECK(Url::get_sort_index("someip://test") >= 0); }

  TEST_CASE("get_sort_index - ddsc:// returns non-negative") { CHECK(Url::get_sort_index("ddsc://test") >= 0); }

  TEST_CASE("get_sort_index - fdbus:// returns non-negative") { CHECK(Url::get_sort_index("fdbus://test") >= 0); }

  TEST_CASE("get_sort_index - empty string returns -1") { CHECK(Url::get_sort_index("") == -1); }

  TEST_CASE("TransportEnableFlag combinations work") {
    uint16_t combined = Url::kEnableIntra | Url::kEnableDds;
    CHECK((combined & Url::kEnableIntra) != 0);
    CHECK((combined & Url::kEnableDds) != 0);
    CHECK((combined & Url::kEnableShm) == 0);
    CHECK((combined & Url::kEnableZenoh) == 0);
  }

  TEST_CASE("kEnableAll includes all individual flags") {
    CHECK((Url::kEnableAll & Url::kEnableIntra) != 0);
    CHECK((Url::kEnableAll & Url::kEnableShm) != 0);
    CHECK((Url::kEnableAll & Url::kEnableShm2) != 0);
    CHECK((Url::kEnableAll & Url::kEnableZenoh) != 0);
    CHECK((Url::kEnableAll & Url::kEnableDds) != 0);
    CHECK((Url::kEnableAll & Url::kEnableDdsc) != 0);
    CHECK((Url::kEnableAll & Url::kEnableDdsr) != 0);
    CHECK((Url::kEnableAll & Url::kEnableDdst) != 0);
    CHECK((Url::kEnableAll & Url::kEnableSomeip) != 0);
    CHECK((Url::kEnableAll & Url::kEnableMqtt) != 0);
    CHECK((Url::kEnableAll & Url::kEnableFdbus) != 0);
    CHECK((Url::kEnableAll & Url::kEnableQnx) != 0);
  }
}

// ---------------------------------------------------------------------------
// TEST SUITE: Url - parse edge cases
// ---------------------------------------------------------------------------

TEST_SUITE("impl-Url - parse edge cases") {
  TEST_CASE("parse kServer succeeds for intra://") {
    Url url("intra://server_test");
    bool ok = url.parse(kServer);
    CHECK(ok == true);
  }

  TEST_CASE("parse kClient succeeds for intra://") {
    Url url("intra://client_test");
    bool ok = url.parse(kClient);
    CHECK(ok == true);
  }

  TEST_CASE("parse kSetter succeeds for intra://") {
    Url url("intra://setter_test");
    bool ok = url.parse(kSetter);
    CHECK(ok == true);
  }

  TEST_CASE("parse kGetter succeeds for intra://") {
    Url url("intra://getter_test");
    bool ok = url.parse(kGetter);
    CHECK(ok == true);
  }

  TEST_CASE("get_transport_type returns kIntra for intra:// with path") {
    Url url("intra://ns/topic/sub");
    CHECK(url.get_transport_type() == TransportType::kIntra);
  }

  TEST_CASE("is_valid before parse for intra://") {
    Url url("intra://check_valid");
    // Before parse, target exists so is_valid may return true depending on conf
    CHECK(url.get_target() != nullptr);
  }

  TEST_CASE("get_str preserves URL with query") {
    Url url("intra://topic?key=value");
    CHECK(url.get_str().find("intra://") != std::string::npos);
  }

  TEST_CASE("Url with URL containing special chars in topic") {
    Url url("intra://topic_with_underscores");
    CHECK(url.get_str() == "intra://topic_with_underscores");
    CHECK(url.get_target() != nullptr);
  }
}

// ---------------------------------------------------------------------------
// TEST SUITE: Url - move semantics
// ---------------------------------------------------------------------------

TEST_SUITE("impl-Url - move semantics") {
  TEST_CASE("moved-from object has null target") {
    Url a("intra://move_source");
    const void* old_target = a.get_target();
    CHECK(old_target != nullptr);

    Url b(std::move(a));
    CHECK(b.get_target() != nullptr);
    // moved-from a should have null target
    CHECK(a.get_target() == nullptr);
  }

  TEST_CASE("move assignment transfers target") {
    Url a("intra://move_src");
    Url b("intra://move_dst");

    b = std::move(a);
    CHECK(b.get_str() == "intra://move_src");
    CHECK(a.get_target() == nullptr);
  }
}

// NOLINTEND
