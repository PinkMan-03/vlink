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

#include "./impl/types.h"

#include <doctest/doctest.h>

#include <sstream>
#include <string>

//
#include "../common_test.h"

// ---------------------------------------------------------------------------
// TEST SUITE: ImplType bitmask
// ---------------------------------------------------------------------------

TEST_SUITE("impl-ImplType - values") {
  TEST_CASE("kUnknownImplType == 0") { CHECK(static_cast<int>(kUnknownImplType) == 0); }

  TEST_CASE("kPublisher == 1") { CHECK(static_cast<int>(kPublisher) == 1); }

  TEST_CASE("kSubscriber == 2") { CHECK(static_cast<int>(kSubscriber) == 2); }

  TEST_CASE("kSetter == 4") { CHECK(static_cast<int>(kSetter) == 4); }

  TEST_CASE("kGetter == 8") { CHECK(static_cast<int>(kGetter) == 8); }

  TEST_CASE("kServer == 16") { CHECK(static_cast<int>(kServer) == 16); }

  TEST_CASE("kClient == 32") { CHECK(static_cast<int>(kClient) == 32); }

  TEST_CASE("bitmask OR: publisher | subscriber") {
    uint8_t combined = kPublisher | kSubscriber;
    CHECK(combined == 3);
  }

  TEST_CASE("bitmask OR: all six types") {
    uint8_t all = kPublisher | kSubscriber | kSetter | kGetter | kServer | kClient;
    CHECK(all == 63);
  }

  TEST_CASE("bitmask AND can test individual bits") {
    uint8_t combined = kPublisher | kServer;
    CHECK((combined & kPublisher) != 0);
    CHECK((combined & kServer) != 0);
    CHECK((combined & kSubscriber) == 0);
  }
}

// ---------------------------------------------------------------------------
// TEST SUITE: TransportType enum
// ---------------------------------------------------------------------------

TEST_SUITE("impl-TransportType - values") {
  TEST_CASE("kUnknown == 0") { CHECK(static_cast<uint8_t>(TransportType::kUnknown) == 0); }

  TEST_CASE("kIntra == 1") { CHECK(static_cast<uint8_t>(TransportType::kIntra) == 1); }

  TEST_CASE("kShm == 2") { CHECK(static_cast<uint8_t>(TransportType::kShm) == 2); }

  TEST_CASE("kShm2 == 3") { CHECK(static_cast<uint8_t>(TransportType::kShm2) == 3); }

  TEST_CASE("kZenoh == 4") { CHECK(static_cast<uint8_t>(TransportType::kZenoh) == 4); }

  TEST_CASE("kDds == 5") { CHECK(static_cast<uint8_t>(TransportType::kDds) == 5); }

  TEST_CASE("kDdsc == 6") { CHECK(static_cast<uint8_t>(TransportType::kDdsc) == 6); }

  TEST_CASE("kDdsr == 7") { CHECK(static_cast<uint8_t>(TransportType::kDdsr) == 7); }

  TEST_CASE("kDdst == 8") { CHECK(static_cast<uint8_t>(TransportType::kDdst) == 8); }

  TEST_CASE("kSomeip == 9") { CHECK(static_cast<uint8_t>(TransportType::kSomeip) == 9); }

  TEST_CASE("kMqtt == 10") { CHECK(static_cast<uint8_t>(TransportType::kMqtt) == 10); }

  TEST_CASE("kFdbus == 11") { CHECK(static_cast<uint8_t>(TransportType::kFdbus) == 11); }

  TEST_CASE("kQnx == 12") { CHECK(static_cast<uint8_t>(TransportType::kQnx) == 12); }

  TEST_CASE("all values are distinct") {
    CHECK(TransportType::kIntra != TransportType::kShm);
    CHECK(TransportType::kDds != TransportType::kDdsc);
    CHECK(TransportType::kZenoh != TransportType::kDds);
  }
}

// ---------------------------------------------------------------------------
// TEST SUITE: ActionType enum
// ---------------------------------------------------------------------------

TEST_SUITE("impl-ActionType - values") {
  TEST_CASE("kUnknownAction == 0") { CHECK(static_cast<uint8_t>(ActionType::kUnknownAction) == 0); }

  TEST_CASE("kClientRequest == 1") { CHECK(static_cast<uint8_t>(ActionType::kClientRequest) == 1); }

  TEST_CASE("kClientResponse == 2") { CHECK(static_cast<uint8_t>(ActionType::kClientResponse) == 2); }

  TEST_CASE("kServerRequest == 3") { CHECK(static_cast<uint8_t>(ActionType::kServerRequest) == 3); }

  TEST_CASE("kServerResponse == 4") { CHECK(static_cast<uint8_t>(ActionType::kServerResponse) == 4); }

  TEST_CASE("kPublish == 5") { CHECK(static_cast<uint8_t>(ActionType::kPublish) == 5); }

  TEST_CASE("kSubscribe == 6") { CHECK(static_cast<uint8_t>(ActionType::kSubscribe) == 6); }

  TEST_CASE("kSet == 7") { CHECK(static_cast<uint8_t>(ActionType::kSet) == 7); }

  TEST_CASE("kGet == 8") { CHECK(static_cast<uint8_t>(ActionType::kGet) == 8); }
}

// ---------------------------------------------------------------------------
// TEST SUITE: Version struct
// ---------------------------------------------------------------------------

TEST_SUITE("impl-Version - default construction") {
  TEST_CASE("default Version has all fields -1") {
    Version v;
    CHECK(v.major == -1);
    CHECK(v.minor == -1);
    CHECK(v.patch == -1);
  }

  TEST_CASE("default Version is not valid") {
    Version v;
    CHECK(!v.is_valid());
  }
}

TEST_SUITE("impl-Version - from_string") {
  TEST_CASE("parse '2.1.0' correctly") {
    Version v = Version::from_string("2.1.0");
    CHECK(v.major == 2);
    CHECK(v.minor == 1);
    CHECK(v.patch == 0);
    CHECK(v.is_valid());
  }

  TEST_CASE("parse '0.0.1' correctly") {
    Version v = Version::from_string("0.0.1");
    CHECK(v.major == 0);
    CHECK(v.minor == 0);
    CHECK(v.patch == 1);
  }

  TEST_CASE("parse empty string gives all -1") {
    Version v = Version::from_string("");
    CHECK(!v.is_valid());
  }
}

TEST_SUITE("impl-Version - to_string") {
  TEST_CASE("to_string formats as major.minor.patch") {
    Version v;
    v.major = 2;
    v.minor = 3;
    v.patch = 4;

    CHECK(v.to_string() == "2.3.4");
  }
}

TEST_SUITE("impl-Version - comparison operators") {
  TEST_CASE("equal versions compare equal") {
    Version a = Version::from_string("1.2.3");
    Version b = Version::from_string("1.2.3");
    CHECK(a == b);
    CHECK(!(a != b));
  }

  TEST_CASE("different versions compare not equal") {
    Version a = Version::from_string("1.2.3");
    Version b = Version::from_string("1.2.4");
    CHECK(a != b);
    CHECK(!(a == b));
  }

  TEST_CASE("operator< works on major component") {
    Version a = Version::from_string("1.0.0");
    Version b = Version::from_string("2.0.0");
    CHECK(a < b);
    CHECK(!(b < a));
  }

  TEST_CASE("operator< works on minor component") {
    Version a = Version::from_string("1.1.0");
    Version b = Version::from_string("1.2.0");
    CHECK(a < b);
    CHECK(!(b < a));
  }

  TEST_CASE("operator< works on patch component") {
    Version a = Version::from_string("1.0.0");
    Version b = Version::from_string("1.0.1");
    CHECK(a < b);
    CHECK(!(b < a));
  }

  TEST_CASE("operator> is reverse of operator<") {
    Version a = Version::from_string("2.0.0");
    Version b = Version::from_string("1.0.0");
    CHECK(a > b);
    CHECK(!(b > a));
  }
}

// ---------------------------------------------------------------------------
// TEST SUITE: Timeout
// ---------------------------------------------------------------------------

TEST_SUITE("impl-Timeout - constants") {
  TEST_CASE("kDefaultInterval is 5000 ms") { CHECK(Timeout::kDefaultInterval.count() == 5000); }

  TEST_CASE("kInfinite is -1 ms") { CHECK(Timeout::kInfinite.count() == -1); }
}

// ---------------------------------------------------------------------------
// TEST SUITE: SampleLostInfo
// ---------------------------------------------------------------------------

TEST_SUITE("impl-SampleLostInfo - default construction") {
  TEST_CASE("default SampleLostInfo has zero fields") {
    SampleLostInfo info;
    CHECK(info.total == 0);
    CHECK(info.lost == 0);
  }
}

TEST_SUITE("impl-SampleLostInfo - field mutation") {
  TEST_CASE("total and lost can be set") {
    SampleLostInfo info;
    info.total = 1000;
    info.lost = 5;
    CHECK(info.total == 1000);
    CHECK(info.lost == 5);
  }

  TEST_CASE("operator<< produces non-empty output") {
    SampleLostInfo info;
    info.total = 100;
    info.lost = 3;

    std::ostringstream oss;
    oss << info;
    CHECK(!oss.str().empty());
  }
}

// ---------------------------------------------------------------------------
// TEST SUITE: SchemaData
// ---------------------------------------------------------------------------

TEST_SUITE("impl-SchemaData - default construction") {
  TEST_CASE("default SchemaData has empty fields") {
    SchemaData ps;
    CHECK(ps.name.empty());
    CHECK(ps.encoding.empty());
    CHECK(ps.schema_type == SchemaType::kUnknown);
    CHECK(ps.data.empty());
  }

  TEST_CASE("SchemaData fields can be set") {
    SchemaData ps;
    ps.name = "MyProtoMsg";
    ps.encoding = "protobuf";
    ps.schema_type = SchemaType::kProtobuf;
    ps.data = Bytes::create(64);

    CHECK(ps.name == "MyProtoMsg");
    CHECK(ps.encoding == "protobuf");
    CHECK(ps.schema_type == SchemaType::kProtobuf);
    CHECK(!ps.data.empty());
  }
}

TEST_SUITE("impl-SchemaData - schema family inference") {
  TEST_CASE("infer_ser_type recognizes textual raw payloads") {
    CHECK(SchemaData::infer_ser_type("string") == SchemaType::kRaw);
    CHECK(SchemaData::infer_ser_type("std::string") == SchemaType::kRaw);
    CHECK(SchemaData::infer_ser_type("json") == SchemaType::kRaw);
    CHECK(SchemaData::infer_ser_type("application/json") == SchemaType::kRaw);
  }

  TEST_CASE("infer_ser_type recognizes remaining raw aliases") {
    CHECK(SchemaData::infer_ser_type("raw") == SchemaType::kRaw);
    CHECK(SchemaData::infer_ser_type("text") == SchemaType::kRaw);
    CHECK(SchemaData::infer_ser_type("text/json") == SchemaType::kRaw);
  }

  TEST_CASE("infer_ser_type recognizes zerocopy payloads") {
    CHECK(SchemaData::infer_ser_type("vlink::zerocopy::CameraFrame") == SchemaType::kZeroCopy);
    CHECK(SchemaData::infer_ser_type("vlink::zerocopy::RawData") == SchemaType::kZeroCopy);
  }

  TEST_CASE("infer_ser_type falls back to kUnknown for non-matching names") {
    // Names lacking the zero-copy prefix and not in the raw alias set must be unknown;
    // protobuf / flatbuffers are explicitly *not* guessed from ser names here.
    CHECK(SchemaData::infer_ser_type("demo.proto.PointCloud") == SchemaType::kUnknown);
    CHECK(SchemaData::infer_ser_type("demo.fbs.PointCloud") == SchemaType::kUnknown);
    CHECK(SchemaData::infer_ser_type("") == SchemaType::kUnknown);
  }

  TEST_CASE("resolve_type prefers explicit then encoding then ser_type") {
    CHECK(SchemaData::resolve_type(SchemaType::kProtobuf, "vlink::zerocopy::RawData", "flatbuffers") ==
          SchemaType::kProtobuf);
    CHECK(SchemaData::resolve_type(SchemaType::kUnknown, "demo.Message", "protobuf") == SchemaType::kProtobuf);
    CHECK(SchemaData::resolve_type(SchemaType::kUnknown, "demo.Table", "bfbs") == SchemaType::kFlatbuffers);
    CHECK(SchemaData::resolve_type(SchemaType::kUnknown, "vlink::zerocopy::PointCloud", "") == SchemaType::kZeroCopy);
    CHECK(SchemaData::resolve_type(SchemaType::kUnknown, "string", "") == SchemaType::kRaw);
  }
}

// ---------------------------------------------------------------------------
// TEST SUITE: InitType enum
// ---------------------------------------------------------------------------

TEST_SUITE("impl-InitType - values") {
  TEST_CASE("kWithoutInit == 0") { CHECK(static_cast<uint8_t>(InitType::kWithoutInit) == 0); }

  TEST_CASE("kWithInit == 1") { CHECK(static_cast<uint8_t>(InitType::kWithInit) == 1); }

  TEST_CASE("kWithoutInit != kWithInit") { CHECK(InitType::kWithoutInit != InitType::kWithInit); }
}

// ---------------------------------------------------------------------------
// TEST SUITE: SecurityType enum
// ---------------------------------------------------------------------------

TEST_SUITE("impl-SecurityType - values") {
  TEST_CASE("kWithoutSecurity == 0") { CHECK(static_cast<uint8_t>(SecurityType::kWithoutSecurity) == 0); }

  TEST_CASE("kWithSecurity == 1") { CHECK(static_cast<uint8_t>(SecurityType::kWithSecurity) == 1); }

  TEST_CASE("kWithoutSecurity != kWithSecurity") {
    CHECK(SecurityType::kWithoutSecurity != SecurityType::kWithSecurity);
  }
}

// ---------------------------------------------------------------------------
// TEST SUITE: Version - additional edge cases
// ---------------------------------------------------------------------------

TEST_SUITE("impl-Version - edge cases") {
  TEST_CASE("from_string with only major") {
    Version v = Version::from_string("3");
    CHECK(v.major == 3);
  }

  TEST_CASE("from_string with major.minor only") {
    Version v = Version::from_string("2.5");
    CHECK(v.major == 2);
    CHECK(v.minor == 5);
  }

  TEST_CASE("from_string with large numbers") {
    Version v = Version::from_string("100.200.300");
    CHECK(v.major == 100);
    CHECK(v.minor == 200);
    CHECK(v.patch == 300);
    CHECK(v.is_valid());
  }

  TEST_CASE("from_string with zeros") {
    Version v = Version::from_string("0.0.0");
    CHECK(v.major == 0);
    CHECK(v.minor == 0);
    CHECK(v.patch == 0);
    CHECK(v.is_valid());
  }

  TEST_CASE("operator< with equal major and minor") {
    Version a = Version::from_string("1.2.3");
    Version b = Version::from_string("1.2.3");
    CHECK(!(a < b));
    CHECK(!(b < a));
  }

  TEST_CASE("operator> with equal versions") {
    Version a = Version::from_string("1.2.3");
    Version b = Version::from_string("1.2.3");
    CHECK(!(a > b));
    CHECK(!(b > a));
  }

  TEST_CASE("operator< transitivity") {
    Version a = Version::from_string("1.0.0");
    Version b = Version::from_string("1.1.0");
    Version c = Version::from_string("2.0.0");
    CHECK(a < b);
    CHECK(b < c);
    CHECK(a < c);
  }

  TEST_CASE("to_string of default version") {
    Version v;
    std::string s = v.to_string();
    CHECK(s == "-1.-1.-1");
  }

  TEST_CASE("is_valid with partially set fields") {
    Version v;
    v.major = 1;
    CHECK(!v.is_valid());

    v.minor = 0;
    CHECK(!v.is_valid());

    v.patch = 0;
    CHECK(v.is_valid());
  }
}

// ---------------------------------------------------------------------------
// TEST SUITE: SampleLostInfo - additional
// ---------------------------------------------------------------------------

TEST_SUITE("impl-SampleLostInfo - operator<<") {
  TEST_CASE("operator<< contains total and lost values") {
    SampleLostInfo info;
    info.total = 42;
    info.lost = 7;

    std::ostringstream oss;
    oss << info;
    std::string out = oss.str();
    CHECK(out.find("42") != std::string::npos);
    CHECK(out.find("7") != std::string::npos);
  }

  TEST_CASE("operator<< with zero values") {
    SampleLostInfo info;
    std::ostringstream oss;
    oss << info;
    CHECK(!oss.str().empty());
  }

  TEST_CASE("operator<< with large values") {
    SampleLostInfo info;
    info.total = UINT64_MAX;
    info.lost = UINT64_MAX;
    std::ostringstream oss;
    oss << info;
    CHECK(!oss.str().empty());
  }
}

// ---------------------------------------------------------------------------
// TEST SUITE: ImplType bitmask additional
// ---------------------------------------------------------------------------

TEST_SUITE("impl-ImplType - bitmask edge cases") {
  TEST_CASE("all types have unique power-of-two values") {
    CHECK((kPublisher & kSubscriber) == 0);
    CHECK((kSetter & kGetter) == 0);
    CHECK((kServer & kClient) == 0);
    CHECK((kPublisher & kServer) == 0);
  }

  TEST_CASE("combined bitmask setter+getter") {
    uint8_t combined = kSetter | kGetter;
    CHECK(combined == 12);
    CHECK((combined & kSetter) != 0);
    CHECK((combined & kGetter) != 0);
    CHECK((combined & kPublisher) == 0);
  }

  TEST_CASE("combined server+client") {
    uint8_t combined = kServer | kClient;
    CHECK(combined == 48);
  }
}

// ---------------------------------------------------------------------------
// TEST SUITE: Timeout additional
// ---------------------------------------------------------------------------

TEST_SUITE("impl-Timeout - additional") {
  TEST_CASE("kDefaultInterval is positive") { CHECK(Timeout::kDefaultInterval.count() > 0); }

  TEST_CASE("kInfinite is negative") { CHECK(Timeout::kInfinite.count() < 0); }

  TEST_CASE("Timeout struct is final") { CHECK(std::is_final_v<Timeout>); }
}

// ---------------------------------------------------------------------------
// TEST SUITE: SchemaData copy/move
// ---------------------------------------------------------------------------

TEST_SUITE("impl-SchemaData - copy and move") {
  TEST_CASE("SchemaData copy") {
    SchemaData a;
    a.name = "test.Message";
    a.encoding = "protobuf";
    a.schema_type = SchemaType::kProtobuf;
    a.data = Bytes::create(10);

    SchemaData b = a;
    CHECK(b.name == a.name);
    CHECK(b.encoding == a.encoding);
    CHECK(b.schema_type == a.schema_type);
    CHECK(b.data.size() == a.data.size());
  }

  TEST_CASE("SchemaData move") {
    SchemaData a;
    a.name = "test.Message";
    a.encoding = "protobuf";
    a.schema_type = SchemaType::kProtobuf;
    a.data = Bytes::create(10);

    SchemaData b = std::move(a);
    CHECK(b.name == "test.Message");
    CHECK(b.encoding == "protobuf");
    CHECK(b.schema_type == SchemaType::kProtobuf);
    CHECK(b.data.size() == 10);
  }
}

// NOLINTEND
