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

#include <doctest/doctest.h>

#include <memory>
#include <sstream>
#include <string>

#include "./extension/status_detail.h"

//
#include "../common_test.h"

// ---------------------------------------------------------------------------
// TEST SUITE: Status classification helpers
// ---------------------------------------------------------------------------

TEST_SUITE("extension-Status - classification helpers") {
  TEST_CASE("is_for_writer - kUnknown is not writer side (< kSubscriptionMatched)") {
    CHECK(Status::is_for_writer(Status::kUnknown) == false);
  }

  TEST_CASE("is_for_writer - writer types") {
    CHECK(Status::is_for_writer(Status::kPublicationMatched) == true);
    CHECK(Status::is_for_writer(Status::kOfferedDeadlineMissed) == true);
    CHECK(Status::is_for_writer(Status::kOfferedIncompatibleQos) == true);
    CHECK(Status::is_for_writer(Status::kLivelinessLost) == true);
  }

  TEST_CASE("is_for_writer - reader types return false") {
    CHECK(Status::is_for_writer(Status::kSubscriptionMatched) == false);
    CHECK(Status::is_for_writer(Status::kRequestedDeadlineMissed) == false);
    CHECK(Status::is_for_writer(Status::kLivelinessChanged) == false);
    CHECK(Status::is_for_writer(Status::kSampleRejected) == false);
    CHECK(Status::is_for_writer(Status::kRequestedIncompatibleQos) == false);
    CHECK(Status::is_for_writer(Status::kSampleLost) == false);
  }

  TEST_CASE("is_for_reader - reader types") {
    CHECK(Status::is_for_reader(Status::kSubscriptionMatched) == true);
    CHECK(Status::is_for_reader(Status::kRequestedDeadlineMissed) == true);
    CHECK(Status::is_for_reader(Status::kLivelinessChanged) == true);
    CHECK(Status::is_for_reader(Status::kSampleRejected) == true);
    CHECK(Status::is_for_reader(Status::kRequestedIncompatibleQos) == true);
    CHECK(Status::is_for_reader(Status::kSampleLost) == true);
  }

  TEST_CASE("is_for_reader - writer types return false") {
    CHECK(Status::is_for_reader(Status::kPublicationMatched) == false);
    CHECK(Status::is_for_reader(Status::kOfferedDeadlineMissed) == false);
    CHECK(Status::is_for_reader(Status::kOfferedIncompatibleQos) == false);
    CHECK(Status::is_for_reader(Status::kLivelinessLost) == false);
  }

  TEST_CASE("Type enum numeric values") {
    CHECK(static_cast<int>(Status::kUnknown) == 0);
    CHECK(static_cast<int>(Status::kPublicationMatched) == 1);
    CHECK(static_cast<int>(Status::kOfferedDeadlineMissed) == 2);
    CHECK(static_cast<int>(Status::kOfferedIncompatibleQos) == 3);
    CHECK(static_cast<int>(Status::kLivelinessLost) == 4);
    CHECK(static_cast<int>(Status::kSubscriptionMatched) == 5);
    CHECK(static_cast<int>(Status::kRequestedDeadlineMissed) == 6);
    CHECK(static_cast<int>(Status::kLivelinessChanged) == 7);
    CHECK(static_cast<int>(Status::kSampleRejected) == 8);
    CHECK(static_cast<int>(Status::kRequestedIncompatibleQos) == 9);
    CHECK(static_cast<int>(Status::kSampleLost) == 10);
  }
}

// ---------------------------------------------------------------------------
// TEST SUITE: Status::Unknown
// ---------------------------------------------------------------------------

TEST_SUITE("extension-Status::Unknown") {
  TEST_CASE("get_type returns kUnknown") {
    Status::Unknown u;
    CHECK(u.get_type() == Status::kUnknown);
  }

  TEST_CASE("get_string is not empty") {
    Status::Unknown u;
    CHECK(!u.get_string().empty());
  }

  TEST_CASE("ostream operator") {
    Status::Unknown u;
    std::ostringstream oss;
    oss << u;
    CHECK(!oss.str().empty());
  }
}

// ---------------------------------------------------------------------------
// TEST SUITE: Status::PublicationMatched
// ---------------------------------------------------------------------------

TEST_SUITE("extension-Status::PublicationMatched") {
  TEST_CASE("get_type returns kPublicationMatched") {
    Status::PublicationMatched s;
    CHECK(s.get_type() == Status::kPublicationMatched);
  }

  TEST_CASE("default field values") {
    Status::PublicationMatched s;
    CHECK(s.total_count == 0);
    CHECK(s.total_count_change == 0);
    CHECK(s.current_count == 0);
    CHECK(s.current_count_change == 0);
    CHECK(s.last_subscription_handle == nullptr);
  }

  TEST_CASE("fields are mutable") {
    Status::PublicationMatched s;
    s.total_count = 5;
    s.current_count = 2;
    s.total_count_change = 1;
    s.current_count_change = -1;

    CHECK(s.total_count == 5);
    CHECK(s.current_count == 2);
    CHECK(s.total_count_change == 1);
    CHECK(s.current_count_change == -1);
  }

  TEST_CASE("get_string contains meaningful text") {
    Status::PublicationMatched s;
    s.total_count = 3;
    s.current_count = 1;
    CHECK(!s.get_string().empty());
  }

  TEST_CASE("ostream operator") {
    Status::PublicationMatched s;
    std::ostringstream oss;
    oss << s;
    CHECK(!oss.str().empty());
  }

  TEST_CASE("as<T>() from shared_ptr works") {
    auto base = std::make_shared<Status::PublicationMatched>();
    base->total_count = 10;

    auto derived = base->as<Status::PublicationMatched>();
    REQUIRE(derived != nullptr);
    CHECK(derived->total_count == 10);
  }
}

// ---------------------------------------------------------------------------
// TEST SUITE: Status::OfferedDeadlineMissed
// ---------------------------------------------------------------------------

TEST_SUITE("extension-Status::OfferedDeadlineMissed") {
  TEST_CASE("get_type returns kOfferedDeadlineMissed") {
    Status::OfferedDeadlineMissed s;
    CHECK(s.get_type() == Status::kOfferedDeadlineMissed);
  }

  TEST_CASE("default fields") {
    Status::OfferedDeadlineMissed s;
    CHECK(s.total_count == 0);
    CHECK(s.total_count_change == 0);
    CHECK(s.last_instance_handle == nullptr);
  }

  TEST_CASE("get_string not empty") {
    Status::OfferedDeadlineMissed s;
    CHECK(!s.get_string().empty());
  }
}

// ---------------------------------------------------------------------------
// TEST SUITE: Status::OfferedIncompatibleQos
// ---------------------------------------------------------------------------

TEST_SUITE("extension-Status::OfferedIncompatibleQos") {
  TEST_CASE("get_type returns kOfferedIncompatibleQos") {
    Status::OfferedIncompatibleQos s;
    CHECK(s.get_type() == Status::kOfferedIncompatibleQos);
  }

  TEST_CASE("default fields") {
    Status::OfferedIncompatibleQos s;
    CHECK(s.total_count == 0);
    CHECK(s.total_count_change == 0);
    CHECK(s.last_policy_id == 0);
  }

  TEST_CASE("field mutation") {
    Status::OfferedIncompatibleQos s;
    s.total_count = 2;
    s.last_policy_id = 7;

    CHECK(s.total_count == 2);
    CHECK(s.last_policy_id == 7);
  }
}

// ---------------------------------------------------------------------------
// TEST SUITE: Status::LivelinessLost
// ---------------------------------------------------------------------------

TEST_SUITE("extension-Status::LivelinessLost") {
  TEST_CASE("get_type returns kLivelinessLost") {
    Status::LivelinessLost s;
    CHECK(s.get_type() == Status::kLivelinessLost);
  }

  TEST_CASE("default fields") {
    Status::LivelinessLost s;
    CHECK(s.total_count == 0);
    CHECK(s.total_count_change == 0);
  }

  TEST_CASE("get_string not empty") {
    Status::LivelinessLost s;
    CHECK(!s.get_string().empty());
  }
}

// ---------------------------------------------------------------------------
// TEST SUITE: Status::SubscriptionMatched
// ---------------------------------------------------------------------------

TEST_SUITE("extension-Status::SubscriptionMatched") {
  TEST_CASE("get_type returns kSubscriptionMatched") {
    Status::SubscriptionMatched s;
    CHECK(s.get_type() == Status::kSubscriptionMatched);
  }

  TEST_CASE("default fields") {
    Status::SubscriptionMatched s;
    CHECK(s.total_count == 0);
    CHECK(s.total_count_change == 0);
    CHECK(s.current_count == 0);
    CHECK(s.current_count_change == 0);
    CHECK(s.last_publication_handle == nullptr);
  }

  TEST_CASE("as<T>() from shared_ptr") {
    auto base = std::make_shared<Status::SubscriptionMatched>();
    base->current_count = 3;

    auto derived = base->as<Status::SubscriptionMatched>();
    REQUIRE(derived != nullptr);
    CHECK(derived->current_count == 3);
  }
}

// ---------------------------------------------------------------------------
// TEST SUITE: Status::RequestedDeadlineMissed
// ---------------------------------------------------------------------------

TEST_SUITE("extension-Status::RequestedDeadlineMissed") {
  TEST_CASE("get_type") {
    Status::RequestedDeadlineMissed s;
    CHECK(s.get_type() == Status::kRequestedDeadlineMissed);
  }

  TEST_CASE("default fields") {
    Status::RequestedDeadlineMissed s;
    CHECK(s.total_count == 0);
    CHECK(s.total_count_change == 0);
    CHECK(s.last_instance_handle == nullptr);
  }
}

// ---------------------------------------------------------------------------
// TEST SUITE: Status::LivelinessChanged
// ---------------------------------------------------------------------------

TEST_SUITE("extension-Status::LivelinessChanged") {
  TEST_CASE("get_type") {
    Status::LivelinessChanged s;
    CHECK(s.get_type() == Status::kLivelinessChanged);
  }

  TEST_CASE("default fields") {
    Status::LivelinessChanged s;
    CHECK(s.alive_count == 0);
    CHECK(s.not_alive_count == 0);
    CHECK(s.alive_count_change == 0);
    CHECK(s.not_alive_count_change == 0);
    CHECK(s.last_publication_handle == nullptr);
  }

  TEST_CASE("field mutation") {
    Status::LivelinessChanged s;
    s.alive_count = 2;
    s.not_alive_count = 1;
    s.alive_count_change = 1;
    s.not_alive_count_change = -1;

    CHECK(s.alive_count == 2);
    CHECK(s.not_alive_count == 1);
    CHECK(s.alive_count_change == 1);
    CHECK(s.not_alive_count_change == -1);
  }
}

// ---------------------------------------------------------------------------
// TEST SUITE: Status::SampleRejected
// ---------------------------------------------------------------------------

TEST_SUITE("extension-Status::SampleRejected") {
  TEST_CASE("get_type") {
    Status::SampleRejected s;
    CHECK(s.get_type() == Status::kSampleRejected);
  }

  TEST_CASE("default fields") {
    Status::SampleRejected s;
    CHECK(s.total_count == 0);
    CHECK(s.total_count_change == 0);
    CHECK(s.last_reason == Status::SampleRejected::kNotRejected);
    CHECK(s.last_instance_handle == nullptr);
  }

  TEST_CASE("Kind enum values") {
    CHECK(static_cast<int>(Status::SampleRejected::kNotRejected) == 0);
    CHECK(static_cast<int>(Status::SampleRejected::kRejectedByInstancesLimit) == 1);
    CHECK(static_cast<int>(Status::SampleRejected::kRejectedBySamplesLimit) == 2);
    CHECK(static_cast<int>(Status::SampleRejected::kRejectedBySamplesPerInstanceLimit) == 3);
  }

  TEST_CASE("rejection reason mutation") {
    Status::SampleRejected s;
    s.last_reason = Status::SampleRejected::kRejectedBySamplesLimit;
    CHECK(s.last_reason == Status::SampleRejected::kRejectedBySamplesLimit);
  }
}

// ---------------------------------------------------------------------------
// TEST SUITE: Status::RequestedIncompatibleQos
// ---------------------------------------------------------------------------

TEST_SUITE("extension-Status::RequestedIncompatibleQos") {
  TEST_CASE("get_type") {
    Status::RequestedIncompatibleQos s;
    CHECK(s.get_type() == Status::kRequestedIncompatibleQos);
  }

  TEST_CASE("default fields") {
    Status::RequestedIncompatibleQos s;
    CHECK(s.total_count == 0);
    CHECK(s.total_count_change == 0);
    CHECK(s.last_policy_id == 0);
  }
}

// ---------------------------------------------------------------------------
// TEST SUITE: Status::SampleLost
// ---------------------------------------------------------------------------

TEST_SUITE("extension-Status::SampleLost") {
  TEST_CASE("get_type") {
    Status::SampleLost s;
    CHECK(s.get_type() == Status::kSampleLost);
  }

  TEST_CASE("default fields") {
    Status::SampleLost s;
    CHECK(s.total_count == 0);
    CHECK(s.total_count_change == 0);
  }

  TEST_CASE("get_string not empty") {
    Status::SampleLost s;
    CHECK(!s.get_string().empty());
  }
}

// ---------------------------------------------------------------------------
// TEST SUITE: ostream operators for all status detail types
// ---------------------------------------------------------------------------

TEST_SUITE("extension-Status - ostream operators") {
  TEST_CASE("OfferedDeadlineMissed ostream") {
    Status::OfferedDeadlineMissed s;
    s.total_count = 5;
    s.total_count_change = 2;
    std::ostringstream oss;
    oss << s;
    CHECK(oss.str().find("OfferedDeadlineMissed") != std::string::npos);
    CHECK(oss.str().find("total_count") != std::string::npos);
  }

  TEST_CASE("OfferedIncompatibleQos ostream") {
    Status::OfferedIncompatibleQos s;
    s.total_count = 3;
    s.last_policy_id = 7;
    std::ostringstream oss;
    oss << s;
    CHECK(oss.str().find("OfferedIncompatibleQos") != std::string::npos);
    CHECK(oss.str().find("last_policy_id") != std::string::npos);
  }

  TEST_CASE("LivelinessLost ostream") {
    Status::LivelinessLost s;
    s.total_count = 1;
    std::ostringstream oss;
    oss << s;
    CHECK(oss.str().find("LivelinessLost") != std::string::npos);
  }

  TEST_CASE("SubscriptionMatched ostream") {
    Status::SubscriptionMatched s;
    s.total_count = 10;
    s.current_count = 3;
    std::ostringstream oss;
    oss << s;
    CHECK(oss.str().find("SubscriptionMatched") != std::string::npos);
    CHECK(oss.str().find("current_count") != std::string::npos);
  }

  TEST_CASE("RequestedDeadlineMissed ostream") {
    Status::RequestedDeadlineMissed s;
    s.total_count = 4;
    std::ostringstream oss;
    oss << s;
    CHECK(oss.str().find("RequestedDeadlineMissed") != std::string::npos);
  }

  TEST_CASE("LivelinessChanged ostream") {
    Status::LivelinessChanged s;
    s.alive_count = 2;
    s.not_alive_count = 1;
    std::ostringstream oss;
    oss << s;
    CHECK(oss.str().find("LivelinessChanged") != std::string::npos);
    CHECK(oss.str().find("alive_count") != std::string::npos);
  }

  TEST_CASE("SampleRejected ostream") {
    Status::SampleRejected s;
    s.total_count = 6;
    s.last_reason = Status::SampleRejected::kRejectedBySamplesLimit;
    std::ostringstream oss;
    oss << s;
    CHECK(oss.str().find("SampleRejected") != std::string::npos);
    CHECK(oss.str().find("last_reason") != std::string::npos);
  }

  TEST_CASE("RequestedIncompatibleQos ostream") {
    Status::RequestedIncompatibleQos s;
    s.total_count = 2;
    s.last_policy_id = 5;
    std::ostringstream oss;
    oss << s;
    CHECK(oss.str().find("RequestedIncompatibleQos") != std::string::npos);
  }

  TEST_CASE("SampleLost ostream") {
    Status::SampleLost s;
    s.total_count = 8;
    s.total_count_change = 3;
    std::ostringstream oss;
    oss << s;
    CHECK(oss.str().find("SampleLost") != std::string::npos);
    CHECK(oss.str().find("total_count") != std::string::npos);
  }
}

// ---------------------------------------------------------------------------
// TEST SUITE: get_string for remaining types
// ---------------------------------------------------------------------------

TEST_SUITE("extension-Status - get_string completeness") {
  TEST_CASE("OfferedIncompatibleQos get_string") {
    Status::OfferedIncompatibleQos s;
    CHECK(!s.get_string().empty());
  }

  TEST_CASE("SubscriptionMatched get_string") {
    Status::SubscriptionMatched s;
    CHECK(!s.get_string().empty());
  }

  TEST_CASE("RequestedDeadlineMissed get_string") {
    Status::RequestedDeadlineMissed s;
    CHECK(!s.get_string().empty());
  }

  TEST_CASE("LivelinessChanged get_string") {
    Status::LivelinessChanged s;
    CHECK(!s.get_string().empty());
  }

  TEST_CASE("SampleRejected get_string") {
    Status::SampleRejected s;
    CHECK(!s.get_string().empty());
  }

  TEST_CASE("RequestedIncompatibleQos get_string") {
    Status::RequestedIncompatibleQos s;
    CHECK(!s.get_string().empty());
  }
}

// ---------------------------------------------------------------------------
// TEST SUITE: BasePtr ostream operator
// ---------------------------------------------------------------------------

TEST_SUITE("extension-Status::BasePtr - ostream") {
  TEST_CASE("shared_ptr PublicationMatched ostream") {
    Status::BasePtr ptr = std::make_shared<Status::PublicationMatched>();
    std::ostringstream oss;
    oss << ptr;
    CHECK(!oss.str().empty());
  }

  TEST_CASE("shared_ptr SubscriptionMatched ostream") {
    Status::BasePtr ptr = std::make_shared<Status::SubscriptionMatched>();
    std::ostringstream oss;
    oss << ptr;
    CHECK(!oss.str().empty());
  }

  TEST_CASE("shared_ptr SampleRejected ostream") {
    Status::BasePtr ptr = std::make_shared<Status::SampleRejected>();
    std::ostringstream oss;
    oss << ptr;
    CHECK(!oss.str().empty());
  }

  TEST_CASE("Base::as<T>() throws for Unknown type") {
    auto u = std::make_shared<Status::Unknown>();
    CHECK_THROWS_AS((void)u->as<Status::PublicationMatched>(), Exception::RuntimeError);
  }

  TEST_CASE("Base::as<T>() with wrong concrete type returns nullptr") {
    auto pub = std::make_shared<Status::PublicationMatched>();
    auto lost = pub->as<Status::SampleLost>();
    CHECK(lost == nullptr);
  }
}

// NOLINTEND
